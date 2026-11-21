"""Collect the VEC Lower House results for the 2026 rehearsal or election.

Preparation installs the real candidate and location rosters and keeps VEC test
material separately. During counting a completed, validated ZIP appears in the
selected run's inbox in one rename, ready for the forecast watcher.
"""

import argparse
from datetime import datetime
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
from urllib.parse import quote, urlencode, urlsplit
from urllib.request import build_opener, HTTPRedirectHandler, Request
from xml.etree import ElementTree as ET
from zipfile import BadZipFile, ZipFile
from zoneinfo import ZoneInfo


ROOT = Path(__file__).resolve().parents[1]
PRIVATE = ROOT / 'forecasts/2026vic/live-inputs/vec'
CONTAINER = 'https://stpublishedresults.blob.core.windows.net/results'
EVENT = '17872'
SETUP = {'candidates': 'State2026MediaFileCandidatesV2.zip',
         'booths': 'State2026MediaFilePollingLocationsV2.zip',
         'groups': 'State2026MediaFileGroupsAndTicketsV2.zip'}
ZERO = 'State2026mediafilelitepplhv2_20261110_100505.zip'
RESULT = re.compile(r'State2026mediafilelitepplhv2_(\d{8}_\d{6})\.zip', re.I)
RUN_IDS = {'rehearsal': '2026vic-rehearsal', 'election': '2026vic-live'}


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
            page = ET.fromstring(self.get(restype='container', comp='list', marker=marker))
            for blob in page.findall('./Blobs/Blob'):
                blobs[blob.findtext('Name')] = (int(blob.findtext('Properties/Content-Length')),
                                                blob.findtext('Properties/Etag'))
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
        return datetime(2026, 11, 25, 18) <= instant < datetime(2026, 11, 26)
    return instant >= datetime(2026, 11, 28, 18)


def results_xml(name, data, mode):
    xml = member(name, data)
    root = ET.fromstring(xml)
    event = root.find('./{*}EventIdentifier')
    house = root.find('./{*}Election/{*}ElectionIdentifier')
    if (root.tag.split('}')[-1] != 'MediaFeed' or event is None or event.get('Id') != EVENT
            or house is None or house.findtext('{*}ElectionCategory') != 'LowerHouse'
            or root.find('./{*}Election/{*}House/{*}Contests/{*}Contest') is None):
        raise ValueError(f'{name} does not identify the 2026 Victorian Lower House election')
    created = datetime.fromisoformat(root.attrib['Created'])
    if created.tzinfo is not None:
        created = created.astimezone(ZoneInfo('Australia/Melbourne')).replace(tzinfo=None)
    if mode == 'prepare':
        if name != ZERO or any(int(v.text) for v in root.findall('.//{*}Votes')):
            raise ValueError('The preparation file must be the published zero count.')
    elif not in_period(mode, created):
        raise ValueError(f'{name} has an XML creation time outside the requested count.')
    return xml, root


def prepare(feed, listing):
    """Check all three setup archives and the zero together before installing any preload."""
    cache = PRIVATE / 'setup'
    archives = {name: download(feed, listing, name) for name in SETUP.values()}
    archives[ZERO] = download(feed, listing, ZERO) if ZERO in listing else (cache / ZERO).read_bytes()
    xml = {key: member(name, archives[name]) for key, name in SETUP.items()}
    roots = {key: ET.fromstring(data) for key, data in xml.items()}
    for key, root in roots.items():
        event = root.find('.//{*}EventIdentifier')
        if event is None or event.get('Id') != EVENT:
            raise ValueError(f'Wrong event in the {key} preload')
    xml['zeros'], zero = results_xml(ZERO, archives[ZERO], 'prepare')
    contests = roots['candidates'].findall('./{*}CandidateList/{*}Election[1]/{*}Contest')
    candidate_roster = {c.find('./{*}PollingDistrictIdentifier').get('Id'):
                        {i.get('Id') for i in c.findall('./{*}Candidate/{*}CandidateIdentifier')} for c in contests}
    zero_roster = {c.find('./{*}PollingDistrictIdentifier').get('Id'):
                   {i.get('Id') for i in c.findall('./{*}FirstPreferences/{*}Candidate/{*}CandidateIdentifier')}
                   for c in zero.findall('.//{*}Contest')}
    places = roots['booths'].findall('./{*}PollingDistrict/{*}PollingDistrictIdentifier')
    if len(candidate_roster) != 88 or zero_roster != candidate_roster or {p.get('Id') for p in places} != set(zero_roster):
        raise ValueError('The candidate, location and zero preloads do not cover the same 88 district rosters.')
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
    print(f'Installed preloads for {len(places)} districts and {sum(map(len, candidate_roster.values()))} candidates. '
          f'{len(available)} VEC sample files currently listed.')


def collect(feed, listing, mode, run_folder):
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
            if not in_period(mode, datetime.strptime(match[1], '%Y%m%d_%H%M%S')):
                continue
            target = inbox / name
            if name in received and target.is_file() and target.stat().st_size == size and received[name] == etag:
                continue
            data = download(feed, listing, name)
            results_xml(name, data, mode)
            if target.exists() and target.read_bytes() != data:
                raise ValueError('The published contents changed under an existing timestamp.')
        except (FeedUnavailable, ValueError, KeyError, ET.ParseError, BadZipFile) as exc:
            # One damaged, replaced or mislabelled file must not hold up newer
            # counts. Keep the original, report the error and retry on the next poll.
            rejected.append(f'{name}: {exc}')
            continue
        write_complete(target, data)
        received[name] = etag
        write_complete(manifest, json.dumps(received, indent=2).encode())
        added += 1
        print(f'{mode}: delivered {name}', flush=True)
    if rejected:
        raise FeedUnavailable('\n'.join(rejected))
    return added


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['prepare', *RUN_IDS])
    parser.add_argument('--watch', action='store_true', help='Poll once a minute until stopped.')
    args = parser.parse_args(argv)
    if args.watch and args.mode == 'prepare':
        parser.error('--watch is for collecting rehearsal or election results')
    feed = Container(os.environ.get('VEC_SAS_URL', ''))
    if args.mode == 'prepare':
        prepare(feed, feed.listing())
        return
    folder = ROOT / 'live_runs' / RUN_IDS[args.mode]
    while True:
        try:
            added = collect(feed, feed.listing(), args.mode, folder)
            if not args.watch:
                print(f'{added} new {args.mode} captures.')
                return
        except FeedUnavailable as exc:
            if not args.watch:
                raise
            print(f'{exc} Retrying in one minute.', file=sys.stderr, flush=True)
        time.sleep(60)


if __name__ == '__main__':
    try:
        main()
    except (FeedUnavailable, ValueError, KeyError, OSError, BadZipFile, ET.ParseError) as error:
        sys.exit(str(error))
