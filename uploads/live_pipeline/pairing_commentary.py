"""Explain a change in winning prospects when a strong opponent's route to
the final two opens or closes. Each completed forecast records how often a
pair met and who won those simulated contests. The local writer compares two
such forecasts for an already selected seat. The comparison describes the
forecast arithmetic and does not attribute the movement to a particular booth.
"""
from . import commentary_format as display


def matchup_outlook(summary, seat, subject, opponent):
    """Return the frequency of this pair, the subject's win rate when it
    occurs and their win rate in all other simulations, each as a percentage.
    The last group includes elections where the subject missed the final two."""
    district = summary['seats'][seat]
    pairs = district.get('pairing_percent', {})
    rates = summary.get('conditional_wins', {}).get(seat, {})
    for first, second in ((subject, opponent), (opponent, subject)):
        key = first+'/'+second
        if key in pairs and key in rates:
            break
    else:
        # The simulator omits very rare pairs. A missing pair or win rate
        # gives no basis for saying how this contest would have gone.
        return None
    chance, inside = pairs[key], rates[key]
    iterations = summary.get('iterations') or 0
    if not 0 < chance < 100 or inside is None or not 0 <= inside <= 100:
        return None
    if iterations * min(chance, 100-chance) / 100 < 20:
        return None  # Either conditional rate would describe fewer than 20 draws.
    if first != subject:
        inside = 100-inside
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
        return None
    _, favoured, opponent, p0, p1 = best
    other = opponent if favoured == subject else subject
    verb = 'rose' if p1 > p0 else 'fell'
    outcome = 'gain' if row['change_pp'] > 0 else 'drop'
    return (f'Both forecasts favoured {{candidate:{seat}:{favoured}}} in a final count against {{candidate:{seat}:{other}}}. '
            f'The chance of that pairing {verb} from **{display.decimal(p0)}%** '
            f'to **{display.decimal(p1)}%**, accounting for most of the {outcome} in win chance for '
            f'{{candidate:{seat}:{subject}}}.')
