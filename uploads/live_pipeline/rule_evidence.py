"""Measured booth evidence for the local commentary writer.

The public report compares votes at the places that actually reported with
the earlier forecast for those same places. The local writer can examine
every booth before summarising them. The AI request has a separate size limit.
"""
from collections import defaultdict
import math

from . import activity


def joined(items):
    return ', '.join(items[:-1]) + ' and ' + items[-1] if len(items) > 1 else items[0]


def early_returns(events):
    """Select the three largest additions in a quiet early comparison. Both
    first returns and additions to an existing count qualify. Rank by the
    larger of FP and TCP additions since those counts can cover the same
    ballots. A revised TCP pairing cannot be compared by subtracting totals
    for different opponents; the separate realignment story covers that."""
    units = []
    for event in events:
        if event['kind'] != 'booths' or not event.get('changed_votes'):
            continue
        before, after = event.get('before') or {}, event.get('after') or {}
        fp, tcp = event['fp_vote_change'], event['tcp_vote_change']
        if sum(before.get('tcp_votes_current', {}).values()) and activity.first_tcp_size(before, after) is not None:
            tcp = 0
        if max(fp, tcp) <= 0:
            continue
        units.append(dict(seat=event['seat'], name=after['name'], vote_type=after.get('vote_type'),
                          fp_net_change=fp, tcp_net_change=tcp))
    units.sort(key=lambda u: (-max(u['fp_net_change'], u['tcp_net_change']), u['seat'], u['name']))
    return {'shown': units[:3], 'other_count': max(0, len(units)-3),
            'other_categories': any(u['vote_type'] not in ('Ordinary', None) for u in units[3:])}


def unit_names(units):
    """Name all declaration categories. For a long list, name the two largest
    physical booths (by changed ballots) and count the other physical booths."""
    ordered = sorted(units, key=lambda u: (-u['changed'], u['name']))
    ordinary = [u for u in ordered if not u['declaration']]
    named = ordered if len(ordered) <= 3 else [u for u in ordered if u['declaration'] or u in ordinary[:2]]
    labels = [u['name'] for u in named]
    extra = len(ordered) - len(named)
    if extra:
        labels.append(f'{extra} other booth' + ('s' if extra != 1 else ''))
    return joined(labels)


def booth_projection_after_preferences(old, new, seat, previous, current, ranked_pairs):
    """Find two comparable forecasts for this booth after preferences. A booth
    with FP only can still have a modelled TCP. Compare the same district
    finalists at both dates and leave measured TCP to the counted-vote story."""
    if previous['input_revision'] != current['input_revision'] or any(
            any(b.get('tcp_votes_current', {}).values()) for b in (old, new)):
        return None
    earlier, later = (s['seats'].get(seat, {}) for s in (previous, current))
    frequencies = later.get('pairing_percent', {})
    if not frequencies:
        return None
    pair = max(frequencies, key=frequencies.get).split('/')
    if len(set(pair)) != 2 or len(pair) != 2 or (seat, frozenset(pair)) in ranked_pairs:
        # A ranked allocation for the whole category does not identify how
        # its transfers split at this booth. The older booth TCP is a
        # different calculation and cannot supply the missing ranked share.
        return None
    for party in pair:
        candidate = later.get('candidates', {}).get(party) or {}
        if not candidate.get('name') or candidate != earlier.get('candidates', {}).get(party):
            return None

    # The TPP export always labels the Coalition side as party 1, even
    # when it represents a National. Use the ballot to establish the sole
    # Coalition opponent before interpreting this as a named TCP. A direct
    # booth TCP export already identifies its two candidates.
    fields = [('tcp_votes_projected', pair)]
    if '0' in pair:
        opponent = next(p for p in pair if p != '0')
        for summary, booth in ((previous, old), (current, new)):
            coalition = {p for p in booth.get('fp_votes_projected', {})
                         if summary.get('parties', {}).get(p) in ('LIB', 'LNP', 'NAT', 'CLP')}
            if coalition != {opponent}:
                break
        else:
            fields.insert(0, ('tpp_votes_projected', ['0' if p == '0' else '1' for p in pair]))
    for field, ids in fields:
        shares = []
        for booth in (old, new):
            votes = booth.get(field) or {}
            if (votes.keys() != set(ids) or any(not math.isfinite(v) or v < 0 for v in votes.values())
                    or sum(votes.values()) <= 0):
                break
            shares.append(100 * votes[ids[0]] / sum(votes.values()))
        if len(shares) == 2:
            return {pair[0]: shares, pair[1]: [100-v for v in shares]}
    return None


