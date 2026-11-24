"""Prototype a district FP update from an opening booth forecast and one capture.

Each ordinary booth supplies one observation of the district movement after
allowing for its own expected party mix. The question is whether the new votes
differ from what all the evidence so far would predict at that particular booth.
The published seat estimates alongside the prototype are for comparison only.
Run with --help for the package paths and trial assumptions.
"""
import argparse
from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'uploads'))
from live_pipeline import booth_fp
from live_pipeline.storage import read


def fit_shift(expected, votes, prior, prior_votes, booth_cap):
    """Return relative multipliers for the opening party shares.

    We ask for one common set of multipliers that accounts for the counted
    booths together. Each booth retains its own starting shares. For example,
    the same shift in relative support can leave Labor leading at one booth
    and trailing at another. Candidate shares at each location must sum to one.

    The opening district mix acts like one observation exactly on expectation.
    Its vote equivalent controls how readily the forecast moves. Each real
    booth contributes n/(1+n/cap) effective ballots: twice as many people at
    the same place supply less evidence about other places than two separate
    booths of the original size. These are trial assumptions, not fitted
    reliability estimates. All actual ballots remain counted at full value.
    """
    if not all(np.isfinite(x) and x > 0 for x in (prior_votes, booth_cap)):
        raise ValueError('The prior size and booth cap must be positive and finite.')
    sizes = votes.sum(axis=1)
    if np.any((expected == 0) & (votes > 0)):
        raise ValueError('A counted party has no opening support at its booth.')
    shrink = 1 / (1 + sizes / booth_cap)
    target = prior_votes * prior + (votes * shrink[:, None]).sum(axis=0)
    weights = np.r_[prior_votes, sizes * shrink]
    shapes = np.vstack((prior, expected))
    shift = np.ones(len(prior))
    # Match the predicted and observed effective party totals. This is the
    # multinomial fit with a different starting mix at each booth. Adding a
    # booth exactly at its fitted shares leaves these equations satisfied at
    # the old solution. All booths enter together, regardless of return order.
    for _ in range(200):
        predicted = weights @ adjusted_shares(shapes, shift)
        if np.max(np.abs(predicted - target)) < 1e-9 * weights.sum():
            return shift
        shift *= np.divide(target, predicted, out=np.ones_like(target), where=predicted > 0)
    raise ValueError('Booth fit did not converge.')


def adjusted_shares(expected, shift):
    scaled = expected * shift
    return scaled / scaled.sum(axis=1, keepdims=True)


def group_votes(counts, parties, aliases):
    """Keep the named party IDs and combine every remaining ballot in Others."""
    values = np.zeros(len(parties) + 1)
    for party, count in counts.items():
        party = aliases.get(party, party)
        index = parties.index(party) if party in parties else -1
        values[index] += count
    return values


def read_district(opening, capture, seat, parties):
    """Use a fixed, locally uncounted opening geography and the target capture.
    The opening district FP shares set the background. Regional and statewide
    movements are deliberately held fixed to isolate the local update. Current
    turnout and reported FP come from the target capture alone.
    """
    reference, current = (read(package / 'summary.json') for package in (opening, capture))
    if (opening.resolve().parent != capture.resolve().parent
            or reference['input_revision'] != current['input_revision']
            or (reference['source_time'] or '') > (current['source_time'] or '')):
        raise ValueError('Choose an opening from the same run and input revision, at or before the capture.')
    districts = []
    for package in (opening, capture):
        booths = read(package / 'evidence.json.gz')['booths']
        districts.append({k:b for k,b in booths.items() if b['seat_name'] == seat})
    initial, latest = districts
    if not initial or any(sum(b['fp_votes_current'].values()) for b in initial.values()):
        raise ValueError('The opening must contain this district before local FP counting.')
    if initial.keys() != latest.keys():
        raise ValueError('Booth identities changed between the opening and the capture.')
    means = reference['seats'][seat]['fp_mean_percent']
    fitted = booth_fp.district_expectations(initial, means, {})
    shapes, counts, remaining, ordinary = [], [], [], []
    for key in sorted(latest):
        booth = latest[key]
        votes = group_votes(booth['fp_votes_current'], parties, booth.get('count_to_projection_ids', {}))
        future = booth['expected_remaining_fp']
        if not (votes.sum() or future):
            continue
        mix = group_votes(fitted.get(key, {}), parties, {})
        if not mix.sum() or future < 0:
            raise ValueError(f'Missing opening composition or invalid remaining turnout at {key}.')
        shapes.append(mix / mix.sum())
        counts.append(votes)
        remaining.append(future)
        ordinary.append(booth['booth_type'] == 'Normal' and booth['vote_type'] == 'Ordinary')
    prior = group_votes(means, parties, {})
    return (np.array(shapes), np.array(counts), np.array(remaining), np.array(ordinary),
            prior/prior.sum(), current)


