"""Trace the ordinary-booth FP prototype through a saved Monbulk seat forecast.

Run from the repository root. Reported win probabilities come from the replay.
Alternative probabilities are sensitivities using the replay's distribution for
the Labor–Liberal contest and its published frequency among simulated pairings.
"""
import argparse
from pathlib import Path

import numpy as np

from booth_fp_update import adjusted_shares, fit_shift, group_votes, read, read_district
from score_booth_fp_update import shared_background


PARTIES = ['0', '1', '2']  # Labor, Liberal and Greens. Keep everyone else together.


def share_background(integration):
    return {'shared': {str(row['party_index']): row['value'] / 25
                       for row in integration.get('shared_deviation', [])}}


def fp_scenarios(data, integration, opening_integration):
    shapes, counts, remaining, ordinary, prior, _ = data
    prior, shapes = shared_background(prior, shapes, [
        share_background(opening_integration), share_background(integration)], PARTIES)
    reported = ordinary & (counts.sum(axis=1) > 0)
    weights = {str(row['party_index']): row['value']
               for row in integration.get('individual_count_weights', [])}
    # One common fraction of the fitted move keeps the four groups adding to
    # 100% at every booth. Use the share-weighted average of the three archived
    # party precision weights. The existing simulator applies them separately.
    precision = np.average([weights.get(p, 0) for p in PARTIES], weights=prior[:-1])
    cases = [('250 direct', 250, 1), ('250 ordinary only', 250, ordinary[:, None]),
             ('250 ord / 1000 other', 250, ordinary[:, None]),
             ('250 then precision', 250, precision),
             ('500 direct', 500, 1), ('1000 direct', 1000, 1)]
    for label, starting_votes, fraction in cases:
        shift = fit_shift(shapes[reported], counts[reported], prior, starting_votes, 500)
        fitted = adjusted_shares(shapes, shift)
        background = shapes
        if label == '250 ord / 1000 other':
            # An intermediate transfer: a postal or early centre can share a
            # local movement, while its different voters warrant more caution.
            other_shift = fit_shift(shapes[reported], counts[reported], prior, 1000, 500)
            background = adjusted_shares(shapes, other_shift)
        future = background + fraction * (fitted - background)
        votes = counts.sum(axis=0) + remaining @ future
        applied = np.average(np.broadcast_to(fraction, (len(remaining), 1))[:, 0], weights=remaining)
        yield label, 100 * votes / votes.sum(), applied


def seat_outcome(summary, seat, alternative, greens_rate=None):
    """Shift the archived conditional TCP distribution by the FP contribution.

    Keep preferences and uncertainty within this snapshot fixed. The statewide
    Greens flow and the implied flow for everyone else reproduce the published
    seat median at the published FP mix. The latter is an effective rate, since
    the FP figures are unconditional means and the TCP is a conditional median.
    We keep the chance of meeting each opponent and all other matchups fixed.
    """
    actual = summary['seats'][seat]
    fp = group_votes(actual['fp_mean_percent'], PARTIES, {})
    bands = actual['tcp_percentiles']['0/1']
    median = bands['50']
    if greens_rate is None:
        greens_rate = summary['statewide']['preference_flow_medians']['2/0/1'] / 100
    remainder_rate = (median - fp[0] - greens_rate * fp[2]) / fp[-1]
    if not 0 <= remainder_rate <= 1:
        raise ValueError('The implied preference split is infeasible.')
    movement = (alternative - fp) @ np.array([1., 0., greens_rate, remainder_rate])
    # Insert the known proportion below 50% into the percentile curve so the
    # original shares give exactly the original win chance. A translated curve
    # then gives the fraction of Labor–Liberal draws crossing the winning line.
    below = 100 - summary['conditional_wins'][seat]['0/1']
    curve = sorted([(v, float(k)) for k, v in bands.items()] + [(50., below)])
    shares, percentiles = np.array(curve).T
    if np.any(np.diff(percentiles) < 0) or not shares[0] <= 50 - movement <= shares[-1]:
        raise ValueError('Cannot interpolate the archived TCP distribution.')
    new_below = np.interp(50 - movement, shares, percentiles)
    pairing = actual['pairing_percent']['0/1'] / 100
    win = actual['win_percent']['0'] + pairing * (below - new_below)
    return median + movement, win


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packages', type=Path, default=Path('live_runs/2022vic-7-initial/packages'))
    args = parser.parse_args()
    seat = 'Monbulk'
    opening = args.packages / 'f00000001'

    def integration(package):
        return read(package / 'evidence.json.gz')['seats'][seat].get('fp_integration') or {}

    print('Time   Method                 ALP FP  LIB FP  GRN FP    Gap  ALP TCP  ALP win')
    for sequence in (4, 5, 6):
        package = args.packages / f'f{sequence:08}'
        data = read_district(opening, package, seat, PARTIES)
        summary = data[-1]
        district = summary['seats'][seat]
        fp = group_votes(district['fp_mean_percent'], PARTIES, {})
        rows = [('Published', fp, 1), *fp_scenarios(data, integration(package), integration(opening))]
        for label, values, fraction in rows:
            tcp, win = seat_outcome(summary, seat, values)
            if label == '250 then precision':
                label = f'250 x {fraction:.1%}'
            elif label == '250 ordinary only':
                label = f'250 ordinary ({fraction:.0%})'
            print(f"{summary['source_time'][11:16]}  {label:21} {values[0]:6.2f} {values[1]:7.2f}"
                  f" {values[2]:7.2f} {values[0]-values[1]:6.2f} {tcp:8.2f} {win:8.2f}")


if __name__ == '__main__':
    main()