def booth_units(events):
    """Read cumulative results only for changed ledgers with a comparable
    earlier estimate. FP and each distinct TCP pair remain separate. The
    comparison stage already checks candidate identities and the ranked
    model's benchmark for a TCP. A withdrawal has no new split to quote."""
    by_seat = defaultdict(list)
    for event in events:
        if event['kind'] != 'booths':
            continue
        old, new = event.get('before') or {}, event.get('after') or {}
        name = new.get('name') or event['record'].rsplit('/', 1)[-1]
        category = new.get('vote_type') or 'Ordinary'
        declaration = category.casefold() != 'ordinary'
        label = name
        if declaration:
            label = ('the '+category+' count' if name.casefold() == category.casefold()
                     else name+' ('+category+' count)')
        for ledger in ('FP', 'TCP'):
            field = ledger.lower() + '_votes_current'
            measured = new.get(field, {})
            ballots = sum(measured.values())
            changed = activity.affected_ballots(old.get(field, {}), measured)
            first = sum(old.get(field, {}).values()) == 0
            if ledger == 'TCP':
                first_size = activity.first_tcp_size(old, new)
                first = first_size is not None
                changed = first_size or changed
            if not changed or not ballots:
                continue
            if ledger == 'FP':
                aliases = new.get('count_to_projection_ids', {})
                mapped = defaultdict(int)
                for party, votes in measured.items():
                    mapped[aliases.get(party, party)] += votes
                gaps = event.get('fp_share_minus_previous_expectation_pp') or {}
                rates = {p: (100*mapped[p]/ballots, gap) for p, gap in gaps.items() if p in mapped}
                pair = ()
            else:
                compare = event.get('tcp_against_previous_forecast') or {}
                if compare.get('available'):
                    pair = tuple(sorted(compare['pair']))
                    rates = {p: (r['observed_percent'], r['difference_pp']) for p, r in compare['shares'].items()}
                else:
                    aliases = new.get('count_to_projection_ids', {})
                    pair = tuple(sorted({aliases.get(p, p) for p in measured}))
                    rates = {}
            # An updated declaration with no comparable estimate can still
            # be named separately. It contributes no ballots to a pooled share.
            if rates or declaration:
                by_seat[event['seat']].append(dict(name=label, declaration=declaration, ledger=ledger,
                    category=category, pair=pair, first=first, ballots=ballots, changed=changed, rates=rates,
                    tcp_estimates=event.get('rule_booth_tcp_estimates') if ledger == 'FP' else None))
    return by_seat


def pool(units, party):
    """Weight both the measured shares and the earlier booth estimates by
    the same current ballot totals. A booth expected to receive 500 votes
    which reports 300 contributes 300 to both sides. Only compare rivals
    measured across the very same set of booths."""
    groups = defaultdict(list)
    for unit in units:
        if party in unit['rates']:
            # A one-vote correction to an old 2,000-vote count must not
            # outweigh 300 genuinely new votes in a neighbouring booth.
            groups[(unit['ledger'], unit['pair'], unit['first'])].append(unit)
    result = []
    for (ledger, pair, first), batch in groups.items():
        ballots = sum(u['ballots'] for u in batch)
        parties = set.intersection(*(set(u['rates']) for u in batch))
        rates = {p: [sum(u['rates'][p][i]*u['ballots'] for u in batch)/ballots for i in (0, 1)]
                 for p in sorted(parties)}
        categories = {u['category'] for u in units if u['declaration'] and (u['ledger'], u['pair']) == (ledger, pair)}
        other_categories = sorted(categories - {u['category'] for u in batch if u['declaration']})
        result.append(dict(ledger=ledger, pair=list(pair), first=first, locations=unit_names(batch), units=len(batch),
            declaration=batch[0]['declaration'] if len(batch) == 1 else False, ballots=ballots,
            changed=sum(u['changed'] for u in batch), rates=rates, other_categories=other_categories))
        # Use every booth named in the FP sentence and its same ballot weight
        # at both dates. A projection for only part of the group can tell a
        # different story, particularly when a large centre is missing.
        estimates = batch[0].get('tcp_estimates') or {}
        if party in estimates and all((u.get('tcp_estimates') or {}).keys() == estimates.keys() for u in batch):
            result[-1]['tcp_estimates'] = {p: [sum(u['tcp_estimates'][p][i] * u['ballots'] for u in batch)
                                                / ballots for i in (0, 1)] for p in estimates}
    return result


