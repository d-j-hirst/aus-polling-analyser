"""Batch orchestration and CLI entry helpers for fp_model."""

import calibration_provenance
import calibration_summary
import datetime
import fp_model_checkpoints
import fp_model_provenance
import os
import pandas as pd
import pystan
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from approvals import generate_synthetic_tpps
from election_code import ElectionCode

from fp_model_constants import (
    DEFAULT_BASE_SEED,
    STAN_SEED_NAMESPACE,
    data_source,
    fp_model_source_files,
)
from fp_model_data import (
    Config,
    ConfigError,
    ElectionData,
    ElectionDataInputs,
    ModellingData,
    ModelParams,
    calibration_checkpoint_identity,
    calibration_checkpoint_payload,
    federal_prior_needed_for_states,
    filter_model_eligible_poll_rows,
    restore_calibration_checkpoint,
    stan_seed_mode,
)
from fp_model_outputs import (
    RunPartyInputs,
    finalise_calibrations,
    output_filename_ctx,
    output_probabilities,
    run_party,
    write_federal_calibration_priors,
)
from fp_model_stan import StanDiagnosticsRecorder

def check_suspension(
    suspension_path='suspend.txt',
    before_pause=None,
    input_func=input,
    sleep_func=time.sleep,
):
    """Pause safely between Stan fits when the control file contains 1."""

    def suspension_requested():
        try:
            with open(suspension_path, 'r', encoding='utf-8') as control_file:
                return control_file.read().strip() == '1'
        except FileNotFoundError:
            return False

    if not suspension_requested():
        return False

    if before_pause is not None:
        before_pause()

    try:
        input_func(
            'Suspension requested. Completed outputs have been saved. '
            'Press Enter to resume: '
        )
        with open(suspension_path, 'w', encoding='utf-8') as control_file:
            control_file.write('0\n')
    except EOFError:
        # Detached runs have no keyboard input, so retain the old file-based
        # resume path rather than terminating a long-running batch.
        print(
            'No interactive input is available; change suspend.txt from 1 '
            'to 0 to resume.'
        )
        while suspension_requested():
            sleep_func(5)

    print('Resuming fp_model generation.')
    return True


def build_config() -> Config:
    try:
        return Config()
    except ConfigError as e:
        with open('itsdone.txt', 'w') as f:
            f.write('2')
        raise e


def build_model_params() -> ModelParams:
    model_params = ModelParams()
    model_params.validate()
    return model_params


def maybe_generate_approvals(config: Config) -> None:
    if config.use_approvals():
        config.synthetic_tpps_by_region = generate_synthetic_tpps()


def build_election_data(inputs: ElectionDataInputs) -> Optional[ElectionData]:
    e_data = ElectionData(ElectionDataInputs(
        config=inputs.config,
        m_data=inputs.m_data,
        desired_election=inputs.desired_election,
    ))

    if len(e_data.base_df) == 0:
        print(f'No polls for election {inputs.desired_election.short()} in the requested time range, skipping')
        return None

    return e_data


@dataclass
class ShouldSkipPollsterCalibrationInputs:
    config: Config
    e_data: ElectionData
    excluded_pollster: str

def should_skip_pollster_calibration(inputs: ShouldSkipPollsterCalibrationInputs) -> bool:
    """Skip empty-LOO full fits unless a federal prior is needed by a state."""

    if not (
        inputs.config.calibrate_pollsters
        and inputs.excluded_pollster == ''
        and len(inputs.e_data.poll_calibrations) == 0
    ):
        return False
    election = ElectionCode(
        int(inputs.e_data.e_tuple[0]),
        inputs.e_data.e_tuple[1],
    )
    # Keep the unexcluded federal fit when a later state calibration will load
    # Priors/{year}fed.csv from it (including single-pollster federals where
    # LOO produced no residuals). Early federals with no overlapping state
    # still skip this otherwise unused full fit.
    if federal_prior_needed_for_states(
        election, inputs.e_data.election_cycles
    ):
        return False
    return True


@dataclass
class MaybeCreateTppSeriesInputs:
    desired_election: ElectionCode
    e_data: ElectionData
    m_data: ModellingData
    party: str

