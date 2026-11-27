"""Explain changes in winning prospects by checking who met in the final two
and who won when they met. This distinguishes a change in the exclusion order
from a change in a counted leader's prospects against the same opponent.
"""
from . import commentary_format as display


def pair_win_rate(summary, seat, subject, opponent):
    """Read how often the two candidates met and how often the subject won
    when they did (both percentages). Either candidate may be listed first."""
    district = summary['seats'][seat]
    pairs = district.get('pairing_percent', {})
    rates = summary.get('conditional_wins', {}).get(seat, {})
    for first, second in ((subject, opponent), (opponent, subject)):
        key = first+'/'+second
        if key in pairs and (key in rates or pairs[key] == 100):
            break
    else:
        # The simulator omits very rare pairs. A missing pair or win rate
        # gives no basis for saying how this contest would have gone.
        return None
    chance = pairs[key]
    iterations = summary.get('iterations') or 0
    if not 0 < chance <= 100 or iterations * chance / 100 < 20:
        return None
    # If these opponents met in every draw, the overall win rate gives the
    # same answer even for an archive without separate matchup diagnostics.
    inside = rates.get(key)
    if inside is None and chance == 100:
        inside = district.get('win_percent', {}).get(first)
    if inside is None or not 0 <= inside <= 100:
        return None
    if first != subject:
        inside = 100-inside
    return chance, inside


def possible_opponent_change(endpoints, seat, beneficiary, rival):
    """Explain how a growing pairing could move its median towards the
    beneficiary even though the local FP gap moved towards the rival. Each
    forecast summarises its own set of finals. We can compare frequencies
    and winners across forecasts, but cannot follow individual simulations."""
    if not endpoints or endpoints[0].get('input_revision') != endpoints[1].get('input_revision'):
        return None
    old, new = (s['seats'][seat] for s in endpoints)
    names = old.get('candidates', {})
    def same_candidate(party):
        return (names.get(party, {}).get('name') and
                names[party] == new.get('candidates', {}).get(party))

    if not all(same_candidate(p) for p in (beneficiary, rival)):
        return None
    results = [pair_win_rate(s, seat, beneficiary, rival) for s in endpoints]
    if None in results:
        return None
    (p0, w0), (p1, w1) = results
    growth = p1 - p0
    added_wins = (p1*w1 - p0*w0) / 100
    # Use this explanation only for a material expansion (two percentage
    # points of all simulations), where the candidate also wins this pairing
    # more often and earns at least two thirds of its net increase in finals.
    if growth < 2 or w1 <= w0 or added_wins < growth * 2/3:
        return None

    alternatives = []
    for key in old.get('pairing_percent', {}):
        members = key.split('/')
        if beneficiary not in members or len(set(members)) != 2 or len(members) != 2:
            continue
        other = next(p for p in members if p != beneficiary)
        if other == rival or not same_candidate(other):
            continue
        contests = [pair_win_rate(s, seat, beneficiary, other) for s in endpoints]
        if None in contests:
            continue
        (before, old_win), (after, _) = contests
        # Name a plausible displaced opponent only if their contest clearly
        # favoured this candidate before and its decline is large enough to
        # matter (at least one point and a quarter of the expanding pairing).
        if old_win >= 60 and before - after >= max(1, growth / 4):
            alternatives.append((before - after, other))
    if not alternatives:
        return None
    person, challenger, displaced = ('{candidate:'+seat+':'+p+'}' for p in
                                     (beneficiary, rival, max(alternatives)[1]))
    return (f'This pairing rose from **{p0:.1f}%** to **{p1:.1f}%** of simulations. '
            f'{challenger} may be edging out {displaced} for second place in some simulations '
            f'that already favoured {person}, which could help explain the higher TCP share.')


def matchup_outlook(summary, seat, subject, opponent):
    """Return pair frequency, wins within the pair and wins across the other
    simulations (including draws where the subject missed the final two)."""
    pair = pair_win_rate(summary, seat, subject, opponent)
    if pair is None:
        return None
    chance, inside = pair
    if (summary.get('iterations') or 0) * (100-chance) / 100 < 20:
        return None  # Too few draws outside the pair to describe that group.
    district = summary['seats'][seat]
    overall = district.get('win_percent', {}).get(subject)
    if overall is None or not 0 <= overall <= 100:
        return None
    other_wins = overall - chance * inside / 100
    # Older diagnostics round their conditional percentages to three decimal
    # places. Allow for that rounding when reconciling to the overall wins.
    if not -.001 <= other_wins <= 100-chance+.001:
        return None
    outside = min(100, max(0, 100 * other_wins / (100-chance)))
    return chance, inside, outside


