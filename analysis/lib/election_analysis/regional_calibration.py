"""Estimate predictive regional uncertainty from elections excluded from fitting.

State polling estimates departures from the common national swing. A test
election must not train the coefficients OR choose its prediction's polling
weight. ``fit_predictor`` chooses that weight using inner excluded elections;
``prediction_errors`` measures outer errors. ``calibrate_uncertainty`` converts
these errors to noise scales before states are reconciled to the national vote.
Draws use z=25*log(p/(100-p)), approximately percentage points near 50%.
No extra political-regime-change allowance is added.
"""

import csv
import math

import numpy as np

from lib.shared.sample_kurtosis import one_tail_kurtosis


# A few elections cannot establish precise state-specific slopes or variances.
# Retain the calibration's existing half-pooling of mean coefficients. Variances
# borrow ten election-equivalents from the shared variance; repeated quarters
# of one election do not increase that effective sample size.
STATE_COEFFICIENT_WEIGHT = 0.5
VARIANCE_POOL_ELECTIONS = 10.0
MIN_HORIZON_ELECTIONS = 3


def load_evidence(target_year, directory):
    """Read existing regional sources, retaining only pre-target elections."""
    history = {}
    with (directory / "tpp-fed-regions.csv").open() as source:
        for row in csv.reader(source):
            year, state = int(row[0]), row[2]
            if target_year - 25 <= year < target_year:
                history.setdefault(year, {})[state] = float(row[4])
    polls = {}
    with (directory / "region-polls-fed.csv").open() as source:
        for row in csv.reader(source):
            year = int(row[0])
            if year < target_year:
                polls.setdefault(year, {})[row[2]] = np.array(list(map(float, row[3:])))
    elections = {year: prepare_election(rows) for year, rows in polls.items()}
    if len(elections) < 4:
        raise ValueError("Regional prediction calibration needs at least four polling elections.")
    return {"history": history, "elections": elections}


def prepare_election(rows):
    """Build consistent contrasts within the states covered by a polling source."""
    states = sorted(rows)
    if len(states) < 2:
        raise ValueError("Regional contrasts need at least two states.")
    values = np.array([rows[state] for state in states])
    if values.shape[1] < 4 or not np.isfinite(values).all():
        raise ValueError("Regional polling rows need finite results, weights and polls.")
    previous, actual, population = values[:, 0], values[:, 1], values[:, 2]
    if (np.any(population <= 0) or np.any(previous <= 0) or np.any(previous >= 100)
            or np.any(actual <= 0) or np.any(actual >= 100)):
        raise ValueError("Regional results must be inside 0--100 with positive weights.")
    weights = population / population.sum()
    swing = actual - previous
    poll_swings = values[:, 3:] - previous[:, None]
    return dict(states=states, weights=weights, previous=previous,
                swing=swing, national=float(weights @ swing),
                deviations=poll_swings - weights @ poll_swings,
                horizons=values.shape[1] - 3)


def without_election(evidence, year):
    """Remove the entire election from both predictors and every polling horizon."""
    return {key: {y: value for y, value in rows.items() if y != year}
            for key, rows in evidence.items()}


def regression(pairs):
    """Return slope/intercept; constant inputs provide no slope evidence."""
    values = np.array(pairs, dtype=float)
    x, y = values[:, 0], values[:, 1]
    variance = np.sum((x - x.mean()) ** 2)
    slope = float(np.sum((x - x.mean()) * (y - y.mean())) / variance) if variance > 1e-12 else 0.0
    return np.array([slope, float(y.mean() - slope * x.mean())])


def fit_relationships(evidence):
    """Fit national/state relationships and partly pooled polling relationships."""
    base_pairs, poll_pairs = {}, {}
    for result in evidence["history"].values():
        for state, swing in result.items():
            if state != "all":
                base_pairs.setdefault(state, []).append((result["all"], swing))
    for election in evidence["elections"].values():
        for i, state in enumerate(election["states"]):
            poll_pairs.setdefault(state, []).append((election["deviations"][i, 0],
                                                     election["swing"][i] - election["national"]))
    base = {state: regression(pairs) for state, pairs in base_pairs.items()}
    poll = {state: regression(pairs) for state, pairs in poll_pairs.items()}
    base["all"] = regression([pair for pairs in base_pairs.values() for pair in pairs])
    poll["all"] = regression([pair for pairs in poll_pairs.values() for pair in pairs])
    return dict(base=base, poll=poll)


