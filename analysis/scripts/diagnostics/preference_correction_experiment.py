"""Evaluate smooth, stochastic corrections to reported booth preference counts.

This experiment asks whether booth size and disagreement with other booths' preference
flows help distinguish genuine variation from a count likely to be revised. It
also asks whether a revision tends to close the entire discrepancy or only part
of it. It consumes the same-snapshot regressions from preference_flow_outlier_audit,
each fitted without the booth being predicted. Later counts supply correction
training targets and test outcomes; they never enter those seat regressions.

Each prediction has three outcomes: unchanged count, a routine small revision,
or a broader revision towards the seat relationship. The probability and extent
of the broader revision vary smoothly. Normal-looking booths retain some broad
revision probability. Subset shares are modelled on the log-odds scale and
converted back into the existing preference pool, preserving its parent total.

Calibration uses ordinary maximum likelihood, not a Bayesian fit. Five fixed
groups keep every booth in a test seat out of parameter training. Snapshots are
evaluated separately: repeated observations of one booth do not increase the
training sample. This is within-election validation, not evidence of performance
on an independently held-out election. The last feed is a reference, never a
collection of zero-future-revision training examples.
"""

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.optimize import minimize
from scipy.special import expit, logit
from scipy.stats import t


MODELS = ('routine', 'score_full', 'size_full', 'size_partial', 'local_partial')
MODEL_DESCRIPTIONS = {
    'routine': 'Unchanged-count spike plus routine revisions; no anomaly-directed correction.',
    'score_full': 'Broad correction probability uses discrepancy, and lands around the full seat prediction.',
    'size_full': 'Broad probability also uses formal booth size; conditional correction closes the full discrepancy.',
    'size_partial': 'Size-sensitive probability; less striking discrepancies are corrected only partway on average.',
    'local_partial': 'Partial correction, with peer scatter also weighted towards similarly sized booths.',
    'bounded500_local_partial': 'Size-matched partial correction; discrepancy-driven probability is smoothly tapered by 1-exp(-formal votes/500).',
    'bounded1000_local_partial': 'Size-matched partial correction; discrepancy-driven probability is smoothly tapered by 1-exp(-formal votes/1000).',
    'odds1000_local_partial': 'Size-matched partial correction; small booth size reduces correction odds, without preventing compelling discrepancies from overcoming that penalty.',
    'odds1000_strong_local_partial': 'Fixed conservative discrepancy response without a size ceiling; routine revision size and correction destination are calibrated separately.',
}


def weighted_median(values, weights):
    """Describe peer scatter without allowing one large residual to dominate."""
    order = np.argsort(values)
    return float(values[order][np.searchsorted(np.cumsum(weights[order]), weights.sum() / 2)])


def local_scatter(rows, bandwidth=1.5, support_scale=8):
    """Estimate how spread out preferences are among similarly sized peers.

    A Gaussian weight in log booth size gives a smooth preference for comparable
    booths. Sparse size comparisons revert gradually to the existing seat-wide
    scatter. Both the fitted relationship and scatter exclude the target booth.
    The additional small-count/fitting uncertainty in the original score remains.
    """
    x = np.array([[1, r['primary_log_odds'], r['other_party_log_odds'],
                   float(r['booth_type'] == 'PPVC')] for r in rows])
    y = np.array([r['preference_log_odds'] for r in rows])
    sizes = np.array([r['formal_votes'] for r in rows], dtype=float)
    pools = np.array([r['preference_votes'] for r in rows], dtype=float)
    result = []
    for i, row in enumerate(rows):
        model = row['models']['composition']
        residuals = y - x @ np.array(model['coefficients'])
        weights = pools / (pools + 100) * np.exp(-.5 * (np.log(sizes / sizes[i]) / bandwidth) ** 2)
        weights[i] = 0
        centre = weighted_median(residuals, weights)
        nearby = max(.05, 1.4826 * weighted_median(np.abs(residuals - centre), weights))
        influence = weights.sum() / (weights.sum() + support_scale)
        global_scale = model['peer_scatter_log_odds']
        scale = np.sqrt(influence * nearby ** 2 + (1 - influence) * global_scale ** 2)
        original_variance = (model['transformed_residual'] / model['score']) ** 2 if model['score'] else global_scale ** 2
        other_variance = max(0, original_variance - global_scale ** 2)
        score = model['transformed_residual'] / np.sqrt(scale ** 2 + other_variance)
        result.append((float(scale), float(abs(score))))
    return result


