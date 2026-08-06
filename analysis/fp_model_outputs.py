"""Trend output serialization and calibration evidence for fp_model."""

import calibration_summary
import csv
import math
import os
import pandas as pd
import statistics
import tempfile
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

import fp_model_provenance

from fp_model_data import (
    Config,
    ConfigError,
    ElectionData,
    ModellingData,
    ModelParams,
    RunContext,
    calibration_prior_path,
    compressed_day_number,
    derive_unnamed_others_median,
    house_effect_new_factor,
    output_filename,
    OutputFilenameInputs,
    stan_seed_mode,
)
from fp_model_constants import others_parties
from fp_model_prepare import (
    ApprovalsInputs,
    HouseEffectsInputs,
    OutputContext,
    PartyContext,
    PollVectorInputs,
    ReducedSeriesInputs,
    TrendOutputs,
    build_poll_vectors,
    build_prior_series,
    build_reduced_series,
    build_stan_data,
    get_prior_result,
    maybe_add_approvals,
    prepare_discontinuities,
    prepare_house_effects,
    prepare_poll_df,
    verify_timeline_consistency,
)
from fp_model_stan import ModelInputs, StanDiagnosticsRecorder, run_stan_model

# Helper function to output the filename for an OutputContext
def output_filename_ctx(output_ctx, kind):
    return output_filename(OutputFilenameInputs(
        config=output_ctx.config,
        e_data=output_ctx.e_data,
        party=output_ctx.party,
        excluded_pollster=output_ctx.excluded_pollster,
        file_type=kind,
    ))


@dataclass
class WritingContext:
    output_probs_t: Tuple[float, ...]
    summary: Any

def prepare_writing(fit: Any, median_only=False):
    output_probs_t = output_probabilities(median_only)
    summary = fit.summary(probs=output_probs_t)['summary']

    return WritingContext(
        output_probs_t=output_probs_t,
        summary=summary,
    )


def output_probabilities(median_only=False):
    if median_only:
        return (0.5,)
    return tuple(
        [0.001]
        + [index * 0.01 for index in range(1, 100)]
        + [0.999]
    )


@dataclass
class IterTrendDaysInputs:
    e_data: ElectionData
    run_context: RunContext
    summary: Any
    output_probs_t: Tuple[float, ...]

@dataclass
class TrendDay:
    effective_day: int
    day_infos: List[float]
    median_val: float
    table_index: int

def iter_trend_days(inputs: IterTrendDaysInputs):
    # Isolates the extration of the trend days from the STAN output
    # so that the logic isn't repeated in multiple places
    # and this is therefore done consistently across the program
    e_data = inputs.e_data
    run_context = inputs.run_context
    summary = inputs.summary
    output_probs_t = inputs.output_probs_t
    model_params = run_context.model_params
    poll_vectors = run_context.poll_vectors
    tDayCount = run_context.reduced_series.tDayCount

    # This is the index of the first day in the summary table (STAN output)
    # that corresponds to the first day in the model
    offset = tDayCount + poll_vectors.n_houses * 2
    # The first three Stan summary columns are mean, standard error and
    # standard deviation; percentile columns start at index three.
    median_col = 3 + output_probs_t.index(0.5)

    for summary_day in range(tDayCount):
        for duplicate_num in range(model_params.tFactor):
            effective_day = summary_day * model_params.tFactor + duplicate_num
            if effective_day >= e_data.n_days:
                break
            table_index = summary_day + offset

            day_infos = []
            for col in range(3, 3 + len(output_probs_t)):
                day_infos.append(summary[table_index][col])

            median_val = summary[table_index][median_col]

            yield TrendDay(
                effective_day=effective_day,
                day_infos=day_infos,
                median_val=median_val,
                table_index=table_index,
            )


@dataclass
class WriteTrendInputs:
    output_context: OutputContext
    writing_context: WritingContext