def prediction_components(relationships, election):
    """Use the simulator's partially pooled polling mean rule.

    Condition on the actual common swing to measure specifically regional error.
    The application supplies its sampled national swing and otherwise performs
    the same calculation. National polling error is accounted for elsewhere.
    """
    states, national = election["states"], election["national"]
    base = np.array([relationships["base"][s] for s in states])
    poll = np.array([STATE_COEFFICIENT_WEIGHT * relationships["poll"][s]
                     + (1 - STATE_COEFFICIENT_WEIGHT) * relationships["poll"]["all"] for s in states])
    naive = (base[:, 0] - 1) * national + base[:, 1]
    polled = poll[:, 0, None] * election["deviations"] + poll[:, 1, None]
    return naive, polled


def reconciled_prediction(components, election, horizon, mix_parameters):
    """Mirror the regional transform and common national reconciliation.

    State shares remain bounded through the log-odds transform. Afterwards a
    common adjustment aligns their weighted swing with the national draw. The
    Jacobian maps independent z-scale noise into these reconciled pp errors.
    """
    naive, polled = components
    mix = float(np.clip(mix_parameters[0] * math.exp(-mix_parameters[1] * horizon), 0, 1))
    deviation = naive * (1 - mix) + polled[:, horizon] * mix
    previous = election["previous"]
    z = 25 * np.log(previous / (100 - previous)) + election["national"] + deviation
    predicted = 100 / (1 + np.exp(-z / 25))
    centre = np.eye(len(previous)) - np.ones((len(previous), 1)) @ election["weights"][None, :]
    errors = centre @ (predicted - previous - election["swing"])
    jacobian = centre @ np.diag(predicted * (100 - predicted) / 2500)
    return errors, jacobian


def supported_horizons(elections):
    return [h for h in range(max(e["horizons"] for e in elections.values()))
            if sum(e["horizons"] > h for e in elections.values()) >= MIN_HORIZON_ELECTIONS]


def inner_prediction_samples(evidence):
    """Give each inner test election total weight one across horizons and states."""
    elections = evidence["elections"]
    horizons = supported_horizons(elections)
    samples = []
    for year, election in elections.items():
        relationships = fit_relationships(without_election(evidence, year))
        components = prediction_components(relationships, election)
        available = [h for h in horizons if h < election["horizons"]]
        for horizon in available:
            weight = 1 / math.sqrt(len(available) * len(election["states"]))
            samples.append((components, election, horizon, weight))
    return samples


def select_polling_weight(samples):
    """Fit a bounded polling-weight curve to the inner prediction errors."""
    from scipy.optimize import least_squares

    def residuals(parameters):
        return np.concatenate([reconciled_prediction(c, e, h, parameters)[0] * weight
                               for c, e, h, weight in samples])

    # Multiple starts protect the bounded exponential fit at its flat B=0 edge.
    fits = [least_squares(residuals, [0.5, rate], bounds=([0, 0], [1, 10]))
            for rate in [0.0, 0.1, 1.0]]
    best = min(fits, key=lambda fitted: float(fitted.fun @ fitted.fun))
    return best.x


def fit_predictor(evidence):
    """Select the mixture separately, then fit the production mean relationships.

    Sparse horizons use the bounded exponential continuation instead of another
    noisy quarter-specific coefficient. No outer test-election data enter here.
    """
    samples = inner_prediction_samples(evidence)
    mixture = select_polling_weight(samples)
    relationships = fit_relationships(evidence)
    return dict(relationships=relationships, mix=mixture,
                training_years=sorted(evidence["elections"]))


