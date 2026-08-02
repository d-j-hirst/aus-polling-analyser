"""Standalone diagnostics for the Gaussian daily-prior chain.

The exact solver is independent of Stan and has no import-time side effects.
Use ``--stan`` to load PyStan lazily, sample the companion model, and compare
its posterior moments with the exact Gaussian result.
"""

import argparse
from dataclasses import dataclass
from pathlib import Path
from time import perf_counter

import numpy as np


MODEL_PATH = Path(__file__).parent / "Models" / "prior_chain_diagnostic.stan"


@dataclass(frozen=True)
class ExactPosterior:
    """Mean and covariance of a Gaussian day-chain posterior."""

    mean: np.ndarray
    covariance: np.ndarray

    @property
    def standard_deviation(self):
        return np.sqrt(np.diag(self.covariance))


@dataclass(frozen=True)
class ChainScenario:
    """Configuration for one prior-chain comparison."""

    chain_length: int
    poll_position: str
    prior_mean: float
    prior_sigma: float
    transition_sigma: float
    poll_offset: float
    poll_sigma: float

    @property
    def poll_day(self):
        return poll_day_for_position(
            self.chain_length,
            self.poll_position,
        )

    def stan_data(self):
        poll_days = [] if self.poll_day is None else [self.poll_day]
        poll_values = (
            [] if self.poll_day is None
            else [self.prior_mean + self.poll_offset]
        )
        poll_sigmas = [] if self.poll_day is None else [self.poll_sigma]
        return {
            "dayCount": self.chain_length,
            "priorMeans": [self.prior_mean] * self.chain_length,
            "priorSigmas": [self.prior_sigma] * self.chain_length,
            "transitionSigmas": (
                [self.transition_sigma] * (self.chain_length - 1)
            ),
            "pollCount": len(poll_days),
            "pollDays": poll_days,
            "pollValues": poll_values,
            "pollSigmas": poll_sigmas,
        }


@dataclass(frozen=True)
class StanComparison:
    """Sampled moments and their errors relative to the exact result."""

    sampled_mean: np.ndarray
    sampled_standard_deviation: np.ndarray
    maximum_mean_error: float
    maximum_standard_deviation_error: float
    elapsed_seconds: float
    hmc_checks: dict


def _numeric_vector(values, name, expected_length=None, allow_empty=False):
    """Return a finite one-dimensional float vector."""

    try:
        vector = np.asarray(values, dtype=float)
    except (TypeError, ValueError) as error:
        raise ValueError("{} must contain numeric values.".format(name)) from error
    if vector.ndim != 1:
        raise ValueError("{} must be one-dimensional.".format(name))
    if not allow_empty and vector.size == 0:
        raise ValueError("{} must not be empty.".format(name))
    if expected_length is not None and vector.size != expected_length:
        raise ValueError(
            "{} must contain {} value(s).".format(name, expected_length)
        )
    if not np.all(np.isfinite(vector)):
        raise ValueError("{} must contain only finite values.".format(name))
    return vector


def _positive_vector(
    values, name, expected_length, allow_scalar=False, allow_empty=False
):
    """Return a finite vector whose entries are all strictly positive."""

    raw = np.asarray(values, dtype=float)
    if allow_scalar and raw.ndim == 0:
        vector = np.full(expected_length, float(raw))
    else:
        vector = _numeric_vector(
            values,
            name,
            expected_length=expected_length,
            allow_empty=allow_empty,
        )
    if vector.size and (not np.all(np.isfinite(vector)) or np.any(vector <= 0)):
        raise ValueError("{} must contain only positive finite values.".format(name))
    return vector


def _poll_day_vector(poll_days, day_count):
    """Validate one-based poll-day indices shared with the Stan model."""

    numeric = _numeric_vector(
        poll_days,
        "poll_days",
        allow_empty=True,
    )
    if np.any(numeric != np.floor(numeric)):
        raise ValueError("poll_days must contain integer day numbers.")
    days = numeric.astype(int)
    if np.any(days < 1) or np.any(days > day_count):
        raise ValueError(
            "poll_days must be between 1 and day_count, inclusive."
        )
    return days


