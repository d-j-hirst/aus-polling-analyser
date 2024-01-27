import math
import datetime
import statistics
import numpy as np
import pandas as pd
import statsmodels.api as sm

poll_files = ['fed','nsw','vic','qld','wa','sa']


def generate_synthetic_tpps(display_analysis=False):
    Approvals(display_analysis)


class Approvals:
    def __init__(self, display_analysis):
        self.load_data()
        self.create_synthetic_tpps()
        if (display_analysis): self.analyse_synthetic_tpps()
    
    def load_data(self):
        self.trends = {}
        self.polls = {}
        self.approvals = {}
        self.start_dates = {}
        self.end_dates = {}
        self.leaderships = {}
        ## Need to adjust this so that only "previous" elections are used
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
        for election in self.elections:
            self.load_election(election)
        for poll_file in poll_files:
            self.load_approvals(poll_file)
        for election in self.elections:
            self.weight_approvals(election)
        self.load_leaderships()
    
    def load_election(self, election):
        el_tag = f'{election[0]}{election[1]}'
        trend_filename = f'Outputs/fp_trend_{el_tag}_@TPP_pure.csv'
        with open(trend_filename, 'r') as f:
            lines = [b.strip().split(',') for b in f.readlines()]
        self.trends[election] = {
            int(a[0]): float(a[52])
            for a in lines[3:]
        }
        self.start_dates[election] = datetime.date(
            year=int(lines[1][2]),
            month=int(lines[1][1]),
            day=int(lines[1][0]),
        )
        self.end_dates[election] = (
            self.start_dates[election]
            + datetime.timedelta(days=len(lines)-3)
        )
        polls_filename = f'Outputs/fp_polls_{el_tag}_@TPP_pure.csv'
        with open(polls_filename, 'r') as f:
            self.polls[election] = [
                int(math.floor(float(a[1])))
                for a in [b.strip().split(',') for b in f.readlines()[1:]]
            ]
    
    def load_approvals(self, poll_file):
        filename = f'Data/poll-data-{poll_file}.csv'
        cols = ['MidDate', 'Firm', 'GLApp', 'GLDis']
        df = pd.read_csv(filename, usecols=cols)
        print(filename)
        approvals = [
            (
                datetime.date.fromisoformat(date),
                pollster,
                float(app)-float(dis)
            )
            for date, pollster, app, dis
            in zip(df['MidDate'], df['Firm'], df['GLApp'], df['GLDis'])
            if not math.isnan(app) and not math.isnan(dis)
        ]
        for election in self.elections:
            if election[1] != poll_file: continue
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
    
    def weight_approvals(self, election):
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

    def is_coalition(self, election, day):
        return ([
            a for a in 
            self.leaderships[election[1]]
            if (a[0] - self.start_dates[election]).days < day
        ][-1][1] != 'ALP')

    def get_leader(self, election, day):
        return ([
            a for a in 
            self.leaderships[election[1]]
            if (a[0] - self.start_dates[election]).days < day
        ][-1][2])
    
    def regression(
        self,
        target_election,
        target_pollster,
        observation,
        obs_date
    ):
        # This project only uses information "from the future"
        # up to the end of 2021 (so that a large enough sample)
        # size can be obtained)
        threshold = datetime.date.fromisoformat('2022-01-01')
        y = []
        x = []
        w = []
        obs_leader = self.get_leader(
            target_election,
            (obs_date - self.start_dates[target_election]).days
        )
        # Regress poll trend (for government) vs. approval rating
        for election, approvals in self.approvals.items():
            for day, pollster, netapp, weight in approvals:
                date = (self.start_dates[election] + datetime.timedelta(day))
                day_diff = (obs_date - date).days
                same_area = election[1] == target_election[1]
                # Don't use dates from the future if they're from the same
                # area or they're after the date threshold (end 2021)
                if (day_diff <= 12 and (same_area or
                    (date - threshold).days >= 0)): continue
                # Treat "future" dates as if they're 18 years ago
                # This makes sure that the regression isn't more
                # heavily sampled than it would have been at the time
                if day_diff < 0: day_diff += 365 * 18
                x.append(netapp)
                # Get last leader who entered office before this poll
                is_coalition = self.is_coalition(election, day)
                poll_leader = self.get_leader(election, day)
                alp_trend = self.trends[election][day]
                gov_trend = 100 - alp_trend if is_coalition else alp_trend
                if not election == target_election:
                    weight *= 0.1
                if not same_area:
                    weight *= 0.5
                if not obs_leader == poll_leader:
                    weight *= 0.2
                if not pollster == target_pollster:
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
        initial_param_ratio = 0
        while param_ratio < 0.7:
            x = sm.add_constant(x)
            wls_model = sm.WLS(y, x, weights=w)
            wls_results = wls_model.fit()

            alt_weights = [a ** 0 for a in w]

            alt_wls_model = sm.WLS(y, x, weights=alt_weights)
            alt_wls_results = alt_wls_model.fit()
            param_ratio = wls_results.params[1] / alt_wls_results.params[1]
            if initial_param_ratio == 0:
                initial_param_ratio = param_ratio

            w_sum = sum(w)

            if (param_ratio < 0.7): w = [a ** 0.9 for a in w]

        weight_sum = sum(initial_weights) * min(1, (initial_param_ratio + 0.3)) ** 2

        # The extra zero prevents this array from being implicitly
        # converted into something which would prevent the prediction from
        # working properly
        pred = np.array([observation, 0])
        pred = sm.add_constant(pred)
        predictions = [a + 50 for a in wls_results.predict(pred)]
        return (predictions[0], weight_sum)

    def create_synthetic_tpps(self):
        files = {}
        self.synthetic_tpps = {}
        for election in sorted(self.approvals.keys(), key=lambda x: x[0]):

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
                if not election[1] in files: files[election[1]] = []
                stpp_item = (date, pollster, synthetic_tpp, weight_sum)
                files[election[1]].append(stpp_item)
                if not election in self.synthetic_tpps:
                    self.synthetic_tpps[election] = []
                self.synthetic_tpps[election].append(stpp_item)
        for area, approvals in files.items():
            with open(f'Synthetic TPPs/{area}.csv', 'w') as f:
                for date, pollster, tpp, weight_sum in approvals:
                    f.write(f'{date.isoformat()},{pollster},{round(tpp, 3)},{round(weight_sum, 4)}\n')
    
    def analyse_synthetic_tpps(self):
        errors = []
        for threshold in [0.02, 0.1, 0.25, 0.5, 1, 2, 10000]:
            for election, stpp_items in self.synthetic_tpps.items():
                stpp_items = self.synthetic_tpps[election]
                for stpp_item in stpp_items:
                    if (stpp_item[3] > threshold): continue
                    day = (stpp_item[0] - self.start_dates[election]).days
                    trend_val = self.trends[election][day]
                    error = trend_val - stpp_item[2]
                    errors.append(error)
            print(threshold)
            print(f'{statistics.mean(errors)}')
            print(f'{statistics.mean(abs(a) for a in errors)}')
            print(f'{statistics.stdev(errors)}')
    

if __name__ == "__main__":
    generate_synthetic_tpps(True)