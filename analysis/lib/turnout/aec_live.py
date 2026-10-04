"""Read retained AEC House counts and reproduce deterministic LiveV2 sizes.

The baseline is a count-only reference to the current C++ rules, not a GUI
simulation export. It has no party-share model, preparation noise or winner
probabilities. Candidate IDs remain source IDs rather than project party IDs.
"""

from collections import defaultdict
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile

import numpy as np


NS = dict(a='http://www.aec.gov.au/xml/schema/mediafeed', e='urn:oasis:names:tc:evs:schema:eml')
DECLARATIONS = dict(Absent='absent', PrePoll='early_declaration', Provisional='other', Postal='postal')
BASELINE_VERSION = 'livev2-deterministic-counts-1'


def calibration_exclusion_reason(event_id, seat, identifier):
    """Identify known reporting changes that cannot measure booth behaviour.

    In Federal 2025, Rockingham PPVC's temporary return was consolidated into
    Rockingham Central. Neither identity has a comparable individual final
    size. Keep its counted votes and prediction identity, but exclude it from
    learning size trends or compensation. Scope the exception to this election.
    """
    if event_id == '31496' and seat == 'Brand' and identifier in ('58768', '97689'):
        return 'Rockingham/Rockingham Central reporting consolidation in Federal 2025.'
    return None


def xml_root(path):
    """Read a retained XML or the single House/Senate result member of its ZIP."""
    path = Path(path)
    if path.suffix.lower() == '.zip':
        with zipfile.ZipFile(path) as archive:
            members = [n for n in archive.namelist() if n.lower().endswith('.xml') and 'results-detailed' in n.lower()]
            if len(members) != 1:
                raise ValueError('Expected one detailed result XML in ' + str(path))
            return ET.fromstring(archive.read(members[0]))
    return ET.parse(path).getroot()


def integer(text):
    """A missing measurement is not a zero; official numeric zeros are valid."""
    if text is None or not text.strip() or int(text) < 0:
        raise ValueError('Missing or invalid AEC count.')
    return int(text)


def booth_metadata(path):
    """Retain the current preload's district, name and booth classification.

    Light results contain IDs but often omit names/types. The polling-district
    preload is therefore required, rather than guessing types from a name.
    """
    root = xml_root(path)
    event = root.find('a:PollingDistrictList/e:EventIdentifier', NS).get('Id')
    if event != '31496':
        raise ValueError('Current polling-place preload is not Federal 2025.')
    result = {}
    for district in root.findall('a:PollingDistrictList/a:PollingDistrict', NS):
        seat = district.findtext('a:PollingDistrictIdentifier/a:Name', namespaces=NS)
        for place in district.findall('a:PollingPlaces/a:PollingPlace', NS):
            identity = place.find('a:PollingPlaceIdentifier', NS)
            identifier = identity.get('Id')
            if identifier in result:
                raise ValueError('Duplicate current AEC booth ID: ' + identifier)
            result[identifier] = dict(seat_name=seat, name=identity.get('Name'),
                                      classification=identity.get('Classification', 'Normal'),
                                      position=preload_position(place))
    return result


def preload_position(place):
    """Keep election-day coordinates for screening a centre's service area.

    These are prediction inputs from the preload, rather than coordinates
    borrowed retrospectively from the final results. Missing locations remain
    unknown; they must not look like a site at latitude/longitude zero.
    """
    namespace = '{urn:oasis:names:tc:ciq:xsdschema:xAL:2.0}'
    latitude = place.findtext('.//' + namespace + 'AddressLatitude')
    longitude = place.findtext('.//' + namespace + 'AddressLongitude')
    if not latitude or not longitude:
        return None
    position = [float(latitude), float(longitude)]
    return position if position != [0., 0.] else None


