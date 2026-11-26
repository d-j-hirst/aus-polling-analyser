"""Write an election update from the saved numerical comparison.

The comparison already identifies candidates, changed counts and observations
that can be matched to an earlier estimate. The rules below choose what to tell
a reader about the winner, then give supporting measurements where available.
They make editorial choices only. They neither recalculate a forecast nor use
the other writer's selections, prose or subsequent counting.
"""
import re
from collections import defaultdict
from typing import NamedTuple, Optional
from . import activity, commentary_format as display, count_context, pairing_commentary, rule_evidence
from .comparisons import headline_candidate_priority


# An unassigned development with this exact first line separates the editions
# inside the existing website payload. Ordinary seat sections have a seat ID.
MARKER = '[[AEF_RULE_BASED_COMMENTARY_V1]]'


def candidate(seat, party):
    return '{candidate:' + seat + ':' + party + '}'


def movement(after, delta):
    verb = 'rose' if delta > 0 else 'fell'
    return f'{verb} to **{display.decimal(after)}% ({display.decimal(delta, True)} pp)**'


def win_outlook(row, *, name_seat=False):
    """Give the simulated win chance and its movement. Use extra precision
    near either endpoint so a candidate losing a few simulations still reads
    differently from one who won every simulation."""
    seat, after, delta = row['seat'], row['after'], row['change_pp']
    subject = candidate(seat, row['party_id'])
    opening = f'In {{seat:{seat}}}, the' if name_seat else 'The'
    if after in (0, 100):
        change = f' **({display.decimal(delta, True)} pp)**' if abs(delta) >= .05 else ''
        return f'{opening} forecast gave {subject} a win in **{after:.0f}%** of simulations{change}.'
    level = display.decimal(after) + '%'
    if level in ('0.0%', '100.0%'):
        level = f'{after:.2f}%'
        if level == '0.00%':
            level = 'less than 0.01%'
        elif level == '100.00%':
            level = 'over 99.99%'
    if abs(delta) < .05:
        return f'{opening} forecast puts {subject} on a **{level}** win chance.'
    verb = 'rose' if delta > 0 else 'fell'
    return f'{opening} win chance for {subject} {verb} to **{level} ({display.decimal(delta, True)} pp)**.'


def seat_total_headline(old, new, parties, endpoints):
    """Describe up to two changes in the statewide seat medians. When a
    median moves by one, check the chance of reaching the higher total in
    both forecasts. A rise from 49.9% to 50.1% can change the median just
    as surely as a rise from 40% to 60%, but says much less to readers."""
    moves = []
    for party in old.keys() & new.keys() & parties.keys():
        delta = new[party] - old[party]
        if not delta:
            continue
        boundary = str(max(old[party], new[party]))
        support = 0
        if endpoints:
            before, after = [side.get('chance_at_least_seats', {}).get(party, {}).get(boundary)
                             for side in endpoints]
            if before is not None and after is not None:
                support = after - before if delta > 0 else before - after
        # Five percentage points is an editorial minimum for a one-seat
        # headline. A shift of two or more seats already moves the middle
        # outcome past a whole intervening total and earns a mention on its own.
        if abs(delta) >= 2 or support >= 5:
            moves.append((abs(delta), support, party))
    moves.sort(key=lambda move: (-move[0], -move[1], move[2]))
    phrases = []
    for _, _, party in moves[:2]:
        verb = 'rose' if new[party] > old[party] else 'fell'
        phrases.append(f'for {{party:{party}}} {verb} from **{old[party]:g}** to **{new[party]:g}**')
    if phrases:
        return 'The median seat total '+', and the median '.join(phrases)+'.'
    return None


def new_pair_headlines(group, packet, selected):
    """Highlight the first substantial non-classic party count or several
    districts realigning together, even when their winners remain clear.
    Compare like opponents and count only ballots for the new pairing."""
    groups = defaultdict(dict)
    for seat, count in group.get('replacement_tcp_counts', {}).items():
        pair = tuple(sorted(count['pair']))
        # Different independents and unnamed candidates do not make up a
        # statewide party. Their counts belong in their own district stories.
        if seat in packet['all_seats'] and count_context.named_party_pair(pair, packet['parties']):
            groups[pair][seat] = count

    # Two districts changing pairs establish a shared development. The first
    # substantial set of returns statewide for a non-classic pairing can
    # come from a single district. Report the largest qualifying sample.
    eligible = [(sum(c['counted_votes'] for c in rows.values()), pair, True, rows)
                for pair, rows in groups.items() if len(rows) >= 2]
    for rows in group.get('rule_early_party_counts', []):
        pair = tuple(sorted(next(iter(rows.values()))['pair']))
        eligible.append((sum(c['counted_votes'] for c in rows.values()), pair, False, rows))
    if not eligible:
        return []
    total, pair, replacement, rows = max(eligible, key=lambda item: item[:3])
    if total < 500:
        return []

    ordered = sorted(rows, key=lambda s: (-rows[s]['counted_votes'], s))
    shown = sorted(ordered if len(ordered) <= 3 else ordered[:2])
    labels = ['{seat:'+s+'}' for s in shown]
    if len(ordered) > len(shown):
        labels.append(f'{len(ordered)-len(shown)} other districts')
    districts = rule_evidence.joined(labels)
    first, second = pair
    matchup = f'{{party:{first}}}–{{party:{second}}}'
    if replacement:
        text = f'{districts} reported **{total:,}** TCP votes for their new {matchup} pairing.'
    else:
        text = f'In {districts}, the {matchup} TCP count reached **{total:,}** votes.'
    # Count transfers only where both ledgers cover the same number of votes.
    # Use ballot totals so a small booth has proportionately less influence.
    # Fifty transfers is an editorial minimum for quoting the local split.
    # Giving the sample size keeps early booth results in perspective.
    units = sum(c['units'] for c in rows.values())
    matched_units = preferences = to_first = 0
    matched_name = None
    for count in rows.values():
        if 'transfers_from_other_first_preferences' in count:
            split = count['transfers_from_other_first_preferences']
            matched_units += count['units']
        elif 'matched_transfer_sample' in count:
            sample = count['matched_transfer_sample']
            split = sample['transfers']
            matched_units += sample['units']
            if sample.get('unit_name'):
                matched_name = sample['unit_name']
        else:
            continue
        preferences += sum(split.values())
        to_first += split[first]
    if preferences >= 50:
        share = 100 * to_first / preferences
        scope = ('Across those counts' if matched_units == units else
                 f'Where FP and TCP totals matched (**{matched_units} of {units}** reporting units)')
        if matched_units == 1 and matched_name and units > 1:
            scope = f'At {matched_name}, the only reporting unit with matching FP and TCP totals'
        text += (f' {scope}, **{share:.1f}%** of the **{preferences:,}** ballots first cast '
                 f'for other candidates went to {{party:{first}}}.')
    headlines = [text]

    # Give one example outside the reporting districts when the ordinary
    # preference diagnostics trace a change to a named source and the
    # chance of winning that particular matchup moved in the same direction.
    # A district already selected for its own story has access to this detail.
    effects = []
    for seat, effect in group.get('district_preference_evidence', {}).items():
        donor = effect['strongest_supporting_district']
        if (seat in selected or seat in rows or seat not in packet['all_seats']
                or donor not in rows or set(effect['pair']) != set(pair)):
            continue
        favoured = effect['ordinary_preference_shift_favours']
        before, after = effect['first_candidate_conditional_win_percent']
        if favoured != effect['pair'][0]:
            before, after = 100-before, 100-after
        # A second headline about another district needs a material change.
        # Use two points of conditional win chance and require a competitive
        # outlook at one endpoint, allowing for movement to near certainty.
        if after-before < 2 or min(before, after) > 95 or max(before, after) < 5:
            continue
        effects.append((after-before, seat, donor, favoured, after))
    if effects and not packet.get('input_revision_changed'):
        change, seat, donor, favoured, after = max(effects)
        other = second if favoured == first else first
        headlines.append(f'Counting in {{seat:{donor}}} led the forecast to expect stronger preferences '
                         f'for {{party:{favoured}}} in {{seat:{seat}}}. The chance of {{party:{favoured}}} '
                         f'winning a final count against {{party:{other}}} there '+movement(after, change)+'.')
    return headlines


def election_headlines(packet, endpoints, window=None):
    """Describe broad movements separately from individual districts. A reset
    concerns reported progress. Only a verified match to the missing ballots
    attributes the whole drop to those resets."""
    old, new = packet['statewide']['before'], packet['statewide']['after']
    headlines = []
    reporting = packet.get('tcp_reporting_changes', {})
    same = reporting.get('reset_same_candidates', {}).get('count', 0)
    changed = reporting.get('reset_different_candidates', {}).get('count', 0)
    if same or changed:
        districts = 'one district' if same + changed == 1 else f'**{same + changed} districts**'
        text = f'The reported TCP count reset in {districts}.'
        if packet.get('tcp_reporting_reconciliation'):
            earlier, current = [side['countProgress']['tcp_votes'] for side in (old, new)]
            text += f' This accounts for the drop of **{earlier-current:,.0f} votes** to **{current:,.0f}**.'
        if changed:
            text += ' The feed also names a new pairing.' if not same else ' The feed also names new pairings in some districts.'
        if same and not changed:
            text += ' A change of opponents is likely, although the feed still names the original candidates.'
        if reporting.get('reset_districts_with_earlier_booth_counts_in_forecast', {}).get('count'):
            text += ' Earlier booth counts still inform the forecast for their original pairings where available.'
        headlines.append(text)

    # Half a percentage point is an editorial threshold for the median vote
    # share. Name at most two parties so the headline remains readable.
    old_fp = old.get('fp_percentiles_by_party') or {}
    new_fp = new.get('fp_percentiles_by_party') or {}
    changes = []
    for party in old_fp.keys() & new_fp.keys() & packet['parties'].keys():
        before, after = old_fp[party].get('50'), new_fp[party].get('50')
        if before is not None and after is not None and abs(after-before) >= .5:
            changes.append((abs(after-before), party, movement(after, after-before)))
    changes.sort(key=lambda item: (-item[0], item[1]))
    if changes:
        parties = ['for {party:'+p+'} '+phrase for _, p, phrase in changes[:2]]
        headlines.append('Across the election, the projected first-preference share '+' and '.join(parties)+'.')
    old_tpp, new_tpp = [(side.get('tpp_percentiles') or {}).get('50') for side in (old, new)]
    if old_tpp is not None and new_tpp is not None and abs(new_tpp-old_tpp) >= .5:
        headlines.append('The projected Labor two-party-preferred share '+movement(new_tpp, new_tpp-old_tpp)+'.')
    old_seats, new_seats = [side.get('medianSeats') or {} for side in (old, new)]
    seats = seat_total_headline(old_seats, new_seats, packet['parties'], endpoints)
    if seats:
        headlines.append(seats)
    # The comparison supplies only matched, material changes in estimated
    # preferences. These refer to projected first-preference voters, including
    # those whose eventual distribution between the named opponents is unknown.
    regional = packet['rule_context']['regional']
    revision = packet['rule_context'].get('previous_regional_change' if window == 'previous' else 'regional_change')
    if regional and window == 'since_start':
        stronger, weaker = regional
        def location(row):
            rounded = round(row['swing'], 1)
            change = ('held steady' if rounded == 0 else
                      f"{'rose' if rounded > 0 else 'fell'} **{abs(rounded):.1f} pp**")
            return f"{change} in {row['region']} (**{row['booths']} booths**)"
        regional_text = (f"Using the forecast’s regional groupings, the {{party:0}} {stronger['ledger']} share "
                         f"at matched election-day booths {location(stronger)} and {location(weaker)} "
                         f"compared with the previous election.")
        # Mention substantial variation once, alongside the regional swing.
        # Three points is an editorial threshold for the average distance of
        # district swings from their region's swing, weighted by counted votes.
        varied = max(regional, key=lambda region: region.get('spread') or 0)
        if (varied.get('spread') or 0) >= 3:
            regional_text += (f" In {varied['region']}, swings in districts with substantial counts differed "
                              f"from the regional swing by **{varied['spread']:.1f} pp** on average.")
        headlines.append(regional_text)
    elif window != 'since_start' and revision:
        r = revision
        def swing(value):
            if round(value, 1) == 0:
                return 'an even result relative to the last election'
            return f"**{abs(value):.1f} pp** {'towards' if value > 0 else 'away from'} {{party:0}}"
        coverage = (f"as the count grew by **{r['added_booths']} booths** and **{r['added_votes']:,} votes**"
                    if window == 'previous' else f"as the count reached **{r['booths']} booths**")
        headlines.append(f"In the forecast’s {r['region']} region, the {r['ledger']} swing at matched "
                         f"election-day booths changed from {swing(r['before'])} to {swing(r['after'])} {coverage}.")
    headlines.extend(packet['rule_context'].get('new_pair_headlines', []))
    flows = packet['rule_context']['supported_flows']
    if flows:
        flow = flows[0]
        source, destination, other = flow['parties']
        if all(p in packet['parties'] for p in flow['parties']):
            headlines.append(f"The estimated share of {{party:{source}}} voters preferring "
                             f"{{party:{destination}}} over {{party:{other}}} "+movement(flow['after'], flow['change_pp'])+'.')
    return headlines


def late_tcp_count(count):
    """A raw district lead becomes worth discussing once more than 90% of
    the expected votes for these two opponents have been counted. Use the
    same district turnout estimate that supplies the remaining vote figure.
    A missing or inconsistent remainder cannot establish a late count."""
    counted = count.get('counted_after') or 0
    remaining = count.get('remaining_after')
    return remaining is not None and remaining >= 0 and counted > 9 * remaining