def maybe_create_tpp_series(inputs: MaybeCreateTppSeriesInputs) -> None:
    if inputs.party == "@TPP" or inputs.party == "OTH FP":
        inputs.e_data.create_tpp_series(
            ElectionData.CreateTppSeriesInputs(
                m_data=inputs.m_data,
                desired_election=inputs.desired_election,
                df=inputs.e_data.base_df
            )
        )


def cutoff_work_items(config, m_data, schedule):
    """Return distinct scheduled cutoff and actual poll-endpoint pairs."""

    poll_dates_by_region = {}
    for election in config.elections:
        election_tuple = (str(election.year()), election.region())
        region = election.region()
        if region not in poll_dates_by_region:
            poll_data = pd.read_csv(
                data_source[region],
                usecols=['MidDate', 'OTH FP'],
            )
            poll_data = filter_model_eligible_poll_rows(poll_data)
            parsed_dates = pd.to_datetime(
                poll_data['MidDate'],
                errors='raise',
            )
            poll_dates_by_region[region] = [
                poll_date.date()
                for poll_date in parsed_dates
                if not pd.isna(poll_date)
            ]

        cycle_start, election_day = m_data.election_cycles[election_tuple]
        cycle_poll_dates = [
            poll_date
            for poll_date in poll_dates_by_region[region]
            if cycle_start.date() <= poll_date <= election_day.date()
        ]
        effective_cutoffs = (
            fp_model_provenance.effective_cutoff_schedule(
                election_day.date(),
                cycle_poll_dates,
                schedule,
            )
        )
        print(
            '{} has {} distinct poll information sets across {} scheduled '
            'cutoff points.'.format(
                election.short(),
                len(effective_cutoffs),
                len(schedule),
            )
        )
        for cutoff_index, (
            scheduled_days,
            poll_trend_end_days,
        ) in enumerate(effective_cutoffs):
            yield (
                election,
                scheduled_days,
                poll_trend_end_days,
                cutoff_index == len(effective_cutoffs) - 1,
            )


def sync_cutoff_federal_priors(
    store, recorder, election, federal_prior_files
):
    """Keep resume metadata current as federal cutoff parents accumulate."""

    files = sorted(set(federal_prior_files))
    dependencies = None
    if recorder is not None:
        dependencies = recorder.dependencies_for_election(election, files)
    store.update_federal_priors(election, files, dependencies)


def promote_cutoff_output(store, recorder, election, federal_prior_files):
    files = sorted(set(federal_prior_files))
    sync_cutoff_federal_priors(store, recorder, election, files)
    dependencies = recorder.dependencies_for_election(election, files)
    store.promote(
        election,
        certify=lambda output: recorder.record(
            election=election,
            output=output,
            dependencies=dependencies,
        ),
    )
    print(
        'Completed and promoted consolidated cutoff file for {}.'.format(
            election
        )
    )


# Batch orchestration, suspension checks and provenance completion