def prepare_data(snapshot, bandwidth):
    """Isolate opposing TCP revisions in a fixed preference parent group.

    Move only the smaller opposing candidate change between the pair; exclude
    net additions/removals from this target. It is a minimum observed revision,
    not proof that individual ballots moved. Changes to FP itself are retained
    as an evaluation diagnostic, rather than silently classified as preferences.
    """
    groups = defaultdict(list)
    for row in snapshot['rows']:
        if row['models']['composition'] is not None:
            groups[row['seat']].append(row)
    records = []
    exclusions = defaultdict(int)
    for rows in groups.values():
        nearby = local_scatter(rows, bandwidth)
        for row, (scale, score) in zip(rows, nearby):
            revision = row.get('later_revision')
            if revision is None:
                exclusions['missing_or_different_later_pair'] += 1
                continue
            pool, gain = row['preference_votes'], row['preference_gain_a']
            transfer = revision['signed_minimum_revision']
            target = gain + transfer
            if not 0 <= target <= pool:
                exclusions['revision_incompatible_with_fixed_fp_parent'] += 1
                continue
            current = row['preference_log_odds']
            later = float(np.log((target + .25) / (pool - target + .25)))
            model = row['models']['composition']
            gap = -model['transformed_residual']
            group = int.from_bytes(hashlib.sha256(row['seat'].encode()).digest()[:4], 'big') % 5
            records.append(dict(seat=row['seat'], booth=row['booth'], pool=pool, gain=gain,
                                target=target, transfer=transfer, size=row['formal_votes'], y=current,
                                gap=gap, change=later-current, score=abs(model['score']),
                                scatter=model['peer_scatter_log_odds'], local_score=score,
                                local_scatter=scale, group=group,
                                fp_unchanged=revision['fp_vector_unchanged']))
    names = ('pool', 'gain', 'target', 'transfer', 'size', 'y', 'gap', 'change',
             'score', 'scatter', 'local_score', 'local_scatter', 'group', 'fp_unchanged')
    arrays = {name: np.array([r[name] for r in records], dtype=float) for name in names}
    # Discrete likelihoods give equal units to a one-vote revision in a small or
    # large booth. Boundary outcomes use open log-odds tails, not clipped shares.
    target, pool = arrays['target'], arrays['pool']
    lo, hi = (target-.5)/pool, (target+.5)/pool
    arrays['lower'] = np.full(len(records), -np.inf)
    arrays['upper'] = np.full(len(records), np.inf)
    mask = lo > 0
    arrays['lower'][mask] = logit(lo[mask])
    mask = hi < 1
    arrays['upper'][mask] = logit(hi[mask])
    return records, arrays, dict(exclusions)