def effectively_decided(before, after):
    """The same finalist won at least 98% of this matchup at both updates."""
    return min(before, after) >= 98 or max(before, after) <= 2


def matchup_frequency_point(seat, pair, before, after):
    """Describe a pairing as a share of all simulations."""
    first, second = ('{candidate:'+seat+':'+party+'}' for party in pair)
    if round(before, 1) == round(after, 1):
        return f'The chance of a matchup between {first} and {second} held at **{after:.1f}%**.'
    verb = 'rose' if after > before else 'fell'
    return f'The chance of a matchup between {first} and {second} {verb} from **{before:.1f}%** to **{after:.1f}%**.'


def matchup_win_point(seat, subject, before, after):
    """Describe wins as a share of simulations of the pairing just named."""
    point = f'{{candidate:{seat}:{subject}}} won **{after:.1f}%** of those simulations'
    if before is not None and round(before, 1) != round(after, 1):
        point += f', {"up" if after > before else "down"} from **{before:.1f}%**'
    return point + '.'


def alternative_matchup_points(seat, endpoints, pair, change, entities):
    """Name the other finals that account for a substantial opposing movement.

    The paired probabilities are shares of all simulations at each update.
    Use pairs with measured frequencies and the same named candidates in both
    forecasts. A missing entry can be an omitted rare pairing, so its change
    is unknown. Report up to two moves of at least three percentage points
    that each represent at least a quarter of the main pairing's change.
    For alternatives involving the candidate under discussion, show how
    often they won that particular final. Even a steady win rate explains
    why facing this opponent more often matters to their overall chances.
    """
    old, new = (summary['seats'][seat] for summary in endpoints)
    subject = pair[0]
    main = set(pair)
    latest = {frozenset(key.split('/')): chance for key, chance in new['pairing_percent'].items()}
    alternatives = []
    for key, before in old['pairing_percent'].items():
        members = key.split('/')
        identities = frozenset(members)
        if len(members) != 2 or len(identities) != 2 or identities == main:
            continue
        after = latest.get(identities)
        if after is None or (after-before)*change >= 0 or abs(after-before) < max(3, abs(change)/4):
            continue
        if any(not (old['candidates'].get(p) or {}).get('name')
               or old['candidates'][p] != new['candidates'].get(p)
               or (entities is not None and '{candidate:'+seat+':'+p+'}' not in entities)
               for p in members):
            continue
        alternatives.append((abs(after-before), members, before, after))
    points = {}
    for _, members, before, after in sorted(alternatives, reverse=True)[:2]:
        first, second = ('{candidate:'+seat+':'+p+'}' for p in members)
        direction = 'more' if after > before else 'less'
        point = f'{first} vs {second} became {direction} likely (**{before:.1f}%** to **{after:.1f}%**).'
        if subject in members:
            opponent = next(p for p in members if p != subject)
            prior, current = (pair_win_rate(s, seat, subject, opponent) for s in endpoints)
            if current is not None:
                point += ' ' + matchup_win_point(seat, subject, prior[1] if prior is not None else None, current[1])
        points[frozenset(members)] = point
    return points