def write_trend(inputs: WriteTrendInputs):
    output_context = inputs.output_context
    writing_context = inputs.writing_context

    config = output_context.config
    e_data = output_context.e_data
    output_probs_t = writing_context.output_probs_t
    party = output_context.party
    run_context = output_context.run_context
    summary = writing_context.summary

    output_trend = output_filename_ctx(output_context, 'trend')

    # Extract trend data from model summary and write to file
    trend_file = open(output_trend, 'w')
    trend_file.write('Start date day,Month,Year\n')
    trend_file.write(e_data.start.strftime('%d,%m,%Y\n'))
    trend_file.write('Day,Party')
    for prob in output_probs_t:
        trend_file.write(',' + str(round(prob * 100)) + "%")
    trend_file.write('\n')

    day_data = []
    for day in iter_trend_days(IterTrendDaysInputs(
        e_data=e_data,
        run_context=run_context,
        summary=summary,
        output_probs_t=output_probs_t,
    )):
        to_write = f"{day.effective_day},{party}"
        to_write += "," + ",".join(str(round(v, 3)) for v in day.day_infos)
        to_write += "\n"
        trend_file.write(to_write)
        day_data.append(day.day_infos)

    trend_file.close()
    print('Saved trend file at ' + output_trend)
    return TrendOutputs(
        day_data=day_data,
        final_median=round(day_data[-1][output_probs_t.index(0.5)], 3),
    )


def collect_trend_outputs(output_context, writing_context):
    """Extract only the compact calibration values without serialising a trace."""

    days = list(iter_trend_days(IterTrendDaysInputs(
        e_data=output_context.e_data,
        run_context=output_context.run_context,
        summary=writing_context.summary,
        output_probs_t=writing_context.output_probs_t,
    )))
    if not days:
        raise ConfigError(
            'Stan output contained no trend days for {}.'.format(
                output_context.party
            )
        )
    return TrendOutputs(
        day_data=[day.day_infos for day in days],
        # Match legacy CSV compaction, which reads the rounded 50% column.
        final_median=round(days[-1].median_val, 3),
    )


def retain_federal_calibration_prior(output_context, trend_outputs, writing_context):
    """Retain the full-fit median series needed by later state calibration.

    Leave-one-out variants are only used to reduce pollster variability. The
    unexcluded federal fit is the stable prior for every overlapping state
    calibration and is intentionally kept even when detailed traces are off.
    """

    if (
        not output_context.config.calibrate_pollsters
        or output_context.e_data.e_tuple[1] != 'fed'
        or output_context.excluded_pollster
    ):
        return
    median_index = writing_context.output_probs_t.index(0.5)
    output_context.e_data.calibration_federal_priors[
        output_context.party
    ] = [
        (
            output_context.e_data.start + pd.to_timedelta(day, unit='D'),
            round(values[median_index], 3),
        )
        for day, values in enumerate(trend_outputs.day_data)
    ]


def write_federal_calibration_priors(e_data):
    """Atomically publish compact daily priors after one federal LOO batch."""

    if e_data.e_tuple[1] != 'fed' or not e_data.calibration_federal_priors:
        return None
    expected_parties = set(e_data.expected_parties)
    actual_parties = set(e_data.calibration_federal_priors)
    if actual_parties != expected_parties:
        raise ConfigError(
            'Federal calibration prior for {} has incomplete party coverage '
            '(missing: {}; unexpected: {}).'.format(
                ''.join(e_data.e_tuple),
                ', '.join(sorted(expected_parties - actual_parties)) or 'none',
                ', '.join(sorted(actual_parties - expected_parties)) or 'none',
            )
        )
    expected_dates = [
        e_data.start + pd.to_timedelta(day, unit='D')
        for day in range(e_data.n_days)
    ]
    for party, values in e_data.calibration_federal_priors.items():
        dates = [date for date, _ in values]
        medians = [median for _, median in values]
        if dates != expected_dates:
            raise ConfigError(
                'Federal calibration prior for {} {} has incomplete or '
                'misaligned daily coverage.'.format(
                    ''.join(e_data.e_tuple), party
                )
            )
        if (
            not all(math.isfinite(value) for value in medians)
            or any(value < 0 or value > 100 for value in medians)
        ):
            raise ConfigError(
                'Federal calibration prior for {} {} has an invalid median.'
                .format(''.join(e_data.e_tuple), party)
            )
    output = calibration_prior_path(e_data.e_tuple[0])
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        dir=output.parent,
        prefix='.{}-'.format(output.stem),
        suffix='.tmp',
        text=True,
    )
    try:
        with os.fdopen(descriptor, 'w', newline='', encoding='utf-8') as file:
            writer = csv.writer(file, lineterminator='\n')
            writer.writerow(['Date', 'Party', '50%'])
            for party in sorted(e_data.calibration_federal_priors):
                for date, median in e_data.calibration_federal_priors[party]:
                    writer.writerow([date.strftime('%Y-%m-%d'), party, median])
            file.flush()
            os.fsync(file.fileno())
        os.replace(temporary_name, output)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise
    return output


