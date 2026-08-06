"""Party preparation and Stan input construction for fp_model."""

import math
import numpy as np
import pandas as pd
from dataclasses import dataclass
from datetime import timedelta
from typing import List, Optional, Tuple

from fp_model_constants import (
    CAMPAIGN_WINDOW_DAYS,
    FINAL_WINDOW_DAYS,
    major_parties,
    others_parties,
)
from fp_model_data import (
    Config,
    ConfigError,
    ElectionData,
    HouseEffects,
    ModellingData,
    ModelParams,
    PollVectors,
    PriorSeries,
    ReducedSeries,
    RunContext,
    compressed_day_number,
    compressed_calendar_offset,
    transition_entering_calendar_offset,
)

def order_parties_for_model(parties):
    """Return the dependency order required by the sequential party fits."""

    if len(parties) != len(set(parties)):
        raise ConfigError(
            'Significant-party configuration contains duplicate parties: {}'
            .format(', '.join(parties))
        )

    median_inputs = set(others_parties + ['GRN FP', 'NAT FP'])

    def dependency_rank(party):
        if party in median_inputs:
            return 0
        if party == 'OTH FP':
            return 1
        if party in major_parties:
            return 2
        if party == '@TPP':
            return 3
        return 0

    # sorted() is stable, preserving the configured order within each stage.
    return sorted(parties, key=dependency_rank)


@dataclass
class PartyContext:
    config: Config
    e_data: ElectionData
    excluded_pollster: str
    m_data: ModellingData
    model_params: ModelParams
    party: str


@dataclass
class PollPrepResult:
    df: pd.DataFrame
    exc_polls: pd.DataFrame


@dataclass
class OutputContext:
    config: Config
    e_data: ElectionData
    excluded_pollster: str
    party: str
    poll_prep_result: PollPrepResult
    random_seed: int
    run_context: RunContext


@dataclass
class TrendOutputs:
    day_data: List[List[float]]
    final_median: float


def prepare_poll_df(party_context: PartyContext) -> Optional[PollPrepResult]:
    e_data = party_context.e_data
    party = party_context.party
    excluded_pollster = party_context.excluded_pollster
    config = party_context.config

    df = party_context.e_data.base_df.copy()

    # drop any rows with N/A values for the current party
    df = df.dropna(subset=[party])

    # If we're not excluding any pollster then we want to record
    # calibration stats for all pollsters (so that they may be
    # compared to those with pollsters excluded)
    if excluded_pollster != '':
        exc_polls = df[df.Firm == excluded_pollster]
        if exc_polls.empty:
            return None
    elif config.calibrate_pollsters:
        exc_polls = df
    else:
        # Make sure we have an empty dataframe with the right columns
        # to avoid errors but still skip the calibration process later on
        exc_polls = df.iloc[0:0]

    # if we're excluding a pollster for calibrations
    # remove their polls now
    df = df[df.Firm != excluded_pollster]
    n_polls = len(df)
    # It's possible for there to actually be no polls at all if
    # the party hasn't been polled before the cutoff date
    if n_polls == 0:
        return None
    
    return PollPrepResult(df=df, exc_polls=exc_polls)


def get_prior_result(party_context: PartyContext) -> float:
    m_data = party_context.m_data
    e_data = party_context.e_data
    party = party_context.party
    model_params = party_context.model_params

    # Get the prior result, or a small vote share if
    # the prior result is not given
    if (e_data.e_tuple, party) in m_data.prior_results:
        prior_result = max(model_params.prior_min_result, m_data.prior_results[(e_data.e_tuple, party)])
    elif party == '@TPP':
        prior_result = model_params.prior_tpp_default  # placeholder TPP
    else:
        prior_result = model_params.prior_min_result  # percentage

    return prior_result


@dataclass
class PollVectorInputs:
    df: pd.DataFrame
    party_context: PartyContext
    prior_result: float