def both_finalists_gain(row, opponent, endpoints, win_changes):
    """Explain how both usual finalists can gain overall in a near-even seat.
    Count wins in their shared matchup as a fraction of all simulations. The
    losing candidate elsewhere and a more frequent matchup are observations
    across the two forecasts. Neither establishes who received booth preferences."""
    seat, subject = row['seat'], row['party_id']
    results = [pair_win_rate(s, seat, subject, opponent) for s in endpoints]
    if None in results:
        return None
    (p0, w0), (p1, w1) = results
    districts = [s['seats'][seat] for s in endpoints]
    gains = []
    for party in (subject, opponent):
        if districts[0]['candidates'].get(party) != districts[1]['candidates'].get(party):
            return None
        levels = [d['win_percent'].get(party) for d in districts]
        if None in levels:
            return None
        gains.append(levels[1] - levels[0])
    # Reserve the explanation for visible gains on both sides, with at least
    # one additional final per hundred draws. Require the extra wins within
    # the pair to account for at least half the subject's overall gain.
    if (min(gains) < .1 or p1-p0 < 1 or (p1*w1-p0*w0)/100 < gains[0]/2):
        return None
    rivals = [r for r in win_changes if r['seat'] == seat and r['party_id'] not in (subject, opponent)
              and not r.get('identity_changed') and -r['change_pp'] >= sum(gains)/2]
    if not rivals:
        return None
    rival = min(rivals, key=lambda r: r['change_pp'])
    text = (f"Both finalists gained as the win chance for {{candidate:{seat}:{rival['party_id']}}} "
            f"fell from **{rival['before']:.1f}%** to **{rival['after']:.1f}%**. "
            f"The chance of a {{candidate:{seat}:{subject}}}–{{candidate:{seat}:{opponent}}} final "
            f"rose from **{p0:.1f}%** to **{p1:.1f}%**.")
    if round(w1, 1) > round(w0, 1):
        text += (f' {{candidate:{seat}:{subject}}} won **{w1:.1f}%** of those simulations, '
                 f'up from **{w0:.1f}%**.')
    return text


def fewer_votes_favour_counted_leader(seat, count, previous, current):
    """Decide whether a smaller projected remainder helps explain a win gain
    in a continuing close TCP. The commission supplies the lead. The remaining
    votes are a net forecast, including possible revisions to existing counts.
    This selects commentary only and cannot isolate the effect of turnout."""
    old, new = count['remaining_before'], count['remaining_after']
    old_lead, new_lead = count['lead_before'], count['lead_after']
    if (old is None or new is None or not 0 < new < old
            or not 0 < old_lead < old or not 0 < new_lead < new):
        return False
    # To draw level the trailer needs half the remaining votes plus half the
    # counted deficit. Compare that share with the earlier hurdle, then hold
    # today's lead fixed and check whether the smaller remainder itself makes
    # a material difference. Two percentage points in each comparison is an
    # editorial threshold. It avoids highlighting a handful of revised votes,
    # or crediting turnout for a change driven entirely by a larger lead.
    hurdle_increase = 50 * (new_lead/new - old_lead/old)
    effect_of_remainder = 50 * new_lead * (1/new - 1/old)
    if min(hurdle_increase, effect_of_remainder) < 2:
        return False
    leader, trailer = count['pair']
    chances = [s['seats'][seat]['win_percent'].get(leader, 0) for s in (previous, current)]
    gain = chances[1] - chances[0]
    if gain < 1:
        return False
    rates = [pair_win_rate(s, seat, leader, trailer) for s in (previous, current)]
    if None in rates:
        return False
    # A candidate can gain by facing a different opponent. Require the
    # improvement *within this pair*, weighted by how often they met across
    # both forecasts, to account for at least half the overall gain before
    # offering this explanation. Other factors can still contribute.
    (freq0, win0), (freq1, win1) = rates
    return (freq0 + freq1) / 200 * (win1 - win0) >= gain / 2


def qualifying_shift(districts, favoured, change):
    """Check that the favoured candidate's chance of making the final two
    actually moved with the matchup. A candidate who merely swaps opponents
    while remaining a finalist throughout has a different story. Even allowing
    every unreported pairing to contain this candidate, require the change in
    qualification to agree within a quarter of the change in this matchup."""
    bounds = []
    for district in districts:
        pairs = district['pairing_percent']
        known = sum(chance for key, chance in pairs.items() if favoured in key.split('/'))
        missing = max(0, 100 - sum(pairs.values()))
        bounds.append((known, known + missing))
    low = bounds[1][0] - bounds[0][1]
    high = bounds[1][1] - bounds[0][0]
    return max(abs(low-change), abs(high-change)) <= abs(change) / 4


def rival_for_second(districts, finalist, challenger, frequencies):
    """Identify the other candidate competing to face the same finalist.
    Require the same two alternatives to account for virtually all finals at
    both dates. The allowance for other outcomes is at most one point of all
    draws, and at most a quarter of the change we are trying to explain."""
    allowance = min(1, abs(frequencies[1]-frequencies[0])/4)
    rivals = []
    for district, frequency in zip(districts, frequencies):
        alternatives = {}
        for key, chance in district['pairing_percent'].items():
            pair = key.split('/')
            if len(set(pair)) == len(pair) == 2 and finalist in pair and challenger not in pair:
                alternatives[pair[1] if pair[0] == finalist else pair[0]] = chance
        rival = max(alternatives, key=alternatives.get, default=None)
        if rival is None or 100-frequency-alternatives[rival] > min(allowance, (100-frequency)/4):
            return None
        rivals.append(rival)
    identity = districts[0]['candidates'].get(rivals[0]) or {}
    if rivals[0] == rivals[1] and identity.get('name') and identity == districts[1]['candidates'].get(rivals[1]):
        return rivals[0]
    return None


