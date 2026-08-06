"""Shared constants and deferred calibration notes for fp_model."""

from pathlib import Path

# File paths for polling data in each jurisdiction
data_source = {
    'fed': './Data/poll-data-fed.csv',
    'nsw': './Data/poll-data-nsw.csv',
    'vic': './Data/poll-data-vic.csv',
    'qld': './Data/poll-data-qld.csv',
    'wa': './Data/poll-data-wa.csv',
    'sa': './Data/poll-data-sa.csv',
}
CALIBRATION_PRIOR_DIRECTORY = Path('./Outputs/Calibration/Priors')
CAMPAIGN_WINDOW_DAYS = 42
FINAL_WINDOW_DAYS = 14
DEFAULT_BASE_SEED = 20260803
STAN_SEED_NAMESPACE = 'fp-model-v1'

# N.B. The "Others" (OTH) "party" values include votes for these other
# minor parties, so these are effectively counted twice. The reason for
# this is that many polls do not report separate UAP/ONP figures, so they
# are aggregated from the polls that do, count them together with the
# other "others" under OTH, and then (in the main program) subtract the
# minor parties from the OTH value to get the true exclusive-others value
others_parties = ['ONP FP', 'UAP FP', 'SFF FP', 'CA FP',
                'KAP FP', 'SAB FP', 'DEM FP', 'FF FP',
                'DLP FP']

major_parties = ['ALP FP', 'LNP FP', 'LIB FP']

unnamed_others_base = 3.0
unnamed_others_diagnostic_threshold = 1.0
unnamed_others_diagnostic_limit = 10

FP_MODEL_MODULE_PATHS = (
    'fp_model.py',
    'fp_model_constants.py',
    'fp_model_data.py',
    'fp_model_prepare.py',
    'fp_model_stan.py',
    'fp_model_outputs.py',
    'fp_model_runner.py',
)


def fp_model_source_files():
    """Return paths to the core fp_model modules fingerprinted with Stan fits.

    Checkpoint and trend-provenance helpers are tracked separately via
    ``calibration_checkpoint_source_files`` / provenance recorders.
    """

    base = Path(__file__).resolve().parent
    return [base / path for path in FP_MODEL_MODULE_PATHS]


# DEFERRED_NEXT_CALIBRATION
# Planned methodological revisions for a future calibration cycle, in rough
# dependency order:
#
# 1. Variable-prior endpoint boundary correction
#    Align state/federal prior endpoints with the actual poll-trend end date
#    used at each cutoff or final fit so variable priors do not extrapolate
#    beyond the information set that produced them.
#
# 2. Bounded inference reparameterization
#    Reparameterise vote-share states on a bounded scale in Stan so extreme
#    posteriors near 0/100 do not distort HMC geometry; keep exported medians
#    on the existing percentage scale for downstream compatibility.
#
# 3. MAE / full-fit reducer replacement
#    Replace the legacy quotient-and-neighbours weighting in finalise_calibrations
#    with a pollster-matched MAE reducer that compares leave-one-out residuals
#    against the full fit on identical poll days only.
#
# 4. Pollster-matched federal priors
#    When state calibration consumes federal priors, match the excluded
#    pollster (or full-fit) variant rather than always using the unexcluded
#    federal median series.
#
# 5. Iteration / chain tuning
#    Revisit desired-iterations.csv defaults and per-mode chain counts once
#    the reparameterization stabilises divergences without widening adapt_delta.
#