@dataclass
class PrepareOthersMediansInputs:
    output_context: OutputContext
    writing_context: WritingContext

def prepare_others_medians(inputs: PrepareOthersMediansInputs):
    output_context = inputs.output_context
    writing_context = inputs.writing_context

    e_data = output_context.e_data
    party = output_context.party
    run_context = output_context.run_context
    summary = writing_context.summary
    output_probs_t = writing_context.output_probs_t

    if party in others_parties or party in ['GRN FP', 'NAT FP', 'OTH FP']:
        e_data.others_medians[party] = {}
    else:
        return

    for day in iter_trend_days(IterTrendDaysInputs(
        e_data=e_data,
        run_context=run_context,
        summary=summary,
        output_probs_t=output_probs_t,
    )):
        if party == 'OTH FP':
            named_minor_total = sum(
                medians[day.effective_day]
                for oth_party, medians in e_data.others_medians.items()
                if oth_party in others_parties
            )
            unnamed_others = derive_unnamed_others_median(
                day.median_val,
                named_minor_total,
            )
            e_data.others_medians[party][
                day.effective_day
            ] = unnamed_others
            if output_context.config.use_approvals():
                mode = (
                    'scheduled cutoff {}d / poll endpoint {}d'.format(
                        output_context.config.cutoff_days,
                        e_data.days_to_election,
                    )
                    if output_context.config.cutoff_mode
                    else 'final trend'
                )
                output_context.config.unnamed_others_diagnostics.record(
                    election=''.join(e_data.e_tuple),
                    mode=mode,
                    day=day.effective_day,
                    inclusive_others=day.median_val,
                    named_minor_total=named_minor_total,
                    adjusted_unnamed_others=unnamed_others,
                )
        else:
            e_data.others_medians[party][
                day.effective_day
            ] = day.median_val


def write_cutoff_trend(
    output_context: OutputContext,
    writing_context: WritingContext,
):
    """Store only the endpoint distribution needed by downstream calibration."""

    trend_days = iter_trend_days(IterTrendDaysInputs(
        e_data=output_context.e_data,
        run_context=output_context.run_context,
        summary=writing_context.summary,
        output_probs_t=writing_context.output_probs_t,
    ))
    final_day = None
    for final_day in trend_days:
        pass
    if final_day is None:
        raise ConfigError(
            'Stan output contained no trend days for {}.'.format(
                output_context.party
            )
        )

    election = ''.join(output_context.e_data.e_tuple)
    output_context.config.cutoff_output_store.write(
        election=election,
        party=output_context.party,
        scheduled_cutoff_days=output_context.config.cutoff_days,
        poll_trend_end_days=output_context.e_data.days_to_election,
        random_seed=output_context.random_seed,
        probabilities=writing_context.output_probs_t,
        values=final_day.day_infos,
    )
    print(
        'Saved scheduled cutoff {}d (poll trend ends {}d out) for {} in {}'
        .format(
            output_context.config.cutoff_days,
            output_context.e_data.days_to_election,
            output_context.party,
            fp_model_provenance.cutoff_output_path(election),
        )
    )


@dataclass
class WriteHouseEffectsInputs:
    output_context: OutputContext
    party: str
    run_context: RunContext
    writing_context: WritingContext

@dataclass
class WriteHouseEffectsOutputs:
    new_house_effects: List[float]
    old_house_effects: List[float]
    new_house_effect_medians: Dict[str, float]


def collect_house_effect_outputs(inputs: WriteHouseEffectsInputs):
    """Extract the house-effect data required by poll adjustment and summary."""

    output_probs_t = inputs.writing_context.output_probs_t
    poll_vectors = inputs.run_context.poll_vectors
    summary = inputs.writing_context.summary
    offset = inputs.run_context.reduced_series.tDayCount
    median_column = 3 + output_probs_t.index(0.5)
    new_house_effects = []
    old_house_effects = []
    new_house_effect_medians = {}
    for house in range(poll_vectors.n_houses):
        new_house_effects.append(summary[offset + house, 0])
        old_house_effects.append(
            summary[offset + poll_vectors.n_houses + house, 0]
        )
        new_house_effect_medians[poll_vectors.houses[house]] = round(
            summary[offset + house][median_column], 3
        )
    return WriteHouseEffectsOutputs(
        new_house_effects=new_house_effects,
        old_house_effects=old_house_effects,
        new_house_effect_medians=new_house_effect_medians,
    )

