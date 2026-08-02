import csv
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import pandas as pd

import fp_model


class PartyOrderingTests(unittest.TestCase):
    def test_parties_are_ordered_by_model_dependency(self):
        self.assertEqual(
            fp_model.order_parties_for_model([
                '@TPP',
                'ALP FP',
                'OTH FP',
                'ONP FP',
                'GRN FP',
                'LNP FP',
            ]),
            [
                'ONP FP',
                'GRN FP',
                'OTH FP',
                'ALP FP',
                'LNP FP',
                '@TPP',
            ],
        )


class ConfigValidationTests(unittest.TestCase):
    def test_pure_calibration_combination_fails_before_loading_inputs(self):
        with mock.patch.object(
            fp_model.sys,
            'argv',
            [
                'fp_model.py',
                '--election',
                '2025-fed',
                '--pure',
                '--calibrate',
            ],
        ):
            with self.assertRaisesRegex(
                fp_model.ConfigError,
                '--pure cannot be combined',
            ):
                fp_model.Config()

    def test_bias_and_leave_one_out_combination_is_rejected(self):
        with mock.patch.object(
            fp_model.sys,
            'argv',
            [
                'fp_model.py',
                '--election',
                '2025-fed',
                '--bias',
                '--calibrate',
            ],
        ):
            with self.assertRaisesRegex(
                fp_model.ConfigError,
                '--calibrate cannot be combined',
            ):
                fp_model.Config()


class UnnamedOthersTests(unittest.TestCase):
    def test_ordinary_residual_is_unchanged(self):
        self.assertAlmostEqual(
            fp_model.derive_unnamed_others_median(20.0, 12.0),
            8.0,
        )

    def test_incoherent_components_use_soft_positive_residual(self):
        result = fp_model.derive_unnamed_others_median(
            16.338,
            17.574,
        )

        self.assertAlmostEqual(
            result,
            16.338 * 3.0 / 20.574,
        )
        self.assertGreater(result, 0.0)
        self.assertLessEqual(result, 16.338)

    def test_no_named_parties_does_not_impose_three_point_floor(self):
        self.assertAlmostEqual(
            fp_model.derive_unnamed_others_median(1.5, 0.0),
            1.5,
        )

    def test_diagnostics_keep_only_the_lowest_examples(self):
        recorder = fp_model.UnnamedOthersDiagnosticsRecorder(
            threshold=1.0,
            example_limit=2,
        )
        for raw_residual in (0.5, -1.0, 0.25):
            recorder.record(
                election='test',
                mode='test',
                day=0,
                inclusive_others=10.0 + raw_residual,
                named_minor_total=10.0,
                adjusted_unnamed_others=2.0,
            )

        self.assertEqual(recorder.issue_count, 3)
        self.assertEqual(
            [round(example[0], 2) for example in recorder.examples],
            [-1.0, 0.25],
        )