def build_poll_vectors(inputs: PollVectorInputs) -> PollVectors:
    party_context = inputs.party_context
    model_params = party_context.model_params
    df = inputs.df
    party = party_context.party
    config = party_context.config
    e_data = party_context.e_data

    # Get a series for any missing data
    missing = df[party].apply(lambda x: 1 if np.isnan(x) else 0)
    y = df[party].fillna(inputs.prior_result)
    y = y.apply(lambda x: max(x, model_params.min_observation))

    # We are excluding some houses
    # from the sum to zero constraint because
    # they have unusual or infrequent poll results compared
    # with other pollsters
    # Organise the polling houses so that the pollsters
    # included in the sum-to-zero are first, and then the
    # others follow
    houses = df['Firm'].unique().tolist()
    house_map = dict(zip(houses, range(1, len(houses)+1)))
    df['House'] = df['Firm'].map(house_map)
    n_houses = len(df['House'].unique())

    # Have a standard sigma for calibrating pollsters,
    # otherwise used the observed sigmas
    sample_size = model_params.calibration_sample_size
    calibration_sigma = np.sqrt((50 * 50) / (sample_size))
    sigmas = df['Firm'].apply(
        lambda x: calibration_sigma if (
            config.calibrate_pollsters or config.calibrate_bias
        ) else
        e_data.pollster_sigmas[(x, party)] if
        (x, party) in e_data.pollster_sigmas else model_params.default_poll_sigma
    )

    # convert columns to list
    pollObs = y.values.tolist()
    missingObs = missing.values.tolist()
    pollHouses = df['House'].values.tolist()
    pollDays = [int(a) for a in df['DayNum'].values]
    sigmasList = sigmas.values.tolist()

    return PollVectors(
        pollObs=pollObs,
        missingObs=missingObs,
        pollHouses=pollHouses,
        pollDays=pollDays,
        sigmasList=sigmasList,
        houses=houses,
        n_houses=n_houses,
        n_polls=len(pollObs),
    )


def build_prior_series(party_context: PartyContext, prior_result: float) -> PriorSeries:
    # When federal polls for a minor party are rapidly changing but state polls are
    # sparse/nonexistent/unreliable, we want to use the federal trends to establish a prior
    # rather than the one derived from the previous election. This function creates a
    # series for each day in the state period, using the greater of the federal trend
    # and the expected value based on the state prior result.
    e_data = party_context.e_data
    party = party_context.party
    model_params = party_context.model_params

    # Build daily series for full state period
    days = pd.date_range(e_data.start, e_data.end, freq='D')
    fed_series = e_data.fed_trends_aligned.get(party)
    prior_daily = []
    sigma_daily = []

    for day in days:
        fed_val = None if fed_series is None else fed_series.get(day, None)
        # Maximum of federal trend (if available) and state prior result is used
        # A low federal trend shouldn't drag down a party that was strong in a state
        # and a low state trend is often a result of sparse polling for that particular party
        if fed_val is None or fed_val < prior_result:
            prior_daily.append(prior_result)
            sigma_daily.append(model_params.prior_sigma_no_fed)
        else:
            prior_daily.append(fed_val)
            # Parameters estimated as a best guess from limited scenarios;
            # federal OTH is a less reliable indicator of state OTH,
            # but we still need it because it includes other "minor" parties that
            # will have their prior series calculated from the federal trends.
            sigma_daily.append(
                model_params.prior_sigma_fed_oth
                if party == 'OTH FP'
                else model_params.prior_sigma_fed
            )

    return PriorSeries(prior_series_daily=prior_daily, sigma_daily=sigma_daily)


def should_use_approvals(party_context: PartyContext) -> bool:
    config = party_context.config
    party = party_context.party
    # We only use (government leader) approvals for the TPP and major parties
    # there's no useful connection between the leader ratings and minor parties' vote shares
    return config.use_approvals() and (party == "@TPP" or party in major_parties)


def load_approvals(
    party_context: PartyContext
) -> List[Tuple[pd.Timestamp, float, float]]:
    config = party_context.config
    e_data = party_context.e_data
    if config.synthetic_tpps_by_region is not None:
        return [
            (pd.Timestamp(date), float(tpp), float(weight))
            for date, pollster, tpp, weight
            in config.synthetic_tpps_by_region.get(e_data.e_tuple[1], ())
        ]
    with open(f'Synthetic TPPs/{e_data.e_tuple[1]}.csv') as f:
        return [
            (pd.Timestamp(line[0]), float(line[2]), float(line[3]))
            for line in (
                row.strip().split(',')
                for row in f.readlines()
            )
        ]


