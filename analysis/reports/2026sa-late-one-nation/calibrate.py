"""Choose the SA declaration prior using results available before polling day.

Queensland 2020 and 2024 separately reported both kinds of absent voting and
postals. Test how well each state's preceding federal result would have
predicted its state vote by category. SA supplies only its 2025 federal
returns. The March 2026 SA count is used separately to evaluate the choice.
"""
import argparse
import json
from pathlib import Path
from tempfile import TemporaryDirectory

import numpy as np
from scipy.special import expit, logit

from history import MODES, REFERENCE, ROOT, contrast, federal, queensland

PRIOR = ROOT / 'forecasts/2026sa/one-nation-declaration-prior.json'
STRENGTHS = (0., .5, 1.)


def transfer_errors(state_year, federal_year):
    """Return predicted minus observed percentage points within each vote type.

    All three alternatives receive each Queensland district's actual in-person
    result and category sizes. Only the amount of the earlier federal difference
    transferred changes. Sum predicted party votes before taking the statewide
    share so that each voter in the category has the same weight.
    """
    state = queensland(state_year)
    previous = federal(federal_year)['QLD']
    formal = state.pivot(index='seat', columns='mode', values='formal').fillna(0)
    on = state.pivot(index='seat', columns='mode', values='on').fillna(0)
    base = logit((on[REFERENCE] + .5) / (formal[REFERENCE] + 1))
    errors = {}
    for mode in MODES:
        shift = contrast(previous, mode)['shift']
        actual = on[mode].sum() / formal[mode].sum()
        for strength in STRENGTHS:
            estimate = np.average(expit(base + strength * shift), weights=formal[mode])
            errors[mode, strength] = 100 * (estimate - actual)
    return errors


def choose_weights(elections):
    """Use different weights only if each Queensland election supports the distinction.

    Compare zero, half and full transfer. Give each election and each type
    within a group equal weight, using absolute error so over- and under-
    predictions cannot cancel. Use one weight for both absent groups. For a
    postal discount, both elections must individually prefer the selected
    higher weight for absents AND the lower one for postals. Otherwise use
    the best common weight across all three categories.
    """
    def loss(errors, modes, weight):
        return np.mean([abs(errors[mode, weight]) for mode in modes])

    def best(modes):
        return min(STRENGTHS, key=lambda w: sum(loss(e, modes, w) for e in elections))

    absents, postals = ('Absent', 'PrePoll'), ('Postal',)
    absent_weight, postal_weight = best(absents), best(postals)
    if postal_weight < absent_weight and all(
        loss(e, absents, absent_weight) < loss(e, absents, postal_weight)
        and loss(e, postals, postal_weight) < loss(e, postals, absent_weight) for e in elections
    ):
        return {'absent': absent_weight, 'postal': postal_weight}
    common = best(MODES)
    return {'absent': common, 'postal': common}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--write', action='store_true', help='replace the checked-in SA prior')
    args = parser.parse_args()
    checks = []
    for year, preceding in ((2020, 2019), (2024, 2022)):
        errors = transfer_errors(year, preceding)
        checks.append(errors)
        for mode in MODES:
            print(year, mode, 'signed errors for zero, half, full (pp):',
                  ' '.join(f'{errors[mode, w]:+.3f}' for w in STRENGTHS))
    source = federal(2025)['SA']
    prior = dict(schema_version=1, election='2026sa', federal_election='2025fed',
                 federal_log_odds={m: round(contrast(source, m)['shift'], 9) for m in MODES},
                 weights=choose_weights(checks))
    if args.write:
        # A watcher fingerprints and reads this file. Publish a complete new
        # revision in one rename, including when calibration is repeated.
        with TemporaryDirectory(dir=PRIOR.parent) as directory:
            updated = Path(directory) / PRIOR.name
            updated.write_text(json.dumps(prior, indent=2) + '\n', encoding='utf-8')
            updated.replace(PRIOR)
    elif prior != json.loads(PRIOR.read_bytes()):
        raise ValueError('The checked-in prior differs from the historical calculation.')
    print(prior)


if __name__ == '__main__':
    main()
