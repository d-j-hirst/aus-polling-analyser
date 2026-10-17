"""Estimate conservative category covariance from every usable election pair.

Each election contributes one equally weighted matrix of category-swing
departures from ordinary voting. District residuals estimate a separate local
matrix. Missing subtypes are observed as uncertain mixtures, never invented
counts. This is a small regularised moment calculation, with no Stan fitting.
Run from analysis: python -B -m scripts.diagnostics.calibrate_tpp_composition.
"""

import json
import hashlib

import numpy as np

from scripts.diagnostics.category_sources import CACHE, ROOT
from scripts.diagnostics.category_tpp_relationships import seat_accounts, share_z, SEQUENCES

NAMES = ('early_booth', 'early_absent', 'day_absent', 'postal')
INDEX = tuple((i, j) for i in range(4) for j in range(i + 1))
PRIOR_ELECTIONS = 8.


def definition(region, old_year):
    """Return comparable raw categories and their latent mixture exposures.

    A uniform unknown split has mean 1/2 and variance 1/12. This does NOT
    assert equal subgroup counts: the variance retains the unidentified split.
    Federal comparisons spanning the introduction of ordinary PPVCs use the
    combined early observation; subsequent comparisons retain both subtypes.
    """
    unit = np.eye(4)
    early = ('early',), unit[0], np.zeros(4)
    absent = ('absent',), unit[2], np.zeros(4)
    if region == 'fed' and old_year >= 2010:
        return [early, (('early_declaration',), unit[1], np.zeros(4)), absent,
                (('postal',), unit[3], np.zeros(4))]
    if region in ('vic', 'fed'):
        early = (('early', 'early_declaration'), (unit[0]+unit[1])/2,
                 (unit[0]-unit[1])/np.sqrt(12))
    if region == 'qld':
        return [early, (('declaration_early',), unit[1], np.zeros(4)), absent,
                (('postal',), unit[3], np.zeros(4))]
    if region in ('nsw', 'wa'):
        absent = (('absent',), (unit[1]+unit[2])/2, (unit[1]-unit[2])/np.sqrt(12))
    return [early, absent, (('postal',), unit[3], np.zeros(4))]


def count(account, keys):
    values = [account[key] for key in keys if key in account]
    return sum(values, np.zeros(2))


def election_sample(accounts, old, new):
    region, old_year = new[4:], int(old[:4])
    groups = definition(region, old_year)
    keys = [('ordinary',)] + [row[0] for row in groups]
    # iVote disappeared in NSW 2023. Compare the remote channel consistently;
    # this is not evidence that every iVote voter switched to postal voting.
    if new == '2023nsw':
        keys[-1] = ('postal', 'ivote')
    previous, current = accounts[old], accounts[new]
    seats = sorted(s for s in previous.keys() & current.keys()
                   if all(sum(count(a[s], k)) > 0 for a in (previous, current) for k in keys))
    if len(seats) < 30:
        return None
    totals = [[sum((count(a[s], k) for s in seats), np.zeros(2)) for k in keys]
              for a in (previous, current)]

    def departure(pair):
        change = np.array([share_z(b)-share_z(a) for a, b in zip(*pair)])
        return change[1:]-change[0]

    shared = departure(totals)
    local = np.array([departure([[count(a[s], k) for k in keys] for a in (previous, current)])
                      for s in seats])
    weights = np.array([sum(sum(count(current[s], k)) for k in keys) for s in seats])
    local -= np.average(local, axis=0, weights=weights)
    covariance = (local.T * weights) @ local / sum(weights)
    return dict(election=new, previous=old, districts=len(seats), groups=groups,
                shared=np.outer(shared, shared), local=covariance)


def moment_operator(groups):
    # E[A C A'] includes uncertainty about an unresolved group's split on the
    # diagonal. Distinct groups' unknown splits are independent at this layer.
    rows = []
    for i, (_, exposure, split) in enumerate(groups):
        for j, (_, other, _) in enumerate(groups[:i+1]):
            coefficient = np.outer(exposure, other)
            if i == j:
                coefficient += np.outer(split, split)
            rows.append([coefficient[a, b] + (coefficient[b, a] if a != b else 0)
                         for a, b in INDEX])
    return np.array(rows)