def district_order(packet, entities, *, single_update=False):
    """Normally choose up to three districts. Give priority to a win chance movement
    alongside actual local counting (one percentage point). A five-point
    movement elsewhere can also merit coverage. Use the local threshold when
    the same counted leader gains with materially fewer votes projected to come
    late in counting, even if no new booth reported. A large reversal within the
    period or a late counted lead changing hands in a competitive seat can earn a
    place even when the endpoint movements are small. The thresholds select
    stories; coincidence alone gives no cause for the movement."""
    selected, subjects = {}, {}
    for row in packet['win_changes']:
        seat, party = row['seat'], row['party_id']
        if row.get('identity_changed') or candidate(seat, party) not in entities:
            continue
        local = packet.get('observed_count_changes_by_seat', {}).get(seat, {}).get('affected_votes', 0) > 0
        change = abs(row['change_pp'])
        reversal = row['maximum_within_window'] - row['minimum_within_window'] - change
        score = max(change, reversal if reversal >= 10 else 0)
        count = packet.get('close_tcp_counts', {}).get(seat, {})
        favourite = max(packet['all_seats'].get(seat, {}).values(), default=0)
        late = late_tcp_count(count)
        if (late and party in count.get('pair', []) and row['after'] >= 10 and favourite <= 90
                and count['lead_before'] < 0 < count['lead_after']):
            score = max(3, score)
        remainder_story = late and count.get('fewer_votes_favour_counted_leader') and party == count['pair'][0]
        if score < (1 if local or remainder_story else 5):
            continue
        # With no local counts, a movement twice as large can take the place of
        # a local story. This is an editorial preference for results a reader
        # can inspect, independent of how strongly the forecast weighs either.
        priority = (score if local else score * .5, change, row['after'])
        if seat not in selected or priority > selected[seat][0]:
            selected[seat] = priority, row
        subject_rank = headline_candidate_priority(row, packet['all_seats'][seat])
        if seat not in subjects or subject_rank > subjects[seat][0]:
            subjects[seat] = subject_rank, row
    ordered = sorted(sorted(selected), key=lambda seat: selected[seat][0], reverse=True)
    # A district may have earned its place through a reversal inside the
    # window rather than an endpoint movement. Keep that story if the
    # alternative subject has little net movement to explain.
    seats = ordered[:3]
    if single_update:
        # A fourth or fifth local change of eight win-probability points
        # warrants its own account even on a busy election night.
        seats += [seat for seat in ordered[3:] if selected[seat][0][1] >= 8 and
                  packet.get('observed_count_changes_by_seat', {}).get(seat, {}).get('affected_votes', 0) > 0][:2]
    return [subjects[seat][1] if abs(subjects[seat][1]['change_pp']) >= 1 else selected[seat][1] for seat in seats]


class LocalCount(NamedTuple):
    """A local count bullet, its locations and changed ballot total. The gap
    describes the candidate's share relative to the earlier estimate, or the
    change in the FP gap against a rival when the bullet reports both."""
    location: Optional[str]
    changed_ballots: int
    point: str
    gap_pp: Optional[float] = None
    count_type: Optional[str] = None
    units: int = 1
    attribution: Optional[str] = None
    contrast: Optional[str] = None
    fresh_fp: Optional[str] = None


class PairProjection(NamedTuple):
    """District median final share for the subject against one opponent, at
    the two ends of the comparison. Pairing frequency is the percentage of
    current simulations in which those two candidates made the final pair."""
    opponent: str
    before: Optional[float]
    after: float
    pairing_percent: Optional[float] = None

    def point(self, row):
        first = candidate(row['seat'], row['party_id'])
        other = candidate(row['seat'], self.opponent)
        text = f'The projected final TCP share for {first} against {other}'
        if self.before is None:
            return text + f' is **{self.after:.1f}%**.'
        if round(self.before, 1) == round(self.after, 1):
            return text + f' held at **{self.after:.1f}%**.'
        if abs(self.after-self.before) < .05:
            return text + f' stayed near **{self.after:.1f}%**.'
        return text + ' ' + movement(self.after, self.after-self.before) + '.'


def most_likely_pair(seat, endpoints):
    chances = endpoints[1].get('seats', {}).get(seat, {}).get('pairing_percent', {}) if endpoints else {}
    if chances and max(chances.values()) > 0:
        return max(chances, key=chances.get).split('/')
    return []


def local_pair_projection(packet, row, endpoints):
    """Use the district's most likely final pairing. The commission can be
    counting another opponent. Use a counted pairing as a fallback only
    where the saved forecast lacks simulated pairing probabilities."""
    seat, party = row['seat'], row['party_id']
    count = packet.get('close_tcp_counts', {}).get(seat, {})
    replacement = packet.get('replacement_tcp_counts', {}).get(seat, {})
    districts = [side.get('seats', {}).get(seat, {}) for side in endpoints] if endpoints else []
    pair = most_likely_pair(seat, endpoints) or replacement.get('pair') or count.get('pair')
    if not pair:
        measured_pairs = set()
        for event in packet['evidence']:
            comparison = event.get('tcp_against_previous_forecast') or {}
            if event.get('seat') == seat and comparison.get('available'):
                measured_pairs.add(tuple(sorted(comparison['pair'])))
        if len(measured_pairs) == 1:
            pair = measured_pairs.pop()
    if not pair or party not in pair or len(pair) != 2:
        return None
    opponent = pair[1] if pair[0] == party else pair[0]
    if candidate(seat, opponent) not in packet['entities']:
        return None
    before = after = None
    if districts:
        before, after = [count_context.district_tcp_median(d, party, opponent) for d in districts]
        if districts[0].get('candidates', {}).get(opponent) != districts[1].get('candidates', {}).get(opponent):
            before = None
    if after is None and set(count.get('pair', [])) == set(pair):
        median = count.get('projected_median_percent_for_first')
        if median is not None:
            after = median if count['pair'][0] == party else 100 - median
    chances = districts[1].get('pairing_percent', {}) if districts else {}
    frequency = chances.get('/'.join(pair))
    return PairProjection(opponent, before, after, frequency) if after is not None else None


def booth_difference(value):
    if abs(value) < .05:
        return 'matching the earlier estimate'
    return f"**{abs(value):.1f} pp** {'above' if value > 0 else 'below'} the earlier estimate"


def mixed_pair_returns(packet, row, projection, rival, endpoints, groups):
    """Compare fresh FP-only and measured TCP returns for the same non-classic pair.

    The FP booth estimate holds the earlier preference split fixed. Multiplying
    its change by the ballot count puts both returns on a comparable scale for
    choosing what to report. It does not attribute the district-wide revision
    to a particular booth, since returns can also change the remaining forecast.
    """
    party, seat = row['party_id'], row['seat']
    if (not endpoints or not projection or rival != projection.opponent or projection.before is None
            or packet.get('input_revision_changed') or {party, rival} == {'0', '1'}):
        return None
    shift = projection.after - projection.before
    if abs(shift) < .1 or shift * row['change_pp'] <= 0:
        return None
    for endpoint in endpoints:
        pairs = endpoint['seats'][seat].get('pairing_percent', {})
        if max(pairs.get(party+'/'+rival, 0), pairs.get(rival+'/'+party, 0)) <= 65:
            return None

    # Revisions to existing counts cannot be compared using the entire booth
    # size as if it had just arrived. Require a separate fresh return on each
    # ledger and a TCP estimate covering every FP booth in its pooled result.
    if len(groups) != 2 or not all(g['first'] for g in groups):
        return None
    ledgers = {g['ledger']: g for g in groups}
    if ledgers.keys() != {'FP', 'TCP'}:
        return None
    fp, tcp = ledgers['FP'], ledgers['TCP']
    estimates = fp.get('tcp_estimates') or {}
    if (set(estimates) != {party, rival} or set(tcp['pair']) != {party, rival}
            or rival not in fp['rates']):
        return None
    before, after = estimates[party]
    fp_votes = fp['ballots'] * (after-before) / 100
    share, difference = tcp['rates'][party]
    tcp_votes = tcp['ballots'] * difference / 100
    # Ordinarily lead with the measured TCP. Lead with FP when the projected
    # same-pair difference at its booths is at least ten ballots and twice the
    # measured difference, even if both point the same way. The district FP
    # gap must corroborate it.
    if (fp_votes*shift <= 0 or abs(fp_votes) < max(10, 2*abs(tcp_votes))
            or (fp['rates'][party][1]-fp['rates'][rival][1])*shift <= 0):
        return None
    district_gap, _ = district_primary_evidence(packet, row, projection, endpoints)
    if not district_gap:
        return None
    where = ('In ' if tcp['declaration'] else 'At ') if tcp['units'] == 1 else 'Across '
    direction = 'the TCP count went the other way. ' if tcp_votes*shift < 0 else ''
    contrast = (f'{where}{tcp["locations"]}, '+direction+f'{candidate(seat, party)} '
                f'received **{share:.1f}%** of **{tcp["ballots"]:,}** TCP votes against {candidate(seat, rival)}, '
                + booth_difference(difference) + '.')
    return fp, district_gap, contrast


def other_opponent_primary(group, row, rival, projected_opponent, endpoints, entities):
    """Select one more FP comparison when it helps interpret the contest.
    Give the likely opponent priority if the frequency of that final changed
    materially. Their booth share is informative even if close to expectations.
    Link other candidates to pairing movement only with supporting district FP."""
    if not endpoints or not group['first']:
        return ''
    seat, subject = row['seat'], row['party_id']
    districts = [side['seats'][seat] for side in endpoints]
    options, local_gains = [], []
    subject_gap = group['rates'][subject][1]
    for party, (share, gap) in group['rates'].items():
        if party in (subject, rival) or candidate(seat, party) not in entities:
            continue
        info = districts[0].get('candidates', {}).get(party) or {}
        if not info.get('name') or info != districts[1].get('candidates', {}).get(party):
            continue
        pairs = [pairing_commentary.pair_win_rate(s, seat, subject, party) for s in endpoints]
        change = pairs[1][0] - pairs[0][0] if None not in pairs else None
        if party == projected_opponent and change is not None and abs(change) >= 2:
            return f' {candidate(seat, party)} received **{share:.1f}%**, '+booth_difference(gap)+'.'
        if abs(gap) < 3:
            continue
        # A large primary shortfall deserves its local counterpart even when
        # that third candidate did not become a more likely district finalist.
        # These comparisons of shares do not identify individual vote transfers.
        if subject_gap <= -5 and gap >= -subject_gap/2:
            local_gains.append((gap, party, share))
        means = [d.get('fp_mean_percent', {}).get(party) for d in districts]
        if None in means or change is None:
            continue
        # Mention one other opponent only for a visible (two-point) change
        # in pairing frequency backed by a district FP shift of at least 0.5 pp.
        if abs(change) >= 2 and abs(means[1]-means[0]) >= .5 and gap*change > 0 and gap*(means[1]-means[0]) > 0:
            options.append((abs(change), party, share, gap))
    if not options and local_gains:
        gap, party, share = max(local_gains)
        return (f' {candidate(seat, party)} received **{share:.1f}%**, '
                f'**{gap:.1f} pp** above the earlier estimate.')
    if not options:
        return ''
    _, party, share, gap = max(options)
    return (f' {candidate(seat, party)} received **{share:.1f}%**, **{abs(gap):.1f} pp** '
            f'{"above" if gap > 0 else "below"} the earlier estimate, consistent with facing '
            f'{candidate(seat, subject)} {"more" if gap > 0 else "less"} often in the final two.')


def attributed_count(packet, row, projection, rival, endpoints, candidates):
    """Choose between concurrent FP and TCP returns using the simulated district
    TPP account. First establish which component moved the district, then ask
    whether the fresh comparable booths illustrate it. Choosing a booth just
    because it points towards the winner would reverse that reasoning."""
    seat, party = row['seat'], row['party_id']
    parts = packet['rule_context'].get('tpp_components', {}).get(seat)
    if (not parts or not endpoints or not projection or projection.before is None
            or packet.get('input_revision_changed') or rival != projection.opponent or {party, rival} != {'0', '1'}):
        return None
    # The exported account is for Labor versus Coalition across every draw.
    # Only relate it to a named TCP when those finalists dominate both reports.
    districts = [s['seats'][seat] for s in endpoints]
    if any(d.get('pairing_percent', {}).get('0/1', 0) < 90 for d in districts) or any(
            not (districts[0].get('candidates', {}).get(p) or {}).get('name')
            or districts[0]['candidates'][p] != districts[1].get('candidates', {}).get(p) for p in (party, rival)):
        return None
    shift = projection.after - projection.before
    if abs(shift) < .1 or shift*row['change_pp'] <= 0:
        return None
    sign = 1 if party == '0' else -1
    effects = {'FP': sign*parts['primary_change_pp'], 'TCP': sign*parts['measured_change_pp']}
    lead = max(effects, key=lambda key: abs(effects[key]))
    other = 'TCP' if lead == 'FP' else 'FP'
    effect = effects[lead]
    # Require a visible contribution, at least half the median revision,
    # and larger than the other count and preference-learning terms together.
    # The mean adjustment describes the vote projection, not a split of the
    # change in win probability or a measured impact of a particular booth.
    if (effect*shift <= 0 or abs(effect) < max(.2, .5*abs(shift))
            or abs(effect) <= abs(effects[other])+abs(parts['learned_change_pp'])):
        return None
    groups = {}
    for ledger in effects:
        batches = [g for g in candidates if g['ledger'] == ledger]
        if not batches:
            return None
        group = max(batches, key=lambda g: g['changed'])
        if not group['first'] or rival not in group['rates']:
            return None
        groups[ledger] = group
    def departure(group):
        gap = group['rates'][party][1]
        return gap-group['rates'][rival][1] if group['ledger'] == 'FP' else gap
    if departure(groups[lead])*effect <= 0:
        return None
    beneficiary = candidate(seat, party if effect > 0 else rival)
    attribution = (f'The newly measured preferences moved the projected final share about **{abs(effect):.1f} pp** '
                   f'towards {beneficiary}.' if lead == 'TCP' else None)
    contrast = None
    g = groups[other]
    if departure(g)*shift < 0 and effects[other]*shift < 0:
        who = party if departure(g) > 0 else rival
        share, gap = g['rates'][who]
        if other == 'FP':
            contrast = (f'The FP returns at {g["locations"]} favoured {candidate(seat, who)} relative to '
                        f'{candidate(seat, rival if who == party else party)} by **{abs(departure(g)):.1f} pp** '
                        f'more than estimated across **{g["ballots"]:,}** ballots.')
        else:
            extra = round(g['ballots']*gap/100)
            scale = f' (about **{extra:,}** extra votes)' if extra >= 1 else ''
            contrast = (f'The TCP count at {g["locations"]} went the other way. {candidate(seat, who)} '
                        f'received **{share:.1f}%** of **{g["ballots"]:,}** TCP votes, '+booth_difference(gap)+scale+'.')
    return groups[lead], attribution, contrast