def write_house_effects(inputs: WriteHouseEffectsInputs):
    output_context = inputs.output_context
    e_data = output_context.e_data
    output_probs_t = inputs.writing_context.output_probs_t
    party = inputs.party
    poll_vectors = inputs.run_context.poll_vectors
    summary = inputs.writing_context.summary
    tDayCount = inputs.run_context.reduced_series.tDayCount
    
    output_house_effects = output_filename_ctx(inputs.output_context, 'house_effects')

    collected = collect_house_effect_outputs(inputs)
    new_house_effects = collected.new_house_effects
    old_house_effects = collected.old_house_effects

    # Extract house effect data from model summary and write to file
    house_effects_file = open(output_house_effects, 'w')
    house_effects_file.write('House,Party')
    for prob in output_probs_t:
        house_effects_file.write(',' + str(round(prob * 100)) + "%")
    house_effects_file.write('\n')
    house_effects_file.write('New house effects\n')
    offset = tDayCount
    for house_index in range(0, poll_vectors.n_houses):
        house_effects_file.write(poll_vectors.houses[house_index])
        table_index = offset + house_index
        house_effects_file.write("," + party)
        for col in range(3, 3+len(output_probs_t)):
            house_effects_file.write(
                ',' + str(round(summary[table_index][col], 3)))
        house_effects_file.write('\n')
    offset = tDayCount + poll_vectors.n_houses
    house_effects_file.write('Old house effects\n')
    for house_index in range(0, poll_vectors.n_houses):
        house_effects_file.write(poll_vectors.houses[house_index])
        table_index = offset + house_index
        house_effects_file.write("," + party)
        for col in range(3, 3+len(output_probs_t)):
            house_effects_file.write(
                ',' + str(round(summary[table_index][col], 3)))
        house_effects_file.write('\n')

    house_effects_file.close()
    print('Saved house effects file at ' + output_house_effects)
    
    return collected


# Output serialization and calibration-evidence reduction

def calibration_recent_poll_counts(df):
    """Match the legacy compactor's final-183-model-day pollster counts."""

    polls = [
        (str(df.loc[index, 'Firm']), int(df.loc[index, 'DayNum']))
        for index in df.index
    ]
    if not polls:
        raise ConfigError('Bias calibration had no polls to summarise.')
    final_day = max(day for _, day in polls)
    start_day = final_day - calibration_summary.RECENT_POLL_WINDOW_DAYS
    counts = {pollster: 0 for pollster, _ in polls}
    for pollster, day in polls:
        if day >= start_day:
            counts[pollster] += 1
    return counts


@dataclass
class WritePollsInputs:
    df: pd.DataFrame
    output_context: OutputContext
    party: str
    run_context: RunContext
    write_house_effects_outputs: WriteHouseEffectsOutputs