def prediction_errors(evidence):
    """Score outer test elections, with mixture selection also kept separate."""
    horizons = supported_horizons(evidence["elections"])
    samples = []
    for year, election in evidence["elections"].items():
        predictor = fit_predictor(without_election(evidence, year))
        components = prediction_components(predictor["relationships"], election)
        for horizon in horizons:
            if horizon >= election["horizons"]:
                continue
            errors, jacobian = reconciled_prediction(components, election, horizon, predictor["mix"])
            samples.append(dict(year=year, horizon=horizon, states=election["states"],
                                errors=errors, jacobian=jacobian, mix=predictor["mix"],
                                training_years=predictor["training_years"]))
    return samples


def projected_variance(sample, modifiers):
    """Per-state variance after national reconciliation, for common input SD=1."""
    scales = np.array([modifiers[state] for state in sample["states"]])
    return np.sum((sample["jacobian"] * scales) ** 2, axis=1)


def horizon_scales(samples, modifiers):
    """Infer pre-reconciliation SDs from held-out reconciled error budgets."""
    result = {}
    for horizon in sorted({s["horizon"] for s in samples}):
        selected = [s for s in samples if s["horizon"] == horizon]
        # Each election contributes its average state error. Reconciliation
        # removes common error and changes variance: use that same account on
        # both sides, rather than feeding a post-reconciliation SD in as noise.
        error_variance = np.mean([np.mean(s["errors"] ** 2) for s in selected])
        variance_transfer = np.mean([np.mean(projected_variance(s, modifiers)) for s in selected])
        result[horizon] = math.sqrt(max(1e-12, error_variance / variance_transfer))
    return result


def pooled_modifiers(samples, scales):
    """Shrink state variance ratios using elections, never counts of quarters."""
    ratios = {}
    unit = {state: 1.0 for s in samples for state in s["states"]}
    for sample in samples:
        expected = projected_variance(sample, unit) * scales[sample["horizon"]] ** 2
        for i, state in enumerate(sample["states"]):
            ratios.setdefault(state, {}).setdefault(sample["year"], []).append(float(sample["errors"][i] ** 2 / expected[i]))
    return {state: math.sqrt((VARIANCE_POOL_ELECTIONS + sum(np.mean(v) for v in years.values()))
                             / (VARIANCE_POOL_ELECTIONS + len(years)))
            for state, years in ratios.items()}


def smooth_scales(scales):
    """Anchor election-day error and keep uncertainty nondecreasing with lead time."""
    from scipy.optimize import least_squares

    horizons = np.array(sorted(scales), dtype=float)
    observed = np.array([scales[h] for h in horizons])
    day_zero = scales[0]
    # Earlier polls must not become more certain just because fewer elections
    # have long polling records. This also prevents negative extrapolated SDs.
    def residuals(parameters):
        growth, rate = parameters
        return day_zero + growth * (1 - np.exp(-rate * horizons)) - observed
    fits = [least_squares(residuals, [max(0.1, max(observed) - day_zero), rate],
                          bounds=([0, 0], [20, 10])) for rate in [0.1, 1.0]]
    growth, rate = min(fits, key=lambda f: float(f.fun @ f.fun)).x
    return np.array([-growth, rate, day_zero + growth])


def balanced_kurtosis(samples, modifiers, scales):
    """Estimate a single tail shape, giving each election equal total weight."""
    counts = {year: sum(s["year"] == year for s in samples) for year in {s["year"] for s in samples}}
    values, weights = [], []
    for sample in samples:
        sd = scales[sample["horizon"]] * np.sqrt(projected_variance(sample, modifiers))
        values.extend(sample["errors"] / sd)
        weights.extend([1 / (counts[sample["year"]] * len(sd))] * len(sd))
    empirical = one_tail_kurtosis(values, weights)
    # Tail shape is noisy with a handful of elections. This is ordinary sample
    # regularisation, not an extra future political-shock component.
    strength = len(counts) / (len(counts) + VARIANCE_POOL_ELECTIONS)
    return max(3.0, 3 + strength * (empirical - 3))