def exact_gaussian_posterior(
    prior_means,
    prior_sigmas,
    transition_sigmas,
    poll_days=(),
    poll_values=(),
    poll_sigmas=(),
):
    """Solve the Gaussian chain posterior exactly.

    ``poll_days`` uses one-based indices to match Stan. ``transition_sigmas``
    may be one positive scalar or one value for every adjacent-day link.
    """

    means = _numeric_vector(prior_means, "prior_means")
    day_count = means.size
    prior_scale = _positive_vector(
        prior_sigmas,
        "prior_sigmas",
        expected_length=day_count,
        allow_scalar=True,
    )
    transition_scale = _positive_vector(
        transition_sigmas,
        "transition_sigmas",
        expected_length=max(0, day_count - 1),
        allow_scalar=True,
        allow_empty=day_count == 1,
    )
    days = _poll_day_vector(poll_days, day_count)
    values = _numeric_vector(
        poll_values,
        "poll_values",
        expected_length=days.size,
        allow_empty=True,
    )
    poll_scale = _positive_vector(
        poll_sigmas,
        "poll_sigmas",
        expected_length=days.size,
        allow_scalar=days.size > 0,
        allow_empty=days.size == 0,
    )

    prior_precision = 1.0 / np.square(prior_scale)
    precision = np.diag(prior_precision)
    information = prior_precision * means

    for day, sigma in enumerate(transition_scale):
        link_precision = 1.0 / sigma ** 2
        precision[day, day] += link_precision
        precision[day + 1, day + 1] += link_precision
        precision[day, day + 1] -= link_precision
        precision[day + 1, day] -= link_precision

    for poll_day, value, sigma in zip(days, values, poll_scale):
        day = poll_day - 1
        observation_precision = 1.0 / sigma ** 2
        precision[day, day] += observation_precision
        information[day] += observation_precision * value

    posterior_mean = np.linalg.solve(precision, information)
    posterior_covariance = np.linalg.solve(
        precision,
        np.eye(day_count),
    )
    posterior_covariance = (
        posterior_covariance + posterior_covariance.T
    ) * 0.5
    return ExactPosterior(
        mean=posterior_mean,
        covariance=posterior_covariance,
    )


def poll_day_for_position(chain_length, position):
    """Return a one-based endpoint/interior poll day."""

    if chain_length < 2:
        raise ValueError("chain_length must be at least 2.")
    if position == "none":
        return None
    if position == "first":
        return 1
    if position == "middle":
        return (chain_length + 1) // 2
    if position == "last":
        return chain_length
    raise ValueError(
        "poll position must be one of: none, first, middle, last."
    )


def make_scenario(
    chain_length,
    poll_position,
    prior_mean=0.0,
    prior_sigma=16.0,
    transition_sigma=0.25,
    poll_offset=5.0,
    poll_sigma=1.5,
):
    """Create one homogeneous chain with an optional synthetic poll."""

    scenario = ChainScenario(
        chain_length=int(chain_length),
        poll_position=poll_position,
        prior_mean=float(prior_mean),
        prior_sigma=float(prior_sigma),
        transition_sigma=float(transition_sigma),
        poll_offset=float(poll_offset),
        poll_sigma=float(poll_sigma),
    )
    scenario.poll_day
    if not all(np.isfinite([
            scenario.prior_mean,
            scenario.prior_sigma,
            scenario.transition_sigma,
            scenario.poll_offset,
            scenario.poll_sigma,
    ])):
        raise ValueError("scenario values must be finite.")
    if (
        scenario.prior_sigma <= 0
        or scenario.transition_sigma <= 0
        or scenario.poll_sigma <= 0
    ):
        raise ValueError("scenario sigmas must be positive.")
    return scenario


def solve_scenario(scenario):
    """Return the exact posterior for one ``ChainScenario``."""

    data = scenario.stan_data()
    return exact_gaussian_posterior(
        prior_means=data["priorMeans"],
        prior_sigmas=data["priorSigmas"],
        transition_sigmas=data["transitionSigmas"],
        poll_days=data["pollDays"],
        poll_values=data["pollValues"],
        poll_sigmas=data["pollSigmas"],
    )