def filter_approvals_by_cycle(
    approvals: List[Tuple[pd.Timestamp, float, float]],
    party_context: PartyContext
) -> List[Tuple[pd.Timestamp, float, float]]:
    e_data = party_context.e_data
    return [
        approval
        for approval in approvals
        if (
            approval[0] >= e_data.start_date
            # Use the actual final voting-intention poll date, not merely the
            # configured election/cutoff boundary. Approval observations must
            # not extend either a cutoff or an ordinary trend into the future.
            and approval[0] <= e_data.end
            and approval[2] > 0
        )
    ]


def adjust_approvals_for_party (
    approvals: List[Tuple[pd.Timestamp, float, float]],
    party_context: PartyContext
) -> List[Tuple[pd.Timestamp, float, float]]:
    e_data = party_context.e_data
    m_data = party_context.m_data
    party = party_context.party
    
    # We previously converted the approval rating to a TPP estimate
    # Now we need to calculate how that converts to FP for the "major" parties

    # Go through each approval and remove the part of the TPP
    # that comes from preferences, leaving an estimate of the
    # major party FP
    if party == 'ALP FP':
        for oth_party in others_parties + ['GRN FP', 'NAT FP', 'OTH FP']:
            if oth_party in e_data.others_medians:
                pref_tuple = (e_data.e_tuple[0], e_data.e_tuple[1], oth_party)
                flow = m_data.preference_flows[pref_tuple][0]
                approvals = [
                    (
                        a, 
                        b - flow *
                        e_data.others_medians[oth_party][(a - e_data.start).days],
                        c
                    )
                    for a, b, c in approvals
                ]
    elif party in ['LNP FP', 'LIB FP']:
        # Convert to LNP TPP
        approvals = [(a, 100 - b, c) for a, b, c in approvals]
        for oth_party in others_parties + ['GRN FP', 'NAT FP', 'OTH FP']:
            if oth_party in e_data.others_medians:
                pref_tuple = (e_data.e_tuple[0], e_data.e_tuple[1], oth_party)
                flow = m_data.preference_flows[pref_tuple][0]
                approvals = [
                    (
                        a,
                        b - (1 - flow) *
                        # if the other party's trend doesn't reach this
                        # point, just use the last value
                        e_data.others_medians[oth_party][min(
                            (a - e_data.start).days,
                            len(e_data.others_medians[oth_party]) - 1
                        )],
                        c
                    )
                    for a, b, c in approvals
                ]
    
    return approvals


def filter_approvals_by_poll_range(
    approvals: List[Tuple[pd.Timestamp, float, float]],
    party_context: PartyContext
) -> Tuple[List[Tuple[pd.Timestamp, float, float]], List[int]]:
    e_data = party_context.e_data

    approval_days = [(a[0] - e_data.start).days + 1 for a in approvals]
    approvals_in_range = [
        a for a, day in zip(approvals, approval_days)
        if 1 <= day <= e_data.n_days
    ]
    approval_days_in_range = [
        day for day in approval_days
        if 1 <= day <= e_data.n_days
    ]

    return approvals_in_range, approval_days_in_range


@dataclass
class AppendApprovalsInputs:
    approval_days: List[int]
    approvals: List[Tuple[pd.Timestamp, float, float]]
    house_effects: HouseEffects
    model_params: ModelParams
    poll_vectors: PollVectors