def calibrate_uncertainty(samples):
    states = {state for sample in samples for state in sample["states"]}
    pooled = horizon_scales(samples, dict.fromkeys(states, 1.0))
    modifiers = pooled_modifiers(samples, pooled)
    scales = horizon_scales(samples, modifiers)
    return dict(modifiers=modifiers, raw_scales=scales, scale=smooth_scales(scales),
                kurtosis=balanced_kurtosis(samples, modifiers, scales))


def base_uncertainty(evidence):
    """Use excluded-election errors for the fallback national/state model too."""
    errors = {}
    for year, result in evidence["history"].items():
        relationships = fit_relationships(without_election(evidence, year))["base"]
        for state, swing in result.items():
            if state == "all":
                continue
            slope, intercept = relationships[state]
            errors.setdefault(state, []).append(slope * result["all"] + intercept - swing)
    return {state: (float(np.sqrt(np.mean(np.square(values)))), max(3.0, one_tail_kurtosis(values)))
            for state, values in errors.items()}


def calibration_diagnostics(predictor, samples, uncertainty):
    """Expose test elections and the final reconciled variance account."""
    rows = []
    for sample in samples:
        a, b, c = uncertainty["scale"]
        general_sd = a * math.exp(-b * sample["horizon"]) + c
        sd = general_sd * np.sqrt(projected_variance(sample, uncertainty["modifiers"]))
        rows.append(dict(year=sample["year"], horizon=sample["horizon"], states=sample["states"],
                         errors_pp=sample["errors"].tolist(), reconciled_sd_pp=sd.tolist(),
                         training_years=sample["training_years"], mix=sample["mix"].tolist()))
    return dict(method="election_excluded_regional_v1", training_years=predictor["training_years"],
                state_coefficient_weight=STATE_COEFFICIENT_WEIGHT,
                variance_pool_elections=VARIANCE_POOL_ELECTIONS,
                mix=predictor["mix"].tolist(), scale=uncertainty["scale"].tolist(),
                modifiers=uncertainty["modifiers"], kurtosis=uncertainty["kurtosis"],
                raw_scales=uncertainty["raw_scales"], predictions=rows)


def calibrate_regions(evidence):
    """Outline the mean, excluded-election error and uncertainty stages."""
    predictor = fit_predictor(evidence)
    samples = prediction_errors(evidence)
    uncertainty = calibrate_uncertainty(samples)
    base_scales = base_uncertainty(evidence)
    diagnostics = calibration_diagnostics(predictor, samples, uncertainty)
    return dict(predictor=predictor, uncertainty=uncertainty,
                base_scales=base_scales, diagnostics=diagnostics)


def write_parameters(calibration, target_year, directory):
    """Preserve the four consumer CSV layouts; export only after fitting succeeds."""
    directory.mkdir(parents=True, exist_ok=True)
    predictor, uncertainty = calibration["predictor"], calibration["uncertainty"]
    base, poll = predictor["relationships"]["base"], predictor["relationships"]["poll"]
    base_rows = [[s, *coefs, *calibration["base_scales"][s]] for s, coefs in base.items() if s != "all"]
    # C++ ignores the aggregate base row and the poll residual columns. Zero
    # poll residual avoids presenting training error as predictive uncertainty;
    # operational uncertainty is recorded in the mix files instead.
    base_rows.append(["all", *base["all"], np.sqrt(np.mean([v[0] ** 2 for v in calibration["base_scales"].values()])), 3.0])
    tables = {
        "regions-base": base_rows,
        "regions-polled": [[s, *coefs, 0.0, uncertainty["kurtosis"]] for s, coefs in poll.items()],
        # Mean intercepts already express systematic state differences. Remove
        # the second, in-sample median-error correction rather than learning an
        # additional bias from the elections used to assess uncertainty.
        "mix-regions": [[s, 0.0, modifier] for s, modifier in sorted(uncertainty["modifiers"].items())],
        "mix-parameters": [["mix_factor", *predictor["mix"]], ["rmse", *uncertainty["scale"]],
                           ["kurtosis", 0.0, uncertainty["kurtosis"]]],
    }
    for suffix, rows in tables.items():
        with (directory / f"{target_year}fed-{suffix}.csv").open("w", newline="") as output:
            csv.writer(output).writerows(rows)