def components(parameters, data, model):
    """Separate occurrence from destination of a possible count correction.

    Routine revisions can happen without a discrepancy. A broad revision has a
    smooth logistic probability driven by discrepancy and optionally booth size.
    Its destination closes either all or a smoothly varying fraction of the gap.
    Log-odds noise keeps every corrected outcome within the preference parent.
    """
    q, routine_scale, power, b0, discrepancy, size_effect, partial, landing = parameters
    log_size = np.log(data['size'] / 1000)
    is_local = model.endswith('local_partial')
    score = data['local_score'] if is_local else data['score']
    scatter = data['local_scatter'] if is_local else data['scatter']
    broad = expit(b0 + discrepancy * np.log1p(score ** 2)
                  + (size_effect * log_size if model not in ('routine', 'score_full') else 0))
    if model.startswith('odds1000'):
        # Small booths supply weaker evidence, but an exceptionally implausible
        # flow can still be a counting error. Penalise the odds rather than the
        # probability itself so increasing evidence can overcome the penalty;
        # multiplying a probability instead imposes an artificial ceiling.
        broad = expit(b0 + discrepancy * np.log1p(score ** 2)
                      + size_effect * log_size
                      + np.log(-np.expm1(-data['size'] / 1000)))
    if model.startswith('bounded'):
        # A small booth can genuinely have a spectacular flow. Only the extra
        # discrepancy-driven probability is tapered; its background possibility
        # remains. This is a continuous response, not a minimum-booth-size rule.
        taper_scale = 500 if model.startswith('bounded500') else 1000
        background = expit(b0 + size_effect * log_size)
        broad = background + (1-np.exp(-data['size']/taper_scale))*(broad-background)
    if model == 'routine':
        broad = np.zeros(len(score))
    extent = partial + (1-partial) * score ** 2 / (score ** 2 + 2.25) if model.endswith('partial') else np.ones(len(score))
    centre = data['y'] + extent * data['gap']
    share = expit(data['y'])
    # log1p makes a count-scale prior usable near possible zeros without the
    # explosive derivative of a direct linear conversion to log odds.
    small_scale = np.log1p(np.exp(routine_scale + power * log_size)
                           / (data['pool'] * share * (1-share)))
    broad_scale = np.exp(landing) * scatter
    weights = np.array([(1-broad)*(1-q), (1-broad)*q, broad])
    return weights, data['y'], small_scale, centre, broad_scale, extent


def likelihood(parameters, data, model):
    """Score the probability assigned to the observed integer vote revision."""
    weights, original, small, centre, large, extent = components(parameters, data, model)
    routine = t.cdf((data['upper']-original)/small, 5)-t.cdf((data['lower']-original)/small, 5)
    broad = t.cdf((data['upper']-centre)/large, 5)-t.cdf((data['lower']-centre)/large, 5)
    probability = weights[0]*(data['transfer'] == 0) + weights[1]*routine + weights[2]*broad
    # Numerical underflow protection affects the logarithmic score only, never
    # an outcome's count or probability model.
    return -np.log(np.maximum(probability, np.finfo(float).tiny))


def fit_model(data, model):
    """Fit a small frequentist mixture using only the current training seats."""
    initial = np.array([.4, np.log(3), .5, -4, 1.8, .7, .3, np.log(.6)])
    bounds = [(.01,.99), (np.log(.2),np.log(100)), (0,1), (-12,1), (0,5), (0,2), (0,1), (np.log(.05),np.log(3))]
    active = [0,1,2] if model == 'routine' else [0,1,2,3,4,7]
    if model not in ('routine', 'score_full'):
        active.append(5)
    if model.endswith('partial'):
        active.append(6)
    if model == 'odds1000_strong_local_partial':
        # This deliberately stronger evidence requirement is an operational
        # comparison, not an unconstrained best fit. Let ordinary rechecking
        # retain its own uncertainty instead of forcing it to be represented
        # as frequent movement towards the fitted seat relationship.
        initial[3:6] = [-7.809508178103534, 3.20705363776961, .520532187574425]
        active = [0, 1, 2, 6, 7]
    def objective(values):
        candidate = initial.copy(); candidate[active] = values
        # A weak slope penalty prevents a handful of spectacular cases making
        # probability rise arbitrarily steeply in the training sample.
        penalty = .1 * (candidate[4]**2 + candidate[5]**2) / len(data['gain']) if model != 'routine' else 0
        return float(likelihood(candidate, data, model).mean() + penalty)
    fit = minimize(objective, initial[active], method='L-BFGS-B', bounds=[bounds[i] for i in active],
                   options=dict(maxiter=160, ftol=1e-9))
    if not fit.success:
        # Discrete mixture probabilities can be too flat for numerical gradient
        # line search. Retry only a failed fit using bounded, derivative-free
        # optimisation; never silently count an incomplete fit as validation.
        fit = minimize(objective, fit.x, method='Powell', bounds=[bounds[i] for i in active],
                       options=dict(maxiter=160, ftol=1e-8, xtol=1e-6))
    parameters = initial.copy(); parameters[active] = fit.x
    return parameters, dict(success=bool(fit.success), message=str(fit.message), iterations=int(fit.nit))