def supported_flows(events, flows):
    """A changing modelled flow earns a headline when matched counts for
    those opponents also changed and included that source's voters. The
    booth split cannot identify an individual voter's preference. Require
    100 original-source ballot equivalents at changed, approximately matched
    counts across two districts before giving a statewide flow a headline."""
    supported = []
    for flow in flows:
        source, first, second = flow['parties']
        source_votes = 0
        districts = set()
        for e in events:
            if e['kind'] != 'booths' or not e.get('changed_votes'):
                continue
            old, new = e.get('before') or {}, e.get('after') or {}
            aliases = new.get('count_to_projection_ids', {})
            fp = new.get('fp_votes_current', {})
            tcp = new.get('tcp_votes_current', {})
            if {aliases.get(p, p) for p in tcp} != {first, second}:
                continue
            total = sum(fp.values())
            if not total or not sum(tcp.values()) or abs(total-sum(tcp.values())) > .05*total:
                continue
            # A one-ballot recheck of an old matched booth supplies at most
            # one ballot of fresh evidence, even if the booth itself is large.
            changed = max(activity.affected_ballots(old.get('fp_votes_current', {}), fp),
                          activity.first_tcp_size(old, new) or activity.affected_ballots(old.get('tcp_votes_current', {}), tcp))
            fresh = sum(v for p, v in fp.items() if aliases.get(p, p) == source)*min(1, changed/total)
            if fresh > 0:
                source_votes += fresh
                districts.add(e.get('seat'))
        if source_votes >= 100 and len(districts) >= 2:
            supported.append(flow)
    return supported


def major_district_moves(rows):
    """Sum the absolute endpoint changes in each district's win chances, in
    percentage points. Both sides of a transfer count: a 13-point gain paired
    with a 13-point loss totals 26. Count changes for comparable identities."""
    totals = defaultdict(float)
    for row in rows:
        if row.get('identity_changed'):
            continue
        seat, change = row['seat'], row['change_pp']
        totals[seat] += abs(change)
    return [seat for seat in sorted(totals, key=lambda s: (-totals[s], s))
            if totals[seat] > 25]


def primary_centre(prior, count, ordinary, weight):
    """One candidate's central FP percentage before balancing the district."""
    def share(log_odds):
        odds = math.exp(-abs(log_odds))
        return (1 if log_odds >= 0 else odds)/(1+odds)

    legacy = share((1-weight)*prior + weight*count)
    if ordinary is None:
        return 100*legacy
    # Keep counted ballots fixed when asking what a different precision
    # would have done. Unmatched ordinary returns retain the booth estimate.
    row, remaining = ordinary
    response = weight + .5*row['breadth']*(1-weight)**2
    matched = share(row['prior_remaining_log_odds'] + response*row['local_log_odds_change'])
    booth = row['booth_remaining_share']
    future = booth + row['matched_fraction']*(matched-booth)
    fixed = row['fixed_fraction']
    district = fixed + remaining*future
    legacy = max(fixed, legacy)
    return 100*(legacy + row['ordinary_fraction']*(district-legacy))