class ElectionBatchOrderingTests(unittest.TestCase):
    def setUp(self):
        self.elections = {
            code.short(): code
            for code in [
                fp_model.ElectionCode(2025, 'fed'),
                fp_model.ElectionCode(2026, 'vic'),
                fp_model.ElectionCode(2027, 'nsw'),
                fp_model.ElectionCode(2028, 'fed'),
                fp_model.ElectionCode(2029, 'wa'),
                fp_model.ElectionCode(2031, 'fed'),
            ]
        }
        self.cycles = {
            ('2025', 'fed'): (
                pd.Timestamp('2022-05-22'),
                pd.Timestamp('2025-05-03'),
            ),
            ('2026', 'vic'): (
                pd.Timestamp('2022-11-27'),
                pd.Timestamp('2026-11-28'),
            ),
            ('2027', 'nsw'): (
                pd.Timestamp('2023-03-26'),
                pd.Timestamp('2027-03-27'),
            ),
            ('2028', 'fed'): (
                pd.Timestamp('2025-05-04'),
                pd.Timestamp('2028-05-20'),
            ),
            ('2029', 'wa'): (
                pd.Timestamp('2025-03-09'),
                pd.Timestamp('2029-03-10'),
            ),
            ('2031', 'fed'): (
                pd.Timestamp('2028-05-21'),
                pd.Timestamp('2031-05-17'),
            ),
        }

    def test_federal_dependencies_are_emitted_before_states(self):
        original = [
            self.elections[code]
            for code in (
                '2025fed',
                '2026vic',
                '2027nsw',
                '2028fed',
                '2029wa',
                '2031fed',
            )
        ]

        ordered = fp_model.order_elections_by_federal_dependencies(
            original,
            self.cycles,
        )

        self.assertEqual(
            [election.short() for election in ordered],
            [
                '2025fed',
                '2028fed',
                '2026vic',
                '2027nsw',
                '2031fed',
                '2029wa',
            ],
        )

    def test_state_onwards_assumes_its_federal_terms_are_complete(self):
        starting_state = self.elections['2026vic']
        suffix = [
            self.elections[code]
            for code in (
                '2026vic',
                '2027nsw',
                '2028fed',
                '2029wa',
                '2031fed',
            )
        ]
        assumed_complete = fp_model.overlapping_federal_elections(
            starting_state,
            self.cycles,
        )
        selected = [
            election
            for election in suffix
            if election not in assumed_complete
        ]

        ordered = fp_model.order_elections_by_federal_dependencies(
            selected,
            self.cycles,
            assumed_complete,
        )

        self.assertEqual(
            [election.short() for election in ordered],
            [
                '2026vic',
                '2027nsw',
                '2031fed',
                '2029wa',
            ],
        )