def write_polls(inputs: WritePollsInputs):
    df = inputs.df
    output_context = inputs.output_context
    config = output_context.config
    e_data = output_context.e_data
    model_params = output_context.run_context.model_params
    new_house_effects = inputs.write_house_effects_outputs.new_house_effects
    old_house_effects = inputs.write_house_effects_outputs.old_house_effects
    party = inputs.party

    output_polls = output_filename_ctx(output_context, 'polls')

    # Write poll data to file, giving both raw and
    # house effect adjusted values
    polls_file = open(output_polls, 'w')
    polls_file.write('Firm,Day')
    polls_file.write(',' + party)
    polls_file.write(',' + party + ' adj')
    if party == "@TPP":
        polls_file.write(',' + party + ' reported')
    polls_file.write('\n')
    for poll_index in df.index:
        if ('Brand' in df and isinstance(df.loc[poll_index, 'Brand'], str)
            and len(df.loc[poll_index, 'Brand']) > 0
            and not config.calibrate_pollsters and not config.calibrate_bias):
            polls_file.write(str(df.loc[poll_index, 'Brand']))
        else:
            polls_file.write(str(df.loc[poll_index, 'Firm']))
        day = int(df.loc[poll_index, 'DayNum'])
        compressed_poll_day = compressed_day_number(
            day, model_params.tFactor
        )
        days_ago = (
            output_context.run_context.reduced_series.tDayCount
            - compressed_poll_day
        )
        polls_file.write(',' + str(day))
        fp = df.loc[poll_index, party]
        new_he = new_house_effects[df.loc[poll_index, 'House'] - 1]
        old_he = old_house_effects[df.loc[poll_index, 'House'] - 1]
        new_factor = house_effect_new_factor(
            days_ago,
            output_context.run_context.reduced_series.tHouseEffectNew,
            output_context.run_context.reduced_series.tHouseEffectOld,
        )
        mixed_he = (
            new_factor * new_he
            + (1 - new_factor) * old_he
        )
        adjusted_fp = fp - mixed_he
        polls_file.write(',' + str(round(fp, 3)))
        polls_file.write(',' + str(round(adjusted_fp, 3)))
        if party == "@TPP":
            polls_file.write(',' + str(round(df.loc[poll_index, 'old_tpp'], 3)))
        polls_file.write('\n')
    polls_file.close()
    print('Saved polls file at ' + output_polls)


@dataclass
class CalibratePollstersInputs:
    df: pd.DataFrame
    excluded_pollster: str
    exc_polls: pd.DataFrame
    output_context: OutputContext
    party: str
    trend_outputs: TrendOutputs
    writing_context: WritingContext

@dataclass
class ExcludedPoll:
    day_index: int  # 0-based day (DayNum - 1)
    vote: float
    poll_index: int
    pollster: str

def build_excluded_polls(inputs: CalibratePollstersInputs) -> List[ExcludedPoll]:
    exc_polls = inputs.exc_polls
    party = inputs.party
    rows = zip(exc_polls['DayNum'], exc_polls[party], exc_polls.axes[0], exc_polls['Firm'])
    return [
        ExcludedPoll(day_index=int(day) - 1, vote=vote, poll_index=poll_index, pollster=pollster)
        for day, vote, poll_index, pollster in rows
    ]
                    

@dataclass
class ComputePollsterHouseEffectsInputs:
    excluded_polls: List[ExcludedPoll]
    median_col: int
    parent_inputs: CalibratePollstersInputs

def compute_pollster_house_effects(inputs: ComputePollsterHouseEffectsInputs) -> Dict[str, float]:
    excluded_polls = inputs.excluded_polls
    median_col = inputs.median_col
    day_data = inputs.parent_inputs.trend_outputs.day_data

    diff_sum = {}
    pollster_count = {}
    house_effects = {}
    for a in excluded_polls:
        day, vote, pollster = a.day_index, a.vote, a.pollster
        trend_value = day_data[day][median_col]
        if pollster not in diff_sum:
            diff_sum[pollster] = 0
            pollster_count[pollster] = 0
        diff_sum[pollster] += vote - trend_value
        pollster_count[pollster] += 1
    for key in diff_sum.keys():
        house_effects[key] = diff_sum[key] / pollster_count[key]
    return house_effects

@dataclass
class InterpolatePercentileInputs:
    day_distribution: List[float]
    output_probs: List[float]
    value: float

def interpolate_percentile(inputs: InterpolatePercentileInputs) -> float:
    day_distribution = inputs.day_distribution
    output_probs = inputs.output_probs
    value = inputs.value

    for index, upper_prob in enumerate(output_probs):
        upper_value = day_distribution[index]
        if value < upper_value:
            if index == 0:
                return 0.001
            else:
                lower_value = day_distribution[index - 1]
                lower_prob = output_probs[index - 1]
                lerp = ((value - lower_value) /
                    (upper_value - lower_value))
                return (lower_prob + lerp * 
                    (upper_prob - lower_prob))
    # default high percentile if above all thresholds
    return 0.999

@dataclass
class PollCalibration:
    vote: float
    trend_median: float
    adjusted_vote: float
    percentile: Optional[float]
    deviation: float
    prob_deviation: Optional[float]
    neighbours: float

@dataclass
class BuildPollCalibrationInputs:
    poll: ExcludedPoll
    day_data: List[List[float]]
    median_col: int
    output_probs: List[float]
    house_effects: Dict[str, float]
    df_daynum: pd.Series

