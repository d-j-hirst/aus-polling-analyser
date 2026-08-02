"""Generate the historical adjustments used by the election forecast model.

This command coordinates input loading, fundamentals fitting, poll/fundamentals
mixing, output validation and provenance recording. The calculation stages live
in focused modules so they can be understood and tested independently.

Main functions:
* ``Config`` validates requested elections, horizons and authored inputs.
* ``generate_staged_target_outputs`` performs one target election's core
  fundamentals/mixing calculation and validates its staged files.
* ``trend_adjust`` loads all inputs, orders targets, promotes complete output
  bundles and records their provenance.
"""

import argparse
import os
from pathlib import Path
import sys
import tempfile

import generated_provenance
import trend_adjust_provenance
from election_code import ElectionCode, no_target_election_marker
from trend_adjust_cutoffs import CutoffTrendError
from trend_adjust_data import (
    Inputs,
    PartyGroupConfig,
    PollTrend,
    TrendAdjustmentDataError,
)
from trend_adjust_fundamentals import run_fundamentals_regression
from trend_adjust_mixing import generate_adjustments
from trend_adjust_io import (
    promote_staged_outputs,
    validate_generated_adjustment,
    validate_generated_fundamentals,
)
from trend_adjust_check import check_poll_predictiveness


# Command-line configuration and input validation

class ConfigError(ValueError):
    pass


class Config:
    def __init__(self, party_groups):
        parser = argparse.ArgumentParser(
            description='Determine trend adjustment parameters')
        parser.add_argument('-f', '--files', action='store_true',
                            help='Show loaded files')
        parser.add_argument('-p', '--parameters', action='store_true',
                            help='Show parameters by party group and day')
        parser.add_argument('--election', action='store', type=str,
                            help='Exclude this election from calculations '
                            '(so that they can be used for hindcasting that '
                            'election). Enter as 1234-xxx format,'
                            ' e.g. 2013-fed. Write "none" to exclude no '
                            'elections (for present-day forecasting) or "all" '
                            'to do it for all elections (including "none"). '
                            'Default is "none"', default='none')
        parser.add_argument('--check', choices=('no', 'yes', 'only'),
                            help='Compares accuracy of projection types. Enter'
                            ' "no" (default) for no checking, yes for '
                            'checking after doing adjustment calculations, '
                            'and "only" to do only checks and skip '
                            'calculations (of course calculations will still '
                            'need to have been done at some point before). '
                            'Note the check will only include elections '
                            'included under the --election argument, if '
                            'you get a StatisticsError, try setting '
                            '"--election none" as well.'
                            , default='no')
        parser.add_argument('--checkday', action='store', type=int,
                            help='Number of days out to check poll data.'
                            ' Not used if --check is absent or set to "no".'
                            , default=300)
        parser.add_argument('--checkregion', action='store', type=str,
                            help='Filter for a region to check for'
                            '(e.g. "fed", "nsw" or "sa").'
                            'Use "nofed" to exclude federal election only.'
                            , default='')
        parser.add_argument('-w', '--writtenfiles', action='store_true',
                            help='Show written files')
        parser.add_argument('-u', '--fundamentals', action='store_true',
                            help='Show regression results for fundamentals '
                            'forecasts')
        parser.add_argument(
            '--diagnostics', nargs='?', const='*', default=None,
            metavar='CATEGORY',
            help='Show bounded day-zero poll-bias diagnostics. Optionally '
                 'give a party-group category such as "Misc-p"; omit the '
                 'category to show diagnostics for all party groups.')
        args = parser.parse_args()
        self.show_loaded_files = args.files
        self.show_parameters = args.parameters
        self.show_written_files = args.writtenfiles
        self.show_fundamentals = args.fundamentals
        self.diagnostic_party_group = args.diagnostics
        if self.diagnostic_party_group not in (None, '*'):
            matching_group = next(
                (group for group in party_groups.groups
                 if group.lower() == self.diagnostic_party_group.lower()),
                None)
            if matching_group is None:
                valid_groups = ', '.join(party_groups.groups)
                raise ConfigError(
                    f'Unknown diagnostics category '
                    f'"{self.diagnostic_party_group}". Valid categories: '
                    f'{valid_groups}')
            self.diagnostic_party_group = matching_group
        self.check = args.check
        self.check_day = args.checkday
        if self.check_day < 0:
            raise ConfigError('Check day cannot be negative')
        self.check_region = args.checkregion
        self.election_instructions = args.election.lower()
        self.prepare_election_list()
        day_test_count = 46
        self.days = [int((n * (n + 1)) / 2) for n in range(0, day_test_count)]

    def diagnostics_enabled_for(self, party_group):
        return self.diagnostic_party_group in ('*', party_group)

    def prepare_election_list(self):
        with open('./Data/polled-elections.csv', 'r') as f:
            elections = ElectionCode.load_elections_from_file(f)
        with open('./Data/future-elections.csv', 'r') as f:
            future_elections = ElectionCode.load_elections_from_file(f)
        if self.election_instructions == 'all':
            self.elections = elections + [no_target_election_marker] + future_elections
            self.elections = [e for e in self.elections if e.year() >= 1994]
        elif self.election_instructions == 'none':
            self.elections = [no_target_election_marker] + future_elections
        else:
            parts = self.election_instructions.split('-')
            if len(parts) != 2:
                raise ConfigError('Error in "elections" argument: given value '
                                  'did not consist of two parts separated '
                                  'by a hyphen (e.g. 2013-fed)')
            try:
                code = ElectionCode(parts[0], parts[1])
            except ValueError:
                raise ConfigError('Error in "elections" argument: first part '
                                  'of election name could not be converted '
                                  'into an integer')
            if code not in elections and code not in future_elections:
                raise ConfigError('Error in "elections" argument: given value '
                                  'value given did not match any election '
                                  'given in Data/polled-elections.csv ')
            self.elections = [code]