class SuspensionTests(unittest.TestCase):
    def test_active_control_file_flushes_and_resets_after_enter(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / 'suspend.txt'
            path.write_text('1\n', encoding='utf-8')
            before_pause = mock.Mock()
            input_func = mock.Mock(return_value='')

            paused = fp_model.check_suspension(
                suspension_path=path,
                before_pause=before_pause,
                input_func=input_func,
            )

            self.assertTrue(paused)
            before_pause.assert_called_once_with()
            input_func.assert_called_once()
            self.assertEqual(path.read_text(encoding='utf-8'), '0\n')

    def test_inactive_control_file_does_not_prompt(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / 'suspend.txt'
            path.write_text('0\n', encoding='utf-8')
            before_pause = mock.Mock()
            input_func = mock.Mock()

            paused = fp_model.check_suspension(
                suspension_path=path,
                before_pause=before_pause,
                input_func=input_func,
            )

            self.assertFalse(paused)
            before_pause.assert_not_called()
            input_func.assert_not_called()


class CutoffElectionSelectionTests(unittest.TestCase):
    def make_config(self, election_instruction):
        completed = [
            fp_model.ElectionCode('2025', 'fed'),
            fp_model.ElectionCode('2026', 'sa'),
        ]
        future = [
            fp_model.ElectionCode('2026', 'vic'),
            fp_model.ElectionCode('2027', 'nsw'),
        ]
        with mock.patch('sys.argv', [
                'fp_model.py',
                '--election',
                election_instruction,
                '--cutoff',
            ]), mock.patch(
                'builtins.open', mock.mock_open()
            ), mock.patch.object(
                fp_model.ElectionCode,
                'load_elections_from_file',
                side_effect=[completed, future],
            ) as loader:
            config = fp_model.Config()
        return config, loader

    def test_all_cutoffs_stop_at_latest_completed_election(self):
        config, loader = self.make_config('all')

        self.assertEqual(
            [election.short() for election in config.elections],
            ['2025fed', '2026sa'],
        )
        loader.assert_called_once()

    def test_onwards_cutoffs_stop_at_latest_completed_election(self):
        config, loader = self.make_config('2025-fed-onwards')

        self.assertEqual(
            [election.short() for election in config.elections],
            ['2025fed', '2026sa'],
        )
        loader.assert_called_once()

    def test_future_election_cannot_be_requested_for_cutoffs(self):
        with self.assertRaisesRegex(
            fp_model.ConfigError,
            'Cutoff generation only supports completed elections',
        ):
            self.make_config('2026-vic')


class FederalCutoffPriorTests(unittest.TestCase):
    def test_latest_cutoff_not_after_state_endpoint_is_selected(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / 'cutoffs_2025fed.csv'
            with path.open('w', newline='', encoding='utf-8') as output:
                writer = csv.writer(output)
                writer.writerow([
                    'ScheduledCutoffDays',
                    'PollTrendEndDays',
                    'Party',
                    'StanSeed',
                    '50%',
                ])
                writer.writerow([30, 30, 'ONP FP', 1, 8.5])
                writer.writerow([20, 20, 'ONP FP', 2, 9.5])

            used_files = []
            with mock.patch.object(
                fp_model.fp_model_provenance,
                'cutoff_output_path',
                return_value=path,
            ):
                series = fp_model.load_fed_cutoff_median(
                    fp_model.LoadFedTrendMedianInputs(
                        available_through=pd.Timestamp('2025-04-05'),
                        election_end=pd.Timestamp('2025-05-03'),
                        election_year=2025,
                        party='ONP FP',
                        pure=False,
                        used_files=used_files,
                    )
                )

            self.assertEqual(list(series.index), [
                pd.Timestamp('2025-04-03'),
            ])
            self.assertEqual(list(series.values), [8.5])
            self.assertEqual(used_files, [str(path)])


class FederalCalibrationPriorTests(unittest.TestCase):
    def test_compact_calibration_prior_is_loaded_before_legacy_trace(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            (directory / '2028fed.csv').write_text(
                'Date,Party,50%\n'
                '2026-01-01,ONP FP,6.25\n'
                '2026-01-02,ONP FP,6.5\n',
                encoding='utf-8',
            )
            used_files = []
            with mock.patch.object(
                fp_model, 'CALIBRATION_PRIOR_DIRECTORY', directory
            ):
                series = fp_model.load_fed_trend_median(
                    fp_model.LoadFedTrendMedianInputs(
                        available_through=None,
                        election_end=None,
                        election_year=2028,
                        party='ONP FP',
                        pure=False,
                        calibration=True,
                        used_files=used_files,
                    )
                )

            self.assertEqual(
                list(series.index),
                [pd.Timestamp('2026-01-01'), pd.Timestamp('2026-01-02')],
            )
            self.assertEqual(list(series.values), [6.25, 6.5])
            self.assertEqual(used_files, [str(directory / '2028fed.csv')])

    def test_missing_calibration_prior_does_not_fall_back_to_final_trend(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            with mock.patch.object(
                fp_model,
                'CALIBRATION_PRIOR_DIRECTORY',
                Path(temporary_directory),
            ), mock.patch.object(
                fp_model.os.path,
                'exists',
                return_value=False,
            ):
                with self.assertRaisesRegex(
                    fp_model.ConfigError,
                    'requires a federal calibration prior',
                ):
                    fp_model.load_fed_trend_median(
                        fp_model.LoadFedTrendMedianInputs(
                            available_through=None,
                            election_end=None,
                            election_year=2028,
                            party='ONP FP',
                            pure=False,
                            calibration=True,
                            used_files=[],
                        )
                    )

    def test_federal_prior_writer_records_full_fit_medians(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            e_data = SimpleNamespace(
                e_tuple=('2028', 'fed'),
                expected_parties=('GRN FP', 'ONP FP'),
                start=pd.Timestamp('2026-01-01'),
                n_days=2,
                calibration_federal_priors={
                    'GRN FP': [
                        (pd.Timestamp('2026-01-01'), 11.25),
                        (pd.Timestamp('2026-01-02'), 11.5),
                    ],
                    'ONP FP': [
                        (pd.Timestamp('2026-01-01'), 6.25),
                        (pd.Timestamp('2026-01-02'), 6.5),
                    ],
                },
            )
            with mock.patch.object(
                fp_model, 'CALIBRATION_PRIOR_DIRECTORY', directory
            ):
                output = fp_model.write_federal_calibration_priors(e_data)

            self.assertEqual(output, directory / '2028fed.csv')
            with output.open(newline='', encoding='utf-8') as source:
                rows = list(csv.DictReader(source))
            self.assertEqual(
                rows,
                [
                    {'Date': '2026-01-01', 'Party': 'GRN FP', '50%': '11.25'},
                    {'Date': '2026-01-02', 'Party': 'GRN FP', '50%': '11.5'},
                    {'Date': '2026-01-01', 'Party': 'ONP FP', '50%': '6.25'},
                    {'Date': '2026-01-02', 'Party': 'ONP FP', '50%': '6.5'},
                ],
            )

    def test_incomplete_federal_prior_does_not_replace_existing_file(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            output = directory / '2028fed.csv'
            output.write_text('certified\n', encoding='utf-8')
            e_data = SimpleNamespace(
                e_tuple=('2028', 'fed'),
                expected_parties=('GRN FP', 'ONP FP'),
                start=pd.Timestamp('2026-01-01'),
                n_days=2,
                calibration_federal_priors={
                    'GRN FP': [
                        (pd.Timestamp('2026-01-01'), 11.25),
                        (pd.Timestamp('2026-01-02'), 11.5),
                    ],
                },
            )
            with mock.patch.object(
                fp_model, 'CALIBRATION_PRIOR_DIRECTORY', directory
            ):
                with self.assertRaisesRegex(
                    fp_model.ConfigError, 'incomplete party coverage'
                ):
                    fp_model.write_federal_calibration_priors(e_data)

            self.assertEqual(
                output.read_text(encoding='utf-8'),
                'certified\n',
            )


class ApprovalRangeTests(unittest.TestCase):
    def test_generated_approvals_are_loaded_from_the_run_snapshot(self):
        context = SimpleNamespace(
            config=SimpleNamespace(
                synthetic_tpps_by_region={
                    'vic': (
                        (
                            pd.Timestamp('2020-01-09').date(),
                            'Pollster',
                            52.0,
                            0.5,
                        ),
                    ),
                },
            ),
            e_data=SimpleNamespace(e_tuple=('2020', 'vic')),
        )

        self.assertEqual(
            fp_model.load_approvals(context),
            [(pd.Timestamp('2020-01-09'), 52.0, 0.5)],
        )

    def test_future_and_zero_information_approvals_are_excluded(self):
        context = SimpleNamespace(
            e_data=SimpleNamespace(
                start=pd.Timestamp('2020-01-01'),
                start_date=pd.Timestamp('2020-01-01'),
                end=pd.Timestamp('2020-01-10'),
                end_date=pd.Timestamp('2020-01-31'),
                n_days=10,
            )
        )
        approvals = [
            (pd.Timestamp('2020-01-05'), 51.0, 0.0),
            (pd.Timestamp('2020-01-09'), 52.0, 0.5),
            (pd.Timestamp('2020-01-11'), 53.0, 0.5),
        ]

        approvals = fp_model.filter_approvals_by_cycle(
            approvals,
            context,
        )
        approvals, days = fp_model.filter_approvals_by_poll_range(
            approvals,
            context,
        )

        self.assertEqual(approvals, [
            (pd.Timestamp('2020-01-09'), 52.0, 0.5),
        ])
        self.assertEqual(days, [9])

    def test_absent_or_out_of_range_approvals_are_quiet(self):
        context = SimpleNamespace(
            e_data=SimpleNamespace(
                start=pd.Timestamp('2020-01-01'),
                n_days=10,
            )
        )
        with mock.patch('builtins.print') as print_mock:
            approvals, days = fp_model.filter_approvals_by_poll_range(
                [(pd.Timestamp('2020-02-01'), 52.0, 0.5)],
                context,
            )
        self.assertEqual((approvals, days), ([], []))
        print_mock.assert_not_called()


class SparseCalibrationControlFlowTests(unittest.TestCase):
    def test_pollster_without_party_observation_skips_quietly(self):
        context = SimpleNamespace(
            e_data=SimpleNamespace(
                base_df=pd.DataFrame({
                    'Firm': ['Firm A'],
                    '@TPP': [51.0],
                    'GRN FP': [float('nan')],
                })
            ),
            party='GRN FP',
            excluded_pollster='Firm A',
            config=SimpleNamespace(calibrate_pollsters=True),
        )
        with mock.patch('builtins.print') as print_mock:
            result = fp_model.prepare_poll_df(context)
        self.assertIsNone(result)
        print_mock.assert_not_called()


class IndexingTests(unittest.TestCase):
    def test_election_day_uses_same_one_based_compression_as_polls(self):
        model_params = fp_model.ModelParams(tFactor=2)
        result = fp_model.build_reduced_series(
            fp_model.ReducedSeriesInputs(
                discontinuities_filtered=[0],
                e_data=SimpleNamespace(
                    election_day=10,
                    n_days=5,
                ),
                model_params=model_params,
                poll_vectors=SimpleNamespace(
                    pollDays=[1, 5],
                ),
                prior_series=fp_model.PriorSeries(
                    prior_series_daily=[1.0] * 5,
                    sigma_daily=[1.0] * 5,
                ),
            )
        )

        self.assertEqual(result.tElectionDay, 6)
        self.assertEqual(result.tPollDays, [1, 3])

    def test_campaign_windows_are_calendar_offsets_not_compressed_nodes(self):
        model_params = fp_model.ModelParams(tFactor=2)
        result = fp_model.build_reduced_series(
            fp_model.ReducedSeriesInputs(
                discontinuities_filtered=[0],
                e_data=SimpleNamespace(
                    election_day=100,
                    n_days=101,
                ),
                model_params=model_params,
                poll_vectors=SimpleNamespace(pollDays=[1, 101]),
                prior_series=fp_model.PriorSeries(
                    prior_series_daily=[1.0] * 101,
                    sigma_daily=[1.0] * 101,
                ),
            )
        )

        self.assertEqual(result.tElectionDay, 51)
        self.assertEqual(result.tCampaignStartDay, 30)
        self.assertEqual(result.tFinalStartDay, 44)
        self.assertEqual(
            (result.tElectionDay - result.tCampaignStartDay)
            * model_params.tFactor,
            fp_model.CAMPAIGN_WINDOW_DAYS,
        )
        self.assertEqual(
            (result.tElectionDay - result.tFinalStartDay)
            * model_params.tFactor,
            fp_model.FINAL_WINDOW_DAYS,
        )

    def test_discontinuity_breaks_transition_entering_event_date(self):
        self.assertEqual(
            fp_model.transition_entering_calendar_offset(1, 1),
            1,
        )
        self.assertEqual(
            fp_model.transition_entering_calendar_offset(8, 2),
            4,
        )
        self.assertEqual(
            fp_model.transition_entering_calendar_offset(9, 2),
            5,
        )
        self.assertEqual(
            fp_model.transition_entering_calendar_offset(0, 2),
            0,
        )

    def test_exported_house_effect_mix_matches_stan_formula(self):
        for days_before in (0, 59, 60, 90, 119, 120, 180):
            python_factor = fp_model.house_effect_new_factor(
                days_before, 60, 120
            )
            if days_before >= 120:
                stan_factor = 0.0
            elif days_before >= 60:
                stan_factor = (120 - days_before) / (120 - 60)
            else:
                stan_factor = 1.0
            self.assertAlmostEqual(python_factor, stan_factor)

    def test_internal_median_uses_the_fifty_percent_column(self):
        probabilities = tuple(
            [0.001]
            + [index * 0.01 for index in range(1, 100)]
            + [0.999]
        )
        summary = [
            [0.0] * (3 + len(probabilities)),
            [0.0, 0.0, 0.0] + list(range(len(probabilities))),
        ]
        days = list(fp_model.iter_trend_days(
            fp_model.IterTrendDaysInputs(
                e_data=SimpleNamespace(n_days=1),
                run_context=SimpleNamespace(
                    model_params=SimpleNamespace(tFactor=1),
                    poll_vectors=SimpleNamespace(n_houses=0),
                    reduced_series=SimpleNamespace(tDayCount=1),
                ),
                summary=summary,
                output_probs_t=probabilities,
            )
        ))

        self.assertEqual(days[0].median_val, 50)


class SeedIdentityTests(unittest.TestCase):
    def test_mode_labels_separate_pure_final_and_actual_cutoffs(self):
        e_data = SimpleNamespace(days_to_election=17)
        self.assertEqual(
            fp_model.stan_seed_mode(
                SimpleNamespace(
                    calibrate_bias=False,
                    calibrate_pollsters=False,
                    cutoff_mode=False,
                    pure=True,
                ),
                e_data,
            ),
            'pure',
        )
        self.assertEqual(
            fp_model.stan_seed_mode(
                SimpleNamespace(
                    calibrate_bias=False,
                    calibrate_pollsters=False,
                    cutoff_mode=False,
                    pure=False,
                ),
                e_data,
            ),
            'final',
        )
        self.assertEqual(
            fp_model.stan_seed_mode(
                SimpleNamespace(
                    calibrate_bias=False,
                    calibrate_pollsters=False,
                    cutoff_mode=True,
                    pure=False,
                ),
                e_data,
            ),
            'cutoff-17d',
        )

    def test_seed_is_stable_and_changes_with_mode(self):
        common = (fp_model.DEFAULT_BASE_SEED, '2025fed', '@TPP', '')
        final_seed = fp_model.calibration_provenance.derive_stan_seed(
            *common, '{}:final'.format(fp_model.STAN_SEED_NAMESPACE)
        )
        self.assertEqual(
            final_seed,
            fp_model.calibration_provenance.derive_stan_seed(
                *common, '{}:final'.format(fp_model.STAN_SEED_NAMESPACE)
            ),
        )
        self.assertNotEqual(
            final_seed,
            fp_model.calibration_provenance.derive_stan_seed(
                *common, '{}:pure'.format(fp_model.STAN_SEED_NAMESPACE)
            ),
        )


class CalibrationSemanticsTests(unittest.TestCase):
    def test_house_effect_is_unweighted_mean_residual(self):
        excluded = [
            fp_model.ExcludedPoll(0, 52.0, 0, 'Firm A'),
            fp_model.ExcludedPoll(1, 48.0, 1, 'Firm A'),
            fp_model.ExcludedPoll(1, 53.0, 2, 'Firm B'),
        ]
        result = fp_model.compute_pollster_house_effects(
            fp_model.ComputerPollsterHouseEffectsInputs(
                excluded_polls=excluded,
                median_col=0,
                parent_inputs=SimpleNamespace(
                    trend_outputs=SimpleNamespace(
                        day_data=[[50.0], [51.0]]
                    )
                ),
            )
        )
        self.assertEqual(result, {'Firm A': -0.5, 'Firm B': 2.0})

    def test_median_only_calibration_omits_unused_percentile_diagnostics(self):
        result = fp_model.build_poll_calibration(
            fp_model.BuildPollCalibrationInputs(
                poll=fp_model.ExcludedPoll(0, 52.0, 3, 'Firm A'),
                day_data=[[50.0]],
                median_col=0,
                output_probs=(0.5,),
                house_effects={'Firm A': 1.0},
                df_daynum=pd.Series([1, 3]),
            )
        )
        self.assertEqual(result.adjusted_vote, 51.0)
        self.assertEqual(result.deviation, 1.0)
        self.assertIsNone(result.percentile)
        self.assertIsNone(result.prob_deviation)

    def test_finalise_keeps_existing_mae_and_full_fit_weighting(self):
        e_data = SimpleNamespace(
            e_tuple=('2025', 'fed'),
            poll_calibrations={
                ('Firm A', 0, '@TPP', 3): (
                    52.0, 50.0, 52.0, None, 2.0, None, 4.0
                ),
                ('', 0, '@TPP', 3): (
                    52.0, 51.0, 52.0, None, 1.0, None, 4.0
                ),
            },
        )

        files, summaries, evidence = fp_model.finalise_calibrations(e_data)

        self.assertEqual(files, [])
        self.assertEqual(len(summaries), 1)
        party, pollster, weighted_abs_error, weight = summaries[0]
        self.assertEqual((party, pollster), ('@TPP', 'Firm A'))
        self.assertAlmostEqual(weighted_abs_error, 4.0)
        self.assertAlmostEqual(weight, 0.5)
        self.assertEqual(len(evidence), 1)
        self.assertAlmostEqual(evidence[0]['quotient_weight'], 0.5)
        self.assertAlmostEqual(evidence[0]['final_weight'], 0.5)

    def test_checkpoint_resume_matches_uninterrupted_compact_outputs(self):
        loo_key = ('Firm A', 0, '@TPP', 3)
        full_key = ('', 0, '@TPP', 3)
        loo_value = (52.0, 50.0, 52.0, None, 2.0, None, 4.0)
        full_value = (52.0, 51.0, 52.0, None, 1.0, None, 4.0)
        uninterrupted = SimpleNamespace(
            e_tuple=('2025', 'fed'),
            poll_calibrations={
                loo_key: loo_value,
                full_key: full_value,
            },
        )
        expected = fp_model.finalise_calibrations(uninterrupted)[1:]

        interrupted = SimpleNamespace(
            poll_calibrations={loo_key: loo_value},
            calibration_federal_priors={},
            resolved_stan_seeds={
                ('calibration', 'Firm A', '@TPP'): 123,
            },
        )
        records, priors, stan_seeds = fp_model.calibration_checkpoint_payload(
            interrupted, 'Firm A'
        )
        identity = {
            'election': '2025fed',
            'excluded_pollster': 'Firm A',
            'mode': 'calibration',
            'base_seed': 1,
            'seed_namespace': fp_model.STAN_SEED_NAMESPACE,
            'parties': ['@TPP'],
            'party_seeds': {'@TPP': 123},
            'source_fingerprint': 'inputs',
        }
        with tempfile.TemporaryDirectory() as temporary_directory:
            store = (
                fp_model.fp_model_checkpoints.CalibrationCheckpointStore(
                    temporary_directory
                )
            )
            store.write(identity, records, priors, stan_seeds)
            resumed = SimpleNamespace(
                e_tuple=('2025', 'fed'),
                poll_calibrations={},
                calibration_federal_priors={},
                resolved_stan_seeds={},
            )
            fp_model.restore_calibration_checkpoint(
                resumed, identity, store.load(identity)
            )
            resumed.poll_calibrations[full_key] = full_value
            actual = fp_model.finalise_calibrations(resumed)[1:]

        self.assertEqual(actual, expected)


class DiagnosticRecorderTests(unittest.TestCase):
    def test_failed_checks_are_accumulated_across_models(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / 'diagnostics.log'
            recorder = fp_model.StanDiagnosticsRecorder(str(path))
            all_pass = {
                check: True
                for check in recorder.EXPECTED_CHECKS
            }
            recorder.record(
                '2026vic',
                'ALP FP',
                '',
                1,
                all_pass,
            )
            some_fail = dict(all_pass)
            some_fail['Rhat'] = False
            recorder.record(
                '2026vic',
                'ONP FP',
                'Pollster',
                2,
                some_fail,
                mode='calibration',
            )
            recorder.report()

            contents = path.read_text(encoding='utf-8')

        self.assertIn('2026vic | ONP FP', contents)
        self.assertIn('mode calibration', contents)
        self.assertIn('Rhat=1', contents)
        self.assertIn('1 of 2 Stan models', contents)


if __name__ == '__main__':
    unittest.main()
