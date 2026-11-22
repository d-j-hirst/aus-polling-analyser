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


def mixed_matchup_explanation(row, endpoints):
    """Look for two simultaneous changes: which opponent the candidate faces,
    and how often they win a specified contest. Compare the same people in
    both forecasts. The observations describe simulated outcomes, so they
    do not assign a causal effect to any particular booth."""
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
            strengths.append((abs(strength), opponent, w0, w1))
    if not frequencies or not strengths:
        return None
    # With two possible opponents the frequency comparisons can describe
    # precisely the same shift. Lead with the favourable matchup in a tie
    # at reporting precision, rather than letting party IDs decide the story.
    _, opponent, p0, p1, favoured = max(frequencies, key=lambda item: (round(item[0], 1), item[4], item[0]))
    _, other, w0, w1 = max(strengths)
    person = '{candidate:'+seat+':'+subject+'}'
    rival = '{candidate:'+seat+':'+opponent+'}'
    second = '{candidate:'+seat+':'+other+'}'
    prospect = 'was favoured' if favoured else 'was the underdog'
    return (f'The chance of {person} facing {rival} {"rose" if p1 > p0 else "fell"} '
            f'from **{p0:.1f}%** to **{p1:.1f}%**. {person} {prospect} in that contest. '
            f'When facing {second}, the win chance for {person} also {"rose" if w1 > w0 else "fell"} '
            f'from **{w0:.1f}%** to **{w1:.1f}%**.')


def explain(row, endpoints):
    """Choose at most one explanation for an already newsworthy win change.
    The two-point minimum and the dominance tests below are editorial choices.
    The summaries come from the two saved forecasts for this exact window."""
    if not endpoints or abs(row['change_pp']) < 2:
        return None
    previous, current = endpoints
    if previous['input_revision'] != current['input_revision']:
        return None
    seat, subject = row['seat'], row['party_id']
    if any(seat not in summary['seats'] for summary in endpoints):
        return None
    districts = [summary['seats'][seat] for summary in endpoints]
    identities = [district['candidates'] for district in districts]
    best = None
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
        if min(win0, win1) >= 60:
            favoured = subject
        elif max(win0, win1) <= 40:
            favoured = opponent
        else:
            continue  # Both forecasts must clearly favour the same candidate in the pair.
        change = p1 - p0
        if change * row['change_pp'] * (1 if subject == favoured else -1) <= 0:
            continue
        if not qualifying_shift(districts, favoured, change):
            continue

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
        if best is None or abs(frequency_effect) > best[0]:
            best = abs(frequency_effect), favoured, opponent, p0, p1
    if best is None:
        return mixed_matchup_explanation(row, endpoints)
    _, favoured, opponent, p0, p1 = best
    other = opponent if favoured == subject else subject
    verb = 'rose' if p1 > p0 else 'fell'
    outcome = 'gain' if row['change_pp'] > 0 else 'drop'
    return (f'Both forecasts favoured {{candidate:{seat}:{favoured}}} in a final count against {{candidate:{seat}:{other}}}. '
            f'The chance of that pairing {verb} from **{display.decimal(p0)}%** '
            f'to **{display.decimal(p1)}%**, accounting for most of the {outcome} in win chance for '
            f'{{candidate:{seat}:{subject}}}.')
