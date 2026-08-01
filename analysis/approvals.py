"""Derive low-weight synthetic TPP observations from leader approval polls.

The model relates historical net approval ratings to the voting-intention-only
TPP trend available before each observation. The resulting observations are
passed directly to normal fp_model runs; jurisdiction CSVs are retained as
diagnostic and provenance artefacts.
"""

import csv
import datetime
import math
import os
import statistics
import sys

import numpy as np
import pandas as pd
import statsmodels.api as sm

import approvals_provenance


POLL_REGIONS = ('fed', 'nsw', 'vic', 'qld', 'wa', 'sa')
MAX_WEIGHT_FLATTENING_ROUNDS = 100


class ApprovalDataError(ValueError):
    """Raised when an approval-model input cannot be used safely."""


def load_pure_trend(path):
    """Load a pure TPP median series by its percentile header."""

    with open(path, newline='', encoding='utf-8-sig') as source:
        rows = list(csv.reader(source))
    if len(rows) < 4 or '50%' not in rows[2]:
        raise ApprovalDataError(
            '{} lacks a usable 50% trend column.'.format(path)
        )
    if len(rows[1]) < 3:
        raise ApprovalDataError(
            '{} lacks a usable start date.'.format(path)
        )
    median_index = rows[2].index('50%')
    try:
        start_date = datetime.date(
            year=int(rows[1][2]),
            month=int(rows[1][1]),
            day=int(rows[1][0]),
        )
    except (TypeError, ValueError) as error:
        raise ApprovalDataError(
            '{} contains an invalid start date.'.format(path)
        ) from error

    trend = {}
    for line_number, row in enumerate(rows[3:], start=4):
        if len(row) <= median_index:
            raise ApprovalDataError(
                '{}:{} lacks its trend median.'.format(path, line_number)
            )
        try:
            day = int(row[0])
            median = float(row[median_index])
        except ValueError as error:
            raise ApprovalDataError(
                '{}:{} contains invalid trend data.'.format(
                    path, line_number
                )
            ) from error
        if not math.isfinite(median):
            raise ApprovalDataError(
                '{}:{} contains a non-finite trend median.'.format(
                    path, line_number
                )
            )
        if day in trend:
            raise ApprovalDataError(
                '{}:{} duplicates trend day {}.'.format(
                    path, line_number, day
                )
            )
        trend[day] = median

    expected_days = list(range(len(trend)))
    if sorted(trend) != expected_days:
        raise ApprovalDataError(
            '{} trend days must be contiguous and start at zero.'.format(path)
        )

    return trend, start_date, (
        start_date + datetime.timedelta(days=len(trend))
    )


def load_pure_poll_days(path):
    """Load model-day positions of voting-intention polls."""

    with open(path, newline='', encoding='utf-8-sig') as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or 'Day' not in reader.fieldnames:
            raise ApprovalDataError(
                '{} lacks a poll Day column.'.format(path)
            )
        poll_days = []
        for line_number, row in enumerate(reader, start=2):
            try:
                day = float(row['Day'])
            except (TypeError, ValueError) as error:
                raise ApprovalDataError(
                    '{}:{} contains an invalid poll day.'.format(
                        path, line_number
                    )
                ) from error
            if not math.isfinite(day):
                raise ApprovalDataError(
                    '{}:{} contains a non-finite poll day.'.format(
                        path, line_number
                    )
                )
            poll_days.append(math.floor(day))
    if not poll_days:
        raise ApprovalDataError(
            '{} contains no voting-intention polls.'.format(path)
        )
    return poll_days


def approval_confidence_factor(initial_param_ratio):
    """Scale evidence without allowing a poor fit to increase its weight."""

    bounded_ratio = max(0.0, min(1.0, initial_param_ratio + 0.3))
    return bounded_ratio ** 2


