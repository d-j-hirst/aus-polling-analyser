"""Collect the VEC Lower House results for the 2026 rehearsal or election.

Preparation installs the real candidate and location rosters and keeps VEC test
material separately. During counting a completed, validated ZIP appears in the
selected run's inbox in one rename, ready for the forecast watcher.
"""

import argparse
from datetime import datetime, timedelta, timezone
import hashlib
from http.client import HTTPException
from io import BytesIO
import json
import os
from pathlib import Path
import re
import sys
from tempfile import NamedTemporaryFile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, quote, urlencode, urlsplit
from urllib.request import build_opener, HTTPRedirectHandler, Request
from xml.etree import ElementTree as ET
from zipfile import BadZipFile, ZipFile
from zoneinfo import ZoneInfo


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'uploads'))
from live_pipeline.storage import run_lock

PRIVATE = ROOT / 'forecasts/2026vic/live-inputs/vec'
CONTAINER = 'https://stpublishedresults.blob.core.windows.net/results'
EVENT = '17872'
SETUP = {'candidates': 'State2026MediaFileCandidatesV2.zip',
         'booths': 'State2026MediaFilePollingLocationsV2.zip',
         'groups': 'State2026MediaFileGroupsAndTicketsV2.zip'}
ZERO = 'State2026mediafilelitepplhv2_20261110_100505.zip'
RESULT = re.compile(r'State2026mediafilelitepplhv2_(\d{8}_\d{6})\.zip', re.I)
RUN_IDS = {'rehearsal': '2026vic-trial-1', 'election': '2026vic-live'}
TRIAL_ZEROS = datetime(2026, 11, 25, 17, 30)
TRIAL_RESULTS = datetime(2026, 11, 25, 18)
# Include the final 20:00 publication even when its filename carries seconds
# after the hour. The next five-minute publication would be outside the trial.
TRIAL_END = datetime(2026, 11, 25, 20, 5)
TRIAL_FINAL_SCAN = datetime(2026, 11, 25, 20, 30)
PUBLICATION_INTERVAL = timedelta(minutes=5)
ELECTION_RESULTS = datetime(2026, 11, 28, 18)


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class FeedUnavailable(Exception):
    """Only the status or exception class is reported. Signed URLs stay private."""


class Container:
    def __init__(self, signed_url):
        base, separator, self.query = signed_url.strip().partition('?')
        if base != CONTAINER or not separator or not self.query or urlsplit(signed_url).fragment:
            raise ValueError('VEC_SAS_URL must contain the signed HTTPS URL for the VEC results container.')
        self.client = build_opener(NoRedirect())

    def get(self, name='', **parameters):
        url = CONTAINER + ('/' + quote(name, safe='') if name else '') + '?' + self.query
        if parameters:
            url += '&' + urlencode(parameters)
        try:
            with self.client.open(Request(url), timeout=40) as response:
                return response.read()
        except HTTPError as exc:
            raise FeedUnavailable(f'VEC returned HTTP {exc.code}.') from None
        except (OSError, URLError, HTTPException) as exc:
            raise FeedUnavailable(f'VEC connection failed ({type(exc).__name__}).') from None

    def listing(self):
        marker = ''
        blobs = {}
        while True:
            try:
                page = ET.fromstring(self.get(restype='container', comp='list', marker=marker))
                if page.find('Blobs') is None:
                    raise ValueError
                for blob in page.findall('./Blobs/Blob'):
                    name = blob.findtext('Name')
                    if not name:
                        raise ValueError
                    blobs[name] = (int(blob.findtext('Properties/Content-Length')),
                                   blob.findtext('Properties/Etag'))
            except (ET.ParseError, TypeError, ValueError):
                raise FeedUnavailable('VEC returned an incomplete blob listing.') from None
            marker = page.findtext('NextMarker')
            if not marker:
                return blobs


