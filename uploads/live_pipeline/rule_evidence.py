"""Measured booth evidence for the local commentary writer.

The public report compares votes at the places that actually reported with
the earlier forecast for those same places. The local writer can examine
every booth before summarising them. The AI request has a separate size limit.
"""
from collections import defaultdict
import math

from . import activity, booth_tcp, ranked_tcp


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


def booth_projection_after_preferences(old, new, seat, previous, current, ranked_pairs, *, booth_key,
                                       earlier_tcp, expected_fp, measured_fp, reference_evidence, current_evidence):
    """Separate the news in the local FP mix from changes in the district
    outlook. A retained count or a ranked category allocation cannot establish
    a preference estimate for this booth's FP-only ballots."""
    if previous['input_revision'] != current['input_revision'] or any(
            any(b.get('tcp_votes_current', {}).values()) for b in (old, new)):
        return None
    if any(e.get('booth_tcp_bounds', {}).get(booth_key) for e in (reference_evidence, current_evidence)):
        return None
    # Older captures can establish compulsory preferences through the simulator's
    # FP-based TPP account, which is only prepared in a full-preference election.
    optional = reference_evidence.get('optional_preferential')
    if optional or (optional is None and not reference_evidence.get('fp_based_tpp')):
        return None
    earlier, later = (s['seats'].get(seat, {}) for s in (previous, current))
    frequencies = later.get('pairing_percent', {})
    if not frequencies:
        return None
    pair = sorted(max(frequencies, key=frequencies.get).split('/'))
    if len(set(pair)) != 2 or len(pair) != 2 or (seat, frozenset(pair)) in ranked_pairs:
        # A ranked allocation for the whole category does not identify how
        # its transfers split at this booth. The older booth TCP is a
        # different calculation and cannot supply the missing ranked share.
        return None
    for party in pair:
        candidate = later.get('candidates', {}).get(party) or {}
        if not candidate.get('name') or candidate != earlier.get('candidates', {}).get(party):
            return None

    benchmark = earlier_tcp.get((booth_key, frozenset(pair)))
    if not benchmark or not sum(benchmark.values()):
        return None
    coalition = {p for p in new.get('fp_votes_projected', {})
                 if current.get('parties', {}).get(p) in ('LIB', 'LNP', 'LNQ', 'CLP', 'NAT')}
    if not booth_tcp.split(new, pair, coalition):
        return None
    rates = reference_evidence.get('classic_benchmark_labor_rates', {}).get(seat, {}).get('/'.join(pair))
    return booth_tcp.fixed_flow_shares(pair, benchmark, expected_fp, measured_fp, rates)


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
            approximate_tcp = False
            partial_category_fp = None
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
                    approximate_tcp = compare.get('expectation_kind') == ranked_tcp.ARCHIVED_BASIS
                    pair = tuple(sorted(compare['pair']))
                    rates = {p: (r['observed_percent'], r['difference_pp']) for p, r in compare['shares'].items()}
                else:
                    partial_category_fp = compare.get('partial_category_fp_votes')
                    aliases = new.get('count_to_projection_ids', {})
                    pair = tuple(sorted({aliases.get(p, p) for p in measured}))
                    rates = {}
            # An updated declaration with no comparable estimate can still
            # be named separately. It contributes no ballots to a pooled share.
            # A missing same-pair estimate prevents a claim about performance,
            # but both published totals can still establish where counting grew.
            # Don't subtract ballots for a different pair, or describe a
            # reallocation between candidates as a batch of additional votes.
            addition = 0
            restored = False
            if ledger == 'TCP':
                prior = old.get(field) or {}
                if (len(measured) == len(pair) == 2 and (not any(prior.values()) or prior.keys() == measured.keys())
                        and all(v >= prior.get(p, 0) for p, v in measured.items())):
                    addition = ballots - sum(prior.values())
                    restored = bool(compare.get('already_in_forecast'))
            if rates or declaration or addition:
                by_seat[event['seat']].append(dict(name=label, declaration=declaration, ledger=ledger,
                    category=category, pair=pair, first=first, ballots=ballots, changed=changed, rates=rates,
                    approximate_tcp=ranked_tcp.category(new) if approximate_tcp else None,
                    partial_category_fp=partial_category_fp,
                    tcp_addition=addition, restored=restored,
                    batch_shares=event.get('rule_ranked_tcp_addition') if addition else None,
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
        categories = {u['category'] for u in units if u['declaration'] and not u.get('restored')
                      and (u['ledger'], u['pair']) == (ledger, pair)}
        other_categories = sorted(categories - {u['category'] for u in batch if u['declaration']})
        result.append(dict(ledger=ledger, pair=list(pair), first=first, locations=unit_names(batch), units=len(batch),
            declaration=batch[0]['declaration'] if len(batch) == 1 else False, ballots=ballots,
            changed=sum(u['changed'] for u in batch), rates=rates, other_categories=other_categories))
        # A category with a partial TCP still contributed real counting. Give
        # its progress separately from booths with a comparable benchmark.
        partial = {u['category']: (u['ballots'], u['partial_category_fp']) for u in units
                   if u.get('partial_category_fp') and u['category'] in other_categories
                   and (u['ledger'], u['pair']) == (ledger, pair)}
        if partial:
            result[-1]['partial_categories'] = partial
        approximate = {u.get('approximate_tcp') for u in batch}
        if any(approximate):
            result[-1]['approximate_tcp'] = approximate.pop() if len(approximate) == 1 else 'mixed'
        # Use every booth named in the FP sentence and its same ballot weight
        # at both dates. A projection for only part of the group can tell a
        # different story, particularly when a large centre is missing.
        estimates = batch[0].get('tcp_estimates') or {}
        if party in estimates and all((u.get('tcp_estimates') or {}).keys() == estimates.keys() for u in batch):
            result[-1]['all_booths'] = all(not u['declaration'] for u in batch)
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


def primary_centre(prior, count, ordinary, weight, earlier_breadth=None):
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
    breadth = row['breadth'] if earlier_breadth is None else earlier_breadth
    response = weight + .5*breadth*(1-weight)**2
    matched = share(row['prior_remaining_log_odds'] + response*row['local_log_odds_change'])
    booth = row['booth_remaining_share']
    future = row.get('fitted_remaining_share', booth + row['matched_fraction']*(matched-booth))
    fixed = row['fixed_fraction']
    district = fixed + remaining*future
    legacy = max(fixed, legacy)
    return 100*(legacy + row['ordinary_fraction']*(district-legacy))


def primary_weight_check(event):
    """Compare the central local FP calculation at the two dates, then repeat
    the later calculation with the earlier precision and spread of counted
    ballots across matched booths. Hold the actual new ballots and the new
    estimates for those places fixed. Breadth alone says nothing about
    whether the booths agree on a particular candidate.
    Each record holds three FP percentages for one individually matched party
    and the change in its count weight (a fraction between -1 and 1).
    These centres precede draw-by-draw normalisation and counted-vote floors.
    The writer checks the published district means separately before using them."""
    snapshots = []
    for side in ('before', 'after'):
        model = (event.get(side) or {}).get('fp_integration') or {}
        if model.get('method') not in ('local_precision', 'ordinary_vote_blend', 'cautious_ordinary_fit', 'split_ordinary_fit'):
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
    checks = {}
    for p in before.keys() & after.keys():
        earlier, later = before[p], after[p]
        old_ordinary, new_ordinary = earlier[2], later[2]
        if bool(old_ordinary) != bool(new_ordinary):
            continue
        breadth = old_ordinary[0]['breadth'] if old_ordinary else None
        check = dict(before=primary_centre(*earlier), after=primary_centre(*later),
                     previous_weight=primary_centre(*later[:-1], earlier[-1], earlier_breadth=breadth),
                     weight_change=later[-1]-earlier[-1])
        if old_ordinary and all('fitted_remaining_share' not in r[0] and
                                r[0]['matched_fraction'] > 0 and r[0]['ordinary_fraction'] == 1
                                for r in (old_ordinary, new_ordinary)):
            check['earlier_signal'] = old_ordinary[0]['local_log_odds_change']
            check['current_signal'] = new_ordinary[0]['local_log_odds_change']
            if old_ordinary[0]['matched_fraction'] == new_ordinary[0]['matched_fraction'] == 1:
                check['largest_fractions'] = [1-r[0]['breadth'] for r in (old_ordinary, new_ordinary)]
        checks[p] = check
    return checks


def fresh_booth_pattern(events, party):
    """Compare only genuinely new ordinary FP returns with each location's
    earlier estimate. A correction to an existing booth or a missing benchmark
    prevents claims about agreement across the newly counted booths."""
    gaps = []
    for event in events:
        old, new = event.get('before') or {}, event.get('after') or {}
        votes = sum(new.get('fp_votes_current', {}).values())
        gap = (event.get('fp_share_minus_previous_expectation_pp') or {}).get(party)
        if (not votes or sum(old.get('fp_votes_current', {}).values()) or gap is None
                or new.get('booth_type') != 'Normal' or new.get('vote_type') != 'Ordinary'):
            return None
        gaps.append((gap, votes, new.get('name')))
    if gaps:
        # Attribute a mixed batch to a particular booth only when its
        # surplus or shortfall outweighs all the others combined, in votes.
        impact, ballots, name = max(gaps, key=lambda item: abs(item[0])*item[1])
        largest = abs(impact)*ballots
        dominant = name if 2*largest > sum(abs(g)*n for g, n, _ in gaps) else None
        return dict(low=min(g for g, _, _ in gaps), high=max(g for g, _, _ in gaps),
                    pooled=sum(g*n for g, n, _ in gaps)/sum(n for _, n, _ in gaps),
                    dominant=dominant, dominant_gap=impact)


def tpp_component_changes(previous, current, old_evidence, new_evidence):
    """Compare the simulator's mean ALP TPP adjustments, in district pp.
    Primary composition, directly counted preferences and estimated changes
    in preference flows are separate contributions. The broad preference
    component is already included in the last one. A pre-count origin has
    zero live adjustments. Partial coverage could compare different draws."""
    changes = {}
    fields = ('primary_change_pp', 'measured_change_pp', 'learned_change_pp')
    for seat, new in new_evidence.get('fp_based_tpp', {}).items():
        old = old_evidence.get('fp_based_tpp', {}).get(seat, {})
        if not current.get('iterations') or new.get('accounted_draws') != current['iterations']:
            continue
        if previous.get('origin'):
            old = dict.fromkeys(fields, 0.)
        elif not previous.get('iterations') or old.get('accounted_draws') != previous['iterations']:
            continue
        if all(isinstance(r.get(k), (int, float)) and math.isfinite(r[k]) for r in (old, new) for k in fields):
            changes[seat] = {k: new[k]-old[k] for k in fields}
            # Recent exports identify the part of preference learning shared
            # across districts. It is already included in learned_change_pp.
            key = 'broad_preference_change_pp'
            baseline = 0. if previous.get('origin') else old.get(key)
            if all(isinstance(v, (int, float)) and math.isfinite(v) for v in (baseline, new.get(key))):
                changes[seat][key] = new[key] - baseline
    return changes


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
    fp_events = defaultdict(list)
    for e in comparison['evidence']:
        if e['kind'] == 'booths' and e.get('seat') in weights and activity.affected_ballots(
                (e.get('before') or {}).get('fp_votes_current', {}), (e.get('after') or {}).get('fp_votes_current', {})):
            fp_events[e['seat']].append(e)
    for seat, checks in weights.items():
        for party, check in checks.items():
            if 'earlier_signal' in check:
                check['new_booths'] = fresh_booth_pattern(fp_events[seat], party)
                earlier = comparison.get('rule_earlier_ordinary_count', {}).get(seat, {})
                if earlier and earlier['largest'] == earlier['total']:
                    check['earlier_booth'] = earlier['name']
                fractions = check.get('largest_fractions')
                # Reserve the concentration explanation for a count previously
                # dominated by one booth (at least three quarters of ballots)
                # whose share has fallen at least ten points. Check that the
                # published count describes the same pool as the simulator.
                if (earlier.get('name') and fractions and fractions[0] >= .75 and fractions[0]-fractions[1] >= .10
                        and abs(earlier['largest']/earlier['total']-fractions[0]) < .0001):
                    check['dominant_earlier_count'] = earlier
    # Retain the locations independently of the comparison pools. A named
    # Coalition partner may have a district projection without its own booth
    # benchmark. The public text can still give the sizes of these updates.
    tcp_sources = {}
    for seat in pools:
        by_pair = defaultdict(list)
        for unit in units[seat]:
            if unit.get('tcp_addition', 0) >= 10 and not unit['rates']:
                source = {
                    'name': unit['name'], 'declaration': unit['declaration'],
                    'changed': unit['tcp_addition'], 'restored': unit['restored']}
                if unit.get('batch_shares'):
                    source['batch_shares'] = unit['batch_shares']
                by_pair['/'.join(unit['pair'])].append(source)
        if by_pair:
            tcp_sources[seat] = dict(by_pair)
    return {'pools': pools, 'tcp_sources': tcp_sources, 'primary_weight_checks': weights,
            'tpp_components': {s: v for s, v in comparison.get('rule_tpp_components', {}).items() if s in pools},
            'large_local_moves': sorted(large), 'supported_flows': supported_flows(events, flows),
            'regional': comparison.get('rule_regional', []),
            'regional_change': comparison.get('rule_regional_changes', {}).get('period'),
            'previous_regional_change': comparison.get('rule_regional_changes', {}).get('previous')}


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


def regional_updates(before, after):
    """Find the largest reportable regional swing revision for each kind of update.
    Compare the cumulative matched-booth swing at the two forecast dates.
    A region crossing the minimum coverage for the first time has no reliable
    starting figure. Its historical comparison belongs in the counting overview."""
    best = {}
    for ledger in ('tcp', 'fp'):
        for region, current in after.get(ledger, {}).items():
            earlier = before.get(ledger, {}).get(region)
            if not earlier or any(r['votes'] < 1000 or r['booths'] < 5 or r['seats'] < 3
                                  for r in (earlier, current)):
                continue
            # An update can add several small rural booths while still telling
            # us little about a region. Require at least 2,500 additional
            # matched ballots in this region and count type during the window,
            # and at least 10% of its current matched count. These are editorial
            # thresholds for reporting a revision to the swing.
            added_votes = current['votes'] - earlier['votes']
            if added_votes < max(2500, .1*current['votes']):
                continue
            old, new = [100*r['gain']/r['votes'] for r in (earlier, current)]
            movement = abs(new-old)
            if movement < 1:
                continue
            added_booths = current['booths'] - earlier['booths']
            row = dict(region=region, ledger=ledger.upper(), before=old, after=new, booths=current['booths'],
                       added_booths=added_booths, added_votes=added_votes)
            # A rolling report can describe a developing pattern. For a single
            # update, reserve a headline for a substantial revision with broad
            # fresh counting and an earlier sample large enough to compare.
            # Select independently so a larger swing on two booths cannot hide
            # a well-supported swing elsewhere. The cutoffs are editorial.
            windows = ['period']
            if movement >= 2.5 and earlier['votes'] >= 2500 and added_votes >= 5000 and added_booths >= 5:
                windows.append('previous')
            for window in windows:
                if window not in best or movement > abs(best[window]['after'] - best[window]['before']):
                    best[window] = row
    return best
