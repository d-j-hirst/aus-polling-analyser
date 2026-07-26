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
            )
            recorder.report()

            contents = path.read_text(encoding='utf-8')

        self.assertIn('2026vic | ONP FP', contents)
        self.assertIn('Rhat=1', contents)
        self.assertIn('1 of 2 Stan models', contents)


if __name__ == '__main__':
    unittest.main()