def explanatory_booth_tcp_shift(group, projection, quoted_parties):
    """For a less certain pairing, include TCP if it helps explain a sizeable
    FP surprise or distinguishes a booth movement from the district movement."""
    estimates = group['tcp_estimates']
    for subject in quoted_parties:
        if subject not in estimates:
            continue
        gap = group['rates'][subject][1]
        if abs(gap) < 3:
            continue
        before, after = estimates[subject]
        shift = after-before
        # As in the FP comparison, differences of a couple of points are
        # editorial thresholds. Compare shares in pp, never win probabilities.
        muted = abs(gap-shift) >= 2 and (gap*shift <= 0 or abs(shift) <= abs(gap)/2)
        localised = (projection.before is not None and abs(shift) >= 2 and
                     abs(projection.after-projection.before) <= abs(shift)/2)
        if muted or localised:
            return True
    return False


def booth_tcp_context(seat, group, projection, party, quoted_parties):
    """Show what the reported primary mix implies at earlier preference rates.
    Where the pair occurs in more than 65% of the current simulations, even
    a small change helps readers interpret the FP count after preferences."""
    estimates = group.get('tcp_estimates', {})
    if not projection or group['ledger'] != 'FP' or set(estimates) != {party, projection.opponent}:
        return ''
    dominant = projection.pairing_percent is not None and projection.pairing_percent > 65
    if not dominant and not explanatory_booth_tcp_shift(group, projection, quoted_parties):
        return ''
    # Either finalist can trigger the comparison. Quote the first FP
    # candidate's share and direction to keep both sentences in order.
    before, after = estimates[party]
    shift = after-before
    if group['units'] == 1:
        scope = 'in that count' if group.get('declaration') else 'at that booth'
    else:
        # Older pooled reports do not distinguish a mix of booths and
        # declaration votes. Both are covered by "those counts".
        scope = 'at those booths' if group.get('all_booths') else 'across those counts'
    text = (f' Those FP votes point to **{after:.1f}%** TCP '
            f'for {candidate(seat, party)} against {candidate(seat, projection.opponent)} {scope}')
    if round(before, 1) == round(after, 1):
        return text + ', in line with the earlier estimate.'
    amount = f'**{abs(shift):.1f} pp**' if round(abs(shift), 1) else 'less than **0.1 pp**'
    return text + f', {amount} {"above" if shift > 0 else "below"} the earlier estimate.'


def additional_count_point(increment, location, seat, party, ledger, opponent=None, other_parties=(), declaration=True):
    """Establish the change in the tally, then give the composition of that
    addition. All candidates in the sentence share the same denominator."""
    added, prior = increment['added'], increment['previous']
    additions = increment.get('additions')
    if additions:
        counts = [f'{name} added **{n:,}**' for name,n in additions]
        tally = rule_evidence.joined(counts)
        tally = tally[:1].upper()+tally[1:]+f', taking their combined {ledger} tally from **{prior:,}** to **{prior+added:,}**.'
    else:
        where = 'In' if declaration else 'At'
        tally = f'{where} {location}, the {ledger} tally rose from **{prior:,}** to **{prior+added:,}**.'
    clauses, gaps = [], {}
    for p in dict.fromkeys((party,*other_parties)):
        if p not in increment['shares']:
            continue
        share = increment['shares'][p]
        clause = f'{candidate(seat,p)} received **{share:.1f}%**'
        if ledger == 'TCP' and opponent is not None:
            clause += ' against '+candidate(seat,opponent)
        if p in increment.get('remaining_shares', {}):
            gaps[p] = share-increment['remaining_shares'][p]
            clause += ', '+booth_difference(gaps[p])
        clauses.append(clause)
    composition = ', while '.join(clauses) if len(clauses) == 2 else rule_evidence.joined(clauses)
    return f'{tally} Of the **{added:,}** additional {ledger} votes, {composition}.', gaps.get(party)


def booth_result_opening(group, seat, party):
    """Name the places and denominator for a result. A first return consists
    of new ballots, while a revised count quotes the cumulative share."""
    locations = group['locations']
    if group['units'] == 1:
        if not group['declaration'] and locations == seat:
            locations = 'the '+locations+' booth'
        opening = ('In ' if group['declaration'] else 'At ')+locations
    else:
        opening = 'Across '+locations
    share = group['rates'][party][0]
    point = (f"{opening}, {candidate(seat, party)} received **{share:.1f}%** of "
             f"**{group['ballots']:,}** {group['ledger']} votes" + ('' if group['first'] else ' counted so far'))
    return point


def fresh_fp_comparison(groups, selected, seat, party, rival):
    """A large update to an existing tally can land close to expectations
    while newly reporting booths give a different picture. Compare the two
    contenders across all those fresh FP returns, regardless of which side
    they favour. Keep their ballot total separate from the ongoing tally."""
    if selected['ledger'] != 'FP' or selected['first'] or rival not in selected['rates']:
        return None
    increment = selected.get('increment')
    if increment:
        expectations = increment.get('remaining_shares', {})
        if any(p not in expectations or p not in increment['shares'] for p in (party, rival)):
            return None
        primary_gap, rival_gap = (increment['shares'][p]-expectations[p] for p in (party,rival))
    else:
        primary_gap, rival_gap = (selected['rates'][p][1] for p in (party, rival))
    if max(abs(primary_gap), abs(rival_gap), abs(primary_gap-rival_gap)) >= 1:
        return None
    for group in groups:
        if group['ledger'] != 'FP' or not group['first'] or rival not in group['rates']:
            continue
        gap = group['rates'][party][1]
        share, rival_gap = group['rates'][rival]
        # Limit the extra bullet to a primary lead at least three points
        # different from expected, equivalent to at least ten votes over
        # the whole fresh count. These are editorial cutoffs.
        lead_difference = abs(gap-rival_gap)
        if lead_difference >= 3 and group['ballots']*lead_difference/100 >= 10:
            point = booth_result_opening(group, seat, party)
            return (point+', '+booth_difference(gap)+'. '
                    +f'{candidate(seat, rival)} received **{share:.1f}%**, '+booth_difference(rival_gap)+'.')
    return None


def cumulative_count_point(group, packet, row, rival, projection, endpoints, headline_rival):
    """Describe a first return or a revision to a tally, including any earlier
    same-pair comparison. An identifiable addition to an existing count gets
    its own comparison for just the newly reported ballots instead."""
    seat, party = row['seat'], row['party_id']
    gap = group['rates'][party][1]
    ledger = group['ledger']
    point = booth_result_opening(group, seat, party)
    revised = group.get('revised_total')
    if revised:
        # When a candidate loses votes the update is a revision. Only its
        # net size is known, so quote shares against the whole revised tally.
        diff = group['ballots']-revised
        where = ('in ' if group['declaration'] else 'at ') + group['locations']
        change = (f"{'rose' if diff > 0 else 'fell'} by **{abs(diff):,}** votes to **{group['ballots']:,}**"
                  if diff else f"remained at **{revised:,}** as votes moved between candidates")
        point = (f"The reported {ledger} total {where} {change}. Of the revised total, {candidate(seat, party)} "
                 f"received **{group['rates'][party][0]:.1f}%**")
    if ledger == 'TCP':
        opponent = next(p for p in group['pair'] if p != party)
        point += ' against '+candidate(seat, opponent)
    if group.get('rechecked') and not group.get('approximate_tcp'):
        point += (f", **{abs(gap):.1f} pp** {'above' if gap > 0 else 'below'} the share implied "
                  'by the earlier preference rates on the rechecked FP tally.' if abs(gap) >= .05 else
                  ', in line with the earlier preference rates on the rechecked FP tally.')
    elif group.get('approximate_tcp'):
        basis = {'Ordinary': 'at ordinary booths', 'Early': 'for early votes'}.get(
            group['approximate_tcp'], 'for the same type of vote')
        expected = group['rates'][party][0] - gap
        estimate = f'the **{expected:.1f}%** suggested by local FP votes and the earlier average preference split {basis}'
        if abs(gap) < .05:
            point += ', in line with '+estimate+'.'
        else:
            point += f", about **{abs(gap):.1f} pp** {'above' if gap > 0 else 'below'} "+estimate+'.'
    else:
        point += ', '+booth_difference(gap)+'.'
    prior_votes = group.get('prior_votes')
    if prior_votes and group['ballots'] > prior_votes:
        added = group['ballots']-prior_votes
        prior_share = group.get('prior_shares', {}).get(party)
        if prior_share is not None and group['units'] == 1 and added >= 10:
            observed = group['rates'][party][0]
            batch_share = (observed*group['ballots']-prior_share*prior_votes)/added
            point += (f' {candidate(seat, party)} received **{batch_share:.1f}%** of the **{added:,}** '
                      f'additional votes, compared with **{prior_share:.1f}%** of the first **{prior_votes:,}**.')
        elif added >= 10:
            point += f' This update added **{added:,}** to the **{prior_votes:,}** {ledger} votes previously reported.'
    quoted_parties = [party]
    if ledger == 'FP' and rival in group['rates']:
        rival_share, rival_gap = group['rates'][rival]
        # Usually omit small differences. If this candidate gained win chance
        # despite a substantial primary shortfall, even a rival polling close
        # to expectations helps readers judge the direction of the booth result.
        contrary_shortfall = gap <= -3 and row['change_pp'] >= .5
        # Two mild shortfalls can leave the FP lead almost as expected.
        # Reporting just the winner's shortfall suggests a contrary result.
        both_below = row['change_pp'] >= .5 and rival_gap <= gap <= -1
        # If both contenders' win chances earned a headline, give both FP
        # results even when each polled close to the earlier booth estimate.
        if headline_rival or contrary_shortfall or both_below or (abs(rival_gap) >= 1 and abs(gap-rival_gap) >= 1):
            quoted_parties.append(rival)
            point += f' {candidate(seat, rival)} received **{rival_share:.1f}%**, '+booth_difference(rival_gap)+'.'
            lead_gap = gap-rival_gap
            if gap*rival_gap > 0 and abs(lead_gap) >= 2:
                ahead = party if lead_gap > 0 else rival
                point += f' The FP gap was **{abs(lead_gap):.1f} pp** more favourable to {candidate(seat, ahead)} than estimated.'
        gap -= rival_gap
        if projection and not packet.get('input_revision_changed'):
            point += other_opponent_primary(group, row, rival, projection.opponent, endpoints, packet['entities'])
    point += booth_tcp_context(seat, group, projection, party, quoted_parties)
    return point, gap


def supporting_booth(packet, row, minimum_ballots=10, projection=None, rival=None, modal=(), endpoints=None,
                     headline_rival=False, tcp_only=False):
    """Compare the combined result at all comparable reporting places with
    their own earlier estimates. Use the district TPP account when it can
    distinguish simultaneous FP and TCP updates. For other pairs, compare
    the sizes of contrary local results where both have a same-pair benchmark.
    Otherwise prefer a matched TCP, then FP."""
    seat, party = row['seat'], row['party_id']
    pools = packet['rule_context']['pools'].get(seat, {}).get(party, [])
    candidates = [g for g in pools if g['changed'] >= minimum_ballots and (not tcp_only or g['ledger'] == 'TCP') and
                  (g['ledger'] == 'FP' or
                   (seat not in packet.get('retained_tcp_only_published_zero_at_both_ends', ()) and
                    (not modal or set(modal) == set(g['pair'])) and
                    (not projection or projection.opponent in g['pair'])))]
    if not candidates:
        return None
    attributed = (attributed_count(packet, row, projection, rival, endpoints, candidates) or
                  mixed_pair_returns(packet, row, projection, rival, endpoints, candidates))
    group = attributed[0] if attributed else max(candidates, key=lambda g: (
        bool(projection) and g['ledger'] == 'TCP', g['changed'], g['ledger'] == 'TCP'))
    # A measured TCP between two finalists cannot say whether a third party
    # will displace one of them. A substantial fresh FP return can speak
    # directly to that question when it agrees with the changing opponent.
    if (not attributed and endpoints and projection and rival and rival != projection.opponent
            and not packet.get('input_revision_changed')):
        chances = [pairing_commentary.pair_win_rate(s, seat, party, projection.opponent) for s in endpoints]
        if None not in chances:
            shift = chances[1][0]-chances[0][0]
            choices = [g for g in candidates if g['ledger'] == 'FP' and g['first'] and
                       rival in g['rates'] and projection.opponent in g['rates'] and
                       abs(shift) >= 5 and g['ballots'] >= group['changed'] and
                       (g['rates'][projection.opponent][1]-g['rates'][rival][1])*(1 if shift>0 else -1) >= 3]
            if choices:
                group = max(choices, key=lambda g: g['changed'])
    ledger = group['ledger']
    location = group['locations']
    if group['units'] == 1 and not group['declaration']:
        location = 'the result at '+('the '+location+' booth' if location == seat else location)
    increment = group.get('increment')
    if increment and party in increment['shares'] and not (group.get('approximate_tcp') or group.get('rechecked')):
        opponent = next((p for p in group['pair'] if p != party), None)
        others = []
        if ledger == 'FP':
            others.extend(p for p in (rival, projection.opponent if projection else None)
                          if p and candidate(seat,p) in packet['entities'])
            # Other contenders can explain a change in the likely finalists.
            # Only include another candidate for a material win-chance move.
            others.extend(r['party_id'] for r in packet.get('win_changes', [])
                          if r['seat'] == seat and abs(r['change_pp']) >= 2 and max(r['before'],r['after']) >= 5
                          and candidate(seat,r['party_id']) in packet['entities'])
        point, gap = additional_count_point(increment, group['locations'], seat, party, ledger, opponent, others)
        if (gap is not None and rival in others and rival in increment['shares']
                and rival in increment.get('remaining_shares', {})):
            gap -= increment['shares'][rival]-increment['remaining_shares'][rival]
    else:
        point, gap = cumulative_count_point(group, packet, row, rival, projection, endpoints, headline_rival)
    partial = group.get('partial_categories', {})
    for category, (tcp_total, fp_total) in sorted(partial.items()):
        point += (f' The {category} count also has TCP results for **{tcp_total:,}** of **{fp_total:,}** FP votes. '
                  'There is no separate earlier estimate for that batch.')
    other_categories = [c for c in group.get('other_categories', ()) if c not in partial]
    if other_categories:
        places = rule_evidence.joined(['the '+category+' count' for category in other_categories])
        point += f' Separate {ledger} updates also came from {places}.'
    fresh_fp = None
    if not attributed and headline_rival and not packet.get('input_revision_changed'):
        fresh_fp = fresh_fp_comparison(candidates, group, seat, party, rival)
    # When both sides reported below expectation, it is the difference between
    # their shortfalls that tells us which of these contenders gained ground.
    return LocalCount(location, group['changed'], point, gap, count_type=ledger, units=group['units'],
                      attribution=attributed[1] if attributed else None, contrast=attributed[2] if attributed else None,
                      fresh_fp=fresh_fp)