def read_house(path, metadata=None, event_id=None):
    """Read actual candidate counts, excluding historical Ghost candidates.

    Booth totals reconcile to ordinary counts, and declarations are added from
    district vote-type totals exactly once. Formal summary rows provide a
    second accounting check; TCP and declared-winner status are never substituted
    for first-preference completion or used as finalisation flags.
    """
    root = xml_root(path)
    event = root.find('a:Results/e:EventIdentifier', NS).get('Id')
    if event_id and event != event_id:
        raise ValueError('Unexpected AEC event: ' + event)
    house = next(e for e in root.findall('a:Results/a:Election', NS)
                 if e.find('e:ElectionIdentifier', NS).get('Id') == 'H')
    seats, booths = {}, {}
    for contest in house.findall('a:House/a:Contests/a:Contest', NS):
        identity = contest.find('e:ContestIdentifier', NS)
        name, identifier = identity.findtext('e:ContestName', namespaces=NS), identity.get('Id')
        fp = contest.find('a:FirstPreferences', NS)
        typed = defaultdict(int)
        for candidate in fp.findall('a:Candidate', NS):
            for votes in candidate.findall('a:VotesByType/a:Votes', NS):
                typed[votes.get('Type')] += integer(votes.text)
        if set(typed) != {'Ordinary', *DECLARATIONS}:
            raise ValueError('Unexpected or missing AEC vote categories: ' + name)
        formal = integer(fp.findtext('a:Formal/a:Votes', namespaces=NS))
        if sum(typed.values()) != formal:
            raise ValueError('Candidate and formal district counts differ: ' + name)
        seat_booths = []
        for place in contest.findall('a:PollingPlaces/a:PollingPlace', NS):
            place_id = place.find('a:PollingPlaceIdentifier', NS)
            booth_id = place_id.get('Id')
            info = metadata.get(booth_id) if metadata is not None else dict(
                name=place_id.get('Name'), classification=place_id.get('Classification', 'Normal'), seat_name=name)
            if not info or info['seat_name'] != name:
                raise ValueError('Booth preload and current district differ: ' + booth_id)
            candidate_votes = [dict(candidate_id=c.find('e:CandidateIdentifier', NS).get('Id'),
                                    value=integer(c.findtext('a:Votes', namespaces=NS)))
                               for c in place.findall('a:FirstPreferences/a:Candidate', NS)]
            counted = sum(c['value'] for c in candidate_votes)
            if counted != integer(place.findtext('a:FirstPreferences/a:Formal/a:Votes', namespaces=NS)):
                raise ValueError('Candidate and formal booth counts differ: ' + booth_id)
            if booth_id in booths:
                raise ValueError('Duplicate counted AEC booth ID: ' + booth_id)
            booths[booth_id] = dict(info, id=booth_id, counted=counted, candidate_votes=candidate_votes)
            seat_booths.append(booth_id)
        if sum(booths[b]['counted'] for b in seat_booths) != typed['Ordinary']:
            raise ValueError('Booth and district ordinary counts differ: ' + name)
        seats[name] = dict(id=identifier, name=name, enrolment=integer(contest.findtext('a:Enrolment', namespaces=NS)),
                           counted=formal, vote_types=dict(typed), booths=seat_booths)
    return dict(event_id=event, source_time=root.get('Created'), seats=seats, booths=booths)


def live_booth_type(booth):
    """Match LiveV2's PPVC size rule, including its volatile-centre exclusions.

    Divisional Office and BLV centres still belong to the official ordinary
    pre-poll group for category accounting. The current C++ model gives them
    Other booth treatment, so they do not train or receive its PPVC multiplier.
    """
    if 'Divisional Office' in booth['name'] or 'BLV' in booth['name']:
        return 'other'
    return {'PrePollVotingCentre': 'ppvc', 'SpecialHospital': 'hospital',
            'PrisonMobile': 'prison', 'RemoteMobile': 'remote'}.get(booth['classification'], 'ordinary')


