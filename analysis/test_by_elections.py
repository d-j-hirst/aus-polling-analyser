import contextlib
import io
import tempfile
import unittest
import warnings
from pathlib import Path

import by_elections


class ByElectionTests(unittest.TestCase):
    def test_loads_boolean_strings_and_derives_report_columns(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / 'by-elections.csv'
            path.write_text(
                'Government,By-elec swing,Eventual swing,Statewide swing,Party change\n'
                'ALP,1.5,2.0,0.5,TRUE\n',
                encoding='utf-8',
            )
            data = by_elections.load_by_elections(path)

        self.assertTrue(data.loc[0, 'Party change'])
        self.assertEqual(data.loc[0, 'swingdev'], 1.5)
        self.assertEqual(data.loc[0, 'byelecswing'], 1.5)

    def test_rejects_missing_or_nonfinite_required_values(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / 'by-elections.csv'
            path.write_text(
                'Government,By-elec swing,Eventual swing,Statewide swing\n'
                'ALP,1.5,2.0,0.5\n',
                encoding='utf-8',
            )
            with self.assertRaisesRegex(by_elections.ByElectionDataError, 'Party change'):
                by_elections.load_by_elections(path)

            path.write_text(
                'Government,By-elec swing,Eventual swing,Statewide swing,Party change\n'
                'ALP,nan,2.0,0.5,FALSE\n',
                encoding='utf-8',
            )
            with self.assertRaisesRegex(by_elections.ByElectionDataError, 'non-finite'):
                by_elections.load_by_elections(path)

    def test_ols_and_quantile_reports_both_include_an_intercept(self):
        data = by_elections.pd.DataFrame({
            'byelecswing': [-3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0],
            'swingdev': [-1.4, -1.0, -0.2, 0.5, 0.9, 1.7, 2.2, 2.9],
        })
        with warnings.catch_warnings(), contextlib.redirect_stdout(io.StringIO()):
            warnings.simplefilter('ignore', UserWarning)
            ols_results, quantile_results = by_elections.report(data, 'test data')

        self.assertIn('const', ols_results.params.index)
        self.assertIn('Intercept', quantile_results.params.index)

    def test_report_rejects_empty_or_constant_cohorts(self):
        empty_data = by_elections.pd.DataFrame(
            columns=['byelecswing', 'swingdev']
        )
        with self.assertRaisesRegex(by_elections.ByElectionDataError, 'fewer'):
            by_elections.report(empty_data, 'empty data')

        constant_data = by_elections.pd.DataFrame({
            'byelecswing': [1.0, 1.0, 1.0],
            'swingdev': [0.0, 1.0, 2.0],
        })
        with self.assertRaisesRegex(by_elections.ByElectionDataError, 'no variation'):
            by_elections.report(constant_data, 'constant data')


if __name__ == '__main__':
    unittest.main()