def firmer_lead(packet, row, endpoints):
    """Explain a gain in confidence when the range closes around a TCP lead.
    Compare the same finalists
    in both completed forecasts. When other finals are possible, check how
    much winning more often against this opponent contributed to the change
    in the overall win chance."""
    seat, subject = row['seat'], row['party_id']
    pair = most_likely_pair(seat, endpoints) or packet.get('close_tcp_counts', {}).get(seat, {}).get('pair', [])
    if (not endpoints or packet.get('input_revision_changed') or len(pair) != 2
            or pair[0] == pair[1] or subject not in pair or not row['change_pp']):
        return None
    opponent = next(p for p in pair if p != subject)
    leader, rival = (subject, opponent) if row['change_pp'] > 0 else (opponent, subject)
    if any(candidate(seat, p) not in packet['entities'] for p in pair):
        return None
    identities = [s.get('seats', {}).get(seat, {}).get('candidates', {}) for s in endpoints]
    if any(not identities[0].get(p) or identities[0][p] != identities[1].get(p) for p in pair):
        return None
    ranges, frequencies = [], []
    for summary in endpoints:
        district = summary.get('seats', {}).get(seat, {})
        # Read the share from the leader's perspective. If the stored share
        # belongs to the opponent, their upper bound is the leader's lower one.
        for first, second in ((leader, rival), (rival, leader)):
            key = first+'/'+second
            frequency = district.get('pairing_percent', {}).get(key)
            if not frequency:
                continue
            bands = district.get('tcp_percentiles', {}).get(key) or {}
            values = [bands.get(q) for q in ('5', '50', '95')]
            if None not in values and 0 <= values[0] <= values[1] <= values[2] <= 100:
                if first != leader:
                    values = [100-value for value in reversed(values)]
                ranges.append([round(value, 1) for value in values])
                frequencies.append(frequency)
            break
    if len(ranges) != 2:
        return None
    (old_low, old_median, old_high), (low, median, high) = ranges
    # Require the median to remain ahead and both ends of the middle 90% to
    # move inwards at the precision the reader sees. A smaller range alone
    # could also accompany a shift towards defeat.
    if not (min(median, old_median) > 50 and old_low < low and high < old_high):
        return None
    # A lead can grow while its uncertainty falls. Judge that reduction by
    # the average distance from the median to either end of the range. One
    # end can move only slightly because the median itself has shifted.
    half_width_reduction = ((old_high-old_low) - (high-low))/2
    if median - old_median > half_width_reduction:
        return None
    share = f'The projected TCP share for {candidate(seat, leader)} against {candidate(seat, rival)}'
    if median == old_median:
        share += f' held at **{median:.1f}%**'
    else:
        share += ' ' + movement(median, median-old_median)
    conjunction = 'but' if median < old_median else 'and'
    share += (f', {conjunction} the range of likely results narrowed. The forecast is more confident '
              f'that {candidate(seat, leader)} will hold the lead against {candidate(seat, rival)}.')
    if frequencies == [100, 100]:
        return share

    # With alternative opponents, the lead might firm up in a matchup that
    # barely affects the election. For example, winning ten points more often
    # against an opponent met in half of simulations adds about five points
    # overall. Average the two frequencies so that a change in who qualifies
    # is kept separate. Require at least half of the reported win movement to
    # come from this source before highlighting the narrower range.
    rates = [pairing_commentary.pair_win_rate(s, seat, leader, rival) for s in endpoints]
    if None in rates:
        return None
    (freq0, win0), (freq1, win1) = rates
    if (win1-win0)*(freq0+freq1)/200 < abs(row['change_pp'])/2:
        return None
    # Near-certain pairings can round to exactly the same winning chances
    # already given for the seat. Retain conditional odds when they add detail.
    if min(freq0, freq1) >= 99:
        return share
    return (share + f' Against {candidate(seat, rival)}, the win chance for {candidate(seat, leader)} '
            f'rose from **{win0:.1f}%** to **{win1:.1f}%**.')


def quiet_count_story(packet, endpoints):
    """When the whole report would otherwise be quiet, give at most one
    smaller movement with a local count readers can check. Half the usual
    one-point cutoff and 100 changed ballots in one reporting unit or a
    net addition of 100 votes to an explanatory district TCP count are
    editorial choices. They keep tiny rechecks and simulation noise from
    supplying a story just because nothing larger happened."""
    # Previously prepared comparisons retain the mechanical list of first
    # returns. New comparisons also cover additions at already reporting booths.
    returns = packet['rule_context'].get('early_returns', (packet.get('booth_changes') or {}).get('new', {}))
    early = quiet_early_return(packet, returns)
    if early and len(returns.get('shown', [])) > 1:
        return early, None
    eligible = [row for row in packet['win_changes']
                if .5 <= abs(row['change_pp']) < 1 and not row.get('identity_changed')
                and candidate(row['seat'], row['party_id']) in packet['entities']]
    # Two finalists can have equal and opposite movements to the published
    # precision. In that case describe the candidate now more likely to win.
    eligible.sort(key=lambda row: (-round(abs(row['change_pp']), 1), -row['after'], row['seat'], row['party_id']))
    # Give a district's leading contender first consideration even when small
    # movements for other candidates make the two largest changes unequal.
    by_seat = {}
    for row in eligible:
        by_seat.setdefault(row['seat'], []).append(row)
    eligible = []
    for seat, rows in by_seat.items():
        eligible.extend(sorted(rows, key=lambda r: headline_candidate_priority(r, packet['all_seats'][seat]), reverse=True))
    for row in eligible:
        seat = row['seat']
        count = packet.get('close_tcp_counts', {}).get(seat, {})
        modal = most_likely_pair(seat, endpoints)
        if modal and set(modal) != set(count.get('pair', [])):
            count = {}
        projection = None if late_tcp_count(count) else local_pair_projection(packet, row, endpoints)
        # When a substantial TCP count firms up an existing lead, the district
        # totals and the range of final outcomes explain more than an individual
        # booth's first preferences. Keep the headline short and put that
        # evidence together in the seat section.
        if sum(count.get('net_by_candidate', {}).values()) >= 100:
            confidence = firmer_lead(packet, row, endpoints)
            if confidence:
                booth = supporting_booth(packet, row, minimum_ballots=100, projection=projection) if projection else None
                points = [count_detail(packet, seat, with_forecast_range=True), booth.point if booth else None, confidence]
                return win_outlook(row, name_seat=True), {
                    'seats': [seat], 'text': '\n- '.join([f'In {{seat:{seat}}}:', *filter(None, points)])}
        booth = supporting_booth(packet, row, minimum_ballots=100, projection=projection, modal=modal)
        if projection and not booth:
            # A quiet update can still name the reporting location if a
            # comparison exists but it does not explain the district trend.
            booth = supporting_booth(packet, row, minimum_ballots=100, modal=modal)
        if not booth or any(token not in packet['entities'] for token in display.TOKEN.findall(booth.point)):
            continue
        headline = win_outlook(row, name_seat=True)
        if booth.count_type == 'FP' and booth.gap_pp is not None and booth.gap_pp <= -1:
            swap = pairing_commentary.changed_opponents_after_weak_primary(row, endpoints)
            if swap and all(t in packet['entities'] for t in display.TOKEN.findall(swap[1])):
                rival, explanation = swap
                counts = supporting_booth(packet, row, minimum_ballots=100, rival=rival,
                                          modal=modal, endpoints=endpoints, headline_rival=True)
                if counts and counts.location == booth.location and counts.count_type == 'FP':
                    return headline, {'seats':[seat], 'text':f'In {{seat:{seat}}}:\n- {counts.point}\n- {explanation}'}
        multiple = booth.units == 1 and display.other_local_counts_changed(packet, seat)
        # Separate sentences allow a contrary booth to be quoted as a
        # measured result without claiming it caused the district movement.
        headline += ' '+booth.point
        if projection:
            headline += ' '+projection.point(row)
        if multiple:
            headline += ' Other local counts also changed.'
        return headline, None
    return early, None


def quiet_early_return(packet, returns):
    """Name up to three reporting places while the outlook remains quiet.
    Give the FP and TCP additions separately at each location."""
    progress = packet['statewide']['after'].get('countProgress') or {}
    expected = progress.get('expected_formal_votes') or 0
    counted = [progress.get(key) for key in ('fp_votes', 'tcp_votes')]
    # One percent of projected formal votes is an editorial definition of
    # early counting. Later, a small first return in an otherwise quiet
    # update need not displace the ordinary short notice.
    if (expected <= 0 or None in counted or max(counted) > .01 * expected
            or not packet['rule_context'].get('all_win_changes_below_one_point')
            or packet.get('category_transfers')):
        return None
    places = []
    for unit in returns.get('shown', []):
        seat = '{seat:'+unit['seat']+'}'
        totals = []
        for ledger in ('FP', 'TCP'):
            votes = unit[ledger.lower()+'_net_change']
            if votes > 0:
                totals.append(f'**{votes:,}** {ledger} '+('vote' if votes == 1 else 'votes'))
        if not totals or seat not in packet['entities']:
            return None
        location = unit['name']
        if location == unit.get('vote_type'):
            location = 'the '+location+' count'
        places.append((location+' in '+seat, ' and '.join(totals)))
    if not places:
        return None
    if len(places) == 1:
        location, votes = places[0]
        counting = location[:1].upper()+location[1:]+' reported '+votes
    else:
        labels = [f'{location} ({votes})' for location, votes in places]
        other = returns.get('other_count', 0)
        if other:
            noun = 'reporting location' if returns.get('other_categories') else 'booth'
            labels.append(f'{other} other {noun}'+('s' if other != 1 else ''))
        counting = 'New counting came from '+rule_evidence.joined(labels)
    return counting+'. Across the election, every candidate’s win chance moved by less than **1 pp**.'


def count_detail(packet, seat, *, with_forecast_range=False):
    """Put a late, continuing district TCP in perspective. The remaining share
    compares the same opponents and comes from the district forecast and
    estimated final turnout, including possible count revisions."""
    count = packet.get('close_tcp_counts', {}).get(seat)
    if not count or not late_tcp_count(count):
        return None
    first, other = [candidate(seat, p) for p in count['pair']]
    before, after = count['lead_before'], count['lead_after']
    added = sum(count.get('net_by_candidate', {}).values())
    fewer_remaining = count.get('fewer_votes_favour_counted_leader')
    if before == after and not fewer_remaining and not (with_forecast_range and added > 0):
        return None
    if fewer_remaining and before == after:
        unit = 'vote' if after == 1 else 'votes'
        text = f'The TCP count put {first} **{after:,} {unit}** ahead of {other} at both updates.'
    elif before < 0 < after:
        unit = 'vote' if before == -1 else 'votes'
        text = f'In the TCP count, {first} went from **{-before:,} {unit}** behind {other} to **{after:,}** ahead.'
    elif after == 0:
        text = f'{first} and {other} are now level in the TCP count.'
    elif before > 0:
        opening = 'The counted TCP lead'
        if with_forecast_range and added > 0:
            votes = 'vote' if added == 1 else 'votes'
            opening = f'With **{added:,}** more TCP {votes} counted, the lead'
        text = f'{opening} for {first} over {other}'
        if before == after:
            text += f' held at **{after:,}** votes.'
        else:
            direction = 'grew' if after > before else 'narrowed'
            text += f' {direction} from **{before:,}** to **{after:,}** votes.'
    else:
        unit = 'vote' if after == 1 else 'votes'
        text = f'{first} now leads {other} by **{after:,} {unit}** in the TCP count.'
    if count.get('lead_source'):
        text += ' '+count['lead_source']
    remaining = count.get('remaining_after')
    if fewer_remaining:
        old = count['remaining_before']
        text += (f' The forecast now projects about **{remaining:,} votes** still to come, down from '
                 f'**{old:,} votes**. That leaves {other} needing a larger share to catch {first}.')
    elif remaining is not None and remaining > 0:
        share = count.get('approx_remaining_tcp_percent_for_first')
        if share is not None and count.get('count_and_projection_diverge'):
            text += (f' The forecast puts {first} on about **{display.decimal(share)}%** of the '
                     f'**{remaining:,}** TCP votes projected to remain, allowing for revisions.')
        else:
            text += ' '+display.projected_remaining_votes(remaining)
    elif remaining == 0:
        text += ' '+display.projected_remaining_votes(remaining)
    median = count.get('projected_median_percent_for_first')
    if not with_forecast_range and not fewer_remaining and median is not None and abs(median - 50) <= 5:
        # A hundred-vote lead has different implications at the beginning and
        # end of counting. Give the median final share for the same opponents
        # when the forecast itself puts them within five points of 50%.
        text += f' The projected final share for {first} against {other} is **{display.decimal(median)}%**.'
    return text