def boundary_interior_metrics(posterior):
    """Summarize endpoint uncertainty relative to the chain interior."""

    day_count = posterior.mean.size
    if day_count < 2:
        raise ValueError("boundary metrics require at least two days.")
    middle = (day_count - 1) // 2
    standard_deviation = posterior.standard_deviation
    interior_sd = float(standard_deviation[middle])
    return {
        "left_boundary_sd": float(standard_deviation[0]),
        "interior_sd": interior_sd,
        "right_boundary_sd": float(standard_deviation[-1]),
        "left_to_interior_sd": float(standard_deviation[0] / interior_sd),
        "right_to_interior_sd": float(standard_deviation[-1] / interior_sd),
        "interior_mean": float(posterior.mean[middle]),
    }


def distance_profile(posterior):
    """Return posterior moments and distance from both ends for every day."""

    day_count = posterior.mean.size
    standard_deviation = posterior.standard_deviation
    return [
        {
            "day": day + 1,
            "distance_from_left": day,
            "distance_from_right": day_count - day - 1,
            "mean": float(posterior.mean[day]),
            "standard_deviation": float(standard_deviation[day]),
        }
        for day in range(day_count)
    ]


def infinite_chain_interior_standard_deviation(
    prior_sigma,
    transition_sigma,
):
    """Return the exact homogeneous infinite-chain interior marginal SD."""

    prior_sigma = float(prior_sigma)
    transition_sigma = float(transition_sigma)
    if (
        not np.isfinite(prior_sigma)
        or not np.isfinite(transition_sigma)
        or prior_sigma <= 0
        or transition_sigma <= 0
    ):
        raise ValueError("prior and transition sigmas must be positive.")
    prior_precision = 1.0 / prior_sigma ** 2
    transition_precision = 1.0 / transition_sigma ** 2
    interior_variance = 1.0 / np.sqrt(
        prior_precision ** 2
        + 4.0 * prior_precision * transition_precision
    )
    return float(np.sqrt(interior_variance))


def _homogeneous_chain_centre_variance(
    chain_length,
    prior_sigma,
    transition_sigma,
):
    """Solve one tridiagonal precision column in linear time."""

    if chain_length < 2:
        raise ValueError("chain_length must be at least 2.")
    prior_precision = 1.0 / float(prior_sigma) ** 2
    transition_precision = 1.0 / float(transition_sigma) ** 2
    diagonal = np.full(
        chain_length,
        prior_precision + 2.0 * transition_precision,
    )
    diagonal[[0, -1]] = prior_precision + transition_precision
    off_diagonal = np.full(chain_length - 1, -transition_precision)
    right_hand_side = np.zeros(chain_length)
    centre = (chain_length - 1) // 2
    right_hand_side[centre] = 1.0

    # Thomas algorithm for the centre column of the inverse precision matrix.
    upper = off_diagonal.copy()
    rhs = right_hand_side.copy()
    for row in range(1, chain_length):
        multiplier = off_diagonal[row - 1] / diagonal[row - 1]
        diagonal[row] -= multiplier * upper[row - 1]
        rhs[row] -= multiplier * rhs[row - 1]
    solution = np.empty(chain_length)
    solution[-1] = rhs[-1] / diagonal[-1]
    for row in range(chain_length - 2, -1, -1):
        solution[row] = (
            rhs[row] - upper[row] * solution[row + 1]
        ) / diagonal[row]
    return float(solution[centre])


def chain_length_to_asymptotic_interior(
    prior_sigma,
    transition_sigma,
    relative_tolerance=0.01,
    maximum_length=10001,
):
    """Return the first odd chain whose centre SD is within a limit."""

    if not 0 < relative_tolerance < 1:
        raise ValueError("relative_tolerance must be between zero and one.")
    limit = infinite_chain_interior_standard_deviation(
        prior_sigma, transition_sigma
    )
    for chain_length in range(3, maximum_length + 1, 2):
        centre_sd = np.sqrt(_homogeneous_chain_centre_variance(
            chain_length,
            prior_sigma,
            transition_sigma,
        ))
        relative_difference = abs(centre_sd / limit - 1.0)
        if relative_difference <= relative_tolerance:
            return {
                "chain_length": chain_length,
                "centre_standard_deviation": float(centre_sd),
                "infinite_chain_standard_deviation": limit,
                "relative_difference": float(relative_difference),
            }
    raise ValueError(
        "centre did not reach the requested asymptotic tolerance by {} days."
        .format(maximum_length)
    )