def contest_for_second(row, opponent, endpoints):
    """Explain an apparently contrary FP return when the candidate's main
    route to winning was to qualify ahead of a third candidate. The booth
    comparison is against the likely opponent. The simulation summaries
    show who competed to face that opponent across the district."""
    seat, subject, delta = row['seat'], row['party_id'], row['change_pp']
    if not endpoints or endpoints[0].get('input_revision') != endpoints[1].get('input_revision'):
        return None
    districts = [s['seats'][seat] for s in endpoints]
    def same_candidate(party):
        earlier = districts[0]['candidates'].get(party) or {}
        return earlier.get('name') and earlier == districts[1]['candidates'].get(party)

    if not all(same_candidate(p) for p in (subject, opponent)):
        return None
    outcomes = [matchup_outlook(s, seat, subject, opponent) for s in endpoints]
    if None in outcomes:
        return None
    (p0, w0, outside0), (p1, w1, outside1) = outcomes
    change = p1-p0
    # Hold the win rates inside and outside this matchup at their averages.
    # Only use the story if making the final changed by at least one point
    # and that change accounts for at least half the overall win movement.
    contribution = change * ((w0-outside0) + (w1-outside1)) / 200
    if (abs(change) < 1 or delta*change <= 0 or contribution*delta <= 0
            or abs(contribution) < abs(delta)/2 or not qualifying_shift(districts, subject, change)):
        return None

    third = rival_for_second(districts, opponent, subject, (p0, p1))
    if third is None:
        return None

    person, finalist, competitor = ('{candidate:'+seat+':'+p+'}' for p in (subject, opponent, third))
    verb = 'rose' if change > 0 else 'fell'
    more_likely = person if change > 0 else competitor
    less_likely = competitor if change > 0 else person
    text = ''
    means = [d.get('fp_mean_percent', {}) for d in districts]
    if all(m.get(p) is not None for m in means for p in (subject, third)):
        gap_shift = (means[1][subject]-means[1][third]) - (means[0][subject]-means[0][third])
        if gap_shift*change > 0 and abs(gap_shift) >= .1:
            text = (f'Across the seat, the projected FP gap shifted **{abs(gap_shift):.1f} pp** '
                    f'towards {more_likely} relative to {less_likely}. ')
    text += (f'The chance of {person} getting past {competitor} to face {finalist} {verb} '
             f'from **{p0:.1f}%** to **{p1:.1f}%**.')
    if round(w0, 1) != round(w1, 1):
        return text + (f' Against {finalist}, the win chance for {person} {"rose" if w1 > w0 else "fell"} '
                       f'from **{w0:.1f}%** to **{w1:.1f}%**.')
    return text + f' {person} won **{w1:.1f}%** of those matchups at both updates.'