# This script is used to generate synthetic TPPs from approval ratings.
# It uses a regression model to predict the trend TPP at each individual
# approval poll based on the approval rating and other factors.
# In each case, only data at least 13 days before the poll is used to generate
# synthetic TPP.
def generate_synthetic_tpps(display_analysis=False):
    recorder = approvals_provenance.SyntheticTppRecorder(
        [os.path.basename(sys.executable)] + sys.argv
    )
    analysis = Approvals(display_analysis, recorder)
    return analysis.synthetic_tpps_by_region


class Approvals:
    def __init__(self, display_analysis, provenance_recorder=None):
        print('*** Generating synthetic TPPs ***')
        self.load_data()
        dependencies = (
            provenance_recorder.dependencies_for(
                {
                    "{}{}".format(*election)
                    for election, approvals in self.approvals.items()
                    if approvals
                }
            )
            if provenance_recorder is not None
            else None
        )
        self.create_synthetic_tpps()
        if provenance_recorder is not None:
            provenance_recorder.record(
                self.output_files,
                self.output_elections,
                dependencies,
            )
        if display_analysis:
            self.analyse_synthetic_tpps()
        print('*** Finished generating synthetic TPPs ***')
    
    def load_data(self):
        self.trends = {}
        self.polls = {}
        self.approvals = {}
        self.start_dates = {}
        self.end_dates = {}
        self.leaderships = {}

        with open('Data/polled-elections.csv', 'r') as f:
            self.elections = {
                (a[0], a[1])
                for a in [b.strip().split(',') for b in f.readlines()]
            }
        with open('Data/future-elections.csv', 'r') as f:
            self.elections = self.elections | {
                (a[0], a[1])
                for a in [b.strip().split(',') for b in f.readlines()]
            }
        approval_terms = approvals_provenance.approval_elections()
        for election in sorted(self.elections):
            if "{}{}".format(*election) not in approval_terms:
                continue
            self.load_election(election)
        for poll_file in POLL_REGIONS:
            self.load_approvals(poll_file)
        for election in sorted(self.elections):
            self.weight_approvals(election)
        self.load_leaderships()
    
    def load_election(self, election):
        el_tag = f'{election[0]}{election[1]}'
        trend_filename = f'Outputs/fp_trend_{el_tag}_@TPP_pure.csv'
        try:
            trend, start_date, end_date = load_pure_trend(trend_filename)
        except FileNotFoundError:
            # This is expected in the period while some previous poll
            # trends have not yet been generated. Eventually this
            # will be removed once all those poll trends have been generated
            # and a sample of the output data is uploaded as a repository.
            return
        self.trends[election] = trend
        self.start_dates[election] = start_date
        self.end_dates[election] = end_date
        polls_filename = f'Outputs/fp_polls_{el_tag}_@TPP_pure.csv'
        self.polls[election] = load_pure_poll_days(polls_filename)
    
    def load_approvals(self, poll_file):
        filename = f'Data/poll-data-{poll_file}.csv'
        cols = ['MidDate', 'Firm', 'GLApp', 'GLDis']
        df = pd.read_csv(filename, usecols=cols)
        approvals = [
            (
                datetime.date.fromisoformat(date),
                pollster,
                float(app)-float(dis)
            )
            for date, pollster, app, dis
            in zip(df['MidDate'], df['Firm'], df['GLApp'], df['GLDis'])
            if math.isfinite(app) and math.isfinite(dis)
        ]
        for election in sorted(self.elections):
            if election[1] != poll_file:
                continue
            if election not in self.start_dates:
                continue
            self.approvals[election] = [
                (
                    (date - self.start_dates[election]).days,
                    firm,
                    netapp
                )
                for date, firm, netapp in approvals
                if date >= self.start_dates[election]
                and date < self.end_dates[election]
            ]
    
    # This function calculates a weight for each approval poll
    # based on the number of polls in the same election, and how close
    # in time the poll is to the other polls.
    def weight_approvals(self, election):
        if election not in self.approvals: return
        
        def weight(date):
            return min(1, sum(
                0.3333 * 2 ** -(abs(date - poll) / 14)
                for poll in self.polls[election]
            ))

        self.approvals[election] = [
            (date, firm, netapp, weight(date))
            for date, firm, netapp in self.approvals[election]
        ]

    def load_leaderships(self):
        filename = f'Data/government-leaders.csv'
        with open(filename, 'r') as f:
            lines = [b.strip().split(',') for b in f.readlines()]
        for line in lines:
            if line[0] not in self.leaderships:
                self.leaderships[line[0]] = []
            self.leaderships[line[0]].append((
                datetime.date.fromisoformat(line[1]),
                line[2],
                line[3]
            ))
        for region in self.leaderships:
            self.leaderships[region].sort(key=lambda leadership: leadership[0])

    def get_leadership(self, election, day):
        """Return the government and leader in office before a model day."""

        eligible = [
            leadership
            for leadership in self.leaderships.get(election[1], ())
            if (
                leadership[0] - self.start_dates[election]
            ).days < day
        ]
        if not eligible:
            raise ApprovalDataError(
                'No government leader is configured before day {} of {}{}.'
                .format(day, *election)
            )
        return eligible[-1]

    def is_coalition(self, election, day):
        return self.get_leadership(election, day)[1] != 'ALP'

    def get_leader(self, election, day):
        return self.get_leadership(election, day)[2]
    
    # Use a regression to create a prediction for a specific poll
    def regression(
        self,
        target_election,
        target_pollster,
        observation,
        obs_date
    ):
        # Full smoothed historical trends are intentional here: the regression
        # estimates the eventual relationship between approvals and vote
        # support, rather than reproducing what was knowable at the time.
        y = []
        x = []
        w = []
        obs_leader = self.get_leader(
            target_election,
            (obs_date - self.start_dates[target_election]).days
        )
        # Regress poll trend (for government) vs. approval rating
        # Stable accumulation order avoids platform-dependent floating-point
        # differences without changing the regression being estimated.
        for election in sorted(self.approvals):
            approvals = self.approvals[election]
            for day, pollster, netapp, weight in approvals:
                date = (self.start_dates[election] + datetime.timedelta(day))
                day_diff = (obs_date - date).days
                same_area = election[1] == target_election[1]
                # Don't use dates very close to the poll, as the eventual trend
                # at that point would be too influenced by future polls
                if day_diff <= 12:
                    continue
                x.append(netapp)
                # Get last leader who entered office before this poll
                is_coalition = self.is_coalition(election, day)
                poll_leader = self.get_leader(election, day)
                alp_trend = self.trends[election][day]
                gov_trend = 100 - alp_trend if is_coalition else alp_trend
                if election != target_election:
                    weight *= 0.1
                if not same_area:
                    weight *= 0.5
                if obs_leader != poll_leader:
                    weight *= 0.2
                if pollster != target_pollster:
                    weight *= 0.1
                if same_area and obs_leader == poll_leader:
                    recent_threshold = 60
                    recent_weighting = 100
                    long_term_halflife = 730  # two years
                    if day_diff < recent_threshold:
                        weight *= 0.01 + 0.9 * (
                            recent_weighting **
                            (-abs(day_diff) / recent_threshold)
                        )
                    else:
                        weight *= 0.01 + 0.9 * (
                            2 ** (-(abs(day_diff) - recent_threshold) /
                            long_term_halflife) / recent_weighting
                        )
                else:
                    weight *= 0.01
                y.append(gov_trend-50)
                w.append(weight)

        y = np.array(y)
        x = np.array(x)

        if len(x) < 2:
            return (50, 0)

        # This process makes sure that the relationship between approvals
        # and trends for a specific poll is not far below the historical
        # relationship overall. If this relationship is too low, it is
        # a sign that the weightings are too high and the regression
        # will not extrapolate well.
        # It is also a sign that the approval is not a good indicator
        # of 2pp, so remember the initial ratio and use it to scale
        # the final weight sum
        initial_weights = [a for a in w]
        param_ratio = 0
        initial_param_ratio = None
        flattening_rounds = 0
        while param_ratio < 0.7:
            flattening_rounds += 1
            if flattening_rounds > MAX_WEIGHT_FLATTENING_ROUNDS:
                raise ApprovalDataError(
                    'Approval regression did not converge for {} {} on {}.'
                    .format(
                        '{}{}'.format(*target_election),
                        target_pollster,
                        obs_date,
                    )
                )
            x = sm.add_constant(x)
            wls_model = sm.WLS(y, x, weights=w)
            wls_results = wls_model.fit()

            alt_weights = [a ** 0 for a in w]

            alt_wls_model = sm.WLS(y, x, weights=alt_weights)
            alt_wls_results = alt_wls_model.fit()
            param_ratio = wls_results.params[1] / alt_wls_results.params[1]
            if not math.isfinite(float(param_ratio)):
                raise ApprovalDataError(
                    'Approval regression produced a non-finite slope ratio '
                    'for {} {} on {}.'.format(
                        '{}{}'.format(*target_election),
                        target_pollster,
                        obs_date,
                    )
                )
            if initial_param_ratio is None:
                initial_param_ratio = param_ratio

            if param_ratio < 0.7:
                w = [a ** 0.9 for a in w]

        weight_sum = (
            sum(initial_weights)
            * approval_confidence_factor(initial_param_ratio)
        )

        # The extra zero prevents this array from being implicitly
        # converted into something which would prevent the prediction from
        # working properly
        pred = np.array([observation, 0])
        pred = sm.add_constant(pred)
        predictions = [a + 50 for a in wls_results.predict(pred)]
        return (predictions[0], weight_sum)

    def create_synthetic_tpps(self):
        files = {}
        self.output_elections = {}
        self.synthetic_tpps = {}
        for election in sorted(self.approvals):

            for day, pollster, netapp, weight in self.approvals[election]:
                date = self.start_dates[election] + datetime.timedelta(day)
                synthetic_tpp, weight_sum = self.regression(
                    target_election=election,
                    target_pollster=pollster,
                    observation=netapp,
                    obs_date=date
                )
                if self.is_coalition(election, day):
                    synthetic_tpp = 100 - synthetic_tpp
                if election[1] not in files:
                    files[election[1]] = []
                if election[1] not in self.output_elections:
                    self.output_elections[election[1]] = set()
                self.output_elections[election[1]].add(
                    "{}{}".format(*election)
                )
                stpp_item = (date, pollster, synthetic_tpp, weight_sum)
                files[election[1]].append(stpp_item)
                if election not in self.synthetic_tpps:
                    self.synthetic_tpps[election] = []
                self.synthetic_tpps[election].append(stpp_item)
        # fp_model consumes this in-memory snapshot. The CSV files remain for
        # provenance and standalone consumers, but a concurrent generator
        # cannot change observations already selected for the current run.
        self.synthetic_tpps_by_region = {
            area: tuple(approvals)
            for area, approvals in files.items()
        }
        self.output_files = {}
        for area, approvals in files.items():
            filename = f'Synthetic TPPs/{area}.csv'
            with open(filename, 'w') as f:
                for date, pollster, tpp, weight_sum in approvals:
                    f.write(
                        f'{date.isoformat()},{pollster},{round(tpp, 3)},'
                        f'{round(weight_sum, 4)}\n'
                    )
            self.output_files[area] = filename
    
    def analyse_synthetic_tpps(self):
        for threshold in [0.02, 0.1, 0.25, 0.5, 1, 2, 10000]:
            errors = []
            for election, stpp_items in self.synthetic_tpps.items():
                for stpp_item in stpp_items:
                    if stpp_item[3] > threshold:
                        continue
                    day = (stpp_item[0] - self.start_dates[election]).days
                    trend_val = self.trends[election][day]
                    error = trend_val - stpp_item[2]
                    errors.append(error)
            if not errors:
                print(f'{threshold}: no synthetic TPPs at this threshold')
                continue
            print(threshold)
            print(f'{statistics.mean(errors)}')
            print(f'{statistics.mean(abs(a) for a in errors)}')
            print(f'{statistics.stdev(errors) if len(errors) > 1 else 0.0}')
    

if __name__ == "__main__":
    generate_synthetic_tpps(True)
