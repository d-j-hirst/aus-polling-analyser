import math
import datetime
import numpy as np
import pandas as pd
import statsmodels.api as sm

poll_files = ['fed','nsw','vic','qld','wa','sa']

class Data:
    def __init__(self):
        self.load_data()
        self.create_synthetic_tpps()
    
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
    
    def regression(
        self,
        target_election,
        target_pollster,
        observation,
        obs_date
    ):
        # Regress poll trend (for government) vs. approval rating
        y = []
        x = []
        w = []
        for election, approvals in self.approvals.items():
            for day, pollster, netapp, weight in approvals:
                if (self.start_dates[election] + datetime.timedelta(day) 
                    >= obs_date): continue
                x.append(netapp)
                # Get last leader who entered office before this poll
                is_coalition = self.is_coalition(election, day)
                alp_trend = self.trends[election][day]
                gov_trend = 100 - alp_trend if is_coalition else alp_trend
                if not election == target_election:
                    weight *= 0.1
                if not election[1] == target_election[1]:
                    weight *= 0.5
                if not pollster == target_pollster:
                    weight *= 0.1
                # *** check if it's the same leader too
                y.append(gov_trend-50)
                w.append(weight)
        y = np.array(y)
        x = np.array(x)

        x = sm.add_constant(x)
        wls_model = sm.WLS(y, x, weights=w)
        wls_results = wls_model.fit()

        # The extra zero prevents this array from being implicitly
        # converted into something which would prevent the prediction from
        # working properly
        pred = np.array([observation, 0])
        pred = sm.add_constant(pred)
        predictions = [a + 50 for a in wls_results.predict(pred)]
        return predictions[0]

    def create_synthetic_tpps(self):
        #for election, approvals in self.approvals.items():
        election = ('2025', 'fed')
        for day, pollster, netapp, weight in self.approvals[election]:
            date = self.start_dates[election] + datetime.timedelta(day)
            synthetic_tpp = self.regression(
                target_election=election,
                target_pollster=pollster,
                observation=netapp,
                obs_date=date
            )
            if self.is_coalition(election, day):
                synthetic_tpp = 100 - synthetic_tpp
            print(str(date) + ' ' + pollster + ' ' + str(synthetic_tpp))




                


if __name__ == "__main__":
    data = Data()