def run_models() -> None:
    # check version information
    print('Python version: {}'.format(sys.version))
    print('pystan version: {}'.format(pystan.__version__))

    diagnostics_recorder = StanDiagnosticsRecorder()
    try:
        config = build_config()

        model_params = build_model_params()
        base_seed = (
            config.seed if config.seed is not None else DEFAULT_BASE_SEED
        )
        print('Base random seed: {}'.format(base_seed))
        if config.calibration_traces:
            trace_run_id = '{}-{}-{}'.format(
                datetime.datetime.now(datetime.timezone.utc).strftime(
                    '%Y%m%dT%H%M%SZ'
                ),
                os.getpid(),
                base_seed,
            )
            config.calibration_trace_directory = (
                './Outputs/Calibration/Diagnostics/{}/'.format(trace_run_id)
            )
            os.makedirs(config.calibration_trace_directory, exist_ok=True)
            print(
                'Writing optional calibration traces under {}.'.format(
                    config.calibration_trace_directory
                )
            )
        provenance_recorder = (
            calibration_provenance.CalibrationRecorder(
                [os.path.basename(sys.executable)] + sys.argv
            )
            if config.calibrate_pollsters or config.calibrate_bias
            else None
        )
        pure_provenance_recorder = (
            fp_model_provenance.PureTrendRecorder(
                [os.path.basename(sys.executable)] + sys.argv
            )
            if (
                config.pure
                and not config.calibrate_pollsters
                and not config.calibrate_bias
                and not config.cutoff_mode
            )
            else None
        )
        final_provenance_recorder = (
            fp_model_provenance.FinalTrendRecorder(
                [os.path.basename(sys.executable)] + sys.argv
            )
            if config.use_approvals() and not config.cutoff_mode
            else None
        )
        cutoff_provenance_recorder = (
            fp_model_provenance.CutoffTrendRecorder(
                [os.path.basename(sys.executable)] + sys.argv
            )
            if config.use_approvals() and config.cutoff_mode
            else None
        )
        calibration_checkpoint_store = (
            fp_model_checkpoints.CalibrationCheckpointStore()
            if config.calibrate_pollsters and not config.calibration_traces
            else None
        )

        maybe_generate_approvals(config)

        m_data = ModellingData()
        if config.cutoff_mode:
            cutoff_schedule = fp_model_provenance.cutoff_schedule()
            config.cutoff_output_store = (
                fp_model_provenance.CutoffOutputStore()
            )
            print(
                'Loaded {} triangular cutoff points shared with '
                'trend_adjust.py.'
                .format(len(cutoff_schedule))
            )
            work_items = list(cutoff_work_items(
                config,
                m_data,
                cutoff_schedule,
            ))
            cutoff_expected_endpoints = {}
            for (
                election,
                scheduled_days,
                poll_end_days,
                _,
            ) in work_items:
                cutoff_expected_endpoints.setdefault(
                    election.short(), []
                ).append({
                    'scheduled_cutoff_days': int(scheduled_days),
                    'poll_trend_end_days': int(poll_end_days),
                })
        else:
            work_items = (
                (election, 0, 0, True) for election in config.elections
            )
            cutoff_expected_endpoints = {}

        cutoff_elections_started = set()
        cutoff_federal_prior_files = {}
        for (
            desired_election,
            requested_cutoff_days,
            expected_poll_trend_end_days,
            final_cutoff_for_election,
        ) in work_items:
            config.cutoff_days = requested_cutoff_days
            e_data = build_election_data(ElectionDataInputs(
                config=config,
                m_data=m_data,
                desired_election=desired_election,
            ))
            if e_data is None:
                continue
            election_tag = ''.join(e_data.e_tuple)
            if (
                config.cutoff_mode
                and election_tag not in cutoff_elections_started
            ):
                cutoff_dependencies = None
                if cutoff_provenance_recorder is not None:
                    cutoff_dependencies = (
                        cutoff_provenance_recorder
                        .dependencies_for_election(
                            election_tag,
                            sorted(set(e_data.federal_prior_files)),
                        )
                    )
                resume_metadata = {
                    'schema_version': 1,
                    'election': election_tag,
                    'expected_endpoints': cutoff_expected_endpoints[
                        desired_election.short()
                    ],
                    'parties': list(m_data.parties[e_data.e_tuple]),
                    'probability_columns': [
                        '{}%'.format(round(probability * 100))
                        for probability in output_probabilities()
                    ],
                    'base_seed': base_seed,
                    'seed_namespace': STAN_SEED_NAMESPACE,
                    # Provenance lineage for certification. Evolving federal
                    # cutoff parents are refreshed via sync_cutoff_federal_priors
                    # and excluded from resume equality.
                    'dependencies': cutoff_dependencies,
                    # Local code/Stan contents for resume invalidation before
                    # source manifests are re-recorded.
                    'source_fingerprint': (
                        fp_model_checkpoints.fingerprint_files(
                            fp_model_source_files()
                            + [
                                Path(fp_model_provenance.__file__),
                                Path('./Models/fp_model.stan'),
                            ]
                        )
                    ),
                    'federal_prior_files': sorted(
                        set(e_data.federal_prior_files)
                    ),
                }
                resumed = config.cutoff_output_store.begin(
                    election_tag, resume_metadata
                )
                if resumed:
                    cutoff_federal_prior_files[election_tag] = set(
                        config.cutoff_output_store.federal_prior_files(
                            election_tag
                        )
                    )
                else:
                    cutoff_federal_prior_files[election_tag] = set()
                cutoff_federal_prior_files[election_tag].update(
                    e_data.federal_prior_files
                )
                sync_cutoff_federal_priors(
                    config.cutoff_output_store,
                    cutoff_provenance_recorder,
                    election_tag,
                    cutoff_federal_prior_files[election_tag],
                )
                cutoff_elections_started.add(election_tag)
                print(
                    '{} consolidated cutoff working file for {}.'.format(
                        'Resumed' if resumed else 'Started a fresh',
                        election_tag,
                    )
                )
            if (
                config.cutoff_mode
                and e_data.days_to_election
                != expected_poll_trend_end_days
            ):
                raise ConfigError(
                    'Scheduled cutoff {}d for {} resolved to a {}d poll '
                    'endpoint, but preflight resolved it to {}d.'.format(
                        requested_cutoff_days,
                        desired_election.short(),
                        e_data.days_to_election,
                        expected_poll_trend_end_days,
                    )
                )
            if (
                config.cutoff_mode
                and config.cutoff_output_store.is_complete(
                    election_tag,
                    requested_cutoff_days,
                    e_data.days_to_election,
                )
            ):
                print(
                    'Scheduled cutoff {}d (poll trend ends {}d out) for '
                    'election {} is complete, skipping.'
                    .format(
                        requested_cutoff_days,
                        e_data.days_to_election,
                        desired_election.short(),
                    )
                )
                cutoff_federal_prior_files.setdefault(
                    election_tag, set()
                ).update(e_data.federal_prior_files)
                sync_cutoff_federal_priors(
                    config.cutoff_output_store,
                    cutoff_provenance_recorder,
                    election_tag,
                    cutoff_federal_prior_files[election_tag],
                )
                if final_cutoff_for_election:
                    promote_cutoff_output(
                        config.cutoff_output_store,
                        cutoff_provenance_recorder,
                        election_tag,
                        cutoff_federal_prior_files[election_tag],
                    )
                continue
            expected_cutoff_parties = (
                [
                    party
                    for party in m_data.parties[e_data.e_tuple]
                    if (
                        party in e_data.base_df
                        and e_data.base_df[party].notna().any()
                    )
                ]
                if config.cutoff_mode
                else []
            )
            for excluded_pollster in e_data.pollster_exclusions:
                # Each exclusion is an independent fit. Never allow a skipped
                # party to leave a median from the previous pollster round.
                e_data.others_medians = {}
                # Skip the unexcluded fit when LOO produced nothing and this
                # election does not publish a federal prior for state use.
                # Overlapping federal cycles still run the full fit so
                # Priors/{year}fed.csv can be written for later state runs.
                if should_skip_pollster_calibration(ShouldSkipPollsterCalibrationInputs(
                    config=config,
                    e_data=e_data,
                    excluded_pollster=excluded_pollster,
                )):
                    continue

                parties = m_data.parties[e_data.e_tuple]
                checkpoint_identity = None
                if calibration_checkpoint_store is not None:
                    checkpoint_identity = calibration_checkpoint_identity(
                        config,
                        e_data,
                        excluded_pollster,
                        base_seed,
                        parties,
                    )
                    checkpoint = calibration_checkpoint_store.load(
                        checkpoint_identity
                    )
                    if checkpoint is not None:
                        restore_calibration_checkpoint(
                            e_data, checkpoint_identity, checkpoint
                        )
                        print(
                            'Resumed completed calibration block for {} / {}.'
                            .format(
                                election_tag,
                                excluded_pollster or 'full fit',
                            )
                        )
                        continue

                for party in parties:
                    if not config.priority:
                        check_suspension(
                            before_pause=(
                                provenance_recorder.flush
                                if provenance_recorder is not None
                                else None
                            )
                        )

                    preparation_target = election_tag
                    if config.cutoff_mode:
                        preparation_target += (
                            f' at poll endpoint '
                            f'{e_data.days_to_election}d'
                        )
                    print(
                        f'*** Beginning preparation for {party} in '
                        f'{preparation_target} ***'
                    )

                    # This has to be done here because it updates the TPP based on
                    # others_medians, allowing the estimation of the size of
                    # minor parties that some pollsters don't report
                    maybe_create_tpp_series(MaybeCreateTppSeriesInputs(
                        desired_election=desired_election,
                        e_data=e_data,
                        m_data=m_data,
                        party=party,
                    ))

                    mode = stan_seed_mode(config, e_data)
                    random_seed = (
                        checkpoint_identity['party_seeds'][party]
                        if checkpoint_identity is not None
                        else calibration_provenance.derive_stan_seed(
                            base_seed,
                            election_tag,
                            party,
                            excluded_pollster,
                            '{}:{}'.format(STAN_SEED_NAMESPACE, mode),
                        )
                    )
                    output_context = run_party(RunPartyInputs(
                        config=config,
                        diagnostics_recorder=diagnostics_recorder,
                        e_data=e_data,
                        excluded_pollster=excluded_pollster,
                        m_data=m_data,
                        model_params=model_params,
                        party=party,
                        random_seed=random_seed,
                    ))
                    if output_context is not None:
                        e_data.resolved_stan_seeds[
                            (mode, excluded_pollster, party)
                        ] = random_seed
                    if (
                        provenance_recorder is not None
                        and output_context is not None
                        and config.calibration_traces
                    ):
                        output_files = [
                            output_filename_ctx(output_context, kind)
                            for kind in (
                                'trend',
                                'polls',
                                'house_effects',
                            )
                        ]
                        provenance_recorder.record_model_outputs(
                            election=election_tag,
                            party=party,
                            excluded_pollster=excluded_pollster,
                            bias_calibration=config.calibrate_bias,
                            outputs=output_files,
                            random_seed=random_seed,
                            feedback_files=sorted(set(
                                e_data.federal_prior_files
                            )),
                            feedback_category=e_data.federal_prior_category,
                        )
                    if (
                        pure_provenance_recorder is not None
                        and output_context is not None
                    ):
                        pure_dependencies = (
                            pure_provenance_recorder.dependencies_for(
                                election_tag,
                                e_data.federal_prior_files_by_party.get(
                                    party, []
                                ),
                            )
                        )
                        output_files = [
                            output_filename_ctx(output_context, kind)
                            for kind in (
                                'trend',
                                'polls',
                                'house_effects',
                            )
                        ]
                        pure_provenance_recorder.record(
                            election=election_tag,
                            party=party,
                            outputs=output_files,
                            dependencies=pure_dependencies,
                            random_seed=random_seed,
                        )
                    if (
                        final_provenance_recorder is not None
                        and output_context is not None
                    ):
                        final_dependencies = (
                            final_provenance_recorder.dependencies_for(
                                election_tag,
                                party,
                                e_data.federal_prior_files_by_party.get(
                                    party, []
                                ),
                            )
                        )
                        output_files = [
                            output_filename_ctx(output_context, kind)
                            for kind in (
                                'trend',
                                'polls',
                                'house_effects',
                            )
                        ]
                        final_provenance_recorder.record(
                            election=election_tag,
                            party=party,
                            outputs=output_files,
                            dependencies=final_dependencies,
                            random_seed=random_seed,
                        )
                if calibration_checkpoint_store is not None:
                    poll_records, federal_priors, stan_seeds = (
                        calibration_checkpoint_payload(
                            e_data, excluded_pollster
                        )
                    )
                    checkpoint_path = calibration_checkpoint_store.write(
                        checkpoint_identity,
                        poll_records,
                        federal_priors,
                        stan_seeds,
                    )
                    print(
                        'Saved calibration restart checkpoint at {}.'.format(
                            checkpoint_path
                        )
                    )
                # Preserve completed work-unit provenance if a later Stan fit
                # or a later excluded-pollster block is interrupted.
                if provenance_recorder is not None:
                    provenance_recorder.flush()

            if config.calibrate_pollsters:
                (
                    _trace_files,
                    summary_values,
                    residual_evidence_rows,
                ) = finalise_calibrations(
                    e_data=e_data,
                    trace_directory=(
                        config.calibration_trace_directory
                        if config.calibration_traces else None
                    ),
                )
                component_rows = calibration_summary.build_leave_one_out_rows(
                    election_tag, summary_values
                )
                component_output = calibration_summary.direct_component_path(
                    './Outputs/Calibration', election_tag, 'leave-one-out'
                )
                residual_evidence_output = (
                    calibration_summary.residual_evidence_path(
                        './Outputs/Calibration', election_tag
                    )
                )
                # Always publish a durable LOO component so compact can depend
                # on calibrate even for single-pollster / empty-LOO elections
                # (header-only file). Residual evidence is only written when
                # there are held-out residuals.
                calibration_summary.write_component_atomically(
                    component_output, component_rows
                )
                if component_rows:
                    calibration_summary.write_residual_evidence_atomically(
                        residual_evidence_output,
                        sorted(
                            residual_evidence_rows,
                            key=lambda row: (
                                row['party'],
                                row['pollster'],
                                row['poll_day_index'],
                                row['poll_index'],
                            ),
                        ),
                    )
                else:
                    if residual_evidence_output.is_file():
                        residual_evidence_output.unlink()
                    print(
                        'No leave-one-out residuals for {}; wrote empty LOO '
                        'component for compact dependency.'
                        .format(election_tag)
                    )
                calibration_seed_output = (
                    calibration_summary.seed_manifest_path(
                        './Outputs/Calibration',
                        election_tag,
                        'calibration',
                    )
                )
                calibration_summary.write_seed_manifest_atomically(
                    calibration_seed_output,
                    calibration_summary.build_seed_rows(
                        election_tag,
                        'calibration',
                        e_data.resolved_stan_seeds,
                    ),
                )
                federal_priors = write_federal_calibration_priors(e_data)
                if provenance_recorder is not None:
                    provenance_recorder.record_summaries(
                        election=election_tag,
                        outputs=[component_output],
                        residual_evidence=(
                            residual_evidence_output
                            if component_rows else None
                        ),
                    )
                    provenance_recorder.record_seed_manifest(
                        election_tag,
                        'calibration',
                        calibration_seed_output,
                    )
                    if federal_priors is not None:
                        provenance_recorder.record_federal_priors(
                            election_tag, federal_priors
                        )
                    provenance_recorder.flush()
                if calibration_checkpoint_store is not None:
                    calibration_checkpoint_store.clear_election(election_tag)
            if config.calibrate_bias:
                component_rows = calibration_summary.build_bias_rows(
                    election_tag,
                    [
                        (party, *values)
                        for party, values in e_data.calibration_bias_records.items()
                    ],
                )
                if not component_rows:
                    raise RuntimeError(
                        'Bias calibration for {} produced no abridged component '
                        'rows; refusing to publish an empty bias component.'
                        .format(election_tag)
                    )
                component_output = calibration_summary.direct_component_path(
                    './Outputs/Calibration', election_tag, 'bias'
                )
                calibration_summary.write_component_atomically(
                    component_output, component_rows
                )
                bias_seed_output = calibration_summary.seed_manifest_path(
                    './Outputs/Calibration', election_tag, 'bias'
                )
                calibration_summary.write_seed_manifest_atomically(
                    bias_seed_output,
                    calibration_summary.build_seed_rows(
                        election_tag,
                        'bias',
                        e_data.resolved_stan_seeds,
                    ),
                )
                if provenance_recorder is not None:
                    provenance_recorder.record_bias_component(
                        election_tag, component_output
                    )
                    provenance_recorder.record_seed_manifest(
                        election_tag, 'bias', bias_seed_output
                    )
                    provenance_recorder.flush()
                print(
                    'Bias calibration component for {} is complete; run '
                    'calibration_summary compact to publish its summary.'
                    .format(election_tag)
                )
            if cutoff_provenance_recorder is not None:
                config.cutoff_output_store.mark_complete(
                    election_tag,
                    requested_cutoff_days,
                    e_data.days_to_election,
                    expected_parties=expected_cutoff_parties,
                )
                cutoff_federal_prior_files.setdefault(
                    election_tag, set()
                ).update(e_data.federal_prior_files)
                sync_cutoff_federal_priors(
                    config.cutoff_output_store,
                    cutoff_provenance_recorder,
                    election_tag,
                    cutoff_federal_prior_files[election_tag],
                )
                if final_cutoff_for_election:
                    promote_cutoff_output(
                        config.cutoff_output_store,
                        cutoff_provenance_recorder,
                        election_tag,
                        cutoff_federal_prior_files[election_tag],
                    )

    # Write a simple completion marker for external batch monitors.
    except Exception as e:
        diagnostics_recorder.report(completed=False)
        if 'config' in locals():
            config.unnamed_others_diagnostics.report(completed=False)
        with open('itsdone.txt', 'w') as f:
            f.write('2')
        raise
    
    diagnostics_recorder.report()
    config.unnamed_others_diagnostics.report()
    with open('itsdone.txt', 'w') as f:
        f.write('1')