def primary_weight_check(event):
    """Compare the central local FP calculation at the two dates, then repeat
    the later calculation holding only the count weights at the earlier values.
    Each record holds three FP percentages for one individually matched party
    and the change in its count weight (a fraction between -1 and 1).
    These centres precede draw-by-draw normalisation and counted-vote floors.
    The writer checks the published district means separately before using them."""
    snapshots = []
    for side in ('before', 'after'):
        model = (event.get(side) or {}).get('fp_integration') or {}
        if model.get('method') not in ('local_precision', 'ordinary_vote_blend'):
            return {}
        ordinary = {str(row['party_index']): (row, model['remaining_fraction'])
                    for row in model.get('ordinary_comparisons', [])}
        fields = {}
        for key in ('baseline_log_odds_mean', 'count_log_odds_mean', 'individual_count_weights', 'shared_deviation'):
            fields[key] = {str(r['party_index']): r['value'] for r in model.get(key, [])
                           if isinstance(r.get('value'), (float, int)) and math.isfinite(r['value'])}
        snapshot = {}
        for party, weight in fields['individual_count_weights'].items():
            prior = fields['baseline_log_odds_mean'].get(party)
            count = fields['count_log_odds_mean'].get(party)
            if prior is None or count is None or not 0 <= weight <= 1:
                continue
            # LiveFpIntegration uses natural log odds. The shared election
            # and regional movement applied to the prior uses 25 times that scale.
            shift = fields['shared_deviation'].get(party, 0) / 25
            snapshot[party] = (prior + shift, count, ordinary.get(party), weight)
        snapshots.append(snapshot)

    before, after = snapshots
    return {p: dict(before=primary_centre(*before[p]), previous_weight=primary_centre(*after[p][:-1], before[p][-1]),
                    after=primary_centre(*after[p]), weight_change=after[p][-1] - before[p][-1])
            for p in before.keys() & after.keys()}


def context(comparison, win_rows, flows):
    """Save compact summaries for candidates the writer can discuss. Exclude
    transfers between declaration labels: those ballots were already counted."""
    moved = {i for transfer in comparison.get('category_transfers', []) for i in transfer.get('evidence_ids', [])}
    events = [e for e in comparison['evidence'] if e.get('id') not in moved]
    units = booth_units(events)
    pools = {}
    for row in win_rows:
        seat, party = row['seat'], row['party_id']
        if seat in units:
            pools.setdefault(seat, {})[party] = pool(units[seat], party)
    counted = {e.get('seat') for e in events if e.get('changed_votes')}
    large = {r['seat'] for r in comparison['win_changes'] if not r.get('identity_changed') and
             r['seat'] in counted and abs(r['change_pp']) >= 5}
    # Only a growing district count warrants a story about the accumulating
    # evidence. Transfers between reporting categories were removed above.
    net_fp = defaultdict(int)
    for event in events:
        if event['kind'] == 'booths':
            net_fp[event.get('seat')] += event.get('fp_vote_change', 0)
    weights = {e['seat']: primary_weight_check(e) for e in events if e['kind'] == 'seats'
               and e.get('seat') in pools and net_fp[e['seat']] > 0}
    return {'pools': pools, 'primary_weight_checks': weights,
            'large_local_moves': sorted(large), 'supported_flows': supported_flows(events, flows),
            'regional': comparison.get('rule_regional', []),
            'regional_change': comparison.get('rule_regional_change')}