def district_projection(shapes, counts, remaining, ordinary, prior, prior_votes, booth_cap):
    """Count observed ballots once. Estimate the rest using the district shift
    learnt from ordinary booths. Applying it to uncounted declaration votes
    assumes the same district movement there; this prototype does not learn
    separate movements by vote type or forecast TCP or win probabilities.
    """
    use = ordinary & (counts.sum(axis=1) > 0)
    shift = fit_shift(shapes[use], counts[use], prior, prior_votes, booth_cap)
    totals = counts.sum(axis=0) + remaining @ adjusted_shares(shapes, shift)
    return 100 * totals / totals.sum()


def district_gap_sd(shapes, counts, remaining, ordinary, prior, prior_votes, booth_cap):
    """Approximate uncertainty in the first two parties' FP gap due solely to
    estimating the shared movement. Each booth adds information in proportion
    to its effective ballots. The matrix for a party mix describes how its
    predicted shares respond when any one party gains relative support.
    Turnout, booth geography and the assumed strength of the prior stay fixed.
    This is a diagnostic of the update, not a forecast interval for the seat.
    """
    use = ordinary & (counts.sum(axis=1) > 0)
    shift = fit_shift(shapes[use], counts[use], prior, prior_votes, booth_cap)
    sizes = counts[use].sum(axis=1)
    weights = np.r_[prior_votes, sizes / (1 + sizes / booth_cap)]
    def sensitivity(mixes, weight):
        return np.diag(weight @ mixes) - mixes.T @ (weight[:, None] * mixes)
    information = sensitivity(adjusted_shares(np.vstack((prior, shapes[use])), shift), weights)
    district = sensitivity(adjusted_shares(shapes, shift), remaining) / (counts.sum() + remaining.sum())
    gap = district[0] - district[1]
    # Multiplying every party's odds by the same number leaves every mix
    # unchanged. Invert only the directions that distinguish parties.
    return 100 * np.sqrt(max(0., gap @ np.linalg.pinv(information, hermitian=True) @ gap))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('opening', type=Path, help='Package containing the fixed, uncounted district forecast')
    parser.add_argument('capture', type=Path, help='Single target package in the same run')
    parser.add_argument('district')
    parser.add_argument('--parties', nargs='+', default=['0','1','2'], help='Model party IDs to retain separately')
    # This is the trial setting selected on the Victorian and SA replays.
    # score_booth_fp_update.py also retains the original 3,000 for comparison.
    parser.add_argument('--prior-votes', type=float, default=250, help='Opening forecast vote equivalent (default 250)')
    parser.add_argument('--booth-cap', type=float, default=500, help='Maximum effective ballots per booth (default 500)')
    args = parser.parse_args()
    if len(set(args.parties)) != len(args.parties) or len(args.parties) < 2:
        parser.error('Choose at least two distinct model parties.')
    shapes, counts, remaining, ordinary, prior, current = read_district(
        args.opening, args.capture, args.district, args.parties)
    estimate = district_projection(shapes, counts, remaining, ordinary, prior, args.prior_votes, args.booth_cap)
    published = group_votes(current['seats'][args.district]['fp_mean_percent'], args.parties, {})
    print(args.district, current['source_time'])
    print('District FP (%)', *args.parties, 'remainder', sep='\t')
    for name, values in [('Opening',100*prior), ('Prototype',estimate), ('Published',published)]:
        print(name, *(f'{x:.2f}' for x in values), sep='\t')
    spread = district_gap_sd(shapes, counts, remaining, ordinary, prior, args.prior_votes, args.booth_cap)
    print(f'Conditional SD for the {args.parties[0]} vs {args.parties[1]} FP gap: {spread:.3f} pp')


if __name__ == '__main__':
    main()
