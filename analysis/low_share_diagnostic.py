"""Standalone comparison of low-share inference parameterizations.

The raw approach reproduces the current unbounded Gaussian likelihood and
applies the existing exponential-tail transform only to reported output. The
bounded approaches place the same Gaussian prior and likelihood on modeled
share, including the transformation Jacobian, so their HMC geometry can be
compared without changing production code.

PyStan and ``stan_cache`` are imported only when the optional CLI samples the
companion Stan model.
"""

import argparse
from dataclasses import dataclass
from pathlib import Path
from time import perf_counter

import numpy as np


MODEL_PATH = Path(__file__).parent / "Models" / "low_share_diagnostic.stan"
APPROACH_CODES = {
    "raw": 1,
    "exponential": 2,
    "logit": 3,
}


@dataclass(frozen=True)
class HmcDiagnostics:
    """Compact HMC diagnostics for one parameterization."""

    checks: dict
    divergences: int
    maximum_treedepth_observed: int
    treedepth_saturations: int
    maximum_rhat: float
    minimum_effective_sample_size: float


@dataclass(frozen=True)
class ApproachResult:
    """Timing, posterior moments, and HMC diagnostics for one approach."""

    approach: str
    elapsed_seconds: float
    model_share_mean: float
    model_share_standard_deviation: float
    reported_share_mean: float
    reported_share_standard_deviation: float
    diagnostics: HmcDiagnostics


def _return_scalar_when_scalar(original, result):
    if np.asarray(original).ndim == 0:
        return float(np.asarray(result))
    return np.asarray(result)


def raw_share(latent_share):
    """Return the current unbounded share used by the likelihood."""

    values = np.asarray(latent_share, dtype=float)
    return _return_scalar_when_scalar(latent_share, values.copy())


def exponential_tail_share(latent_share):
    """Apply the existing identity-with-exponential-tails output transform."""

    values = np.asarray(latent_share, dtype=float)
    flat_values = values.reshape(-1)
    flat_transformed = flat_values.copy()
    lower = flat_values < 0.5
    upper = flat_values > 99.5
    flat_transformed[lower] = (
        0.5 * np.exp(flat_values[lower] - 0.5)
    )
    flat_transformed[upper] = (
        100.0 - 0.5 * np.exp(99.5 - flat_values[upper])
    )
    transformed = flat_transformed.reshape(values.shape)
    return _return_scalar_when_scalar(latent_share, transformed)


def exponential_tail_inverse(share):
    """Invert ``exponential_tail_share`` for shares strictly inside 0--100."""

    values = np.asarray(share, dtype=float)
    if (
        not np.all(np.isfinite(values))
        or np.any(values <= 0.0)
        or np.any(values >= 100.0)
    ):
        raise ValueError(
            "exponential-tail shares must be finite and strictly between 0 and 100."
        )
    latent = values.copy()
    lower = values < 0.5
    upper = values > 99.5
    latent = np.where(
        lower,
        np.log(values / 0.5) + 0.5,
        latent,
    )
    latent = np.where(
        upper,
        99.5 - np.log((100.0 - values) / 0.5),
        latent,
    )
    return _return_scalar_when_scalar(share, latent)


def smooth_logit_share(latent_share):
    """Map a real latent value smoothly onto a percentage in 0--100."""

    values = np.asarray(latent_share, dtype=float)
    flat_values = values.reshape(-1)
    flat_transformed = np.empty_like(flat_values)
    nonnegative = flat_values >= 0.0
    flat_transformed[nonnegative] = (
        100.0 / (1.0 + np.exp(-flat_values[nonnegative]))
    )
    exponentiated = np.exp(flat_values[~nonnegative])
    flat_transformed[~nonnegative] = (
        100.0 * exponentiated / (1.0 + exponentiated)
    )
    transformed = flat_transformed.reshape(values.shape)
    return _return_scalar_when_scalar(latent_share, transformed)


def smooth_logit_inverse(share):
    """Map a percentage strictly inside 0--100 onto the logit scale."""

    values = np.asarray(share, dtype=float)
    if (
        not np.all(np.isfinite(values))
        or np.any(values <= 0.0)
        or np.any(values >= 100.0)
    ):
        raise ValueError(
            "logit shares must be finite and strictly between 0 and 100."
        )
    latent = np.log(values) - np.log(100.0 - values)
    return _return_scalar_when_scalar(share, latent)


def exponential_tail_log_jacobian(latent_share):
    """Return log absolute derivative of the exponential-tail transform."""

    values = np.asarray(latent_share, dtype=float)
    result = np.zeros_like(values)
    result = np.where(
        values < 0.5,
        np.log(0.5) + values - 0.5,
        result,
    )
    result = np.where(
        values > 99.5,
        np.log(0.5) + 99.5 - values,
        result,
    )
    return _return_scalar_when_scalar(latent_share, result)