def reaching_final_more_often(row, opponent, endpoints, minimum_change=3):
    """Separate qualifying for the final count from winning it. A candidate
    can gain overall even after a weak local primary if they are excluded
    less often. The published pairing table can omit very rare finals, so
    use whole percentages and require nearly complete coverage in both tables."""
    seat, party = row['seat'], row['party_id']
    districts = [s['seats'][seat] for s in endpoints]
    if (not districts[0].get('candidates', {}).get(party) or
            districts[0]['candidates'][party] != districts[1].get('candidates', {}).get(party)):
        return None
    chances = []
    for district in districts:
        pairs = district.get('pairing_percent', {})
        if not 99.8 <= sum(pairs.values()) <= 100.01:
            return None
        chances.append(sum(v for key, v in pairs.items() if party in key.split('/')))
    before, after = chances
    if min(chances) <= 0 or (after-before)*row['change_pp'] <= 0 or abs(after-before) < minimum_change:
        return None
    wins = [d.get('win_percent', {}).get(party, 0) for d in districts]
    # Hold the average chance of winning once qualified fixed. Require the
    # changing frequency of qualification to explain at least half the gain
    # or loss in overall win probability before singling it out.
    effect = (after-before)*(wins[0]/before+wins[1]/after)/2
    if abs(effect) < abs(row['change_pp'])/2:
        return None
    person = '{candidate:'+seat+':'+party+'}'
    point = (f'{person} reached the final two in about **{after:.0f}%** of simulations, '
             f'{"up" if after > before else "down"} from **{before:.0f}%**.')
    # The typical district primary can cross a rival even when the latest
    # booth goes the other way. Marginal medians show the shift in outlook;
    # they cannot establish the exclusion order within any particular draw.
    if districts[0]['candidates'].get(opponent) != districts[1]['candidates'].get(opponent):
        return point
    levels = [[d.get('fp_percentiles', {}).get(p, {}).get('50') for p in (party, opponent)] for d in districts]
    if any(v is None for level in levels for v in level):
        return point
    (p0, r0), (p1, r1) = levels
    if (p1-r1)*(after-before) > 0 and (p0-r0)*(p1-r1) < 0 and abs(p1-r1) >= .2:
        position = 'ahead of' if p1 > r1 else 'behind'
        other = '{candidate:'+seat+':'+opponent+'}'
        point += (f' On projected median FP across the seat, {person} moved {position} {other}, '
                  f'**{p1:.1f}%** to **{r1:.1f}%**.')
    return point


def changed_opponents_after_weak_primary(row, endpoints):
    """A leader can lose primaries but gain win chance when a harder opponent
    also loses ground. Check that both were credible finals, that the easier
    one became more likely, and that the swap is large enough to matter."""
    if not endpoints or row['change_pp'] <= 0 or endpoints[0]['input_revision'] != endpoints[1]['input_revision']:
        return None
    seat, party = row['seat'], row['party_id']
    districts = [s['seats'][seat] for s in endpoints]
    identity = districts[0].get('candidates', {}).get(party)
    if not identity or identity != districts[1].get('candidates', {}).get(party):
        return None
    contests = {}
    for p, identity in districts[0].get('candidates', {}).items():
        if p == party or identity != districts[1].get('candidates', {}).get(p):
            continue
        rates = [pair_win_rate(s, seat, party, p) for s in endpoints]
        if None not in rates:
            contests[p] = rates
    swaps = []
    for hard, ((old_h,win0),(new_h,win1)) in contests.items():
        for easy, ((old_e,better0),(new_e,better1)) in contests.items():
            exchanged = min(old_h-new_h, new_e-old_e)
            # At most the smaller frequency change can represent a switch.
            # Require both comparisons to establish the same advantage and
            # at least half the overall win gain at those observed win rates.
            advantage = min(better0-win0,better1-win1)
            if exchanged >= 5 and advantage >= 3 and exchanged*advantage/100 >= row['change_pp']/2:
                swaps.append((exchanged*advantage, hard, easy, (old_h, new_h, win1), (old_e, new_e, better1)))
    if not swaps:
        return None
    _, hard, easy, harder_outlook, easier_outlook = max(swaps)
    subject = '{candidate:'+seat+':'+party+'}'
    points = []
    for opponent, (before, after, wins) in ((hard, harder_outlook), (easy, easier_outlook)):
        point = matchup_frequency_point(seat, (party, opponent), before, after)
        points.append(f'{point} {subject} won **{wins:.1f}%** of those simulations.')
    return hard, '\n- '.join(points)


