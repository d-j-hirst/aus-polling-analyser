"""Prepare the three early-voting populations reported in the Victorian V2 feed.

The 2022 recheck gives actual first preferences at home early centres and a
combined remainder. The 2026 venue list estimates mobile and centre attendance.
These sources divide the existing formal early turnout allowance. A separate
copy of the historical comparison preserves each candidate's FP and TCP totals.
"""
from collections import Counter, defaultdict
from copy import deepcopy

from scripts.elections.prepare_vec_2022_booths import save
from scripts.elections.prepare_wa_baseline import apportion
from scripts.turnout.turnout_qld_operational import read_xlsx_rows
from scripts.turnout.turnout_vic import _download


NAMES = ('EVC early', 'Mobile early', 'Other early')


def cached_workbooks(urls, cache):
    """Retain the public workbooks by their VEC resource IDs for offline reuse."""
    cache.mkdir(parents=True, exist_ok=True)
    paths = {}
    for name, url in urls.items():
        path = cache / url.rsplit('/', 1)[1]
        if not path.is_file():
            save(path, _download(url))
        paths[name] = path
    return paths


def previous_early_counts(recheck, previous):
    """Reconcile the two published FP populations against the pooled candidate totals."""
    by_district = defaultdict(lambda: (Counter(), Counter()))
    for row in read_xlsx_rows(recheck, 'VEC district recheck').values():
        if (row.get('B'), row.get('C'), row.get('H')) != ('Recheck', 'First Preference', 'Early Votes'):
            continue
        if row['G'] in ('Informal Votes', 'Total Votes'):
            continue
        candidate = str(int(row['F']) - 1)  # The historical JSON indexes candidates from zero in ballot order.
        centre, remainder = by_district[row['A']]
        counts = remainder if row['D'] == 'Other Early Votes' else centre
        counts[candidate] += int(row['I'])
    if set(by_district) != set(previous) - {'Narracan'}:
        raise ValueError('Expected the November recheck in every district except the postponed Narracan poll.')
    for name, (centre, remainder) in by_district.items():
        pooled = previous[name]['booths']['Early Votes']['fp']
        if (set(centre) | set(remainder) != set(pooled) or
                any(centre[c] + remainder[c] != n for c, n in pooled.items())):
            raise ValueError(f'Early recheck differs from the published FP count in {name}.')
    return by_district


def venue_estimates(venues, sheet, column, heading):
    """Sum the VEC's attendance estimates for the district served by each venue row."""
    rows = read_xlsx_rows(venues, 'VEC venue estimates', sheet)
    if rows.pop(1).get(column) != heading:
        raise ValueError(f'Unexpected estimate column in the VEC {sheet} sheet.')
    estimates = Counter()
    for row in rows.values():
        if not row.get('A'):
            continue
        count = int(row[column])
        if count < 0:
            raise ValueError(f'Negative attendance estimate in {sheet}.')
        estimates[row['A'].removesuffix(' District')] += count
    return estimates


def district_weights(centre_share, centre_estimate, mobile_estimate):
    """Reserve the previous home-centre fraction of all formal early votes.

    The old remainder also contained mobile votes. Use the VEC's expected
    mobile-to-centre attendance ratio to estimate their fraction, assuming
    similar formality and home-district participation at the two services.
    The balance belongs to voters counted in the residual Early category.
    """
    if centre_estimate <= 0:
        raise ValueError('Missing early-centre attendance estimate.')
    mobile_share = centre_share * mobile_estimate / centre_estimate
    remaining = 1 - centre_share - mobile_share
    if not (0 < centre_share < 1 and mobile_share >= 0 and remaining > 0):
        raise ValueError('Venue estimates cannot be reconciled with the historical early vote split.')
    return dict(zip(NAMES, (centre_share, mobile_share, remaining)))


def split_previous(pooled, centre, weights):
    """Keep measured centre FP counts wherever available and divide the remaining pool.

    No separate historical mobile FP count or comparable three-way TCP table
    is available. Within the unsplit pool, each candidate gets the same size
    weights, with rounding that conserves their published count. Narracan has
    only combined early figures, so all three of its comparisons are estimates.
    """
    sizes = list(weights.values())
    rows = [dict(fp={}, tcp={}, synthetic=True) for _ in sizes]
    for candidate, votes in pooled['fp'].items():
        if centre is None:
            split = apportion(votes, sizes)
        else:
            measured = centre[candidate]
            split = [measured, *apportion(votes - measured, sizes[1:])]
        for row, count in zip(rows, split):
            row['fp'][candidate] = count
    if centre is not None:
        rows[0]['measured_fp'] = True
    for candidate, votes in pooled['tcp'].items():
        for row, count in zip(rows, apportion(votes, sizes)):
            row['tcp'][candidate] = count
    return dict(zip(('EVC Early Votes', 'Mobile Early Votes', 'Other Early Votes'), rows))


def prepare(previous, paths):
    """Produce 2026 turnout weights and the corresponding historical comparison for every district."""
    recheck = previous_early_counts(paths['recheck'].read_bytes(), previous)
    venues = paths['venues'].read_bytes()
    centres = venue_estimates(venues, 'EVCs', 'I', 'Est. Early Votes')
    mobile = venue_estimates(venues, 'MEVCs', 'E', 'Estimated votes')
    if set(centres) != set(previous) or not set(mobile) <= set(previous):
        raise ValueError('The VEC venue estimates and district roster differ.')
    statewide_centre = sum(c.total() for c, _ in recheck.values())
    statewide_other = sum(r.total() for _, r in recheck.values())
    history, weights = deepcopy(previous), {}
    for seat, record in history.items():
        centre = None
        fraction = statewide_centre / (statewide_centre + statewide_other)
        if seat in recheck:
            centre, other = recheck[seat]
            fraction = centre.total() / (centre.total() + other.total())
        weights[seat] = district_weights(fraction, centres[seat], mobile[seat])
        booths = record['booths']
        booths.update(split_previous(booths.pop('Early Votes'), centre, weights[seat]))
    return weights, history