# Core fundamentals and forecast-error adjustment processing

def generate_staged_target_outputs(config, inputs, poll_trend, exclude):
    """Generate, validate and promote one election's complete output set."""

    with tempfile.TemporaryDirectory(
        prefix=f'.trend-adjust-{exclude.short()}-', dir='.'
    ) as staging_directory:
        staging_root = Path(staging_directory)
        fundamentals_output = run_fundamentals_regression(
            config,
            inputs,
            exclude,
            output_directory=staging_root / 'Fundamentals',
        )
        adjustment_outputs = generate_adjustments(
            config,
            inputs,
            poll_trend,
            exclude,
            output_directory=staging_root / 'Adjustments',
        )
        validate_generated_fundamentals(fundamentals_output)
        for filename in adjustment_outputs.values():
            validate_generated_adjustment(config, filename)
        return promote_staged_outputs(
            fundamentals_output, adjustment_outputs, inputs.party_groups
        )


# Workflow orchestration, output promotion and provenance

def trend_adjust():
    try:
        party_groups = PartyGroupConfig.load()
        config = Config(party_groups)
    except (ConfigError, OSError, TrendAdjustmentDataError) as e:
        print('Could not process configuration due to the following issue:')
        print(str(e))
        return 2

    if config.check != "only":
        try:
            recorder = trend_adjust_provenance.TrendAdjustmentRecorder(
                [os.path.basename(__file__)] + sys.argv[1:]
            )
        except generated_provenance.GeneratedProvenanceError as e:
            print('Could not prepare trend-adjustment provenance:')
            print(str(e))
            return 2

        for exclude in config.elections:
            print(f'Analysing pollsters for {exclude}')
            print(f'Beginning trend adjustment algorithm for: {exclude}')
            try:
                inputs = Inputs(exclude, party_groups)
            except TrendAdjustmentDataError as e:
                print('Could not validate trend-adjustment inputs:')
                print(str(e))
                return 2
            try:
                poll_trend = PollTrend(inputs, config)
            except CutoffTrendError as e:
                print('Could not load historical cutoff trends:')
                print(str(e))
                return 2

            # Leave this until now so it doesn't interfere with initialization
            # of poll_trend
            try:
                inputs.determine_eventual_others_results()
            except TrendAdjustmentDataError as e:
                print('Could not validate trend-adjustment inputs:')
                print(str(e))
                return 2
            try:
                dependencies = recorder.dependencies_for(
                    poll_trend.cutoff_record_keys
                )
            except generated_provenance.GeneratedProvenanceError as e:
                print('Could not validate trend-adjustment dependencies:')
                print(str(e))
                return 2
            try:
                fundamentals_output, adjustment_outputs = (
                    generate_staged_target_outputs(
                        config, inputs, poll_trend, exclude
                    )
                )
            except TrendAdjustmentDataError as e:
                print('Could not generate valid trend adjustments:')
                print(str(e))
                return 2
            try:
                recorder.record(
                    target_election=exclude.short(),
                    adjustment_outputs=adjustment_outputs,
                    fundamentals_output=fundamentals_output,
                    dependencies=dependencies,
                    expected_groups=party_groups.groups,
                )
            except generated_provenance.GeneratedProvenanceError as e:
                print('Could not record trend-adjustment provenance:')
                print(str(e))
                return 2
            print(f'Completed trend adjustment algorithm for: {exclude}')

    if config.check == "only" or config.check == "yes":
        check_poll_predictiveness(config)
    return 0


if __name__ == '__main__':
    sys.exit(trend_adjust())