def boundary_convergence(
    chain_lengths,
    prior_mean=0.0,
    prior_sigma=16.0,
    transition_sigma=0.25,
):
    """Report prior-only boundary/interior behavior by chain length."""

    lengths = sorted(set(int(length) for length in chain_lengths))
    if not lengths:
        raise ValueError("at least one chain length is required.")
    rows = []
    for chain_length in lengths:
        scenario = make_scenario(
            chain_length=chain_length,
            poll_position="none",
            prior_mean=prior_mean,
            prior_sigma=prior_sigma,
            transition_sigma=transition_sigma,
        )
        metrics = boundary_interior_metrics(solve_scenario(scenario))
        metrics["chain_length"] = chain_length
        rows.append(metrics)

    reference_sd = rows[-1]["interior_sd"]
    for row in rows:
        row["interior_sd_difference_from_longest"] = (
            row["interior_sd"] - reference_sd
        )
        row["interior_sd_relative_difference_from_longest"] = (
            row["interior_sd"] / reference_sd - 1.0
        )
    return rows


def _load_stan_model():
    """Load the cached model, importing PyStan infrastructure only on demand."""

    from stan_cache import stan_cache

    model_code = MODEL_PATH.read_text(encoding="utf-8")
    return stan_cache(model_code=model_code)


def compare_stan_with_exact(
    stan_model,
    scenario,
    iterations=800,
    chains=4,
    seed=20260803,
    adapt_delta=0.9,
    max_treedepth=12,
):
    """Sample one scenario and compare its moments with the exact solution."""

    exact = solve_scenario(scenario)
    start = perf_counter()
    fit = stan_model.sampling(
        data=scenario.stan_data(),
        iter=iterations,
        chains=chains,
        seed=seed,
        control={
            "adapt_delta": adapt_delta,
            "max_treedepth": max_treedepth,
        },
    )
    elapsed_seconds = perf_counter() - start
    samples = np.asarray(
        fit.extract(
            pars=["latentDaySeries"],
            permuted=True,
        )["latentDaySeries"],
        dtype=float,
    )
    sampled_mean = np.mean(samples, axis=0)
    sampled_sd = np.std(samples, axis=0, ddof=1)

    import pystan.diagnostics as stan_diagnostics

    hmc_checks = {
        name: bool(passed)
        for name, passed in stan_diagnostics.check_hmc_diagnostics(fit).items()
    }
    return StanComparison(
        sampled_mean=sampled_mean,
        sampled_standard_deviation=sampled_sd,
        maximum_mean_error=float(
            np.max(np.abs(sampled_mean - exact.mean))
        ),
        maximum_standard_deviation_error=float(
            np.max(np.abs(sampled_sd - exact.standard_deviation))
        ),
        elapsed_seconds=elapsed_seconds,
        hmc_checks=hmc_checks,
    )


def _comma_separated_ints(value):
    try:
        values = [int(item.strip()) for item in value.split(",") if item.strip()]
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            "expected comma-separated integers"
        ) from error
    if not values or any(item < 2 for item in values):
        raise argparse.ArgumentTypeError(
            "chain lengths must be comma-separated integers of at least 2"
        )
    return values


def _comma_separated_positions(value):
    positions = [item.strip() for item in value.split(",") if item.strip()]
    valid = {"none", "first", "middle", "last"}
    if not positions or any(item not in valid for item in positions):
        raise argparse.ArgumentTypeError(
            "poll positions must come from: none, first, middle, last"
        )
    return positions