def preference_detail(packet, seat):
    """Name a supporting district only when the live preference diagnostics
    have already checked its contribution for these opponents."""
    context = packet.get('district_preference_evidence', {}).get(seat)
    if context:
        favoured = context['ordinary_preference_shift_favours']
        other = next(p for p in context['pair'] if p != favoured)
        donor = '{seat:'+context['strongest_supporting_district']+'}'
        return (f'Ordinary booth counts in {donor} strengthened the estimated preference '
                f'flow to {candidate(seat, favoured)} against {candidate(seat, other)}.')
    return None


def retained_count_detail(packet, seat):
    """A recheck can change how much earlier TCP evidence the forecast can
    still use after a pairing switch. Describe that loss of coverage only
    when the comparison traces it to a material change for the same pair."""
    revision = packet.get('retained_pair_revisions', {}).get(seat)
    if not revision:
        return None
    favoured = revision['reestimated_booths_favour_more_than_before']
    opponent = next(p for p in revision['pair'] if p != favoured)
    before, after = revision['usable_pair_votes_before_after']
    examples = ' and '.join(revision['booths_no_longer_usable_examples'])
    return (f"The forecast could still use **{after:,}** of the **{before:,}** earlier TCP votes "
            f"for {candidate(seat, favoured)} against {candidate(seat, opponent)}. "
            f"It had to estimate the split at {examples} again, shifting those booths towards "
            f"{candidate(seat, favoured)}. The published district TCP stayed at zero.")


def ranked_count_detail(packet, seat, party, projection=None):
    """A group benchmark applies directly if every one of its ballots has
    now reported TCP. Otherwise use the booth comparisons available elsewhere."""
    groups = [group for group in packet.get('ranked_tcp_groups', {}).get(seat, [])
              if party in group['pair'] and group['newly_counted'] == group['ballots'] >= 10
              and abs(group['first_share_after'] - group['first_share_before']) >= .5]
    if projection:
        groups = [group for group in groups if projection.opponent in group['pair']]
    if not groups:
        return None
    group = max(groups, key=lambda item: item['newly_counted'])
    first, second = group['pair']
    before, after = group['first_share_before'], group['first_share_after']
    opponent = second
    if party == second:
        before, after = 100-before, 100-after
        opponent = first
    booths = group['largest_reporting_booths']
    if not booths:
        return None
    labels = ['the '+name+' count' if name.casefold() == group['category'].casefold() else name for name in booths]
    if group['units'] > len(labels):
        labels = labels[:2] + [f"{group['units']-2} other booths"]
    location = rule_evidence.joined(labels)
    subject, rival = candidate(seat, party), candidate(seat, opponent)
    opening = 'Across' if group['units'] > 1 else ('In' if location.startswith('the ') else 'At')
    point = (f"{opening} {location}, {subject} received **{display.decimal(after)}%** "
             f"of **{group['newly_counted']:,}** new TCP votes against {rival}, "+booth_difference(after-before)+'.')
    return LocalCount(location, group['newly_counted'], point, after-before, 'TCP', group['units'])


def count_revision_headline(packet):
    """A substantial administrative correction can matter even in a safe seat.
    Report at most one. A matched transfer names both categories, counting each
    ballot once. Otherwise look for a fall in a booth's FP total or a change
    in candidate allocations at the same total. A disappearance or a TCP reset
    alone cannot establish a booth recheck."""
    stories = []
    for row in packet.get('category_transfers', []):
        size = max(row['fp_votes'], row['tcp_votes'])
        ledger = 'first-preference' if size == row['fp_votes'] else 'two-candidate-preferred'
        text = (f"In {{seat:{row['seat']}}}, **{size:,}** {ledger} votes appear to have been "
                f"moved from the {row['from']} count to the {row['to']} count.")
        stories.append((size, text))
    for event in packet['evidence']:
        if event['kind'] != 'booths' or not event.get('seat'):
            continue
        before = (event.get('before') or {}).get('fp_votes_current') or {}
        after = (event.get('after') or {}).get('fp_votes_current') or {}
        new_total = sum(after.values())
        if new_total == 0:
            continue
        name = event['after'].get('name') or event['record'].rsplit('/', 1)[-1]
        removed = sum(before.values()) - new_total
        if removed >= 100:
            text = (f"In {{seat:{event['seat']}}}, the reported first-preference total for {name} "
                    f"fell by **{removed:,} votes** to **{new_total:,}**.")
            stories.append((removed, text))
        elif (removed == 0 and before.keys() == after.keys() and event['before'].get('count_to_projection_ids')
              == event['after'].get('count_to_projection_ids')):
            moved = activity.affected_ballots(before, after)
            text = (f"In {{seat:{event['seat']}}}, the allocation of **{moved:,}** first preferences between "
                    f"candidates changed at {name}. The total stayed at **{new_total:,} votes**.")
            stories.append((moved, text))
    # This threshold is for a headline about the administration of the count.
    # Smaller rechecks can still feature through their effect on a close race.
    material = [(size, text) for size, text in stories if size >= 100]
    return max(material)[1] if material else None


def preference_sample_point(packet, seat, matchup=None):
    """Put the measured transfers beside the largest change in a sampled
    candidate’s chance of qualifying. Either finalist can still change.
    The district movement covers everything received in the window."""
    text = display.preference_sample_detail(packet, seat)
    if not text or matchup:
        return text  # A separate matchup bullet already explains the exclusion.
    sample = packet['reported_preference_samples'][seat]
    placing = sample.get('second_place')
    labels = (placing['ahead'], placing['behind']) if placing else sample['destination_percent']
    movements = display.sample_finalist_movements(packet, seat, labels)
    if movements:
        subject = max(movements, key=lambda person: (abs(movements[person]['change']), person))
        row = movements[subject]
        verb = 'rose' if row['change'] > 0 else 'fell'
        text += (f' Across the district, the chance of {subject} reaching the final two {verb} '
                 f'from **{display.decimal(row["before"])}%** to **{display.decimal(row["after"])}%**.')
    return text


def accumulating_primary_point(seat, favoured, other, check, means):
    """Explain the strongest supported candidate movement. Agreement means
    that the fresh booths polled on the same side of their own estimates as
    the earlier count. A larger or more geographically spread count alone
    cannot establish agreement, and a projected fall is not a loss of ballots."""
    contributions = [(direction*(check[p]['after']-check[p]['previous_weight']), direction, p)
                     for direction, p in ((1, favoured), (-1, other))]
    effect, direction, p = max(contributions)
    result = check[p]
    pattern = result.get('new_booths')
    if (not pattern or effect < .5 or direction*(means[1][p]-means[0][p]) < .5
            or direction*result['earlier_signal'] <= 0 or direction*result['current_signal'] <= 0):
        return None
    subject = candidate(seat, p)
    where = ' at '+result['earlier_booth'] if result.get('earlier_booth') else ''
    side = 'above' if direction > 0 else 'below'
    low, high = sorted(direction*pattern[key] for key in ('low', 'high'))
    if low >= .5:
        # Booths can both fall short while the pooled shortfall shrinks.
        # The forecast responds to the size and spread of the count; sharing
        # a direction is not a test used to give the new votes extra weight.
        if abs(result['current_signal']) < abs(result['earlier_signal']):
            difference = 'shortfall' if direction < 0 else 'surplus'
            text = f'The new result reduced the overall FP {difference} for {subject} across the counted booths.'
            if result.get('dominant_earlier_count'):
                return text + (f' With votes now spread across more booths, the forecast applied more of '
                               f'that smaller {difference} to the votes still to come.')
            return text + ' The forecast nevertheless gave the larger count more weight for the remaining votes.'
        return (f'FP support for {subject} was {side} expectations both in the earlier count{where} '
                'and in the new returns. The forecast gave the larger count more weight for the remaining votes.')
    if low <= -.5 and high >= .5:
        dominant = pattern.get('dominant')
        strongest = ''
        if dominant:
            trend = 'shortfall' if pattern['dominant_gap'] < 0 else 'gain'
            strongest = f', with the largest FP {trend} at {dominant}'
        return (f'The new booths were mixed for {subject}{strongest}. Across the count so far, '
                f'FP support remained {side} expectations, and the forecast gave that broader count more weight.')
    if (direction*pattern['pooled'] <= -.5
            and abs(result['current_signal']) < abs(result['earlier_signal'])):
        earlier = result.get('dominant_earlier_count')
        if earlier:
            # A return can weaken the observed pattern while a less concentrated
            # count makes it more informative about the booths still to report.
            # Describe what happened to both candidates when their cumulative
            # comparisons support it, even if just one led the district revision.
            balance = f'{subject} remained {side} expectations'
            if all(check[q].get('current_signal', 0)*d > 0 and check[q].get('earlier_signal', 0)*d > 0
                   for q, d in ((favoured, 1), (other, -1))):
                balance = f'{candidate(seat, favoured)} remained above expectations and {candidate(seat, other)} below'
            return (f"Previously, **{earlier['largest']:,}** of **{earlier['total']:,}** local FP votes came from "
                    f"{earlier['name']}. Across the booths counted so far, {balance}. With the count spread "
                    'across more booths, the forecast put more weight on that pattern for the remaining votes.')
        noun = 'gain' if direction > 0 else 'shortfall'
        return (f'The new count softened the earlier FP {noun}{where} for {subject}. FP support across '
                f'the count so far remained {side} expectations, and the forecast gave it more weight.')
    return None


def district_primary_evidence(packet, row, projection, endpoints):
    """Test whether the extra weight given to the local count helps explain
    the published FP gap between the two finalists. The diagnostic estimates
    the effect at the FP stage only, before preferences and finalist selection.
    Return the observed district gap movement and, separately, any weight-based
    explanation. A gap alone cannot diagnose the reason it changed."""
    seat, party, rival = row['seat'], row['party_id'], projection.opponent
    if not endpoints:
        return None, None
    districts = [s['seats'][seat] for s in endpoints]
    for p in (party, rival):
        info = districts[0].get('candidates', {}).get(p) or {}
        if not info.get('name') or info != districts[1].get('candidates', {}).get(p):
            return None, None
    means = [d.get('fp_mean_percent', {}) for d in districts]
    if any(p not in m for m in means for p in (party, rival)):
        return None, None
    sign = 1 if projection.after > projection.before else -1
    published = [sign*(m[party]-m[rival]) for m in means]
    change = published[1] - published[0]
    favoured, other = (party, rival) if sign > 0 else (rival, party)
    gap = ((f'Across the seat, the projected mean FP gap shifted **{change:.1f} pp** '
            f'towards {candidate(seat, favoured)} relative to {candidate(seat, other)}.') if change >= .2 else None)
    check = packet['rule_context'].get('primary_weight_checks', {}).get(seat, {})
    remaining = [check.get(p, {}).get('remaining_fp') for p in (favoured, other)]
    if all(remaining) and change > 0:
        fav, opp = remaining
        if (fav[1]-opp[1])-(fav[0]-opp[0]) >= .3:
            return gap, (f'For the FP votes still to come, the estimate based on booth results moved from '
                         f'**{fav[0]:.1f}%** to **{fav[1]:.1f}%** for {candidate(seat, favoured)} and from '
                         f'**{opp[0]:.1f}%** to **{opp[1]:.1f}%** for {candidate(seat, other)}.')
    if not gap:
        return None, None
    if any(p not in check or check[p]['weight_change'] < 0 for p in (party, rival)):
        return gap, None
    old, held, new = [sign*(check[party][key]-check[rival][key]) for key in ('before', 'previous_weight', 'after')]
    # Ignore rounding and small contributions: the weight change must add
    # at least half a point and account for at least half the central shift.
    # The same pair's published mean FP gap must move at least half a point.
    if new <= old or new-held < max(.5, (new-old)/2) or change < .5:
        return gap, None
    # Describe the two published estimates at the precision readers see.
    # When both fall, the larger fall explains how the other candidate gained.
    def movement_for(p):
        before, after = (round(m[p], 1) for m in means)
        name = candidate(seat, p)
        if before == after:
            return f'held at **{after:.1f}%** for {name}'
        verb = 'rose' if after > before else 'fell'
        return f'{verb} from **{before:.1f}%** to **{after:.1f}%** for {name}'

    if all(round(means[1][p], 1) < round(means[0][p], 1) for p in (favoured, other)):
        detail = (f'the projected FP share fell more for {candidate(seat, other)} '
                  f'(**{means[0][other]:.1f}%** to **{means[1][other]:.1f}%**) than for '
                  f'{candidate(seat, favoured)} (**{means[0][favoured]:.1f}%** to **{means[1][favoured]:.1f}%**).')
    else:
        detail = f'the projected FP share {movement_for(favoured)} and {movement_for(other)}.'
    explanation = accumulating_primary_point(seat, favoured, other, check, means)
    return gap, (explanation or 'As more votes came in, the forecast relied more on the results counted so far.') + ' Across the seat, ' + detail