def append_approvals_to_vectors(
    inputs: AppendApprovalsInputs
) -> PollVectors:
    poll_vectors = inputs.poll_vectors
    house_effects = inputs.house_effects
    approvals = inputs.approvals
    approval_days = inputs.approval_days
    model_params = inputs.model_params

    if len(approvals) > 0:
        poll_vectors.n_polls += len(approvals)
        poll_vectors.n_houses += 1
        poll_vectors.houses += ['Approvals']
        poll_vectors.pollObs += [a[1] for a in approvals]
        poll_vectors.missingObs += [0 for a in approvals]
        poll_vectors.pollHouses += [len(poll_vectors.houses) for a in approvals]
        poll_vectors.pollDays += approval_days
        # Sigma of approval rating-derived TPP will be between 3 and 5
        # depending on the weight of the approval rating
        # Even at the lowest end this is similar to a "bad" poll
        # and overwhelmed by a good poll
        poll_vectors.sigmasList += [
            max(model_params.approval_sigma_min, model_params.approval_sigma_max - a[2])
            for a in approvals
        ]
        house_effects.he_weights += [0]
        house_effects.biases += [0]


@dataclass
class ApprovalsInputs:
    house_effects: HouseEffects
    party_context: PartyContext
    poll_vectors: PollVectors

def maybe_add_approvals(inputs: ApprovalsInputs) -> PollVectors:
    party_context = inputs.party_context
    model_params = party_context.model_params
    poll_vectors = inputs.poll_vectors
    house_effects = inputs.house_effects

    # Add synthetic data (from approval ratings)
    # for TPP and major party primaries
    if should_use_approvals(party_context):
        # Load the synthetic TPPs from the CSV file
        approvals = load_approvals(party_context)
        # Filter the approvals to only include those within the cycle of the election
        approvals = filter_approvals_by_cycle(approvals, party_context)
        # Make sure that the approvals are all within the range of days that have polls
        # before indexing any already-generated minor-party trend. This is
        # particularly important for cutoff runs, whose trend is truncated.
        approvals_in_range, approval_days_in_range = \
             filter_approvals_by_poll_range(approvals, party_context)
        # Create the FP series from the approvals, if necessary
        approvals_in_range = adjust_approvals_for_party(
            approvals_in_range,
            party_context,
        )

        # Append the approvals to the poll vectors
        # so that they act as (low-impact) polls in the model
        append_approvals_inputs = AppendApprovalsInputs(
            approval_days=approval_days_in_range,
            approvals=approvals_in_range,
            house_effects=house_effects,
            model_params=model_params,
            poll_vectors=poll_vectors
        )
        append_approvals_to_vectors(append_approvals_inputs)

    return poll_vectors


def prepare_discontinuities(party_context: PartyContext) -> List[int]:
    m_data = party_context.m_data
    e_data = party_context.e_data

    # Transform discontinuities to zero-based calendar offsets. The reduced
    # series later maps each event to the transition entering its date.
    discontinuities_filtered = m_data.discontinuities[e_data.e_tuple[1]]
    discontinuities_filtered = \
        [(pd.Timestamp(date) - e_data.start).days
            for date in discontinuities_filtered]

    # Remove discontinuities outside of the election period
    discontinuities_filtered = \
        [date for date in discontinuities_filtered
            if 0 < date < e_data.n_days]

    # Stan doesn't like zero-length arrays so put in a dummy value
    # if there are no discontinuities
    if not discontinuities_filtered:
        discontinuities_filtered.append(0)
    
    return discontinuities_filtered


@dataclass
class HouseEffectsInputs:
    df: pd.DataFrame
    party_context: PartyContext
    poll_vectors: PollVectors


def build_house_effect_weights(inputs: HouseEffectsInputs) -> List[float]:
    poll_vectors = inputs.poll_vectors
    e_data = inputs.party_context.e_data
    party = inputs.party_context.party
    config = inputs.party_context.config

    # Equal weights for house effects when calibrating,
    # use house effect weights when running forecasts
    # that have been determined by the pollster calibration process
    return [
        1 if config.calibrate_pollsters or config.calibrate_bias else
        e_data.pollster_he_weights[(x, party)] ** 2 if
        (x, party) in e_data.pollster_he_weights else 0.05
        for x in poll_vectors.houses
    ]