def build_poll_calibration(inputs: BuildPollCalibrationInputs) -> PollCalibration:
    day_data = inputs.day_data
    df_daynum = inputs.df_daynum
    house_effects = inputs.house_effects
    median_col = inputs.median_col
    output_probs = inputs.output_probs
    poll = inputs.poll

    trend_median = day_data[poll.day_index][median_col]
    adjusted_vote = poll.vote - house_effects[poll.pollster]
    percentile = (
        None
        if output_probs == (0.5,)
        else interpolate_percentile(InterpolatePercentileInputs(
            day_distribution=day_data[poll.day_index],
            output_probs=output_probs,
            value=adjusted_vote,
        ))
    )
    deviation = adjusted_vote - trend_median
    prob_deviation = (
        None if percentile is None else abs(percentile - 0.5)
    )
    neighbours = sum(min(1, 2 ** (-abs(poll.day_index + 1 - other_day) / 20) * 0.5)
                    for other_day in df_daynum)
    return PollCalibration(
        vote=poll.vote,
        trend_median=trend_median,
        adjusted_vote=adjusted_vote,
        percentile=percentile,
        deviation=deviation,
        prob_deviation=prob_deviation,
        neighbours=neighbours,
    )

@dataclass
class RecordCalibrationInputs:
    e_data: ElectionData
    excluded_pollster: str
    party: str
    poll: ExcludedPoll
    cal: PollCalibration

def record_calibration(inputs: RecordCalibrationInputs) -> None:
    e_data = inputs.e_data
    excluded_pollster = inputs.excluded_pollster
    party = inputs.party
    poll = inputs.poll
    cal = inputs.cal
    e_data.poll_calibrations[(excluded_pollster, poll.day_index, party, poll.poll_index)] = (
        cal.vote,
        cal.trend_median,
        cal.adjusted_vote,
        cal.percentile,
        cal.deviation,
        cal.prob_deviation,
        cal.neighbours,
    )

def calibrate_pollsters(inputs: CalibratePollstersInputs) -> None:

    # An initial calibration step without using historical house effects
    # or variability data as inputs. The poll calibrations are later used to
    # determine how reliably each pollster indicates the trend, its historical bias,
    # and how useful it is for estimating overall bias.

    day_data = inputs.trend_outputs.day_data
    df = inputs.df
    e_data = inputs.output_context.e_data
    excluded_pollster = inputs.excluded_pollster
    output_probs = inputs.writing_context.output_probs_t
    party = inputs.party
                        
    excluded_polls = build_excluded_polls(inputs)
    if len(excluded_polls) <= 1: return
    print(f'Trend closeness statistics for {excluded_pollster}')
    median_col = output_probs.index(0.5)
    house_effects = compute_pollster_house_effects(ComputePollsterHouseEffectsInputs(
        excluded_polls=excluded_polls,
        median_col=median_col,
        parent_inputs=inputs,
    ))
        
    deviations = []
    prob_deviations = []
    for a in excluded_polls:
        poll_calibration = build_poll_calibration(BuildPollCalibrationInputs(
            poll=a,
            day_data=day_data,
            median_col=median_col,
            output_probs=output_probs,
            house_effects=house_effects,
            df_daynum=df['DayNum'],
        ))
        record_calibration(RecordCalibrationInputs(
            e_data=e_data,
            excluded_pollster=excluded_pollster,
            party=party,
            poll=a,
            cal=poll_calibration,
        ))
        deviations.append(poll_calibration.deviation)
        if poll_calibration.prob_deviation is not None:
            prob_deviations.append(poll_calibration.prob_deviation)
    std_dev = statistics.stdev(deviations)
    message = (
        f'Overall ({excluded_pollster}, {party}):'
        f' standard deviation from trend median: {std_dev}'
    )
    if prob_deviations:
        message += (
            ' average probability deviation: '
            f'{statistics.mean(prob_deviations)}'
        )
    print(message)