def wider_lead(row, projection, endpoints):
    """A stable median can coexist with more losing draws. Check that the
    same final is effectively certain and that both tails actually widened."""
    if (not endpoints or projection.before is None or row['change_pp'] > -1 or projection.after <= 50
            or round(projection.before,1) != round(projection.after,1)):
        return None
    seat, party = row['seat'], row['party_id']
    pair = '/'.join(sorted((party,projection.opponent)))
    sides = [s['seats'][seat] for s in endpoints]
    if any(s.get('pairing_percent',{}).get(pair,0) < 99 for s in sides):
        return None
    ranges = [s.get('tcp_percentiles',{}).get(pair,{}) for s in sides]
    if any('5' not in r or '95' not in r for r in ranges):
        return None
    old, new = ranges
    if old['5']-new['5'] >= .2 and new['95']-old['95'] >= .2:
        return (f'The range of likely results widened around that lead, with more simulations ending in a win '
                f'for {candidate(seat, projection.opponent)}.')
    return None


def narrower_matchup(row, projection, endpoints):
    """A poor booth can arrive as the forecast becomes more certain of a
    district lead. Check the same named opponents and their middle 90% TCP
    ranges. Require the change in wins within this pair, weighted by how
    often they met, to explain at least half of the overall win gain."""
    seat, party, opponent = row['seat'], row['party_id'], projection.opponent
    if not endpoints or row['change_pp'] <= 0 or projection.before is None:
        return None
    if min(projection.before, projection.after) <= 50 or abs(projection.after-projection.before) >= 1:
        return None
    districts = [s['seats'][seat] for s in endpoints]
    if any(not districts[0]['candidates'].get(p, {}).get('name') or
           districts[0]['candidates'][p] != districts[1]['candidates'].get(p) for p in (party, opponent)):
        return None
    rates = [pairing_commentary.pair_win_rate(s, seat, party, opponent) for s in endpoints]
    if None in rates:
        return None
    (freq0, win0), (freq1, win1) = rates
    if (win1-win0)*(freq0+freq1)/200 < row['change_pp']/2:
        return None
    ranges = []
    for d in districts:
        pairs = d.get('tcp_percentiles', {})
        direct = party+'/'+opponent in pairs
        bands = pairs.get(party+'/'+opponent if direct else opponent+'/'+party, {})
        if '5' not in bands or '95' not in bands:
            return None
        ranges.append((bands['5'], bands['95']) if direct else (100-bands['95'], 100-bands['5']))
    if not (ranges[1][0]-ranges[0][0] >= .2 and ranges[0][1]-ranges[1][1] >= .2):
        return None
    means = [d.get('fp_mean_percent', {}).get(party) for d in districts]
    if None not in means and abs(means[1]-means[0]) < .5:
        opening = (f'Despite the local FP result, the forecast still put {candidate(seat, party)} '
                   f'on **{means[1]:.1f}%** of the district primary. The projected TCP share')
    else:
        opening = f'Despite the local FP result, the projected district TCP share for {candidate(seat, party)}'
    return (f'{opening} '
            f'against {candidate(seat, opponent)} stayed near **{projection.after:.1f}%**. '
            f'The range narrowed around that lead, and {candidate(seat, party)} won '
            f'**{win1:.1f}%** of those matchups, up from **{win0:.1f}%**.')


def opposing_win_moves(packet, row):
    """Choose rivals whose win chances moved against the headline candidate.
    A single rival usually accounts for most of the opposite movement. In a
    three-way race two rivals may share it. Give both figures when each
    movement is at least three points and at least a quarter of the headline
    change. Retain the half-share threshold when mentioning just one rival.
    These are editorial thresholds for the seat detail."""
    delta = row['change_pp']
    minimum = max(3, abs(delta)/4)
    rivals = [r for r in packet['win_changes'] if r['seat'] == row['seat']
              and r['change_pp']*delta < 0 and abs(r['change_pp']) >= minimum
              and not r.get('identity_changed') and candidate(r['seat'], r['party_id']) in packet['entities']]
    rivals.sort(key=lambda r: -abs(r['change_pp']))
    if len(rivals) == 1 and abs(rivals[0]['change_pp']) < abs(delta)/2:
        return []
    return rivals[:2]


def other_contenders(packet, row, moves):
    """In a multi-way race, name serious contenders even if their chances
    barely moved. Ten percent is an editorial threshold for context. Candidates
    already named for a large fall still count towards the number in contention."""
    seat = row['seat']
    mentioned = {r['party_id'] for r in [row, *moves]}
    # The all-seat overview rounds to two decimals, which can push a figure
    # across the one-decimal rounding boundary. Prefer the seat's full
    # simulation figures wherever the local comparison supplies them.
    chances = dict(packet['all_seats'].get(seat, {}))
    chances.update({r['party_id']: r['after'] for r in packet['win_changes'] if r['seat'] == seat})
    others = sorted((p for p, v in chances.items() if v >= 10 and p not in mentioned
                     and candidate(seat, p) in packet['entities']), key=chances.get, reverse=True)
    if len(others) + len(mentioned) < 3 or not others:
        return ''
    if len(others) == 1:
        p = others[0]
        return f' {candidate(seat, p)} also has a win chance of **{chances[p]:.1f}%**.'
    figures = [f'**{chances[p]:.1f}%** for {candidate(seat, p)}' for p in others]
    return ' Other win chances include ' + rule_evidence.joined(figures) + '.'


def other_counted_pair(packet, row, endpoints, modal):
    """A booth can count a different contest from the expected final two.
    Give its result with its own benchmark, and distinguish the outlook for
    those opponents from the candidate's chances across all simulations."""
    seat, party = row['seat'], row['party_id']
    if not endpoints or not modal or packet.get('input_revision_changed'):
        return None
    groups = packet['rule_context']['pools'].get(seat, {}).get(party, ())
    groups = [g for g in groups if g['ledger'] == 'TCP' and set(g['pair']) != modal and
              party in g['pair'] and g['changed'] >= 10]
    if not groups or seat in packet.get('retained_tcp_only_published_zero_at_both_ends', ()):
        return None
    group = max(groups, key=lambda g: g['changed'])
    pair = group['pair']
    other = next(p for p in pair if p != party)
    districts = [s['seats'][seat] for s in endpoints]
    if any(not districts[0].get('candidates', {}).get(p) or
           districts[0]['candidates'][p] != districts[1].get('candidates', {}).get(p) for p in pair):
        return None
    # Use the usual writer so an approximate benchmark is still identified
    # as such. Restrict this detail to the counted opponents we've identified.
    booth = supporting_booth(packet, row, modal=pair, tcp_only=True)
    if not booth or booth.count_type != 'TCP':
        return None
    rates = [pairing_commentary.pair_win_rate(s, seat, party, other) for s in endpoints]
    if None not in rates and min(r[0] for r in rates) >= 5 and abs(rates[1][1]-rates[0][1]) >= 2:
        before, after = (r[1] for r in rates)
        detail = (booth.point + f' When facing {candidate(seat, other)}, the win chance for {candidate(seat, party)} '
                  + movement(after, after-before) + '.')
    else:
        detail = booth.point
    return detail + ' The most likely final pairing was ' + rule_evidence.joined(
        [candidate(seat, p) for p in sorted(modal)]) + '.'


def unchanged_primary_outlook(row, projection, endpoints):
    """Look for a shift in the *projected* primary gap for the likely finalists
    while a different TCP was counted. The caller has established that no local
    FP counts changed. A matching statewide shift is corroboration, not a
    measurement of the cause of the seat's change."""
    seat, party, rival = row['seat'], row['party_id'], projection.opponent
    districts = [s['seats'][seat] for s in endpoints]
    if any(not districts[0].get('candidates', {}).get(p) or
           districts[0]['candidates'][p] != districts[1].get('candidates', {}).get(p) for p in (party, rival)):
        return ''
    means = [d.get('fp_mean_percent', {}) for d in districts]
    if any(p not in m for m in means for p in (party, rival)):
        return ''
    change = (means[1][party]-means[1][rival]) - (means[0][party]-means[0][rival])
    if abs(change) < .2 or change*(projection.after-projection.before) <= 0:
        return ''
    target = party if change > 0 else rival
    result = (f' Local FP counts were unchanged, but the projected district FP gap moved **{abs(change):.1f} pp** '
              f'towards {candidate(seat, target)}.')
    # Use statewide party medians only for identified parties, as two local
    # independents in different districts are not the same electoral choice.
    if any(p.startswith('-') or (districts[1]['candidates'][p].get('party') or '').startswith('IND')
           for p in (party, rival)):
        return result
    state = [s.get('statewide', {}).get('fp_percentiles_by_party', {}) for s in endpoints]
    if all(m.get(p, {}).get('50') is not None for m in state for p in (party, rival)):
        gaps = [m[party]['50']-m[rival]['50'] for m in state]
        shift = gaps[1]-gaps[0]
        if shift*change > 0 and abs(shift) >= .3:
            result += (f' The statewide FP gap moved **{abs(shift):.1f} pp** in the same direction, '
                       'consistent with broader counting trends influencing the seat forecast.')
    return result


def preference_offset(packet, row, projection, endpoints):
    """When a classic booth result and the district move opposite ways,
    check how estimated preferences on outstanding votes changed. The TPP
    account covers every draw, while the named TCP only covers these finalists."""
    seat, party = row['seat'], row['party_id']
    if not endpoints or {party, projection.opponent} != {'0', '1'} or packet.get('input_revision_changed'):
        return None
    rates = [pairing_commentary.pair_win_rate(s, seat, party, projection.opponent) for s in endpoints]
    if None in rates or min(r[0] for r in rates) < 75:
        return None
    parts = packet['rule_context'].get('tpp_components', {}).get(seat)
    if not parts:
        return None
    learned = parts['learned_change_pp']
    movement_alp = (projection.after-projection.before) * (1 if party == '0' else -1)
    if (learned*movement_alp <= 0 or abs(learned) < max(.2, abs(movement_alp)/2)
            or abs(learned) <= abs(parts['measured_change_pp']) + abs(parts['primary_change_pp'])):
        return None
    target = candidate(seat, '0' if learned > 0 else '1')
    broad = parts.get('broad_preference_change_pp', 0)
    if broad*learned > 0 and abs(learned-broad) < abs(learned)/3:
        return (f'For votes still awaiting preferences, broader counting trends shifted the projected final share '
                f'about **{abs(broad):.1f} pp** towards {target}.')
    return (f'Revised preferences on votes still awaiting a count shifted the projected final share '
            f'about **{abs(learned):.1f} pp** towards {target}.')


def tcp_source_point(packet, seat, pair, party, *, include_additions):
    """Name reported TCP additions for the projected opponents. An estimate
    specifically for the added ballots can explain a return even when the
    cumulative booth shares are not comparable. Restored counts stay separate."""
    sources = packet['rule_context'].get('tcp_sources', {}).get(seat, {}).get('/'.join(sorted(pair)), [])
    added = sorted((u for u in sources if not u['restored']), key=lambda u: (-u['changed'], u['name']))
    restored = [u for u in sources if u['restored']]
    points = []
    revision_reported = len(added) == 1 and added[0].get('after_fp_revision') and party in added[0]['counted_tcp']
    if added and include_additions and not revision_reported:
        batch = added[0]
        if len(added) == 1 and party in batch.get('batch_shares', {}):
            observed, expected = batch['batch_shares'][party]
            opponent = next(p for p in pair if p != party)
            increment = dict(previous=batch['previous'], added=batch['changed'],
                             shares={party:observed}, remaining_shares={party:expected})
            points.append(additional_count_point(increment, batch['name'], seat, party, 'TCP', opponent,
                                                 declaration=batch['declaration'])[0])
        elif len(added) == 1 and party in batch.get('increment', {}).get('shares', {}):
            opponent = next((p for p in pair if p != party), None)
            points.append(additional_count_point(batch['increment'], batch['name'], seat, party, 'TCP', opponent)[0])
        elif len(added) <= 3:
            parts = [f"**{u['changed']:,}** " + ('votes ' if i == 0 else '')
                     + ('in ' if u['declaration'] else 'at ') + u['name'] for i, u in enumerate(added)]
            points.append('The reported TCP count increased by '+rule_evidence.joined(parts)+'.')
        else:
            points.append(f"The reported TCP count increased by **{sum(u['changed'] for u in added):,}** votes "
                          f"across {rule_evidence.unit_names(added)}.")
    if restored:
        votes = sum(u['changed'] for u in restored)
        places = rule_evidence.unit_names(restored)
        points.append(f'The **{votes:,}** TCP votes from {places} reappeared in the feed. '
                      'The earlier forecast already included them.')
    for unit in added:
        current = unit.get('counted_tcp', {})
        if unit.get('after_fp_revision') and party in current:
            new_total = sum(current.values())
            added_share = 100*unit['added_votes'][party]/unit['changed']
            increment = dict(previous=new_total-unit['changed'], added=unit['changed'], shares={party:added_share})
            opponent = next(p for p in pair if p != party)
            point, _ = additional_count_point(increment, unit['name'], seat, party, 'TCP', opponent,
                                              declaration=unit['declaration'])
            points.append(point+' While FP counting was ahead, the forecast had combined the earlier TCP '
                          'with the revised first preferences. It now uses the updated TCP tally.')
    if set(pair) == {'0', '1'} and any(u.get('after_fp_revision') for u in added):
        parts = packet['rule_context'].get('tpp_components', {}).get(seat, {})
        effect = parts.get('measured_change_pp', 0)
        if abs(effect) >= max(.5, abs(parts.get('primary_change_pp', 0)) + abs(parts.get('learned_change_pp', 0))):
            beneficiary = candidate(seat, '0' if effect > 0 else '1')
            points.append(f'Across the seat, the change in how counted preferences entered the forecast '
                          f'moved the projected final share about **{abs(effect):.1f} pp** towards {beneficiary}.')
    return ' '.join(points) or None


