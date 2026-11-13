"""Keep observed preference counts, their populations and their contexts together.

The catalogue is evidence for a future preference model, not forecast parameters.
An original-source observation follows one candidate's FP ballots to a named
pair. A distribution of preferences instead records exclusions of mixed piles.
Keeping those two forms distinct prevents inherited ballots being attributed to
the wrong original party, or several views of the same ballots being treated as
independent examples of future behaviour.

This module supplies the common representation, validation and bounded JSON I/O.
The source readers translate commission formats into it; no fitting, network
access, simulation or changes to election feeds occur here.
"""

from collections import Counter, defaultdict
import gzip
import hashlib
import json
from pathlib import Path
import re
import unicodedata


SCHEMA_VERSION = 1
MAX_UNCOMPRESSED_BYTES = 32 * 1024 * 1024

# These aliases identify the printed party, not a preference-behaviour estimate.
# In particular NAT stays separate from LIB. The raw affiliation is retained so
# merged LNP/CLP candidates can still be distinguished from Liberal candidates.
PARTY_ALIASES = {
    'LP': 'LIB', 'NP': 'NAT', 'CLP': 'LIB', 'NT CLP': 'LIB',
    'LNP': 'LIB', 'LNQ': 'LIB', 'ONP': 'ON', 'HAN': 'ON',
    # The 2025 supplemental table uses GVIC/FFPA where the FP table prints
    # GRN/FFP, and sometimes leaves the NAFD (unaffiliated) code blank.
    'GVIC': 'GRN', 'FFPA': 'FFP', 'NAFD': 'UNAFFILIATED',
    'Liberal': 'LIB', 'National': 'NAT', 'Australian Greens': 'GRN',
    'The Greens': 'GRN', 'Independent': 'IND',
    'Australian Labor Party': 'ALP',
    'Australian Labor Party - Victorian Branch': 'ALP',
    "Pauline Hanson's One Nation": 'ON',
    'One Nation NSW Division': 'ON',
}


def require(condition, message):
    """Reject broken evidence even when Python is run with assertions disabled."""
    if not condition:
        raise ValueError(message)


def name_tokens(name):
    """Match complete printed names across surname-first/given-name-first tables.

    Only punctuation, accents and token order are ignored. Callers must still
    demand a unique match; this is not permission to guess a candidate by surname.
    """
    plain = unicodedata.normalize('NFKD', name).encode('ascii', 'ignore').decode()
    return tuple(sorted(re.findall(r'[a-z]+', plain.lower())))


def party_group(code):
    return PARTY_ALIASES.get(code, code or 'UNAFFILIATED')


def candidate_record(raw_id, name, party, votes, party_name=None,
                     id_scheme='commission_candidate_id'):
    """Describe one candidate and their original share of a district's formal FP.

    Identifiers are local to a contest: a ballot position from 2001 must never be
    confused with a later commission-wide candidate ID. Political subtypes such
    as centrist or ex-National are not inferred from this candidate's vote size.
    """
    identifier = str(raw_id) if raw_id is not None else 'name:' + hashlib.sha256(
        name.encode('utf-8')).hexdigest()[:16]
    group = party_group(party)
    if group == 'IND':
        kind = 'independent'
    elif group == 'UNAFFILIATED':
        kind = 'unaffiliated'
    else:
        kind = 'party_candidate'
    return dict(id=identifier, raw_id=raw_id, id_scheme=id_scheme, name=name,
                party_code=party, party_name=party_name, party_group=group,
                candidate_type=kind, fp_votes=votes,
                recommendation=dict(status='unknown'),
                preference_class=dict(value='unspecified', basis='not_assigned'))