def write_outputs(output_context: OutputContext, fit):
    run_context = output_context.run_context
    party = output_context.party
    config = output_context.config
    excluded_pollster = output_context.excluded_pollster
    df = output_context.poll_prep_result.df
    exc_polls = output_context.poll_prep_result.exc_polls
    
    writing_context = prepare_writing(
        fit,
        median_only=(
            (config.calibrate_pollsters or config.calibrate_bias)
            and not config.calibration_traces
        ),
    )

    if config.cutoff_mode:
        prepare_others_medians(PrepareOthersMediansInputs(
            output_context=output_context,
            writing_context=writing_context,
        ))
        write_cutoff_trend(output_context, writing_context)
        return

    detailed_calibration_trace = (
        (config.calibrate_pollsters or config.calibrate_bias)
        and config.calibration_traces
    )
    if detailed_calibration_trace or not (
        config.calibrate_pollsters or config.calibrate_bias
    ):
        trend_outputs = write_trend(WriteTrendInputs(
            output_context=output_context,
            writing_context=writing_context,
        ))
    else:
        trend_outputs = collect_trend_outputs(output_context, writing_context)

    retain_federal_calibration_prior(
        output_context, trend_outputs, writing_context
    )

    prepare_others_medians(PrepareOthersMediansInputs(
        output_context=output_context,
        writing_context=writing_context,
    ))
    
    house_effects_inputs = WriteHouseEffectsInputs(
        output_context=output_context,
        party=party,
        run_context=run_context,
        writing_context=writing_context,
    )
    if detailed_calibration_trace or not (
        config.calibrate_pollsters or config.calibrate_bias
    ):
        house_effects_outputs = write_house_effects(house_effects_inputs)
        write_polls(WritePollsInputs(
            df=df,
            output_context=output_context,
            party=party,
            run_context=run_context,
            write_house_effects_outputs=house_effects_outputs,
        ))
    else:
        house_effects_outputs = collect_house_effect_outputs(
            house_effects_inputs
        )
    
    if config.calibrate_pollsters:
        calibrate_pollsters(CalibratePollstersInputs(
            df=df,
            excluded_pollster=excluded_pollster,
            exc_polls=exc_polls,
            output_context=output_context,
            party=party,
            trend_outputs=trend_outputs,
            writing_context=writing_context,
        ))
    elif config.calibrate_bias:
        e_data = output_context.e_data
        e_data.calibration_bias_records[party] = (
            trend_outputs.final_median,
            house_effects_outputs.new_house_effect_medians,
            calibration_recent_poll_counts(df),
        )


@dataclass
class RunPartyInputs:
    config: Config
    diagnostics_recorder: StanDiagnosticsRecorder
    e_data: ElectionData
    excluded_pollster: str
    m_data: ModellingData
    model_params: ModelParams
    party: str
    random_seed: int

def run_party(inputs: RunPartyInputs) -> Optional[OutputContext]:
    config = inputs.config
    e_data = inputs.e_data
    excluded_pollster = inputs.excluded_pollster
    m_data = inputs.m_data
    model_params = inputs.model_params
    party = inputs.party
    
    if excluded_pollster != '':
        print(f'Excluding pollster: {excluded_pollster}')
    else:
        print('Not excluding any pollsters.')

    party_context = PartyContext(
        config=config,
        m_data=m_data,
        e_data=e_data,
        party=party,
        excluded_pollster=excluded_pollster,
        model_params=model_params,
    )

    poll_prep_result = prepare_poll_df(party_context)

    if poll_prep_result is None:
        return

    prior_result = get_prior_result(party_context)

    # Note "df" is mutated in place by build_poll_vectors
    poll_vector_inputs = PollVectorInputs(
        df=poll_prep_result.df,
        party_context=party_context,
        prior_result=prior_result,
    )

    poll_vectors = build_poll_vectors(poll_vector_inputs)
  
    prior_series = build_prior_series(party_context, prior_result)

    house_effects_inputs = HouseEffectsInputs(
        party_context=party_context,
        poll_vectors=poll_vectors,
        df=poll_prep_result.df,
    )

    house_effects = prepare_house_effects(house_effects_inputs)

    approvals_inputs = ApprovalsInputs(
        party_context=party_context,
        poll_vectors=poll_vectors,
        house_effects=house_effects,
    )

    poll_vectors = maybe_add_approvals(approvals_inputs)

    discontinuities_filtered = prepare_discontinuities(party_context)

    reduced_series_inputs = ReducedSeriesInputs(
        e_data=e_data,
        model_params=model_params,
        poll_vectors=poll_vectors,
        discontinuities_filtered=discontinuities_filtered,
        prior_series=prior_series,
    )

    reduced_series = build_reduced_series(reduced_series_inputs)

    run_context = RunContext(
        poll_vectors=poll_vectors,
        reduced_series=reduced_series,
        house_effects=house_effects,
        prior_result=prior_result,
        model_params=model_params,
    )

    stan_data = build_stan_data(run_context)

    model_inputs = ModelInputs(
        stan_data=stan_data,
        iterations=m_data.desired_iterations[e_data.e_tuple],
        chains=15,
        diagnostics_recorder=inputs.diagnostics_recorder,
        party=party,
        e_data=e_data,
        excluded_pollster=excluded_pollster,
        model_params=model_params,
        mode=stan_seed_mode(config, e_data),
        random_seed=inputs.random_seed,
    )

    verify_timeline_consistency(party_context)

    fit = run_stan_model(model_inputs)
    
    output_context = OutputContext(
        e_data=e_data,
        party=party,
        config=config,
        excluded_pollster=excluded_pollster,
        poll_prep_result=poll_prep_result,
        random_seed=inputs.random_seed,
        run_context=run_context,
    )

    write_outputs(output_context, fit)
    return output_context