def evaluate(parameters, data, model):
    """Evaluate point forecasts and website-relevant intervals without sampling.

    Quadrature computes the mean count of each bounded component. Vectorised
    CDF inversion retains the unchanged-count spike when calculating percentiles.
    All intervals describe preference revisions alone, not the full forecast.
    """
    weights, original, small, centre, large, extent = components(parameters, data, model)
    nodes, quadrature_weights = np.polynomial.legendre.leggauss(32)
    z = t.ppf((nodes+1)/2, 5)
    routine_outcomes = data['pool'][:,None]*expit(original[:,None]+small[:,None]*z)
    broad_outcomes = data['pool'][:,None]*expit(centre[:,None]+large[:,None]*z)
    means = weights[0]*data['gain'] + weights[1]*(routine_outcomes @ (quadrature_weights/2)) + weights[2]*(broad_outcomes @ (quadrature_weights/2))
    # A distribution score in votes balances closeness and uncertainty, unlike
    # mean error alone. Weighted quadrature includes the exact unchanged spike.
    outcomes = np.column_stack((data['gain'],routine_outcomes,broad_outcomes))
    outcome_weights = np.column_stack((weights[0],weights[1,:,None]*(quadrature_weights/2),
                                      weights[2,:,None]*(quadrature_weights/2)))
    order = np.argsort(outcomes,axis=1)
    sorted_outcomes = np.take_along_axis(outcomes,order,axis=1)
    sorted_weights = np.take_along_axis(outcome_weights,order,axis=1)
    cumulative = np.cumsum(sorted_weights,axis=1)
    crps = (outcome_weights*np.abs(outcomes-data['target'][:,None])).sum(axis=1) - (
        sorted_weights*sorted_outcomes*(2*cumulative-sorted_weights-1)).sum(axis=1)
    def quantile(q):
        left, right = np.zeros(len(means)), data['pool'].copy()
        for _ in range(34):
            midpoint = (left+right)/2
            y = logit(midpoint/data['pool'])
            cumulative = weights[0]*(midpoint >= data['gain']) + weights[1]*t.cdf((y-original)/small,5) + weights[2]*t.cdf((y-centre)/large,5)
            below = cumulative < q
            left[below] = midpoint[below]; right[~below] = midpoint[~below]
        return (left+right)/2
    intervals = {}
    for coverage, low, high in [(75,.125,.875),(95,.025,.975),(99,.005,.995),(99.8,.001,.999)]:
        intervals[str(coverage)] = (quantile(low),quantile(high))
    # A finite parent with the observed lower-bound count may differ by tiny
    # floating-point inversion error at the point mass; tolerance is numerical.
    metrics = dict(mean_negative_log_probability=float(likelihood(parameters,data,model).mean()),
                   mean_distribution_score_votes=float(crps.mean()),
                   mean_absolute_candidate_vote_error=float(np.abs(means-data['target']).mean()),
                   signed_mean_candidate_vote_error=float((means-data['target']).mean()),
                   mean_broad_correction_probability=float(weights[2].mean()),
                   intervals={label:dict(coverage=float(np.mean((data['target']>=lo-1e-5)&(data['target']<=hi+1e-5))),
                                          mean_width_votes=float((hi-lo).mean())) for label,(lo,hi) in intervals.items()})
    return metrics, dict(mean=means, broad=weights[2], extent=extent, routine=weights[1], unchanged=weights[0], intervals=intervals,crps=crps)