def build_house_effect_biases(inputs: HouseEffectsInputs) -> List[float]:
    poll_vectors = inputs.poll_vectors
    e_data = inputs.party_context.e_data
    party = inputs.party_context.party
    config = inputs.party_context.config

    return [
        0 if config.calibrate_pollsters or config.calibrate_bias else
        e_data.pollster_biases[(x, party)][0] if
        (x, party) in e_data.pollster_biases else 0
        for x in poll_vectors.houses
    ]


@dataclass
class LogExpectedHouseEffectSumInputs:
    inputs: HouseEffectsInputs
    he_weights: List[float]
    biases: List[float]

def log_expected_house_effect_sum(inputs: LogExpectedHouseEffectSumInputs) -> float:
    biases = inputs.biases
    he_weights = inputs.he_weights
    df = inputs.inputs.df
    poll_vectors = inputs.inputs.poll_vectors

    weightedBiasSum = 0
    housePollCount = [0 for a in poll_vectors.houses]
    houseWeight = [0 for a in poll_vectors.houses]
    houseList = df['House'].values.tolist()
    for poll in range(0, poll_vectors.n_polls):
        housePollCount[houseList[poll] - 1] = housePollCount[houseList[poll] - 1] + 1
    for house in range(0, poll_vectors.n_houses):
        houseWeight[house] = he_weights[house]
        weightedBiasSum += biases[house] * houseWeight[house]
    totalHouseWeight = sum(houseWeight)
    weightedBias = weightedBiasSum / totalHouseWeight
    print(f'Expected house effect sum: {weightedBias}')
    print(f'House effect weights: {houseWeight} for {poll_vectors.houses}')


def prepare_house_effects(inputs: HouseEffectsInputs) -> HouseEffects:
    df = inputs.df
    poll_vectors = inputs.poll_vectors
    e_data = inputs.party_context.e_data
    party = inputs.party_context.party
    config = inputs.party_context.config

    he_weights = build_house_effect_weights(inputs)

    # No biases when calibrating, use biases when running forecasts
    # that are determined by the pollster calibration process
    biases = build_house_effect_biases(inputs)

    # Print an estimate for the expected house effect sum
    # (this whole section doesn't have any impact on subsequent calculations)
    log_expected_house_effect_sum(LogExpectedHouseEffectSumInputs(
        inputs=inputs,
        he_weights=he_weights,
        biases=biases
    ))

    return HouseEffects(he_weights=he_weights, biases=biases)


@dataclass
class ReducedSeriesInputs:
    discontinuities_filtered: List[int]
    e_data: ElectionData
    model_params: ModelParams
    poll_vectors: PollVectors
    prior_series: PriorSeries