def contest_record(term, event, seat, state, candidates, primary_sources,
                   fp_basis='separate_official_fp', by_election=False):
    """Make one district/election evidence unit before adding its observations.

    formal_fp_votes includes all formal original votes, including finalists'
    primaries. It is the denominator for source-primary strength, not the smaller
    preference pool. The same-name geography label only identifies repeated
    places; it does not assert unchanged boundaries between elections.
    """
    candidates = sorted(candidates, key=lambda item: item['id'])
    formal = sum(item['fp_votes'] for item in candidates)
    identifier = f'{term}:{event}:{seat}'
    for item in candidates:
        item['fp_fraction'] = item['fp_votes'] / formal if formal else None
    return dict(id=identifier, election=term, event=str(event), seat=seat,
                state=state, election_type='by_election' if by_election else 'general',
                electoral_system='CPV', formal_fp_votes=formal, fp_basis=fp_basis,
                primary_sources=sorted(primary_sources), candidates=candidates,
                original_source_observations=[], distribution=None,
                same_name_geography=f'{term[4:]}:{state}:{seat}')


def add_original_observation(contest, donor, destinations, source_refs, kind='tcp',
                             exhausted=0, status='accepted', reason=None, locator=None,
                             reported_parties=None):
    """Add a measured view of one source's original ballots, merging exact copies.

    Every view carries the same source_ballots key across TCP and alternative
    TPP pairings. Different pairs may reveal additional information, but are not
    independent new voters. Equal duplicate tables become one observation with
    multiple references; contradictory duplicates require inspection.
    """
    identities = {item['id']: item for item in contest['candidates']}
    require(donor in identities, f'{contest["id"]}: unknown original source {donor}')
    destinations = sorted(destinations, key=lambda item: item['candidate'])
    pair = ','.join(item['candidate'] for item in destinations)
    observation = dict(
        id=f'{contest["id"]}:original:{donor}:to:{pair}',
        evidence_kind='original_source_to_pair', source_candidate=donor,
        source_ballots=f'{contest["id"]}:original:{donor}',
        denominator_kind='original_first_preferences',
        denominator_votes=identities[donor]['fp_votes'],
        destinations=destinations, exhausted_votes=exhausted, views=[kind],
        source_refs=sorted(source_refs), status=status,
    )
    if reason:
        observation['exclusion_reason'] = reason
    if locator:
        observation['source_locator'] = locator
    if reported_parties:
        observation['reported_party_codes'] = reported_parties
    for existing in contest['original_source_observations']:
        if existing['id'] != observation['id']:
            continue
        comparable = ['denominator_votes', 'destinations', 'exhausted_votes', 'status']
        require(all(existing[key] == observation[key] for key in comparable),
                f'Conflicting duplicate preference observation: {observation["id"]}')
        existing['source_refs'] = sorted(set(existing['source_refs'] + source_refs))
        existing['views'] = sorted(set(existing['views'] + [kind]))
        if reported_parties:
            existing.setdefault('reported_party_codes', {}).update(reported_parties)
        return
    contest['original_source_observations'].append(observation)


def attach_distribution(contest, stages, continuing, source_refs):
    """Preserve each exclusion and the remaining count, including an early stop.

    A recipient's one-stage original-source bounds allow the inherited ballots
    to occupy any part of that transfer. They are arithmetic limits, not a
    probability distribution. The complete sequence remains available for joint
    constraints: separate marginal endpoints need not all occur together.
    """
    originals = {item['id']: item['fp_votes'] for item in contest['candidates']}
    for stage in stages:
        original = originals[stage['excluded_candidate']]
        inherited = stage['pile_votes'] - original
        stage.update(
            inherited_votes=inherited,
            evidence_kind='original_source_exclusion' if inherited == 0 else 'mixed_exclusion',
            denominator_kind='excluded_pile',
        )
        for transfer in stage['transfers']:
            amount = transfer['votes']
            transfer['original_source_vote_bounds'] = [
                max(0, amount - inherited), min(original, amount)]
    contest['distribution'] = dict(
        source_refs=sorted(source_refs), stages=stages,
        continuing=sorted(continuing, key=lambda item: item['candidate']),
        completed_to_pair=len(continuing) == 2,
    )