def smooth_logit_log_jacobian(latent_share):
    """Return log absolute derivative of the 0--100 logistic transform."""

    values = np.asarray(latent_share, dtype=float)
    result = (
        np.log(100.0)
        - np.logaddexp(0.0, -values)
        - np.logaddexp(0.0, values)
    )
    return _return_scalar_when_scalar(latent_share, result)


def transform_share(latent_share, approach):
    """Apply one named diagnostic share transform."""

    if approach == "raw":
        return raw_share(latent_share)
    if approach == "exponential":
        return exponential_tail_share(latent_share)
    if approach == "logit":
        return smooth_logit_share(latent_share)
    raise ValueError(
        "approach must be one of: raw, exponential, logit."
    )


def inverse_transform_share(share, approach):
    """Apply the inverse of one named diagnostic transform."""

    if approach == "raw":
        return raw_share(share)
    if approach == "exponential":
        return exponential_tail_inverse(share)
    if approach == "logit":
        return smooth_logit_inverse(share)
    raise ValueError(
        "approach must be one of: raw, exponential, logit."
    )


def _finite_float(value, name):
    try:
        result = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError("{} must be numeric.".format(name)) from error
    if not np.isfinite(result):
        raise ValueError("{} must be finite.".format(name))
    return result


def _poll_vectors(poll_values, poll_sigmas):
    try:
        values = np.asarray(poll_values, dtype=float)
    except (TypeError, ValueError) as error:
        raise ValueError("poll_values must contain numeric values.") from error
    if values.ndim != 1:
        raise ValueError("poll_values must be one-dimensional.")
    if (
        not np.all(np.isfinite(values))
        or np.any(values < 0.0)
        or np.any(values > 100.0)
    ):
        raise ValueError("poll_values must be finite shares from 0 to 100.")

    try:
        sigmas = np.asarray(poll_sigmas, dtype=float)
    except (TypeError, ValueError) as error:
        raise ValueError("poll_sigmas must contain numeric values.") from error
    if sigmas.ndim == 0:
        sigmas = np.full(values.size, float(sigmas))
    if sigmas.ndim != 1 or sigmas.size != values.size:
        raise ValueError(
            "poll_sigmas must be one scalar or match poll_values."
        )
    if not np.all(np.isfinite(sigmas)) or np.any(sigmas <= 0.0):
        raise ValueError("poll_sigmas must contain positive finite values.")
    return values, sigmas


def build_stan_data(
    approach,
    prior_mean,
    prior_sigma,
    poll_values,
    poll_sigmas,
):
    """Validate synthetic inputs and build data for the Stan comparison."""

    if approach not in APPROACH_CODES:
        raise ValueError(
            "approach must be one of: raw, exponential, logit."
        )
    mean = _finite_float(prior_mean, "prior_mean")
    sigma = _finite_float(prior_sigma, "prior_sigma")
    if mean < 0.0 or mean > 100.0:
        raise ValueError("prior_mean must be from 0 to 100.")
    if sigma <= 0.0:
        raise ValueError("prior_sigma must be positive.")
    values, sigmas = _poll_vectors(poll_values, poll_sigmas)
    return {
        "approach": APPROACH_CODES[approach],
        "priorMean": mean,
        "priorSigma": sigma,
        "pollCount": int(values.size),
        "pollValues": values.tolist(),
        "pollSigmas": sigmas.tolist(),
    }


def _load_stan_model():
    """Load the cached comparison model only when sampling is requested."""

    from stan_cache import stan_cache

    model_code = MODEL_PATH.read_text(encoding="utf-8")
    return stan_cache(model_code=model_code)


def _summary_extremes(fit):
    summary = fit.summary(
        pars=["latentShare", "modelShare", "reportedShare"],
        probs=(0.5,),
    )
    matrix = np.asarray(summary["summary"], dtype=float)
    columns = list(summary["summary_colnames"])
    rhat = matrix[:, columns.index("Rhat")]
    effective_sample_size = matrix[:, columns.index("n_eff")]
    finite_rhat = rhat[np.isfinite(rhat)]
    finite_ess = effective_sample_size[np.isfinite(effective_sample_size)]
    return (
        float(np.max(finite_rhat)) if finite_rhat.size else float("nan"),
        float(np.min(finite_ess)) if finite_ess.size else float("nan"),
    )


def _hmc_diagnostics(fit, max_treedepth):
    import pystan.diagnostics as stan_diagnostics

    checks = {
        name: bool(passed)
        for name, passed in stan_diagnostics.check_hmc_diagnostics(fit).items()
    }
    sampler_parameters = fit.get_sampler_params(inc_warmup=False)
    divergences = sum(
        int(np.sum(chain.get("divergent__", 0)))
        for chain in sampler_parameters
    )
    treedepths = [
        np.asarray(chain.get("treedepth__", []), dtype=int)
        for chain in sampler_parameters
    ]
    nonempty_treedepths = [
        values for values in treedepths if values.size
    ]
    if nonempty_treedepths:
        all_treedepths = np.concatenate(nonempty_treedepths)
        maximum_observed = int(np.max(all_treedepths))
        saturations = int(np.sum(all_treedepths >= max_treedepth))
    else:
        maximum_observed = 0
        saturations = 0
    maximum_rhat, minimum_ess = _summary_extremes(fit)
    return HmcDiagnostics(
        checks=checks,
        divergences=divergences,
        maximum_treedepth_observed=maximum_observed,
        treedepth_saturations=saturations,
        maximum_rhat=maximum_rhat,
        minimum_effective_sample_size=minimum_ess,
    )