def build_reduced_series(inputs: ReducedSeriesInputs) -> ReducedSeries:
    # To save on computation, we only compute the Bayesian aggregation every tFactor days
    # This function adjusts the previously prepared data to reflect this
    # Each series starts at 1 as Stan prefers 1-based indexing
    model_params = inputs.model_params

    # For these first series, we do the (n - 1) // tFactor + 1 calculations
    # because it's important that they aren't ever zero
    # For the other series, we can just use n // tFactor as they won't ever by near zero
    # being sometimes off by one is not a problem since the poll dates are fuzzier in the first place

    # Calculate the number of days in the reduced series
    tDayCount = (inputs.e_data.n_days - 1) // model_params.tFactor + 1
    # Calculate the (reduced) day indices for the polls
    tPollDays = [
        compressed_day_number(day, model_params.tFactor)
        for day in inputs.poll_vectors.pollDays
    ]
    # Calculate the transitions entering each discontinuity date. Multiple
    # dates can collapse onto one transition after time compression.
    tDiscontinuities = sorted(set(
        transition_entering_calendar_offset(day, model_params.tFactor)
        for day in inputs.discontinuities_filtered
        if day > 0
    ))
    tDiscontinuities = [
        day for day in tDiscontinuities
        if 1 <= day < tDayCount
    ]
    # Stan doesn't like zero-length arrays so put in a dummy value
    # if there are no discontinuities
    if len(tDiscontinuities) == 0:
        tDiscontinuities.append(0)
    # Calculate the (reduced) day index for the election day
    # (this determines when the lowered sigma for the campaign starts)
    # Raw day zero maps to Stan day one, matching the poll-day conversion.
    tElectionDay = compressed_calendar_offset(
        inputs.e_data.election_day, model_params.tFactor
    )
    tCampaignStartDay = compressed_calendar_offset(
        max(0, inputs.e_data.election_day - CAMPAIGN_WINDOW_DAYS),
        model_params.tFactor,
    )
    tFinalStartDay = compressed_calendar_offset(
        max(0, inputs.e_data.election_day - FINAL_WINDOW_DAYS),
        model_params.tFactor,
    )
    # Calculate the thresholds between new and old house effects
    # (this determines when the house effects are mixed)
    tHouseEffectNew = model_params.houseEffectNew // model_params.tFactor
    tHouseEffectOld = model_params.houseEffectOld // model_params.tFactor
    # Calculate the prior series for each day in the reduced series
    # This is the default assumption for each day before polls are taken into account
    prior_series_t = [inputs.prior_series.prior_series_daily[i * model_params.tFactor] for i in range(tDayCount)]
    # Calculate the prior sigma for each day in the reduced series
    # This is the variance in the default assumption
    prior_sigma_t = [inputs.prior_series.sigma_daily[i * model_params.tFactor] for i in range(tDayCount)]

    return ReducedSeries(
        prior_series=inputs.prior_series,
        prior_series_t=prior_series_t,
        prior_sigma_t=prior_sigma_t,
        tDayCount=tDayCount,
        tPollDays=tPollDays,
        tDiscontinuities=tDiscontinuities,
        tElectionDay=tElectionDay,
        tCampaignStartDay=tCampaignStartDay,
        tFinalStartDay=tFinalStartDay,
        tHouseEffectNew=tHouseEffectNew,
        tHouseEffectOld=tHouseEffectOld,
    )

def build_stan_data(run_context):
    poll_vectors = run_context.poll_vectors
    reduced_series = run_context.reduced_series
    house_effects = run_context.house_effects
    model_params = run_context.model_params

    # Prepare the data for Stan to process
    stan_data = {
        'dayCount': reduced_series.tDayCount,
        'pollCount': poll_vectors.n_polls,
        'houseCount': poll_vectors.n_houses,
        'discontinuityCount': len(reduced_series.tDiscontinuities),
        'priorSeries': reduced_series.prior_series_t,
        'priorVoteShareSigma': reduced_series.prior_sigma_t,

        'pollObservations': poll_vectors.pollObs,
        'missingObservations': poll_vectors.missingObs,
        'pollHouse': poll_vectors.pollHouses,
        'pollDay': reduced_series.tPollDays,
        'discontinuities': reduced_series.tDiscontinuities,
        'sigmas': poll_vectors.sigmasList,
        'heWeights': house_effects.he_weights,
        'biases': house_effects.biases,

        'campaignStartDay': reduced_series.tCampaignStartDay,
        'finalStartDay': reduced_series.tFinalStartDay,

        # distributions for the daily change in vote share
        # higher values during campaigns, since it's more likely
        # people are paying attention and changing their mind then
        'dailySigma': model_params.daily_sigma_base * math.sqrt(model_params.tFactor),
        'campaignSigma': model_params.campaign_sigma_base * math.sqrt(model_params.tFactor),
        'finalSigma': model_params.final_sigma_base * math.sqrt(model_params.tFactor),

        # prior distribution for each house effect
        # modelled as a double exponential to avoid
        # easily giving a large house effect, but
        # still giving a big one when it's really warranted
        'houseEffectSigma': model_params.house_effect_sigma,

        # prior distribution for sum of house effects
        # keep this very small, will deal with systemic bias variability
        # in the main program, so for now keep the variance of house
        # effects at approximately zero
        'houseEffectSumSigma': model_params.house_effect_sum_sigma,

        # prior distribution for each day's vote share
        # very weak prior, want to avoid pulling extreme vote shares
        # towards the center since that historically harms accuracy
        # 'priorVoteShareSigma': 200.0,

        # Bounds for the transition between old and new house effects
        'houseEffectOld': reduced_series.tHouseEffectOld,
        'houseEffectNew': reduced_series.tHouseEffectNew
    }

    validate_stan_data(stan_data)
    return stan_data