def apply_contexts(contests, registry):
    """Attach only explicitly verified advice or declared candidate classifications.

    Absence of a registered card stays unknown, including federal years where
    historical changes in advice are suspected. A printed ranking is evidence of
    the recommendation, not a measured rate of voter compliance with that card.
    """
    applied_recommendations = set()
    applied_classes = set()
    for contest in contests:
        for index, context in enumerate(registry['recommendations']):
            if (contest['election'], contest['seat']) != (context['election'], context['seat']):
                continue
            matches = [c for c in contest['candidates']
                       if c['party_group'] == context['source_party']]
            require(len(matches) == 1, f'Ambiguous recommendation source: {context}')
            candidate = matches[0]
            require(candidate['recommendation']['status'] == 'unknown',
                    f'Duplicate recommendation context: {context}')
            targets = []
            for group in context['order_among_targets']:
                recipients = [c for c in contest['candidates'] if c['party_group'] == group]
                require(len(recipients) == 1, f'Ambiguous recommendation recipient: {context}')
                targets.append(recipients[0]['id'])
            recommendation = dict(
                status='verified_order', candidate_order=targets,
                party_order=context['order_among_targets'],
                source_ref=context['source_file'], page=context['page'],
                verification=context['verification'],
            )
            for key in ['registration', 'printed_numbers']:
                if key in context:
                    recommendation[key] = context[key]
            candidate['recommendation'] = recommendation
            applied_recommendations.add(index)
        for index, context in enumerate(registry['candidate_contexts']):
            if (contest['election'], contest['seat']) != (context['election'], context['seat']):
                continue
            matches = [c for c in contest['candidates']
                       if set(context['candidate_name_tokens']).issubset(name_tokens(c['name']))]
            require(len(matches) == 1, f'Ambiguous candidate classification: {context}')
            matches[0]['preference_class'] = dict(
                value=context['preference_class'], basis=context['basis'],
                reference=context['reference'], note=context['note'])
            applied_classes.add(index)
    require(len(applied_recommendations) == len(registry['recommendations']),
            'At least one recommendation context did not match an included contest')
    require(len(applied_classes) == len(registry['candidate_contexts']),
            'At least one candidate context did not match an included contest')


def make_catalogue(contests, registry, input_hashes):
    """Assemble independent district units and explicit links between ballot views."""
    apply_contexts(contests, registry)
    for contest in contests:
        contest['original_source_observations'].sort(key=lambda item: item['id'])
        ballot_views = defaultdict(list)
        for observation in contest['original_source_observations']:
            ballot_views[observation['source_ballots']].append(observation['id'])
        contest['overlapping_source_views'] = {
            key: sorted(values) for key, values in sorted(ballot_views.items()) if len(values) > 1}
    catalogue = dict(
        schema_version=SCHEMA_VERSION,
        purpose='Observed lower-house preference counts and contexts; no fitted forecast parameters.',
        units=dict(counts='formal ballot papers', fractions='0--1',
                   primary_fraction_denominator='all formal FP votes in the same district',
                   original_observation_denominator='original FP votes for the source candidate',
                   exclusion_denominator='all votes in the excluded pile, including inherited ballots'),
        dependence=dict(
            district_unit='contest id; distribution stages remain together',
            repeated_ballots='source_ballots key links views of the same original ballots',
            shared_context='election/event identifies a polling event; same_name_geography identifies repeated places, not independent jurisdictions'),
        inputs=input_hashes,
        sources=registry['files'], publications=registry['supplemental_publications'],
        exclusion_rules=registry.get('quarantine_rules', []),
        coverage_limits=registry['coverage_limits'],
        contests=sorted(contests, key=lambda item: item['id']),
    )
    validate_catalogue(catalogue)
    return catalogue


def _votes(value, where):
    require(type(value) is int and value >= 0, f'{where}: expected non-negative integer votes')


def _validate_references(references, sources, where):
    require(bool(references), f'{where}: missing source reference')
    require(len(references) == len(set(references)), f'{where}: duplicate source reference')
    for reference in references:
        require(reference in sources, f'{where}: unknown source reference {reference}')