def run_approach(
    stan_model,
    approach,
    prior_mean,
    prior_sigma,
    poll_values,
    poll_sigmas,
    iterations=1000,
    chains=4,
    seed=20260803,
    adapt_delta=0.9,
    max_treedepth=12,
):
    """Sample and summarize one low-share parameterization."""

    data = build_stan_data(
        approach=approach,
        prior_mean=prior_mean,
        prior_sigma=prior_sigma,
        poll_values=poll_values,
        poll_sigmas=poll_sigmas,
    )
    start = perf_counter()
    fit = stan_model.sampling(
        data=data,
        iter=iterations,
        chains=chains,
        seed=seed,
        control={
            "adapt_delta": adapt_delta,
            "max_treedepth": max_treedepth,
        },
    )
    elapsed_seconds = perf_counter() - start
    extracted = fit.extract(
        pars=["modelShare", "reportedShare"],
        permuted=True,
    )
    model_share = np.asarray(extracted["modelShare"], dtype=float)
    reported_share = np.asarray(extracted["reportedShare"], dtype=float)
    return ApproachResult(
        approach=approach,
        elapsed_seconds=elapsed_seconds,
        model_share_mean=float(np.mean(model_share)),
        model_share_standard_deviation=float(
            np.std(model_share, ddof=1)
        ),
        reported_share_mean=float(np.mean(reported_share)),
        reported_share_standard_deviation=float(
            np.std(reported_share, ddof=1)
        ),
        diagnostics=_hmc_diagnostics(fit, max_treedepth),
    )


def _comma_separated_floats(value):
    try:
        values = [
            float(item.strip())
            for item in value.split(",")
            if item.strip()
        ]
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "expected comma-separated numbers"
        ) from error
    if not values or not all(np.isfinite(values)):
        raise argparse.ArgumentTypeError(
            "expected at least one finite number"
        )
    return values


def build_argument_parser():
    parser = argparse.ArgumentParser(
        description="Compare low-share Stan parameterizations.",
    )
    parser.add_argument(
        "--poll-values",
        type=_comma_separated_floats,
        default=[0.05, 0.10, 0.20, 0.40],
    )
    parser.add_argument(
        "--poll-sigmas",
        type=_comma_separated_floats,
        default=[1.5],
        help="one sigma or one per poll",
    )
    parser.add_argument("--prior-mean", type=float, default=0.25)
    parser.add_argument("--prior-sigma", type=float, default=2.0)
    parser.add_argument("--iterations", type=int, default=1000)
    parser.add_argument("--chains", type=int, default=4)
    parser.add_argument("--seed", type=int, default=20260803)
    parser.add_argument("--adapt-delta", type=float, default=0.9)
    parser.add_argument("--max-treedepth", type=int, default=12)
    return parser


def main(argv=None):
    args = build_argument_parser().parse_args(argv)
    poll_sigmas = args.poll_sigmas
    if len(poll_sigmas) == 1:
        poll_sigmas = poll_sigmas[0]

    stan_model = _load_stan_model()
    for approach in APPROACH_CODES:
        result = run_approach(
            stan_model=stan_model,
            approach=approach,
            prior_mean=args.prior_mean,
            prior_sigma=args.prior_sigma,
            poll_values=args.poll_values,
            poll_sigmas=poll_sigmas,
            iterations=args.iterations,
            chains=args.chains,
            seed=args.seed,
            adapt_delta=args.adapt_delta,
            max_treedepth=args.max_treedepth,
        )
        diagnostics = result.diagnostics
        failed = sorted(
            name for name, passed in diagnostics.checks.items()
            if not passed
        )
        print(
            "{:11s} time={:.2f}s model_share={:.6f}+/-{:.6f} "
            "reported_share={:.6f}+/-{:.6f}".format(
                result.approach,
                result.elapsed_seconds,
                result.model_share_mean,
                result.model_share_standard_deviation,
                result.reported_share_mean,
                result.reported_share_standard_deviation,
            )
        )
        print(
            "  HMC={} divergences={} treedepth_max={} "
            "treedepth_saturations={} max_Rhat={:.4f} min_ESS={:.1f}".format(
                ",".join(failed) if failed else "pass",
                diagnostics.divergences,
                diagnostics.maximum_treedepth_observed,
                diagnostics.treedepth_saturations,
                diagnostics.maximum_rhat,
                diagnostics.minimum_effective_sample_size,
            )
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