def ordinary_transfer_point(packet, row, projection, booth, endpoints):
    """A TCP share near the booth benchmark can hide a change in the split
    of transferable votes. Compare measured transfers in ordinary booths,
    where both ledgers cover the same ballots, if the district also moved."""
    if (not projection or projection.before is None or not booth or booth.count_type != 'TCP'
            or booth.gap_pp is None or abs(row['change_pp']) < 5):
        return None
    seat, party = row['seat'], row['party_id']
    context = packet['rule_context'].get('tcp_context', {}).get(seat, {})
    totals, expected = context.get('transfers'), context.get('expected_transfers')
    if (not totals or not expected or set(totals[0]) != {party, projection.opponent}
            or context.get('used_coalition_fit') or abs(booth.gap_pp) > 2):
        return None
    before, after = [100*t[party]/sum(t.values()) for t in totals]
    predicted = 100*expected[party]/sum(totals[1].values())
    if abs(after-before) < 5 or (after-before)*(projection.after-projection.before) <= 0:
        return None
    point = (f'Of the **{sum(totals[1].values()):,}** preferences from other candidates in the newly counted '
             f'ordinary booths, **{after:.1f}%** went to {candidate(seat, party)}. ')
    point += f'The earlier district-aligned booth estimates implied **{predicted:.1f}%** given the FP mix there.'
    if endpoints and seat in packet['rule_context'].get('unchanged_fp', []):
        other = projection.opponent
        means = [s['seats'][seat].get('fp_mean_percent', {}) for s in endpoints]
        if all(p in m for p in (party, other) for m in means):
            point += (f' No local FP totals changed. Across the seat the projected FP share moved from '
                      f'**{means[0][party]:.1f}%** to **{means[1][party]:.1f}%** for {candidate(seat, party)} and '
                      f'from **{means[0][other]:.1f}%** to **{means[1][other]:.1f}%** for {candidate(seat, other)}.')
            shared = context.get('shared_fp_change', {})
            if (all(p in shared for p in (party,other)) and shared[party]*shared[other] < 0
                    and all((means[1][p]-means[0][p])*shared[p] > 0 for p in (party,other))):
                target = party if shared[party] > 0 else other
                point += (f' Regional and statewide counting moved the FP outlook towards {candidate(seat,target)}. '
                          'The figures do not isolate how much of the district TCP change came from that shift.')
    return point


def district_story(packet, row, endpoints, concise=False, review_flags=None):
    seat = row['seat']
    subject = candidate(seat, row['party_id'])
    delta = row['change_pp']
    scope = '{seat:'+seat+'}'
    other_moves = opposing_win_moves(packet, row)
    outcome = ' '.join(win_outlook(r) for r in [row, *other_moves]) + other_contenders(packet, row, other_moves)
    points = [outcome]
    low, high = row['minimum_within_window'], row['maximum_within_window']
    if high - low - abs(delta) >= 10:
        points.append(f'The win chance for {subject} ranged from **{display.decimal(low)}%** to '
                      f'**{display.decimal(high)}%** during the period.')
    count = follow_up = None
    confidence = firmer_lead(packet, row, endpoints)
    close_tcp = packet.get('close_tcp_counts', {}).get(seat, {})
    likely = local_pair_projection(packet, row, endpoints)
    modal = set(most_likely_pair(seat, endpoints))
    counted_pair_relevant = not modal or modal == set(close_tcp.get('pair', ()))
    if counted_pair_relevant and row['party_id'] in close_tcp.get('pair', ()):
        count = count_detail(packet, seat, with_forecast_range=bool(confidence))
        follow_up = display.continued_pair_point(seat, packet)
    prefs = preference_detail(packet, seat)
    retained = retained_count_detail(packet, seat)
    projection = None if counted_pair_relevant and late_tcp_count(close_tcp) else likely
    other = next(iter(other_moves), None)
    rival = other['party_id'] if other else None
    if projection:
        # An opponent's win chance can move too little to earn its own
        # headline while their primary at the reporting booths tells us a
        # great deal about the contest. Compare both sides of the projected
        # TCP when no other rival has already been selected and the matchup
        # is comparable across forecasts. Both candidates can fall short
        # locally while the gap between them moves in favour of one.
        if rival is None and projection.before is not None:
            rival = projection.opponent
        booth = supporting_booth(packet, row, projection=projection, rival=rival, endpoints=endpoints,
                                 headline_rival=bool(other))
        if not booth or (booth.count_type == 'FP' and not booth.attribution):
            # If individual TCP estimates are unavailable, the ranked model
            # can still compare the same group of ballots at the two dates.
            # A matched transfer count is more direct evidence than FP alone.
            ranked = ranked_count_detail(packet, seat, row['party_id'], projection)
            if ranked:
                booth = ranked
    else:
        booth = supporting_booth(packet, row, rival=rival, modal=modal, headline_rival=bool(other))
        if not booth or booth.count_type == 'FP':
            if not modal or row['party_id'] in modal:
                booth = ranked_count_detail(packet, seat, row['party_id'], likely) or booth
    matchup = pairing_commentary.explain(row, endpoints)
    if booth and any(token not in packet['entities'] for token in display.TOKEN.findall(booth.point)):
        booth = None
    alternate = other_counted_pair(packet, row, endpoints, modal) if not booth else None
    sources = tcp_source_point(packet, seat, modal or close_tcp.get('pair', ()), row['party_id'],
                               include_additions=not (count or follow_up or (booth and booth.count_type == 'TCP')))
    if abs(delta) >= 1:
        verb = 'rose' if delta > 0 else 'fell'
        headline = f'In {scope}, the win chance for {subject} {verb} **{display.decimal(abs(delta))} pp**'
        # A comparable booth result helps readers locate the counting.
        # Give it headline space only if its changed ballots account for at
        # least a quarter of the local activity in this window. The timing
        # alone cannot tell us how much of the win chance movement it caused.
        local_ballots = packet.get('observed_count_changes_by_seat', {}).get(seat, {}).get('affected_votes', 0)
        can_highlight_booth = (booth and booth.location and not confidence
                               and (booth.gap_pp is None or booth.gap_pp * delta > 0))
        if (can_highlight_booth and local_ballots and booth.changed_ballots >= .25 * local_ballots
                and not packet.get('input_revision_changed')):
            if booth.units > 1:
                headline += f' with counting in {booth.location}'
            elif display.other_local_counts_changed(packet, seat):
                headline += f' with updates to {booth.location} and other local counts'
            else:
                headline += f' as {booth.location} was updated'
        elif follow_up and not packet.get('input_revision_changed'):
            headline += ' as more votes were counted for the revised TCP pairing'
        elif (packet.get('reported_preference_samples', {}).get(seat, {}).get('use_in_simulations_confirmed')
              and not packet.get('input_revision_changed')):
            headline += ' after a preference sample was reported'
        elif close_tcp.get('fewer_votes_favour_counted_leader') and count:
            headline += ' with fewer TCP votes projected to remain'
        elif not packet.get('input_revision_changed'):
            places, votes = packet['rule_context'].get('count_locations', {}).get(seat, ('', 0))
            if places and votes >= .25 * local_ballots:
                headline += '. Counting was updated '+('in ' if places.startswith('the ') else 'at ')+places
        headline += '.'
    else:
        headline = win_outlook(row, name_seat=True)
    sample = preference_sample_point(packet, seat, matchup)
    # A booth may favour Labor over the Greens while both poll better than
    # the candidate competing with the Greens for second. Check which contest
    # actually changed before treating that local FP gap as contrary evidence.
    qualification = None
    if (not (matchup or sample or prefs or confidence or packet.get('input_revision_changed'))
            and projection and rival == projection.opponent and booth and booth.count_type == 'FP'
            and booth.gap_pp is not None and abs(booth.gap_pp) >= 1 and booth.gap_pp*delta < 0):
        qualification = pairing_commentary.contest_for_second(row, rival, endpoints)
    # Show the projection for the opponents under discussion even when no
    # available booth explains its movement. When a narrowing range explains
    # the increased win chance, that explanation already includes the median
    # for the projected finalists.
    projected_share = None
    local_activity = seat in packet.get('observed_count_changes_by_seat', {})
    if projection and not confidence and (local_activity or close_tcp or booth):
        projected_share = projection.point(row)
        widening = wider_lead(row, projection, endpoints) if not packet.get('input_revision_changed') else None
        if widening:
            projected_share += ' '+widening
        if alternate and endpoints and seat in packet['rule_context'].get('unchanged_fp', ()) and projection.before is not None:
            projected_share += unchanged_primary_outlook(row, projection, endpoints)
        # A disappointing primary and a nearly unchanged TCP median can
        # coexist with a gain in win chance if the two finalists are both
        # picking up wins as a third contender recedes. Check the frequencies
        # actually recorded for this pairing before offering that explanation.
        if (endpoints and not packet.get('input_revision_changed') and not matchup
                and booth and booth.count_type == 'FP' and booth.gap_pp is not None and booth.gap_pp <= -1
                and delta >= .5 and projection.before is not None
                and abs(projection.after - projection.before) < .1):
            explanation = pairing_commentary.both_finalists_gain(row, projection.opponent, endpoints,
                                                                 packet['win_changes'])
            if explanation and all(t in packet['entities'] for t in display.TOKEN.findall(explanation)):
                projected_share += ' ' + explanation
        if (booth and booth.count_type and booth.gap_pp is not None and projection.before is not None and
                (booth.count_type == 'TCP' or rival in (None, projection.opponent)) and
                abs(booth.gap_pp) >= 1 and abs(projection.after - projection.before) >= .1 and
                booth.gap_pp * (projection.after - projection.before) < 0):
            # Be explicit when the measured booths and the district forecast
            # point in opposite directions. Neither gives a causal account
            # of the other. The evidence may also include remaining booths,
            # other parties' transfers or broader movement across districts.
            projected_share = ('Despite that local '+booth.count_type+' result, '+
                               projected_share[0].lower()+projected_share[1:])
            selection = None
            if booth.count_type == 'TCP':
                selection = preference_offset(packet, row, projection, endpoints)
                elsewhere = packet['rule_context'].get('tcp_context', {}).get(seat, {}).get('elsewhere')
                party = row['party_id']
                if not selection and elsewhere and set(elsewhere[0]) == {party, projection.opponent}:
                    prior, now = [s[party] for s in elsewhere]
                    if abs(now-prior) >= .5 and (now-prior)*(projection.after-projection.before) > 0:
                        selection = (f'Across the other counting locations, the projected TCP share for {subject} '
                                     f'against {candidate(seat, projection.opponent)} moved from **{prior:.1f}%** '
                                     f'to **{now:.1f}%**.')
                if selection:
                    projected_share += ' ' + selection
            if booth.count_type == 'FP' and not packet.get('input_revision_changed') and not (sample or prefs):
                beneficiary, rival_in_final = row['party_id'], projection.opponent
                if projection.after < projection.before:
                    beneficiary, rival_in_final = rival_in_final, beneficiary
                district_gap, reweighting = district_primary_evidence(packet, row, projection, endpoints)
                selection = (reweighting or
                             pairing_commentary.possible_opponent_change(endpoints, seat, beneficiary, rival_in_final))
                if selection and all(t in packet['entities'] for t in display.TOKEN.findall(selection)):
                    projected_share += ' ' + selection
                else:
                    selection = None
                    if district_gap:
                        projected_share += ' ' + district_gap
            # A material win movement in the same direction as the district
            # TCP revision warrants a closer look. The measured booth group
            # cannot explain that revision on its own. Distinguish a change
            # in the projected primary from a newly counted primary ballot.
            # Only the complete local evidence can establish an unchanged FP.
            if abs(delta) >= 5 and booth.gap_pp*delta < 0 and not packet.get('input_revision_changed'):
                if not matchup and not sample and endpoints:
                    matchup = pairing_commentary.reaching_final_more_often(row, projection.opponent, endpoints)
                if not selection:
                    projected_share += ' The reported booth result alone does not explain that revision.' if booth.units == 1 else (
                        ' Those booth results alone do not explain that revision.')
                if endpoints and seat in packet['rule_context'].get('unchanged_fp', []):
                    levels = [(s['seats'][seat].get('fp_percentiles', {}).get(row['party_id']) or {}).get('50')
                              for s in endpoints]
                    if None not in levels and abs(levels[1]-levels[0]) >= .5:
                        projected_share += (f' The projected district FP share for {subject} also moved from '
                                            f'**{levels[0]:.1f}%** to **{levels[1]:.1f}%**, '
                                            'although the local FP counts were unchanged.')
                if review_flags is not None:
                    review_flags.append(dict(seat=seat, party=row['party_id'], opponent=projection.opponent,
                        ledger=booth.count_type, booth_evidence=booth.point, win_change_pp=delta,
                        district_tcp_before=projection.before, district_tcp_after=projection.after))
        if close_tcp.get('pair') and not counted_pair_relevant and not matchup:
            chances = endpoints[1]['seats'][seat]['pairing_percent']
            projected_share += f' These candidates reach the TCP in **{max(chances.values()):.1f}%** of simulations.'
        if (booth and booth.count_type == 'FP' and booth.gap_pp is not None and booth.gap_pp <= -3
                and not (matchup or sample or prefs or packet.get('input_revision_changed'))):
            narrowing = narrower_matchup(row, projection, endpoints)
            if narrowing:
                projected_share = narrowing
        # A minor margin revision adds little in an effectively decided
        # matchup. Keep it when the sentence also explains another movement.
        if (endpoints and projection.before is not None and abs(projection.after-projection.before) < .5
                and projected_share == projection.point(row)):
            rates = [pairing_commentary.pair_win_rate(s, seat, row['party_id'], projection.opponent) for s in endpoints]
            if None not in rates and (min(r[1] for r in rates) >= 98 or max(r[1] for r in rates) <= 2):
                projected_share = None
    if booth and booth.attribution:
        projected_share = ' '.join(p for p in (booth.attribution, projected_share) if p)
    transfers = ordinary_transfer_point(packet, row, projection, booth, endpoints)
    replacement = packet.get('replacement_tcp_counts', {}).get(seat)
    realignment = None
    if replacement and replacement.get('previous_pair') and len(replacement.get('pair', ())) == 2:
        def names(pair):
            return ' vs '.join(candidate(seat, p) if candidate(seat, p) in packet['entities'] else '{party:'+p+'}'
                                for p in pair)
        realignment = (f'The reported TCP pairing changed from {names(replacement["previous_pair"])} '
                       f'to {names(replacement["pair"])}. The new pairing has **{replacement["counted_votes"]:,}** TCP votes.')
        if any(t not in packet['entities'] for t in display.TOKEN.findall(realignment)):
            realignment = f'The TCP count was realigned. **{replacement["counted_votes"]:,}** votes have been counted for the new pairing.'
    observations = (sample, matchup, realignment, follow_up, count, retained, prefs, booth.point if booth else alternate, sources, transfers,
                    booth.fresh_fp if booth else None, qualification, confidence, projected_share,
                    booth.contrast if booth else None)
    if concise:
        # In a long overview a second district needs its outcome and strongest
        # explanation. Keep any new preference sample ahead of other details.
        points = [outcome]
        observations = (next((p for p in observations if p), None),)
    for point in observations:
        if point and all(token in packet['entities'] for token in display.TOKEN.findall(point)):
            points.append(point)
    if ((packet.get('count_activity') or {}).get('known') and not prefs and not retained and not sample
            and not (count and close_tcp.get('fewer_votes_favour_counted_leader'))
            and not packet.get('input_revision_changed') and 'observed_count_changes_by_seat' in packet
            and seat not in packet['observed_count_changes_by_seat']):
        points.append('No local counts changed. Movements like this often reflect counting trends elsewhere.')
    return headline, {'seats': [seat], 'text': '\n- '.join([f'In {scope}:', *points])}