def _validate_original(observation, contest, candidates, sources):
    where = observation['id']
    source = observation['source_candidate']
    require(observation['evidence_kind'] == 'original_source_to_pair', f'{where}: wrong evidence kind')
    require(source in candidates, f'{where}: unknown source')
    require(observation['source_ballots'] == f'{contest["id"]}:original:{source}',
            f'{where}: incorrect original-ballot group')
    denominator = observation['denominator_votes']
    _votes(denominator, where)
    require(denominator == candidates[source]['fp_votes'], f'{where}: original FP denominator differs')
    require(observation['denominator_kind'] == 'original_first_preferences',
            f'{where}: wrong denominator kind')
    destinations = observation['destinations']
    recipients = [d['candidate'] for d in destinations]
    require(len(recipients) == 2 and len(set(recipients)) == 2,
            f'{where}: final pair must contain two distinct candidates')
    require(source not in recipients and set(recipients).issubset(candidates),
            f'{where}: invalid original-source recipients')
    expected_id = f'{contest["id"]}:original:{source}:to:{",".join(sorted(recipients))}'
    require(where == expected_id, f'{where}: observation identity does not match its ballots/pair')
    require(bool(observation['views']) and len(observation['views']) == len(set(observation['views']))
            and set(observation['views']).issubset({'tcp', 'alternative_tpp'}),
            f'{where}: unknown or duplicate pairing view')
    for destination in destinations:
        _votes(destination['votes'], where)
    _votes(observation['exhausted_votes'], where)
    require(sum(d['votes'] for d in destinations) + observation['exhausted_votes'] == denominator,
            f'{where}: original-source count does not conserve votes')
    require(observation['status'] in ['accepted', 'quarantined'], f'{where}: unknown status')
    if observation['status'] == 'quarantined':
        require(bool(observation.get('exclusion_reason')), f'{where}: unexplained quarantine')
    _validate_references(observation['source_refs'], sources, where)
    for reference, codes in observation.get('reported_party_codes', {}).items():
        require(reference in observation['source_refs'], f'{where}: unreferenced printed affiliation')
        for key, code in codes.items():
            require(key in candidates and party_group(code) == candidates[key]['party_group'],
                    f'{where}: incompatible printed affiliation')


def _validate_distribution(distribution, contest, candidates, sources):
    """Reconstruct every pile and progressive count without estimating origins."""
    where = contest['id']
    _validate_references(distribution['source_refs'], sources, where)
    current = {key: c['fp_votes'] for key, c in candidates.items()}
    active = set(current)
    for expected_round, stage in enumerate(distribution['stages'], 1):
        label = f'{where} round {expected_round}'
        require(stage['round'] == expected_round, f'{label}: missing or reordered exclusion')
        source = stage['excluded_candidate']
        require(source in active, f'{label}: excluded candidate is not continuing')
        _votes(stage['pile_votes'], label)
        require(stage['pile_votes'] == current[source], f'{label}: wrong excluded pile')
        inherited = stage['pile_votes'] - candidates[source]['fp_votes']
        require(stage['inherited_votes'] == inherited and inherited >= 0,
                f'{label}: wrong inherited count')
        expected_kind = 'original_source_exclusion' if inherited == 0 else 'mixed_exclusion'
        require(stage['evidence_kind'] == expected_kind, f'{label}: wrong evidence classification')
        require(stage['denominator_kind'] == 'excluded_pile', f'{label}: wrong denominator')
        active.remove(source)
        current[source] = 0
        destinations = [t['candidate'] for t in stage['transfers']]
        require(len(destinations) == len(set(destinations)) and set(destinations) == active,
                f'{label}: omitted, repeated or excluded recipient')
        for transfer in stage['transfers']:
            _votes(transfer['votes'], label)
            current[transfer['candidate']] += transfer['votes']
            original = candidates[source]['fp_votes']
            bounds = [max(0, transfer['votes'] - inherited), min(original, transfer['votes'])]
            require(transfer['original_source_vote_bounds'] == bounds,
                    f'{label}: invalid original-source arithmetic bounds')
        require(sum(t['votes'] for t in stage['transfers']) == stage['pile_votes'],
                f'{label}: exclusion does not conserve votes')
        require(stage['progressive_totals'] == current, f'{label}: progressive totals disagree')
        require(sum(current.values()) == contest['formal_fp_votes'], f'{label}: formal total changed')
    continuing = distribution['continuing']
    require(len({c['candidate'] for c in continuing}) == len(continuing), f'{where}: duplicate survivor')
    require({c['candidate']: c['votes'] for c in continuing} == {key: current[key] for key in active},
            f'{where}: final continuing count disagrees')
    require(len(continuing) >= 2 and distribution['completed_to_pair'] == (len(continuing) == 2),
            f'{where}: invented or incorrect final-pair completion')


