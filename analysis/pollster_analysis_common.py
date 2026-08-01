"""Shared configuration and input loading for pollster parameter analysis.

Parent: pollster_analysis.py coordinates the variability, house-effect and bias
reducers after this module selects an election and its calibration inputs. The
functions here intentionally preserve the former entry-point interfaces.
"""

import argparse
import math

import pandas as pd

from election_code import ElectionCode


directory = 'Outputs/Calibration'
LIBERAL_PARTY = 'LIB FP'
COALITION_PARTY = 'LNP FP'


def canonical_party(party):
    """Pool state Liberal evidence with the national Coalition series."""

    return COALITION_PARTY if party == LIBERAL_PARTY else party


def output_party(party, target_uses_liberal):
    if target_uses_liberal and party == COALITION_PARTY:
        return LIBERAL_PARTY
    return party


def output_paths(target_election):
    code = f'{target_election.year()}{target_election.region()}'
    return [
        f'{directory}/variability-{code}.csv',
        f'{directory}/he_weighting-{code}.csv',
        f'{directory}/biases-{code}.csv',
    ]


class ConfigError(ValueError):
    pass


def parse_finite_float(value, context):
    try:
        parsed = float(value)
    except (TypeError, ValueError) as error:
        raise ConfigError(
            '{} is not numeric: {!r}.'.format(context, value)
        ) from error
    if not math.isfinite(parsed):
        raise ConfigError('{} is not finite.'.format(context))
    return parsed


def parse_poll_day(line, filename):
    fields = line.rstrip('\r\n').split(',')
    if len(fields) <= 1:
        raise ConfigError(
            '{} contains a short poll row.'.format(filename)
        )
    return parse_finite_float(
        fields[1], '{} poll day'.format(filename)
    )


class Config:
    def __init__(self, argv=None):
        parser = argparse.ArgumentParser(
            description='Determine trend adjustment parameters')
        parser.add_argument('--election', action='store', type=str,
                            required=True,
                            help='Generate forecast trend for this election.'
                            ' Enter as 1234-xxx format,'
                            ' e.g. 2013-fed. Write "all" '
                            'to do it for all elections.')
        self.election_instructions = (
            parser.parse_args(argv).election.lower()
        )
        self.prepare_election_list()

    def prepare_election_list(self):
        with open('./Data/polled-elections.csv', 'r') as f:
            elections = ElectionCode.load_elections_from_file(f)
        with open('./Data/future-elections.csv', 'r') as f:
            elections += ElectionCode.load_elections_from_file(f)
        if self.election_instructions == 'all':
            self.elections = elections
        else:
            parts = self.election_instructions.split('-')
            if len(parts) < 2:
                raise ConfigError('Error in "elections" argument: given value '
                                  'did not have two parts separated '
                                  'by a hyphen (e.g. 2013-fed)')
            try:
                code = ElectionCode(parts[0], parts[1])
            except ValueError:
                raise ConfigError('Error in "elections" argument: first part '
                                  'of election name could not be converted '
                                  'into an integer')
            if code not in elections:
                raise ConfigError('Error in "elections" argument: '
                                  'value given did not match any election '
                                  'given in Data/polled-elections.csv')
            if len(parts) == 2:
                self.elections = [code]
            elif parts[2] == 'onwards':
                try:
                    self.elections = (elections[elections.index(code):])
                except ValueError:
                    raise ConfigError('Error in "elections" argument: '
                                  'value given did not match any election '
                                  'given in Data/polled-elections.csv')
            else:
                raise ConfigError('Invalid instruction in "elections"'
                                  'argument.')


def get_election_cycles():
    # Load the dates of each election cycle
    # to ensure that we don't use any data from the future
    with open('./Data/election-cycles.csv', 'r') as f:
        election_cycles = {
            (int(a[0]), a[1]):
            (
                pd.Timestamp(a[2]),
                pd.Timestamp(a[3])
            )
            for a in [b.strip().split(',')
            for b in f.readlines()]
        }
        return election_cycles
    

def get_links():
    with open('Data/linked-pollsters.csv', 'r') as input_file:
        return {
            parts[0]: parts[1:]
            for parts in (
                [field.strip() for field in line.split(',')]
                for line in input_file
                if line.strip()
            )
        }


def get_significant_parties(target_election):
    with open('Data/significant-parties.csv', 'r') as f:
        for line in f:
            parts = [a.strip() for a in line.split(',')]
            if (int(parts[0]) == target_election.year()
                and parts[1] == target_election.region()):
                return parts[2:]
    return []


def check_dates(election, target_election, cycles, equals=False):
    """Return whether an election's evidence predates the target endpoint."""

    if election.year() > target_election.year():
        return False
    if election.year() == target_election.year():
        # If we're in the same year, make sure that the election is earlier
        # than the target election (i.e. don't use the target election itself
        # or any future election in the same year)
        # if Equals is true, it's ok for the election to be the same as the
        # target election
        # (Later, add some logic for partial series of a parallel election cycle
        # when that series is before the target election)
        # Check the end dates of the corresponding election period
        if equals:
            if (cycles[
                (election.year(), election.region())
            ][1] > cycles[
                (target_election.year(), target_election.region())
            ][1]):
                return False
        else:
            if (cycles[
                (election.year(), election.region())
            ][1] >= cycles[
                (target_election.year(), target_election.region())
            ][1]):
                return False
    return True