def diagnostic_tables(data):
    """Show occurrence and landing separately, without turning bins into rules."""
    occurrence, landing = [], []
    for lo, hi in [(0,500),(500,2000),(2000,8000),(8000,np.inf)]:
        for a,b in [(0,1),(1,2),(2,3),(3,np.inf)]:
            mask = (data['size']>=lo)&(data['size']<hi)&(data['score']>=a)&(data['score']<b)
            if not mask.any(): continue
            occurrence.append(dict(formal_vote_range=[lo,None if np.isinf(hi) else hi], score_range=[a,None if np.isinf(b) else b],
                                   booths=int(mask.sum()), any_opposing_revision=float(np.mean(data['transfer'][mask]!=0)),
                                   above_two_votes=float(np.mean(np.abs(data['transfer'][mask])>2)),
                                   at_least_25_votes=float(np.mean(np.abs(data['transfer'][mask])>=25))))
    for a,b in [(0,1),(1,2),(2,3),(3,4),(4,np.inf)]:
        mask = (data['score']>=a)&(data['score']<b)&(np.abs(data['transfer'])>2)
        if not mask.any(): continue
        fraction = data['change'][mask]/data['gap'][mask]
        landing.append(dict(score_range=[a,None if np.isinf(b) else b], revised_booths=int(mask.sum()),
                            median_fraction_of_gap=float(np.median(fraction)),
                            quartiles=np.quantile(fraction,[.25,.75]).tolist(),
                            fraction_towards_trend=float(np.mean(fraction>0))))
    return dict(occurrence=occurrence, landing=landing)