def validate_catalogue(catalogue):
    """Check populations, source integrity metadata and all overlapping views.

    This validates the supplied facts, not calibration or political plausibility.
    It also checks quarantined rows so an arithmetic failure cannot be concealed
    merely by labelling a record unavailable for fitting.
    """
    require(catalogue['schema_version'] == SCHEMA_VERSION, 'Unsupported preference-evidence schema')
    sources = {**catalogue['sources'], **catalogue['publications']}
    for name, source in catalogue['sources'].items():
        require(re.fullmatch(r'[0-9a-f]{64}', source['sha256']) is not None,
                f'{name}: missing source-artifact hash')
        require(source['url'].startswith('https://'), f'{name}: missing source URL')
    contest_ids = set()
    observation_ids = set()
    for contest in catalogue['contests']:
        where = contest['id']
        require(where == f'{contest["election"]}:{contest["event"]}:{contest["seat"]}',
                f'{where}: contest identity does not match its election/district')
        require(where not in contest_ids, f'Duplicate contest: {where}')
        contest_ids.add(where)
        require(contest['electoral_system'] == 'CPV', f'{where}: OPV needs explicit exhaustion handling')
        candidates = {c['id']: c for c in contest['candidates']}
        require(len(candidates) == len(contest['candidates']) and len(candidates) >= 2,
                f'{where}: duplicate or incomplete candidate roster')
        formal = contest['formal_fp_votes']
        _votes(formal, where)
        require(formal > 0, f'{where}: empty formal count')
        _validate_references(contest['primary_sources'], sources, where)
        for candidate in candidates.values():
            _votes(candidate['fp_votes'], where)
            require(candidate['fp_fraction'] == candidate['fp_votes'] / formal,
                    f'{where}: wrong source-primary denominator')
            require(candidate['party_group'] == party_group(candidate['party_code']),
                    f'{where}: inconsistent party alias')
            recommendation = candidate['recommendation']
            require(recommendation['status'] in ['unknown', 'verified_order'],
                    f'{where}: inferred or invalid HTV status')
            if recommendation['status'] == 'verified_order':
                _validate_references([recommendation['source_ref']], sources, where)
                order = recommendation['candidate_order']
                require(len(order) == len(set(order)) and set(order).issubset(candidates)
                        and candidate['id'] not in order, f'{where}: invalid card recipient order')
                require([candidates[key]['party_group'] for key in order] == recommendation['party_order'],
                        f'{where}: card party order does not match candidates')
        require(sum(c['fp_votes'] for c in candidates.values()) == formal,
                f'{where}: primary roster does not sum to formal votes')
        groups = defaultdict(list)
        pairing_sources = defaultdict(set)
        for observation in contest['original_source_observations']:
            require(observation['id'] not in observation_ids, f'Duplicate observation: {observation["id"]}')
            observation_ids.add(observation['id'])
            _validate_original(observation, contest, candidates, sources)
            for rule in catalogue.get('exclusion_rules', []):
                if ((rule['election'], rule['seat']) == (contest['election'], contest['seat'])
                        and rule['kind'] in observation['views']):
                    require(observation['status'] == 'quarantined'
                            and observation.get('exclusion_reason') == rule['reason'],
                            f'{where}: source exclusion rule was not preserved')
            groups[observation['source_ballots']].append(observation['id'])
            pair = tuple(sorted(d['candidate'] for d in observation['destinations']))
            for view in observation['views']:
                pairing_sources[(view, pair)].add(observation['source_candidate'])
        # The collected pair tables cover every non-finalist's original ballots.
        # Dropping a donor would otherwise make a still-plausible partial table
        # appear to be a complete population. Quarantined rows remain present
        # for this accounting check, while their status bars their use as evidence.
        for (view, pair), represented in pairing_sources.items():
            require(represented == set(candidates) - set(pair),
                    f'{where}: incomplete {view} original-source population for {pair}')
        expected_links = {key: sorted(ids) for key, ids in groups.items() if len(ids) > 1}
        require(contest['overlapping_source_views'] == expected_links,
                f'{where}: incomplete overlapping-ballot links')
        if contest['distribution'] is not None:
            _validate_distribution(contest['distribution'], contest, candidates, sources)