def validate_stan_data(stan_data):
    """Reject malformed derived vectors before starting an expensive fit."""

    poll_count = stan_data['pollCount']
    house_count = stan_data['houseCount']
    day_count = stan_data['dayCount']
    expected_lengths = {
        'pollObservations': poll_count,
        'missingObservations': poll_count,
        'pollHouse': poll_count,
        'pollDay': poll_count,
        'sigmas': poll_count,
        'heWeights': house_count,
        'biases': house_count,
        'priorSeries': day_count,
        'priorVoteShareSigma': day_count,
        'discontinuities': stan_data['discontinuityCount'],
    }
    for name, expected_length in expected_lengths.items():
        if len(stan_data[name]) != expected_length:
            raise ConfigError(
                'Stan vector {} has length {}; expected {}.'.format(
                    name, len(stan_data[name]), expected_length
                )
            )

    finite_vectors = (
        'pollObservations',
        'sigmas',
        'heWeights',
        'biases',
        'priorSeries',
        'priorVoteShareSigma',
    )
    for name in finite_vectors:
        if not all(math.isfinite(float(value)) for value in stan_data[name]):
            raise ConfigError('Stan vector {} contains a non-finite value.'.format(
                name
            ))
    if not all(0 <= value <= 100 for value in stan_data['pollObservations']):
        raise ConfigError('Stan poll observations must be between 0 and 100.')
    if not all(0 <= value <= 100 for value in stan_data['priorSeries']):
        raise ConfigError('Stan prior series must be between 0 and 100.')
    if not all(value > 0 for value in stan_data['sigmas']):
        raise ConfigError('Stan poll sigmas must be positive.')
    if not all(value > 0 for value in stan_data['priorVoteShareSigma']):
        raise ConfigError('Stan prior sigmas must be positive.')
    if not all(value >= 0 for value in stan_data['heWeights']):
        raise ConfigError('Stan house-effect weights must not be negative.')
    if not all(-100 <= value <= 100 for value in stan_data['biases']):
        raise ConfigError('Stan bias values must be between -100 and 100.')
    if not all(value in (0, 1) for value in stan_data['missingObservations']):
        raise ConfigError('Stan missing-observation flags must be zero or one.')
    if sum(stan_data['heWeights']) <= 0:
        raise ConfigError('Stan house-effect weights must have positive total.')
    if not all(
        1 <= value <= house_count for value in stan_data['pollHouse']
    ):
        raise ConfigError('Stan poll-house indices are out of range.')
    if not all(1 <= value <= day_count for value in stan_data['pollDay']):
        raise ConfigError('Stan poll-day indices are out of range.')
    if not all(
        0 <= value <= day_count for value in stan_data['discontinuities']
    ):
        raise ConfigError('Stan discontinuity transitions are out of range.')
    if not (
        1 <= stan_data['campaignStartDay'] <= stan_data['finalStartDay']
    ):
        raise ConfigError('Stan campaign/final start days are inconsistent.')
    if not (
        stan_data['houseEffectOld'] > stan_data['houseEffectNew'] >= 1
    ):
        raise ConfigError(
            'Compressed house-effect thresholds must be distinct and positive.'
        )
    for name in (
        'dailySigma',
        'campaignSigma',
        'finalSigma',
        'houseEffectSigma',
        'houseEffectSumSigma',
    ):
        if not math.isfinite(stan_data[name]) or stan_data[name] <= 0:
            raise ConfigError('{} must be positive and finite.'.format(name))


def verify_timeline_consistency(party_context: PartyContext):
    e_data = party_context.e_data
    expected_end = e_data.start + timedelta(days=int(e_data.n_days) - 1)
    if e_data.end != expected_end:
        raise ValueError(
            f"Inconsistent timeline: start={e_data.start} end={e_data.end} "
            f"n_days={e_data.n_days} expected_end={expected_end}"
        )