def write_complete(path, data):
    """Readers see either the previous complete file or its complete replacement."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile(dir=path.parent, suffix='.part', delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(data)
        except BaseException:
            stream.close()
            temporary.unlink(missing_ok=True)
            raise
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def member(name, data):
    with ZipFile(BytesIO(data)) as archive:
        expected = Path(name).with_suffix('.xml').name
        if [n.casefold() for n in archive.namelist()] != [expected.casefold()]:
            raise ValueError(f'Unexpected ZIP contents in {name}')
        return archive.read(archive.namelist()[0])  # Also verifies the member CRC.


def download(feed, listing, name):
    size, _ = listing[name]
    data = feed.get(name)
    if len(data) != size:
        raise FeedUnavailable(f'Incomplete download of {name}.')
    return data


def in_period(mode, instant):
    # Both filename and VEC XML clocks are Victorian local times. Midnight
    # is not the end of election night. Retain the later official counts too.
    if mode == 'rehearsal':
        return TRIAL_ZEROS <= instant < TRIAL_END
    return instant >= ELECTION_RESULTS


def preload_zero(name):
    """Recognise the setup zero and the pre-count delivery slots, excluding afternoon internal tests."""
    if name == ZERO:
        return True
    match = RESULT.fullmatch(name)
    if not match:
        return False
    instant = datetime.strptime(match[1], '%Y%m%d_%H%M%S')
    # VEC also retains or reissues its zero files after clearing the trial
    # at noon on Thursday. A matching filename is only a candidate for setup:
    # every vote and the full candidate roster are checked before use.
    return (TRIAL_ZEROS <= instant < TRIAL_RESULTS
            or datetime(2026, 11, 26, 12) <= instant < ELECTION_RESULTS)


def lower_house_root(name, xml):
    root = ET.fromstring(xml)
    event = root.find('./{*}EventIdentifier')
    house = root.find('./{*}Election/{*}ElectionIdentifier')
    if (root.tag.split('}')[-1] != 'MediaFeed' or event is None or event.get('Id') != EVENT
            or house is None or house.findtext('{*}ElectionCategory') != 'LowerHouse'
            or root.find('./{*}Election/{*}House/{*}Contests/{*}Contest') is None):
        raise ValueError(f'{name} does not identify the 2026 Victorian Lower House election')
    return root


def is_zero_count(root):
    # The roster alone does not establish a zero. Each district candidate
    # must have an explicit FP total and all published FP and TCP votes must be zero.
    candidates = root.findall('.//{*}Contest/{*}FirstPreferences/{*}Candidate')
    return (bool(candidates) and all(c.findtext('{*}Votes') is not None for c in candidates)
            and not any(int(v.text or '') for v in root.findall('.//{*}Votes')))


def results_xml(name, data, mode):
    match = RESULT.fullmatch(name)
    if not match:
        raise ValueError(f'Unexpected Lower House results filename: {name}')
    published = datetime.strptime(match[1], '%Y%m%d_%H%M%S')
    xml = member(name, data)
    root = lower_house_root(name, xml)
    created = datetime.fromisoformat(root.attrib['Created'])
    if created.tzinfo is not None:
        created = created.astimezone(ZoneInfo('Australia/Melbourne')).replace(tzinfo=None)
    if mode == 'prepare':
        if not preload_zero(name) or not is_zero_count(root):
            raise ValueError('The preparation file must be the published zero count.')
    elif mode == 'rehearsal':
        # VEC creates the XML shortly before naming the ZIP. A 17:30 zero
        # can therefore say 17:29 inside. Enforce the publication window on
        # the filename, with one cycle of tolerance at the opening for XML.
        if not in_period(mode, published) or not TRIAL_ZEROS - PUBLICATION_INTERVAL <= created < TRIAL_END:
            raise ValueError(f'{name} is outside the requested trial.')
        if ((published < TRIAL_RESULTS or created < TRIAL_RESULTS - PUBLICATION_INTERVAL)
                and any(int(v.text or '') for v in root.findall('.//{*}Votes'))):
            raise ValueError(f'{name} contains votes before the trial count opens at 18:00.')
    else:
        # The first official ZIP can likewise contain XML written a few
        # seconds before 18:00. Allow that lead only for the opening delivery
        # cycle. A renamed Wednesday return can never become a Saturday count.
        opening_delivery = (ELECTION_RESULTS <= published < ELECTION_RESULTS + PUBLICATION_INTERVAL
                            and published - PUBLICATION_INTERVAL <= created < ELECTION_RESULTS)
        if not in_period(mode, published) or not (in_period(mode, created) or opening_delivery):
            raise ValueError(f'{name} is outside the official count.')
    return xml, root


def zero_roster(root):
    return {c.find('./{*}PollingDistrictIdentifier').get('Id'):
            {i.get('Id') for i in c.findall('./{*}FirstPreferences/{*}Candidate/{*}CandidateIdentifier')}
            for c in root.findall('.//{*}Contest')}


def matching_zero(feed, listing, cache, candidate_roster):
    # The November setup zero can disappear when the commission replaces its
    # files. A later genuine zero is equally usable if every district lists
    # exactly the preloaded candidates. Counts stay out of the setup baseline.
    remote = sorted((n for n in listing if preload_zero(n)), key=str.casefold, reverse=True)
    cached = sorted((p.name for p in cache.glob('*.zip') if p.name not in listing and preload_zero(p.name)),
                    key=str.casefold, reverse=True)
    for name in remote + cached:
        data = download(feed, listing, name) if name in listing else (cache / name).read_bytes()
        try:
            xml, zero = results_xml(name, data, 'prepare')
        except ValueError:
            # A pre-opening count, or an XML for another event, cannot be
            # the zero. Continue looking for a genuine matching setup file.
            continue
        if zero_roster(zero) == candidate_roster:
            return name, data, xml
    raise ValueError('No published or cached Lower House zero matches the current candidate roster.')


def preload_files(feed, listing):
    """Read and validate the three setup files and a matching all-zero Lower House roster."""
    cache = PRIVATE / 'setup'
    archives = {name: download(feed, listing, name) for name in SETUP.values()}
    xml = {key: member(name, archives[name]) for key, name in SETUP.items()}
    roots = {key: ET.fromstring(data) for key, data in xml.items()}
    for key, root in roots.items():
        event = root.find('.//{*}EventIdentifier')
        if event is None or event.get('Id') != EVENT:
            raise ValueError(f'Wrong event in the {key} preload')
    contests = roots['candidates'].findall('./{*}CandidateList/{*}Election[1]/{*}Contest')
    candidate_roster = {c.find('./{*}PollingDistrictIdentifier').get('Id'):
                        {i.get('Id') for i in c.findall('./{*}Candidate/{*}CandidateIdentifier')} for c in contests}
    places = roots['booths'].findall('./{*}PollingDistrict/{*}PollingDistrictIdentifier')
    if len(candidate_roster) != 88 or {p.get('Id') for p in places} != set(candidate_roster):
        raise ValueError('The candidate and location preloads do not cover the same 88 districts.')
    zero_name, zero_data, zero_xml = matching_zero(feed, listing, cache, candidate_roster)
    archives[zero_name] = zero_data
    xml['zeros'] = zero_xml
    return archives, xml, zero_name, candidate_roster


def prepare(feed, listing):
    """Install the verified preloads and preserve superseded commission files locally."""
    cache = PRIVATE / 'setup'
    archives, xml, zero_name, candidate_roster = preload_files(feed, listing)
    for name, data in archives.items():
        target = cache / name
        if target.exists():
            previous = target.read_bytes()
            if previous == data:
                continue
            # VEC can reissue a setup under the same filename. Keep the exact
            # earlier roster so the rehearsal can still be examined afterwards.
            write_complete(cache / 'previous' / hashlib.sha256(previous).hexdigest() / name, previous)
        write_complete(target, data)
    for key in ('candidates', 'booths', 'zeros'):
        write_complete(ROOT / 'downloads' / f'2026vic_{key}.xml', xml[key])

    # VEC withdraws samples before the trial. Archived samples have different
    # identities and can never enter either forecast inbox through this tool.
    available = [name for name in listing if name.startswith('Test') and (
        RESULT.fullmatch(name[4:]) or name[4:] in {Path(n).with_suffix('.xml').name for n in SETUP.values()})]
    for name in available:
        target = PRIVATE / 'samples' / name
        if not target.exists() or target.stat().st_size != listing[name][0]:
            data = download(feed, listing, name)
            ET.fromstring(member(name, data) if name.lower().endswith('.zip') else data)
            write_complete(target, data)
    print(f'Installed preloads for {len(candidate_roster)} districts and {sum(map(len, candidate_roster.values()))} candidates. '
          f'Validated {"published" if zero_name in listing else "cached"} zero: {zero_name}. '
          f'{len(available)} VEC sample files currently listed.')


def collect(feed, listing, mode, run_folder, available_at=None):
    manifest = run_folder / 'vec-received.json'
    received = json.loads(manifest.read_bytes()) if manifest.exists() else {}
    inbox = run_folder / 'inbox'
    added = 0
    rejected = []
    for name, (size, etag) in sorted(listing.items()):
        match = RESULT.fullmatch(name)
        if not match:
            continue
        try:
            published = datetime.strptime(match[1], '%Y%m%d_%H%M%S')
            if not in_period(mode, published) or (available_at is not None and published > available_at):
                continue
            target = inbox / name
            if name in received and target.is_file() and target.stat().st_size == size and received[name] == etag:
                continue
            data = download(feed, listing, name)
            results_xml(name, data, mode)
            if target.exists() and target.read_bytes() != data:
                # A changed ZIP with the same identity needs review. Preserve
                # both versions, with only the first one in the watched inbox.
                digest = hashlib.sha256(data).hexdigest()
                conflict = run_folder/'vec-conflicts'/digest/name
                if not conflict.is_file():
                    write_complete(conflict, data)
                raise ValueError(f'The published contents changed under an existing timestamp. See {conflict}.')
        except (FeedUnavailable, ValueError, KeyError, ET.ParseError, BadZipFile) as exc:
            # One damaged, replaced or mislabelled file must not hold up newer
            # counts. Keep the original, report the error and retry on the next poll.
            rejected.append(f'{name}: {exc}')
            continue
        delivered = not target.exists()
        if delivered:
            write_complete(target, data)
        received[name] = etag
        write_complete(manifest, json.dumps(received, indent=2).encode())
        if delivered:
            added += 1
            print(f'{mode}: delivered {name}', flush=True)
    if rejected:
        raise FeedUnavailable('\n'.join(rejected))
    return added


def poll_results(feed, mode, folder, watch):
    first_scan = True
    if watch:
        print(f'Watching the VEC Lower House feed for {RUN_IDS[mode]} -> {folder / "inbox"}', flush=True)
    while True:
        now = datetime.now(ZoneInfo('Australia/Melbourne')).replace(tzinfo=None)
        final_scan = mode == 'rehearsal' and now >= TRIAL_FINAL_SCAN
        try:
            added = collect(feed, feed.listing(), mode, folder, now)
            if not watch:
                print(f'{added} new {mode} captures.')
                return
            if first_scan:
                print(f'Connected. First scan delivered {added} new capture(s).', flush=True)
                first_scan = False
            # Allow delayed uploads to be collected, including a catch-up run
            # the next morning while the VEC still has the trial files online.
            if final_scan:
                print('Final trial scan complete.', flush=True)
                return
        except FeedUnavailable as exc:
            if not watch or final_scan:
                raise
            print(f'{exc} Retrying in one minute.', file=sys.stderr, flush=True)
        time.sleep(60)


def check_installed_feed(feed):
    """Compare the local forecasting inputs with a fresh, authenticated reading of the VEC feed."""
    listing = feed.listing()
    _, xml, zero_name, roster = preload_files(feed, listing)
    print(f'Azure read/list OK. {len(roster)} districts, {sum(map(len, roster.values()))} candidates. '
          f'Validated {"published" if zero_name in listing else "cached"} zero {zero_name}.')
    problems = []
    for key in ('candidates', 'booths'):
        if (ROOT/'downloads'/f'2026vic_{key}.xml').read_bytes() != xml[key]:
            problems.append(f'The installed {key} file differs from the VEC file. Refresh the election inputs.')
    groups = SETUP['groups']
    if member(groups, (PRIVATE/'setup'/groups).read_bytes()) != xml['groups']:
        problems.append('The installed group and ticket file differs from the VEC file. Refresh the election inputs.')
    # A commission reissue can have a later timestamp but the same blank
    # count. The installed zero must describe the current field with no votes.
    installed = lower_house_root('installed zero', (ROOT/'downloads/2026vic_zeros.xml').read_bytes())
    if zero_roster(installed) != roster or not is_zero_count(installed):
        problems.append('The installed zero does not match the current candidate roster and zero votes. Refresh the election inputs.')
    if not problems:
        print('Installed candidates, polling locations, groups and zero match the current setup.')
    expiry = parse_qs(feed.query).get('se', [None])[0]
    if expiry:
        expires = datetime.fromisoformat(expiry.replace('Z', '+00:00')).astimezone(ZoneInfo('Australia/Melbourne'))
        print('Feed credential expires ' + expires.strftime('%Y-%m-%d %H:%M %Z') + '.')
        if expires <= max(ELECTION_RESULTS.replace(tzinfo=expires.tzinfo), datetime.now(timezone.utc)):
            problems.append('Renew the feed credential before collecting official results.')
    else:
        print('The credential has no inline expiry. Its server-side expiry was not verified.')
    return problems


def check_election(feed):
    """Check the commission and each publication destination independently, without publishing."""
    import environ
    import requests
    from live_pipeline.protocol import DESTINATIONS, Server

    try:
        problems = check_installed_feed(feed)
    except (FeedUnavailable, OSError, ValueError, KeyError, BadZipFile, ET.ParseError) as error:
        problems = [f'VEC setup check: {error}']

    environ.Env.read_env(str(ROOT/'uploads/.env'))
    if not all(os.environ.get(key) for key in ('AUTO_EMAIL', 'AUTO_PASSWORD')):
        problems.append('The upload manager needs AUTO_EMAIL and AUTO_PASSWORD in its environment or uploads/.env.')
    for name, base in DESTINATIONS.items():
        server = Server(base, '', '')
        try:
            server.check_capabilities()
            print(f'{name}: publication API v1 available.')
        except (requests.RequestException, ValueError) as error:
            problems.append(f'{name}: {error}')
        finally:
            server.session.close()
    if problems:
        raise ValueError('\n'.join(problems))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['prepare', 'check-election', *RUN_IDS])
    parser.add_argument('--watch', action='store_true', help='Poll once a minute. The trial finishes after its last scan at 20:30.')
    args = parser.parse_args(argv)
    if args.watch and args.mode not in RUN_IDS:
        parser.error('--watch is for collecting rehearsal or election results')
    feed = Container(os.environ.get('VEC_SAS_URL', ''))
    if args.mode == 'check-election':
        check_election(feed)
        return
    if args.mode == 'prepare':
        prepare(feed, feed.listing())
        return
    folder = ROOT / 'live_runs' / RUN_IDS[args.mode]
    with run_lock(folder, 'vec-collector.lock'):
        poll_results(feed, args.mode, folder, args.watch)


if __name__ == '__main__':
    try:
        main()
    except (FeedUnavailable, ValueError, KeyError, OSError, BadZipFile, ET.ParseError) as error:
        sys.exit(str(error))