def mixed_matchup_explanation(row, endpoints, entities):
    """Look for changes in who reaches the final, or who wins within it.

    Explain a combination of both, or an improvement or decline against
    several opponents. Compare the same people in both forecasts. The
    simulated outcomes do not assign a causal effect to any particular booth.
    """
    seat, subject, delta = row['seat'], row['party_id'], row['change_pp']
    old, new = [s['seats'][seat] for s in endpoints]
    frequencies, strengths = [], []
    # Express each change on the scale of the overall win chance. For
    # example, winning a matchup 10 points more often matters by about
    # 3 points overall if those opponents meet in 30% of draws. A change
    # in frequency matters only to the extent that this matchup differs
    # from the candidate's prospects in the other draws.
    minimum = max(1, abs(delta) * .2)
    direction = 1 if delta > 0 else -1
    for pair in old.get('pairing_percent', {}):
        parties = pair.split('/')
        if len(parties) != 2 or subject not in parties or parties[0] == parties[1]:
            continue
        if any(not (old['candidates'].get(p) or {}).get('name') or
               old['candidates'][p] != new['candidates'].get(p) for p in parties):
            continue
        opponent = next(p for p in parties if p != subject)
        if entities is not None and '{candidate:'+seat+':'+opponent+'}' not in entities:
            continue
        samples = [matchup_outlook(s, seat, subject, opponent) for s in endpoints]
        if None in samples:
            continue
        (p0, w0, other0), (p1, w1, other1) = samples
        frequency = (p1-p0) * ((w0-other0)+(w1-other1)) / 200
        strength = (w1-w0) * (p0+p1) / 200
        # Both details must matter to the overall movement. Only call the
        # matchup favourable or difficult when it was on that side of even
        # in both forecasts and its changing frequency supports the story.
        favoured = min(w0, w1) >= 60
        difficult = max(w0, w1) <= 40
        if (abs(p1-p0) >= 3 and frequency*direction >= minimum and
                (favoured and (p1-p0)*direction > 0 or difficult and (p1-p0)*direction < 0)):
            frequencies.append((abs(frequency), opponent, p0, p1, favoured))
        if abs(w1-w0) >= 3 and strength*direction >= minimum:
            strengths.append((abs(strength), opponent, p0, p1, w0, w1))
    if not strengths:
        return None
    strengths.sort(reverse=True)
    if not frequencies:
        # A candidate can improve against two likely opponents even when
        # the shift in which opponent they face does not help them. Show both
        # contests if their win-rate changes together account for at least
        # half the overall movement, measured at their average frequencies.
        strongest = strengths[:2]
        if len(strongest) < 2 or sum(result[0] for result in strongest) < abs(delta)/2:
            return None
        return '\n- '.join(
            matchup_frequency_point(seat, (subject, opponent), p0, p1) + ' ' +
            matchup_win_point(seat, subject, w0, w1)
            for _, opponent, p0, p1, w0, w1 in strongest)
    # With two possible opponents the frequency comparisons can describe
    # precisely the same shift. Lead with the favourable matchup in a tie
    # at reporting precision, rather than letting party IDs decide the story.
    _, opponent, p0, p1, favoured = max(frequencies, key=lambda item: (round(item[0], 1), item[4], item[0]))
    _, other, _, _, w0, w1 = strengths[0]
    person = '{candidate:'+seat+':'+subject+'}'
    pair = subject, opponent
    point = matchup_frequency_point(seat, pair, p0, p1)
    prospect = 'remained favoured' if favoured else 'remained the underdog'
    alternatives = alternative_matchup_points(seat, endpoints, pair, p1-p0, entities)
    direction = 'up' if w1 > w0 else 'down'
    if other == opponent:
        point += (f' {person} {prospect} in that matchup, winning **{w1:.1f}%** of those simulations, '
                  f'{direction} from **{w0:.1f}%**.')
    else:
        point += f' {person} {prospect} in that matchup.'
        other_candidate = '{candidate:'+seat+':'+other+'}'
        # A change in win rate may warrant its own bullet even when that
        # pairing's frequency barely moved. Otherwise its bullet has the rate.
        other_pair = frozenset((subject, other))
        if other_pair not in alternatives and (entities is None or other_candidate in entities):
            verb = 'rose' if w1 > w0 else 'fell'
            alternatives[other_pair] = (f'The chance of {person} winning against {other_candidate} '
                                        f'{verb} from **{w0:.1f}%** to **{w1:.1f}%**.')
    return '\n- '.join((point, *alternatives.values()))


