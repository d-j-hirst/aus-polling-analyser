"""Recover explicitly reviewed SA 2026 feed mistakes for retrospective replay.

These conveniences are separate from the live model and raw source captures.
They use reviewed final candidate records to identify already-counted batches;
this is retrospective data repair, not a rule available to a real-time forecast.
No general category merging or automatic anomaly detection is performed here.
"""

import copy
from collections import defaultdict


RECLASSIFIED_SEATS = ('Croydon', 'Taylor')
LABELS = {'Absent': 'Polling Day Absent Ordinary Votes',
          'Provisional': 'Polling Day Declaration Votes',
          'PrePoll': 'Early Voting Absent Ordinary Votes',
          'Early Provisional': 'Early Voting Declaration Votes',
          'EVM': 'Electoral Visitor/Mobile Declaration Votes',
          'TIO': 'Telephone/Interstate/Overseas Declaration Votes'}
PREFIXES = {'Polling Day - Absent Declaration': 'Absent',
            'Polling Day - Declaration': 'Provisional',
            'Early Voting - Absent Declaration': 'PrePoll',
            'Early Voting - Declaration': 'Early Provisional'}


def reviewed_groups(results, metadata):
    """Read the candidate vectors that identify the two reviewed batch errors.

    The caller selects the raw final revision named by the normalized dataset,
    and records both source hashes. Candidate numbers are ballot positions in
    these ECSA sources; the live XML includes the district prefix as well.
    """
    output = {}
    names = {d['districtName'] for d in metadata['districts']}
    for district in results['districts']:
        name = district['districtId']
        if name not in names:
            raise ValueError('Reviewed SA district metadata differs: ' + name)
        if name not in RECLASSIFIED_SEATS:
            continue
        counts = defaultdict(lambda: defaultdict(int))
        for row in district['absentOrdinary'] + district['declarations']:
            label = row.get('absentOrdinaryType', row.get('declarationType'))
            category = next((v for k, v in PREFIXES.items() if label.startswith(k)), None)
            if category:
                for candidate in row['candidateVotes']:
                    counts[str(candidate['candidateId'])][category] += candidate['votes']
        output[name] = {k: dict(v) for k, v in counts.items()}
    return output


def apply_reviewed_overrides(current, reviewed, missing_categories=None):
    """Restore identifiable batches without changing any candidate's vote total.

    In these two seats the first polling-day batch is the complete absent batch
    under a provisional label. Once each candidate's combined live count contains
    their reviewed absent count, restore that batch and leave the residual as
    provisionals. Earlier incomplete batches are left untouched. The early batch
    is reassigned only when its entire candidate vector matches the reviewed
    vector. Neither operation inserts votes that are not already counted.

    Known missing category entries are removed from the analytical account,
    not declared complete or assigned an invented zero. Related categories are
    left intact; their detailed final partition remains excluded from scoring.
    """
    result = copy.deepcopy(current)
    events = []
    for name, target in reviewed.items():
        seat = result['seats'].get(name)
        if seat is None:
            continue
        groups = seat['candidate_groups']
        identities = {i: str(int(i[-3:])) for i in groups}
        if set(identities.values()) != set(target):
            raise ValueError('Reviewed and live candidate identities differ: ' + name)
        for first, second in (('Absent', 'Provisional'), ('PrePoll', 'Early Provisional')):
            combined = {i: r.get(first, 0) + r.get(second, 0) for i, r in groups.items()}
            absent = {i: target[k].get(first, 0) for i, k in identities.items()}
            if first == 'Absent':
                applicable = sum(absent.values()) > 0 and all(combined[i] >= v for i, v in absent.items())
            else:
                applicable = all(combined[i] == absent[i] + target[k].get(second, 0)
                                 for i, k in identities.items())
            if not applicable:
                continue
            before = [seat['vote_types'].get(c) for c in (first, second)]
            for i, row in groups.items():
                row[first], row[second] = absent[i], combined[i] - absent[i]
            after = [sum(r[c] for r in groups.values()) for c in (first, second)]
            for c, value in zip((first, second), after):
                seat['vote_types'][c] = seat['booths'][LABELS[c]] = value
            if before != after:
                events.append(dict(seat=name, categories=[first, second], before=before, after=after,
                                   reason='Reviewed candidate-level batch relabelling; hindcast only.'))
    for name, categories in (missing_categories or {}).items():
        seat = result['seats'].get(name)
        if seat is None:
            continue
        for category in categories:
            if seat['vote_types'].get(category) != 0:
                continue
            seat['vote_types'][category] = None
            seat['booths'][LABELS[category]] = None
            seat.setdefault('missing_categories', []).append(category)
            for row in seat['candidate_groups'].values():
                row.pop(category, None)
            events.append(dict(seat=name, categories=[category], before=[0], after=[None],
                               reason='Reviewed missing category count; no completion inference.'))
    for name, seat in result['seats'].items():
        if sum(v for v in seat['vote_types'].values() if v is not None) != seat['counted']:
            raise ValueError('Hindcast repair changed the counted district total: ' + name)
    result['hindcast_overrides'] = events
    return result