def catalogue_summary(catalogue):
    """Count evidence units and observations without assigning statistical weights."""
    summary = dict(contests=len(catalogue['contests']), accepted_original_source_views=0,
                   quarantined_original_source_views=0, original_ballot_groups=0,
                   overlapping_ballot_groups=0, overlapping_accepted_ballot_groups=0,
                   published_distributions=0, completed_to_pair_distributions=0, exclusion_stages=0,
                   pure_original_exclusions=0, mixed_exclusions=0,
                   verified_recommendations=0, unknown_recommendations=0)
    elections = defaultdict(Counter)
    for contest in catalogue['contests']:
        counts = elections[contest['election']]
        counts['contests'] += 1
        groups = set()
        accepted_counts = Counter()
        for item in contest['original_source_observations']:
            key = ('accepted_original_source_views' if item['status'] == 'accepted'
                   else 'quarantined_original_source_views')
            summary[key] += 1
            counts[key] += 1
            if item['status'] == 'accepted':
                groups.add(item['source_ballots'])
                accepted_counts[item['source_ballots']] += 1
        summary['original_ballot_groups'] += len(groups)
        summary['overlapping_ballot_groups'] += len(contest['overlapping_source_views'])
        summary['overlapping_accepted_ballot_groups'] += sum(n > 1 for n in accepted_counts.values())
        for candidate in contest['candidates']:
            key = ('verified_recommendations' if candidate['recommendation']['status'] == 'verified_order'
                   else 'unknown_recommendations')
            summary[key] += 1
        distribution = contest['distribution']
        if distribution:
            summary['published_distributions'] += 1
            summary['completed_to_pair_distributions'] += int(distribution['completed_to_pair'])
            counts['published_distributions'] += 1
            for stage in distribution['stages']:
                summary['exclusion_stages'] += 1
                key = ('pure_original_exclusions' if stage['evidence_kind'] == 'original_source_exclusion'
                       else 'mixed_exclusions')
                summary[key] += 1
    summary['elections'] = {key: dict(value) for key, value in sorted(elections.items())}
    return summary


def catalogue_bytes(catalogue):
    return json.dumps(catalogue, ensure_ascii=False, sort_keys=True,
                      separators=(',', ':'), allow_nan=False).encode('utf-8') + b'\n'


def write_catalogue(path, catalogue):
    """Write one compact deterministic artifact, after complete validation."""
    validate_catalogue(catalogue)
    raw = catalogue_bytes(catalogue)
    require(len(raw) <= MAX_UNCOMPRESSED_BYTES, 'Preference catalogue exceeds its size budget')
    Path(path).write_bytes(gzip.compress(raw, mtime=0))


def read_catalogue(path):
    with gzip.open(path, 'rb') as stream:
        raw = stream.read(MAX_UNCOMPRESSED_BYTES + 1)
    require(len(raw) <= MAX_UNCOMPRESSED_BYTES, 'Preference catalogue exceeds its size budget')
    catalogue = json.loads(raw)
    validate_catalogue(catalogue)
    return catalogue