def explain(row, endpoints, *, entities=None):
    """Choose a matchup explanation and, when identifiable, the two candidates
    competing to face the subject, ordered by who gained chances of qualifying.
    The writer compares those challengers' primary votes in a separate bullet.
    The two-point minimum and the dominance tests below are editorial choices.
    The summaries come from the two saved forecasts for this exact window."""
    if not endpoints or abs(row['change_pp']) < 2:
        return None, None
    previous, current = endpoints
    if previous['input_revision'] != current['input_revision']:
        return None, None
    seat, subject = row['seat'], row['party_id']
    if any(seat not in summary['seats'] for summary in endpoints):
        return None, None
    districts = [summary['seats'][seat] for summary in endpoints]
    identities = [district['candidates'] for district in districts]
    best = None
    loss_matchup = None
    for pair in districts[0].get('pairing_percent', {}):
        members = pair.split('/')
        if len(members) != 2 or members[0] == members[1] or subject not in members:
            continue
        opponent = next(p for p in members if p != subject)
        if any(not (identities[0].get(p) or {}).get('name')
               or identities[0][p] != identities[1].get(p) for p in members):
            continue
        samples = [matchup_outlook(summary, seat, subject, opponent) for summary in endpoints]
        if None in samples:
            continue
        (p0, win0, other0), (p1, win1, other1) = samples
        change = p1 - p0
        # Overall win chance is a weighted average of wins inside and outside
        # this matchup. Change the frequency while holding those two win rates
        # at their endpoint averages. The other two terms change the win rates
        # while holding frequency at its average. Together these reproduce the
        # overall movement, whichever forecast is taken first.
        frequency_effect = change * ((win0+win1) - (other0+other1)) / 200
        mean_frequency = (p0+p1) / 200
        inside_effect = mean_frequency * (win1-win0)
        outside_effect = (1-mean_frequency) * (other1-other0)
        if (frequency_effect * row['change_pp'] <= 0 or
                abs(frequency_effect) < 2 * (abs(inside_effect) + abs(outside_effect))):
            continue  # At least two thirds of the absolute movement must come from frequency.
        # A favourite can win almost every draw outside one competitive final.
        # Describe that opponent even when neither candidate has a 60% chance
        # against the other. Check their defeats as a share of ALL simulations
        # so a rare pairing cannot take credit for losses incurred elsewhere.
        losses_change = p1*(100-win1)/100 - p0*(100-win0)/100
        delta = row['change_pp']
        if (min(other0, other1) >= 98 and not effectively_decided(win0, win1)
                and losses_change*delta < 0 and abs(losses_change) >= abs(delta)/2):
            if loss_matchup is None or abs(frequency_effect) > loss_matchup[0]:
                loss_matchup = abs(frequency_effect), opponent, p0, p1, win0, win1
        if min(win0, win1) >= 60:
            favoured = subject
        elif max(win0, win1) <= 40:
            favoured = opponent
        else:
            continue
        if change * delta * (1 if subject == favoured else -1) <= 0:
            continue
        # Usually a contender gains or loses a place in the final. A candidate
        # who already qualifies can also gain just by facing an easier opponent
        # more often. The same frequency test establishes that distinction.
        rank = (qualifying_shift(districts, favoured, change), abs(frequency_effect))
        if best is None or rank > best[0]:
            best = rank, favoured, opponent, p0, p1, win0, win1
    # Keep an informative explanation of who qualified for a competitive
    # final. If the alternative is a routine victory, lead with the contest
    # where this candidate actually stood to lose the seat.
    if loss_matchup and (best is None or effectively_decided(*best[-2:]) or not best[0][0]):
        _, opponent, p0, p1, win0, win1 = loss_matchup
        point = (matchup_frequency_point(seat, (subject, opponent), p0, p1) + ' ' +
                 matchup_win_point(seat, subject, win0, win1))
    elif best is None:
        return mixed_matchup_explanation(row, endpoints, entities), None
    else:
        rank, favoured, opponent, p0, p1, _, win1 = best
        other = opponent if favoured == subject else subject
        verb = 'rose' if p1 > p0 else 'fell'
        outcome = 'gain' if row['change_pp'] > 0 else 'drop'
        if not rank[0]:
            point = (matchup_frequency_point(seat, (subject, opponent), p0, p1) +
                     f' {{candidate:{seat}:{subject}}} won **{win1:.1f}%** '
                     f'of those simulations. The change in opponent accounted for most of the {outcome} in win chance.')
        else:
            point = (f'The latest forecast still favours {{candidate:{seat}:{favoured}}} to beat '
                     f'{{candidate:{seat}:{other}}} if they reach the final two. '
                     f'The chance of that pairing {verb} from **{display.decimal(p0)}%** '
                     f'to **{display.decimal(p1)}%**, accounting for most of the {outcome} in win chance for '
                     f'{{candidate:{seat}:{subject}}}.')

    alternatives = alternative_matchup_points(seat, endpoints, (subject, opponent), p1-p0, entities)
    point = '\n- '.join((point, *alternatives.values()))
    displaced = rival_for_second(districts, subject, opponent, (p0, p1))
    if displaced is None or not qualifying_shift(districts, opponent, p1-p0):
        return point, None
    challengers = (opponent, displaced) if p1 > p0 else (displaced, opponent)
    return point, challengers