def run_snapshot(snapshot, bandwidth, models, fixed_parameters=None):
    """Keep test-seat outcomes separate while comparing alternative hypotheses."""
    records, data, exclusions = prepare_data(snapshot, bandwidth)
    results = {}
    for model in models:
        parameters_by_group, diagnostics = [], []
        forecasts = dict(mean=np.zeros(len(records)), broad=np.zeros(len(records)), extent=np.zeros(len(records)))
        log_scores = np.zeros(len(records))
        distribution_scores = np.zeros(len(records))
        interval_arrays = {str(p):[np.zeros(len(records)),np.zeros(len(records))] for p in (75,95,99,99.8)}
        for group in range(5):
            test = data['group']==group
            train = {key:value[~test] for key,value in data.items()}
            if fixed_parameters is None:
                parameters, diagnostic = fit_model(train, model)
            else:
                # The same held-seat split is retained when transporting one
                # parameter set across dates; test-seat outcomes still do not
                # enter its calibration. This is a retrospective stability check.
                parameters = np.array(fixed_parameters[model]['parameters_by_test_group'][group])
                diagnostic = dict(success=True,message='Fixed parameters from a separately recorded snapshot.',iterations=0)
            parameters_by_group.append(parameters.tolist()); diagnostics.append(diagnostic)
            held = {key:value[test] for key,value in data.items()}
            metrics, predictions = evaluate(parameters, held, model)
            log_scores[test] = likelihood(parameters, held, model)
            distribution_scores[test] = predictions['crps']
            for key in forecasts: forecasts[key][test] = predictions[key]
            for label in interval_arrays:
                for i in (0,1): interval_arrays[label][i][test] = predictions['intervals'][label][i]
        seat_votes = defaultdict(lambda:[0.,0.])
        for i, row in enumerate(records):
            seat_votes[row['seat']][0] += forecasts['mean'][i]-row['gain']
            seat_votes[row['seat']][1] += row['transfer']
        errors = forecasts['mean']-data['target']
        results[model] = dict(description=MODEL_DESCRIPTIONS[model], parameters_by_test_group=parameters_by_group,
                             fit_diagnostics=diagnostics, mean_negative_log_probability=float(log_scores.mean()),
                             mean_distribution_score_votes=float(distribution_scores.mean()),
                             mean_absolute_candidate_vote_error=float(np.abs(errors).mean()),
                             signed_mean_candidate_vote_error=float(errors.mean()),
                             seat_mean_absolute_revision_error_votes=float(np.mean([abs(x-y) for x,y in seat_votes.values()])),
                             mean_broad_correction_probability=float(forecasts['broad'].mean()),
                             intervals={label:dict(coverage=float(np.mean((data['target']>=lo-1e-5)&(data['target']<=hi+1e-5))),
                                                   mean_width_votes=float((hi-lo).mean())) for label,(lo,hi) in interval_arrays.items()},
                             examples=[dict(seat=row['seat'],booth=row['booth'],formal_votes=row['size'],
                                            score=row['score'],size_matched_score=row['local_score'],
                                            observed_revision_votes=row['transfer'],broad_probability=float(forecasts['broad'][i]),
                                            conditional_fraction_of_gap=float(forecasts['extent'][i]),
                                            expected_revision_votes=float(forecasts['mean'][i]-row['gain']),
                                            revision_intervals={label:[float(lo[i]-row['gain']),float(hi[i]-row['gain'])]
                                                                for label,(lo,hi) in interval_arrays.items()})
                                       for i,row in enumerate(records) if row['booth'] in (
                                           'The Ponds GREENWAY PPVC','Cairns South KENNEDY PPVC','Rockdale BARTON PPVC',
                                           'Wattle Park (Menzies)','Norseman','Blackville','Gladstone PPVC','Northcote COOPER PPVC')])
        print(snapshot['source'],model,'log score',round(results[model]['mean_negative_log_probability'],4),
              'vote MAE',round(results[model]['mean_absolute_candidate_vote_error'],3),
              'fits passed',sum(d['success'] for d in diagnostics),flush=True)
    return dict(source=snapshot['source'],comparable_booths=len(records),exclusions=exclusions,
                unchanged_forecast_vote_error=float(np.abs(data['transfer']).mean()),
                unchanged_forecast_seat_error_votes=float(np.mean([abs(sum(r['transfer'] for r in records if r['seat']==seat))
                                                                   for seat in set(r['seat'] for r in records)])),
                descriptive_tables=diagnostic_tables(data),models=results)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--sources',nargs='+',default=['20250504010119','20250511215001','20250516214931','20250520214756'])
    parser.add_argument('--size-bandwidth',type=float,default=1.5)
    parser.add_argument('--models',nargs='+',choices=tuple(MODEL_DESCRIPTIONS),default=MODELS)
    parser.add_argument('--parameters-from',type=Path,help='Optional recorded experiment to reuse the same correction parameters across dates.')
    parser.add_argument('--parameter-source',default='20250520214756')
    args=parser.parse_args()
    source=args.audit.read_bytes(); audit=json.loads(source)
    fixed_parameters = None
    parameter_provenance = None
    if args.parameters_from:
        content=args.parameters_from.read_bytes()
        fitted=json.loads(content)
        fixed_parameters=next(s['models'] for s in fitted['snapshots'] if s['source']==args.parameter_source)
        parameter_provenance=dict(path=str(args.parameters_from),sha256=hashlib.sha256(content).hexdigest(),source=args.parameter_source)
    result=dict(purpose='Test whether smoothly varying correction probability and partial movement improve preference-revision predictions.',
                definitions=dict(target='Minimum opposing TCP candidate change, allocated within the original FP preference pool; net additions/removals omitted.',
                                 probability='Chance of the broader correction component, separate from routine small revisions and an unchanged-count outcome.',
                                 fraction='Fraction of the discrepancy closed on the log-odds scale; 1 reaches the contemporaneous seat prediction.',
                                 discrepancy='Absolute observed-minus-predicted preference log-odds divided by expected peer, sampling and fitting variation; size-matched variants weight peer scatter towards similar formal booth sizes.',
                                 validation='Five deterministic seat groups per snapshot; test seats never supply fitted correction-model outcomes.',
                                 log_score='Mean negative log probability of the observed integer count; lower is better.',
                                 distribution_score='Continuous ranked probability score in votes: rewards closeness while penalising unnecessary width; lower is better.',
                                 intervals='Coverage and width of preference-only count intervals; these are not full candidate-share forecast intervals.',
                                 bins='Descriptive tables only; prediction probabilities and destinations have no bin thresholds.'),
                audit_path=str(args.audit),audit_sha256=hashlib.sha256(source).hexdigest(),
                parameter_provenance=parameter_provenance,
                input_sources=audit['sources'],reference=audit['reference'],
                code_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                settings=dict(size_bandwidth=args.size_bandwidth,scatter_support_scale=8,student_degrees_of_freedom=5,
                              partial_response_scale=1.5,test_groups=5),
                parameter_names=['routine_probability','log_routine_vote_scale','routine_size_power','broad_intercept',
                                 'broad_discrepancy_effect','broad_size_effect','partial_low_discrepancy_fraction','log_landing_scatter_multiplier'],
                snapshots=[run_snapshot(s,args.size_bandwidth,args.models,fixed_parameters) for s in audit['snapshots'] if s['source'] in args.sources])
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