def abbreviate_counts(text):
    """Use the glossary abbreviations throughout the rules edition, including
    points shared with the AI formatter. Leave candidate and seat tokens intact."""
    names = {'first preferences':'FP votes', 'first preference':'FP', 'first-preference':'FP',
             'two-candidate-preferred':'TCP', 'two-candidate':'TCP'}
    return re.sub(r'\{[^{}]+\}|\b(?:first preferences?|first-preference|two-candidate(?:-preferred)?)\b',
                  lambda match: names.get(match[0].lower(), match[0]), text, flags=re.I)


def comparison_context(group, packet):
    """Shortlist districts using every candidate's movement. Keep both rivals
    for each selected story and their comparable local counts. This selection
    travels only to the rules writer, independently of the paid evidence budget."""
    full = dict(packet, win_changes=group['win_changes'], close_tcp_counts=group.get('close_tcp_counts', {}))
    selected = {r['seat'] for r in district_order(full, display.entities(packet), single_update=True)}
    if not selected:
        selected = {r['seat'] for r in group['win_changes'] if .5 <= abs(r['change_pp']) < 1
                    and r['seat'] in packet.get('observed_count_changes_by_seat', {})}
    rows = [r for r in group['win_changes'] if r['seat'] in selected]
    flows = (group.get('statewide_preference_changes') or {}).get('rows', [])
    noteworthy = [f for f in flows if abs(f['change_pp']) >= 2 and not packet.get('input_revision_changed')]
    context = rule_evidence.context(group, rows, noteworthy)
    context['new_pair_headlines'] = new_pair_headlines(group, packet, selected)
    # The seat shortlist can be empty precisely when the only movement is in
    # another district. Check every candidate and the full range during this
    # period before describing the election as quiet.
    # An empty party slot can acquire a label when the first feed arrives.
    # If nobody in that slot won a draw anywhere in the window, the label
    # cannot invalidate the bound. A changed identity with wins still can.
    context['all_win_changes_below_one_point'] = bool(group['win_changes']) and all(
        (not row.get('identity_changed') or row['maximum_within_window'] == 0)
        and row['maximum_within_window'] - row['minimum_within_window'] < 1
        for row in group['win_changes'])
    if context['all_win_changes_below_one_point'] and not group.get('category_transfers'):
        context['early_returns'] = rule_evidence.early_returns(group['evidence'])
    # Preserve the distinction between no net increase and no ballot
    # reallocations. A changed party allocation at a fixed booth total
    # is still fresh FP evidence. Use the full comparison for this check.
    if (group.get('count_activity') or {}).get('known'):
        fp_changes = {e.get('seat') for e in group['evidence'] if e['kind'] == 'booths' and
                      activity.affected_ballots((e.get('before') or {}).get('fp_votes_current', {}),
                                                (e.get('after') or {}).get('fp_votes_current', {}))}
        context['unchanged_fp'] = sorted(selected - fp_changes)
    context['major_moves'] = rule_evidence.major_district_moves(group['win_changes'])
    # The full list is sorted by absolute win movement. Preserve the largest
    # gain in each of the three leading districts for a one-line overview.
    examples = {}
    for row in group['win_changes']:
        if row['seat'] in context['major_moves'][:3] and not row.get('identity_changed') and row['change_pp'] > 0:
            examples.setdefault(row['seat'], row)
    context['overview_gains'] = {s: [r['party_id'], r['change_pp']] for s, r in examples.items()}
    context['district_inputs'] = {'win_changes': rows}
    for key in ('close_tcp_counts', 'replacement_tcp_counts', 'ranked_tcp_groups', 'district_preference_evidence'):
        if key in group:
            context['district_inputs'][key] = {s: v for s, v in group[key].items() if s in selected}
    return context


def district_detail_level(packet, context, window):
    """Keep Previous Update focused on its individual step. For other windows
    six major district changes earn an overview. A quieter long period with
    five locally changing contests can still support two detailed stories."""
    if window == 'previous':
        return 'full'
    if len(context.get('major_moves', [])) > 5:
        return 'overview'
    long_window = (packet.get('actual_seconds') or 0) >= 3600 or not packet.get('reference_source_time')
    if long_window and len(context.get('large_local_moves', [])) >= 5:
        return 'short'
    return 'full'


def write(packet, endpoints=None, rule_context=None, window=None, review_flags=None):
    """Return a summary and district bullets from the frozen window, together
    with the display identities needed by this edition. A change of model
    inputs can affect every movement, even in a seat with new counts."""
    entities = display.entities(packet)
    if rule_context is None:
        # Already-prepared packages may contain only the AI evidence shortlist.
        rule_context = rule_evidence.context(packet, packet['win_changes'], packet.get('statewide_preference_changes', []))
    packet = dict(packet, entities=entities, rule_context=rule_context)
    packet.update(rule_context.get('district_inputs', {}))
    headlines = election_headlines(packet, endpoints, window)
    for warning in packet.get('possible_booth_errors', []):
        seat, booth = warning['seat'], warning['booth']
        confidence = display.decimal(warning['confidencePercent'])
        headlines.append(f'In {{seat:{seat}}}, the reported preference split at {booth} looks unusual. '
                         f'The model applies a correction in **{confidence}%** of simulations.')
    sections = []
    # On a busy long comparison, the overview can explain a pattern spanning
    # many races. Reserve detailed seat accounts for the two largest stories.
    # A short update or a period with only a few competitive changes retains
    # the usual detail. Distinct seats are counted once regardless of how many
    # candidates gained or lost chances in each.
    busy_seats = len(rule_context.get('large_local_moves', []))
    order = district_order(packet, entities, single_update=window == 'previous')
    detail = district_detail_level(packet, rule_context, window)
    broad = detail == 'short'
    # Compare periods by how much the electoral outlook changed rather than
    # their clock length. A busy 15 minutes can warrant an election overview,
    # while the previous-update view always explains the individual step.
    major = rule_context.get('major_moves', [])
    overview = detail == 'overview'
    if overview:
        examples = []
        for seat in major[:3]:
            gain = rule_context.get('overview_gains', {}).get(seat)
            if gain and candidate(seat, gain[0]) in entities:
                examples.append(f'{candidate(seat, gain[0])} in {{seat:{seat}}} (**+{gain[1]:.1f} pp**)')
        headline = f'Win prospects shifted substantially in **{len(major)} districts**'
        if examples:
            headline += '. Candidates gaining in the districts with the largest shifts included '+rule_evidence.joined(examples)
        headlines.append(headline+'.')
        order = []
    elif broad:
        headlines.append(f'In **{busy_seats} districts** with local counting, at least one candidate’s win chance changed by **5 pp** or more.')
        order = order[:2]
    for index, row in enumerate(order):
        headline, section = district_story(packet, row, endpoints, concise=broad and index > 0, review_flags=review_flags)
        headlines.append(headline)
        sections.append(section)
    # A new sample can clarify the exclusion order even if the same person
    # remains favoured to win. Still report that observation when its district
    # was shortlisted but the change in win chance did not earn a section.
    covered = {seat for section in sections for seat in section['seats']}
    for seat in sorted(packet.get('reported_preference_samples', {}).keys() - covered):
        point = preference_sample_point(packet, seat)
        headlines.append(f'In {{seat:{seat}}}, a preference sample was reported.')
        if not overview:
            sections.append({'seats': [seat], 'text': f'In {{seat:{seat}}}:\n- '+point})
    revision = count_revision_headline(packet)
    if revision and all(t in entities for t in display.TOKEN.findall(revision)):
        headlines.append(revision)
    if packet.get('input_revision_changed'):
        headlines.insert(0, 'Forecast inputs also changed during this period.')
    if not headlines and (packet.get('count_activity') or {}).get('known'):
        local, section = quiet_count_story(packet, endpoints)
        if local:
            headlines.append(local)
        if section:
            sections.append(section)
    if headlines:
        summary = '\n- '.join(['Key changes:', *headlines])
    elif (packet.get('count_activity') or {}).get('known'):
        summary = activity.QUIET_RESULTS + '.'
    else:
        summary = 'There is insufficient comparable count evidence to explain this update.'
    summary = abbreviate_counts(summary)
    used = set(display.TOKEN.findall(summary))
    for section in sections:
        section['text'] = abbreviate_counts(section['text'])
        used.update(display.TOKEN.findall(section['text']))
    return summary, sections, {token: entities[token] for token in sorted(used)}


def append_editions(results, requests, forecast_id, load_summary=None, review_flags=None, *, rules_only=False):
    """Publish the rule-written report on its own or alongside the AI edition.

    Comparison trials retain the AI status and receipt even on timeout. A
    rules-only run publishes ordinary website commentary with local attribution.
    Quiet updates and missing comparisons already have their own notices.
    """
    editions = {}
    combined = dict(results)
    for window, request in requests.items():
        # Distinct windows sharing an AI request may have different editorial
        # scope. Reuse the paid response while preserving the previous view.
        context = request.get('rule_context') or {}
        regional_window = (window if window in ('previous', 'since_start') and
                           (context.get('regional') or context.get('regional_change')) else None)
        seat_rows = context.get('district_inputs', {}).get('win_changes', request['packet'].get('win_changes', ()))
        extended = window == 'previous' and len({r['seat'] for r in seat_rows}) > 3
        key = (request['key'], district_detail_level(request['packet'], context, window), regional_window, extended)
        if key not in editions:
            try:
                packet = request['packet']
                # Use the completed forecasts named by this window. The cached
                # summaries hold pairing results and statewide seat totals locally, so this
                # explanation requires no extra model request or prompt data.
                endpoints = None
                if load_summary:
                    try:
                        endpoints = load_summary(packet['reference_forecast_id']), load_summary(forecast_id)
                    except (OSError, ValueError) as error:
                        # A saved comparison can still supply the ordinary
                        # count stories if an older diagnostic is unavailable.
                        print(f'Simulation detail unavailable for {forecast_id}/{window}: {error}', flush=True)
                flags = []
                editions[key] = write(packet, endpoints, request.get('rule_context'), window, flags)
                if flags and review_flags is not None:
                    previous_flags = review_flags.setdefault(packet['reference_forecast_id'], [])
                    previous_flags.extend(flag for flag in flags if flag not in previous_flags)
            except Exception as error:
                # A trial writer failure must leave an otherwise complete
                # numerical publication and the primary commentary available.
                print(f'Rule-based commentary unavailable for {forecast_id}/{window}: {error}', flush=True)
                editions[key] = None
        if editions[key] is None:
            if rules_only:
                combined[window] = {'status': 'unavailable', 'reason': 'The rule-based report could not be prepared.'}
            continue
        summary, sections, entities = editions[key]
        # The same two forecasts can supply several windows. Write the period
        # after reusing the edition, so even a quiet notice names the right one.
        if not sections and summary == activity.QUIET_RESULTS + '.':
            period = activity.comparison_period(window, request['packet'].get('actual_seconds'))
            summary = f'{activity.QUIET_RESULTS} {period}.'
        if rules_only:
            combined[window] = {'status': 'complete', 'source': 'local', 'kind': 'rules_based',
                                'format': 'seat-bullets-v1' if sections or summary.startswith('Key changes:') else 'inline-figures-v1',
                                'entities': entities,
                                'text': {'summary': summary, 'developments': sections}}
            continue
        primary = results[window]
        text = primary.get('text') or {}
        divider = {'seats': [], 'text': MARKER+'\n'+summary}
        combined[window] = dict(primary, entities={**entities, **primary.get('entities', {})},
            text=dict(text, developments=[*text.get('developments', []), divider, *sections]))
    return combined