def build_argument_parser():
    parser = argparse.ArgumentParser(
        description="Diagnose Gaussian prior-chain boundary behavior.",
    )
    parser.add_argument(
        "--chain-lengths",
        type=_comma_separated_ints,
        default=[15, 31, 91, 181],
    )
    parser.add_argument(
        "--poll-positions",
        type=_comma_separated_positions,
        default=["first", "middle", "last"],
    )
    parser.add_argument("--prior-mean", type=float, default=0.0)
    parser.add_argument("--prior-sigma", type=float, default=16.0)
    parser.add_argument("--transition-sigma", type=float, default=0.25)
    parser.add_argument("--poll-offset", type=float, default=5.0)
    parser.add_argument("--poll-sigma", type=float, default=1.5)
    parser.add_argument(
        "--stan",
        action="store_true",
        help="also compile/sample the Stan model",
    )
    parser.add_argument("--iterations", type=int, default=800)
    parser.add_argument("--chains", type=int, default=4)
    parser.add_argument("--seed", type=int, default=20260803)
    parser.add_argument("--adapt-delta", type=float, default=0.9)
    parser.add_argument("--max-treedepth", type=int, default=12)
    parser.add_argument(
        "--asymptotic-tolerance",
        type=float,
        default=0.01,
        help="relative centre-SD tolerance for the infinite-chain limit",
    )
    parser.add_argument(
        "--show-distance-profile",
        action="store_true",
        help="print posterior mean/SD for each day and distance from both ends",
    )
    return parser


def main(argv=None):
    args = build_argument_parser().parse_args(argv)

    print("Prior-only boundary/interior convergence")
    asymptotic = chain_length_to_asymptotic_interior(
        args.prior_sigma,
        args.transition_sigma,
        args.asymptotic_tolerance,
    )
    print(
        "infinite interior SD={:.6f}; first odd length within {:.2%}: "
        "{} days (centre SD={:.6f}, difference={:.3%})".format(
            asymptotic["infinite_chain_standard_deviation"],
            args.asymptotic_tolerance,
            asymptotic["chain_length"],
            asymptotic["centre_standard_deviation"],
            asymptotic["relative_difference"],
        )
    )
    for row in boundary_convergence(
            args.chain_lengths,
            prior_mean=args.prior_mean,
            prior_sigma=args.prior_sigma,
            transition_sigma=args.transition_sigma):
        print(
            "days={chain_length:4d} left/interior={left_to_interior_sd:.4f} "
            "right/interior={right_to_interior_sd:.4f} interior_sd={interior_sd:.6f} "
            "relative_to_longest={interior_sd_relative_difference_from_longest:+.3%}"
            .format(**row)
        )

    stan_model = _load_stan_model() if args.stan else None
    print("Synthetic-poll scenarios")
    for chain_length in args.chain_lengths:
        for position in args.poll_positions:
            scenario = make_scenario(
                chain_length=chain_length,
                poll_position=position,
                prior_mean=args.prior_mean,
                prior_sigma=args.prior_sigma,
                transition_sigma=args.transition_sigma,
                poll_offset=args.poll_offset,
                poll_sigma=args.poll_sigma,
            )
            exact = solve_scenario(scenario)
            metrics = boundary_interior_metrics(exact)
            print(
                "days={:4d} poll={:6s} interior_mean={:+.6f} "
                "left/interior_sd={:.4f} right/interior_sd={:.4f}".format(
                    chain_length,
                    position,
                    metrics["interior_mean"],
                    metrics["left_to_interior_sd"],
                    metrics["right_to_interior_sd"],
                )
            )
            if args.show_distance_profile:
                for row in distance_profile(exact):
                    print(
                        "  day={day:4d} left={distance_from_left:4d} "
                        "right={distance_from_right:4d} mean={mean:+.6f} "
                        "sd={standard_deviation:.6f}".format(**row)
                    )
            if stan_model is not None:
                comparison = compare_stan_with_exact(
                    stan_model,
                    scenario,
                    iterations=args.iterations,
                    chains=args.chains,
                    seed=args.seed,
                    adapt_delta=args.adapt_delta,
                    max_treedepth=args.max_treedepth,
                )
                failed = sorted(
                    name
                    for name, passed in comparison.hmc_checks.items()
                    if not passed
                )
                print(
                    "  Stan {:.2f}s max_mean_error={:.6f} "
                    "max_sd_error={:.6f} HMC={}".format(
                        comparison.elapsed_seconds,
                        comparison.maximum_mean_error,
                        comparison.maximum_standard_deviation_error,
                        ",".join(failed) if failed else "pass",
                    )
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