def make_units(current, previous, categories, seat_names, aliases=None):
    """Construct immutable current identities and observed previous-count matches.

    Reproduce LiveV2's ID match and four-character PPVC name guard for baseline
    sizes. Adaptation is stricter: only same-seat, exact-name, same-type
    ordinary/PPVC matches are eligible. Optional aliases describe project
    previousName and useFpResults settings; none is inferred for a new district.
    """
    aliases = aliases or {}
    units = []
    for name in seat_names:
        seat = current['seats'][name]
        settings = aliases.get(name, {})
        previous_names = [name] + [settings[k] for k in ('previousName', 'useFpResults') if settings.get(k)]
        previous_seat = next((previous['seats'][n] for n in previous_names if n in previous['seats']), None)
        for identifier in seat['booths']:
            booth = current['booths'][identifier]
            kind = live_booth_type(booth)
            old = previous['booths'].get(identifier)
            if old and kind == 'ppvc' and old['name'][:4].lower() != booth['name'][:4].lower():
                old = None
            same_seat = old is not None and old['seat_name'] in (name, settings.get('previousName'))
            matched = bool(same_seat and old['name'] == booth['name'] and old['counted'] > 0
                           and kind in ('ordinary', 'ppvc') and live_booth_type(old) == kind
                           and old['classification'] == booth['classification'])
            group = 'early_ordinary' if booth['classification'] == 'PrePollVotingCentre' else 'ordinary'
            units.append(dict(seat_name=name, name=booth['name'], id=identifier,
                seat_index=seat_names.index(name), group_index=categories.index(group), group=group,
                kind='ppvc' if kind == 'ppvc' else 'ordinary', baseline_type=kind,
                is_eav=booth['name'].startswith('EAV '),
                position=booth.get('position'),
                previous=old['counted'] if old else None, matched=matched, same_seat=bool(same_seat),
                calibration_exclusion_reason=calibration_exclusion_reason(current.get('event_id'), name, identifier),
                counted=booth['counted'], counted_candidate_votes=booth['candidate_votes'], closed_reason=None))
        for vote_type, group in DECLARATIONS.items():
            units.append(dict(seat_name=name, name=vote_type, id=name + '/' + vote_type,
                seat_index=seat_names.index(name), group_index=categories.index(group), group=group,
                kind='declaration', baseline_type='declaration', vote_type=vote_type,
                previous=previous_seat['vote_types'][vote_type] if previous_seat else None,
                matched=False, counted=seat['vote_types'][vote_type], closed_reason=None))
    return units


def baseline_counts(units):
    """Return deterministic sizes from LiveV2.cpp's current count rules.

    The legacy PPVC factor uses all accepted previous-ID matches with current
    counts, including moved-seat matches; that behaviour is reproduced for a
    fair reference. New-model adaptation separately restricts reliable matches.
    The declaration floor and count maximum intentionally reproduce the legacy
    baseline, rather than introducing these bounds into the transformed model.
    """
    ppvcs = [u for u in units if u['baseline_type'] == 'ppvc' and (u['previous'] or 0) > 0 and u['counted'] > 0]
    multiplier = float(np.float32(2000 + sum(u['counted'] for u in ppvcs)) /
                       np.float32(2000 + sum(u['previous'] for u in ppvcs)))
    counts = []
    for unit in units:
        previous = unit['previous'] or 0
        if unit['kind'] == 'declaration':
            # Preserve C++ float multiplication before integer truncation.
            # For example, a base of 100 rounds just below 105 in float32.
            expected = int(np.float32(max(previous, 30)) * np.float32(1.05))
            counts.append(max(unit['counted'], expected))
        elif unit['counted'] > 0:
            counts.append(unit['counted'])
        elif unit['baseline_type'] == 'ppvc':
            counts.append(float(np.float32(previous or 500) * np.float32(multiplier)))
        else:
            counts.append(previous or (50 if unit['baseline_type'] == 'hospital' else 500))
    return counts, dict(multiplier=multiplier, reported_ppvcs=len(ppvcs),
                        previous_votes=sum(u['previous'] for u in ppvcs), current_votes=sum(u['counted'] for u in ppvcs))