def estimate(samples, kind, prior_variance):
    # Equal election influence avoids giving federal elections more authority
    # merely because they report an additional subtype. Eight diagonal prior
    # matrices regularise the small, partly dependent historical sample. Zero
    # mean is deliberate: category growth supplies no directional correction.
    target = np.array([prior_variance if i == j else 0 for i, j in INDEX])
    rows, values = [np.eye(len(INDEX))*np.sqrt(PRIOR_ELECTIONS/len(INDEX))], [target*np.sqrt(PRIOR_ELECTIONS/len(INDEX))]
    for sample in samples:
        operator = moment_operator(sample['groups'])
        moment = sample[kind]
        rows.append(operator/np.sqrt(len(operator)))
        values.append(np.array([moment[i, j] for i in range(len(moment)) for j in range(i+1)])/np.sqrt(len(operator)))
    coefficients = np.linalg.lstsq(np.concatenate(rows), np.concatenate(values), rcond=None)[0]
    covariance = np.zeros((4, 4))
    for value, (i, j) in zip(coefficients, INDEX):
        covariance[i, j] = covariance[j, i] = value
    # The least-squares moment estimate need not be positive definite. Project
    # to a covariance, retaining a small independent component in every type.
    eigenvalues, vectors = np.linalg.eigh(covariance)
    return (vectors * np.maximum(eigenvalues, prior_variance*.1)) @ vectors.T


def calibrate(normalized):
    accounts = seat_accounts(normalized['records'])
    samples, excluded = [], []
    for region, years in SEQUENCES.items():
        for old_year, year in zip(years, years[1:]):
            old, new = f'{old_year}{region}', f'{year}{region}'
            if old not in accounts or new not in accounts:
                excluded.append(dict(previous=old, election=new, reason='Missing comparable category data'))
                continue
            sample = election_sample(accounts, old, new)
            if sample is None:
                excluded.append(dict(previous=old, election=new, reason='Fewer than 30 complete common districts'))
            else:
                samples.append(sample)
    return samples, excluded, estimate(samples, 'shared', 4.), estimate(samples, 'local', 16.)


def write_outputs(samples, excluded, shared, local):
    destination = ROOT/'analysis/reports/live-category-tpp/composition-calibration.json'
    report = dict(categories=NAMES, prior_elections=PRIOR_ELECTIONS,
                  normalized_source_sha256=hashlib.sha256((CACHE/'normalized.json').read_bytes()).hexdigest(),
                  shared_covariance=shared.tolist(), local_covariance=local.tolist(),
                  elections=[{k: s[k] for k in ('previous', 'election', 'districts')} for s in samples], excluded=excluded,
                  leave_one_out_shared_sd=[np.sqrt(np.diag(estimate(samples[:i]+samples[i+1:], 'shared', 4.))).tolist()
                                           for i in range(len(samples))])
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    header = ['#pragma once', '', '// Generated by analysis/scripts/diagnostics/calibrate_tpp_composition.py.',
              '// Zero-mean contrasts on the 25*log-odds scale; no category-growth slope.',
              '// Order: early booth, early absent, day absent, postal. See docs/live-tpp-composition.md.',
              'namespace LiveTppComposition {']
    for name, matrix in [('SharedCovariance', shared), ('LocalCovariance', local)]:
        header.append(f'inline constexpr double {name}[4][4] = {{')
        header.extend('    {'+', '.join(f'{v:.10g}' for v in row)+'},' for row in matrix)
        header.append('};')
    header.append('}\n')
    (ROOT/'LiveTppCompositionParameters.h').write_text('\n'.join(header), encoding='utf-8')
    print(f'{len(samples)} election pairs; shared SD {np.sqrt(np.diag(shared))}; local SD {np.sqrt(np.diag(local))}')


def main():
    normalized = json.loads((CACHE/'normalized.json').read_text(encoding='utf-8'))
    write_outputs(*calibrate(normalized))


if __name__ == '__main__':
    main()