def finalise_calibrations(e_data, trace_directory=None):
    polls_string = {}
    total_weight = {}
    total_weighted_dev = {}
    output_files = []
    summary_values = []
    residual_evidence_rows = []
    election_tag = ''.join(e_data.e_tuple)
    for key, val in e_data.poll_calibrations.items():
        if (key[0] != ''):
            full_val = e_data.poll_calibrations[('', key[1], key[2], key[3])]
            (
                vote,
                trend_median,
                adjusted_vote,
                percentile,
                cal_deviation,
                prob_deviation,
                neighbours_weight,
            ) = val
            (
                _full_vote,
                _full_trend_median,
                _full_adjusted_vote,
                _full_percentile,
                full_deviation,
                _full_prob_deviation,
                _full_neighbours_weight,
            ) = full_val
            difference = abs(cal_deviation) - abs(full_deviation)
            quotient = min(max(0.5, abs(full_deviation)) /
                           max(0.5, abs(cal_deviation)),
                           1)
            final_weight = min(quotient, neighbours_weight)
            new_key = (key[0], key[2])
            if new_key not in total_weight:
                total_weight[new_key] = 0
                total_weighted_dev[new_key] = 0
                polls_string[new_key] = ''
            total_weight[new_key] += final_weight
            total_weighted_dev[new_key] += final_weight * abs(cal_deviation)
            residual_evidence_rows.append(
                calibration_summary.build_residual_evidence_row(
                    election=election_tag,
                    party=key[2],
                    pollster=key[0],
                    poll_day_index=key[1],
                    poll_index=key[3],
                    values=(
                        vote,
                        trend_median,
                        adjusted_vote,
                        percentile,
                        cal_deviation,
                        prob_deviation,
                        neighbours_weight,
                        full_deviation,
                        quotient,
                        final_weight,
                    ),
                )
            )
            print(f'{key}: Calibrated deviation: {cal_deviation},'
                  f' full deviation: {full_deviation},'
                  f' difference: {difference}\n '
                  f' quotient weight: {quotient},'
                  f' neighbours weight: {neighbours_weight},'
                  f' final weight: {final_weight}')
            polls_string[new_key] += (f'{key[1]},{cal_deviation},{full_deviation},'
                             f'{final_weight}\n')
    for key, val in total_weighted_dev.items():
        weight = total_weight[key]
        if weight == 0: continue
        weighted_average_deviation = val / max(weight / 2, weight - 1)
        print(f'{key}: weighted avg deviation: {weighted_average_deviation}, '
              f'total weight: {weight}')
        summary_values.append(
            (key[1], key[0], weighted_average_deviation, weight)
        )
        if trace_directory is not None:
            filename = (
                f'{trace_directory}/calib_'
                f'{e_data.e_tuple[0]}{e_data.e_tuple[1]}_'
                f'{key[0]}_{key[1]}.csv'
            )
            with open(filename, 'w') as f:
                f.write(f'{weighted_average_deviation},'
                        f'{weight},\n{polls_string[key]}')
            output_files.append(filename)
    return output_files, summary_values, residual_evidence_rows