def regional_counts(analysis):
    """Compare Labor's measured share at matched election-day booths with its
    share at those booths last election. Use current booth size on both sides
    and keep only same-seat matches. For TCP the same Coalition party must
    face Labor both times. Declaration votes and new booths cannot establish
    this particular swing. Save small regional totals once per forecast."""
    regions = {s['name']: s.get('region_name') for s in analysis.get('seats', [])}
    totals = {}
    for b in analysis.get('booths', []):
        seat = b.get('seat_name')
        region = regions.get(seat)
        if not region or not b.get('same_seat') or b.get('booth_type') != 'Normal' or b.get('vote_type') != 'Ordinary':
            continue
        node = b.get('node', {})
        for ledger in ('fp', 'tcp'):
            old, new = [node.get(ledger+'_votes_'+side, []) for side in ('previous', 'current')]
            old_n, new_n = [sum(r['value'] for r in rows) for rows in (old, new)]
            if not min(old_n, new_n):
                continue
            if ledger == 'tcp':
                pairs = [set(r.get('abbreviation') for r in rows) for rows in (old, new)]
                if pairs[0] != pairs[1] or pairs[0] not in ({'ALP','LIB'}, {'ALP','LNP'}, {'ALP','NAT'}):
                    continue
            labor = [next((r['value'] for r in rows if r.get('abbreviation') == 'ALP'), None) for rows in (old, new)]
            if None in labor:
                continue
            row = totals.setdefault(ledger, {}).setdefault(region, {'votes':0, 'gain':0, 'booths':0, 'districts':{}})
            district = row['districts'].setdefault(seat, {'votes':0, 'gain':0, 'booths':0})
            gain = labor[1] - new_n * labor[0]/old_n
            row['votes'] += new_n
            row['gain'] += gain
            row['booths'] += 1
            district['votes'] += new_n
            district['gain'] += gain
            district['booths'] += 1
    for ledger in totals.values():
        for row in ledger.values():
            districts = row.pop('districts')
            row['seats'] = len(districts)
            # A few early booths can give a misleading impression of diversity
            # within a region. Use districts with three booths and 1,000 votes,
            # covering at least half the regional ballots between them. Measure
            # their distance from the regional average even when all swings
            # are in the same direction. The weights match the regional swing.
            measured = [d for d in districts.values() if d['booths'] >= 3 and d['votes'] >= 1000]
            votes = sum(d['votes'] for d in measured)
            if len(measured) >= 3 and votes >= row['votes']/2:
                average = row['gain']/row['votes']
                row['mean_absolute_deviation_pp'] = 100*sum(abs(d['gain']-average*d['votes']) for d in measured)/votes
    return totals


def regional_contrast(before, after):
    """Select a substantial contrast in swings across several districts. Require a net
    increase in matched coverage so unchanged regional figures do not recur
    in each short update. These thresholds are editorial, in ballots and pp."""
    for ledger in ('tcp', 'fp'):
        regions = after.get(ledger, {})
        observed = []
        for region, r in regions.items():
            if r['votes'] >= 1000 and r['booths'] >= 5 and r['seats'] >= 3:
                observed.append((100*r['gain']/r['votes'], region))
        if len(observed) < 2:
            continue
        low, high = min(observed), max(observed)
        if high[0]-low[0] < 4:
            continue
        selected = (high, low)
        growth = sum(max(0, regions[name]['votes']-before.get(ledger, {}).get(name, {}).get('votes', 0))
                     for _, name in selected)
        old_rows = [before.get(ledger, {}).get(name, {}) for _, name in selected]
        newly_established = any(r.get('votes', 0) < 1000 or r.get('booths', 0) < 5 or r.get('seats', 0) < 3
                                for r in old_rows)
        revised = any(r.get('votes') and abs(swing - 100*r['gain']/r['votes']) >= 1
                      for (swing, _), r in zip(selected, old_rows))
        if (newly_established or revised) and growth >= max(500, .1*sum(regions[name]['votes'] for _, name in selected)):
            return [{'region':name, 'ledger':ledger.upper(), 'swing':swing, 'booths':regions[name]['booths'],
                     'spread':regions[name].get('mean_absolute_deviation_pp')}
                    for swing, name in selected]
    return []


def regional_update(before, after):
    """Find the largest revision to an already established regional swing.
    Compare the cumulative matched-booth swing at the two forecast dates.
    A region crossing the minimum coverage for the first time has no reliable
    starting figure. Its historical comparison belongs in the counting overview."""
    candidates = []
    for ledger in ('tcp', 'fp'):
        for region, current in after.get(ledger, {}).items():
            earlier = before.get(ledger, {}).get(region)
            if not earlier or any(r['votes'] < 1000 or r['booths'] < 5 or r['seats'] < 3
                                  for r in (earlier, current)):
                continue
            # One percentage point and 500 additional matched votes (or 10%
            # of the current sample) keep small, unstable shifts off the headline.
            if current['votes'] - earlier['votes'] < max(500, .1*current['votes']):
                continue
            old, new = [100*r['gain']/r['votes'] for r in (earlier, current)]
            if abs(new-old) >= 1:
                candidates.append(dict(region=region, ledger=ledger.upper(), before=old,
                                       after=new, booths=current['booths']))
    return max(candidates, key=lambda r: abs(r['after']-r['before']), default=None)
