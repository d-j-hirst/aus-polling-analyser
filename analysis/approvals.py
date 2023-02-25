import math
import datetime
import pandas as pd

poll_files = ['fed','nsw','vic','qld','wa','sa']

class Data:
    def __init__(self):
        self.load_data()
    
    def load_data(self):
        self.trends = {}
        self.polls = {}
        self.approvals = {}
        self.start_dates = {}
        self.end_dates = {}
        with open('Data/polled-elections.csv', 'r') as f:
            self.elections = {
                (a[0], a[1])
                for a in [b.strip().split(',') for b in f.readlines()]
            }
        for election in self.elections:
            self.load_election(election)
        for poll_file in poll_files:
            self.load_approvals(poll_file)
        for election in self.elections:
            self.weight_approvals(election)
        print(self.approvals[('2021', 'wa')])
    
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
                firm,
                float(app)-float(dis)
            )
            for date, firm, app, dis
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




                


if __name__ == "__main__":
    data = Data()