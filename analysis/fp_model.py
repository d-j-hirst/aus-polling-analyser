import argparse
import math
import numpy as np
import pandas as pd
import pystan
import sys
from time import perf_counter
from datetime import timedelta
from election_code import ElectionCode, no_target_election_marker

from stan_cache import stan_cache


# File paths for polling data in each jurisdiction
data_source = {
    'fed': './Data/poll-data-fed.csv',
    'nsw': './Data/poll-data-nsw.csv',
    'vic': './Data/poll-data-vic.csv',
    'qld': './Data/poll-data-qld.csv',
    'wa': './Data/poll-data-wa.csv',
    'sa': './Data/poll-data-sa.csv',
    }


# N.B. The "Others" (OTH) "party" values include votes for these other
# minor parties, so these are effectively counted twice. The reason for
# this is that many polls do not report separate UAP/ONP figures, so they
# are aggregated from the polls that do, count them together with the
# other "others" under OTH, and then (in the main program) subtract the
# minor parties from the OTH value to get the true exclusive-others value
others_parties = ['ONP FP', 'UAP FP', 'SFF FP', 'CA FP',
                'KAP FP', 'SAB FP', 'DEM FP', 'FF FP']


class ConfigError(ValueError):
    pass


class Config:
    def __init__(self):
        parser = argparse.ArgumentParser(
            description='Determine trend adjustment parameters')
        parser.add_argument('--election', action='store', type=str,
                            help='Generate forecast trend for this election.'
                            'Enter as 1234-xxx format,'
                            ' e.g. 2013-fed. Write "none" to exclude no '
                            'elections (for present-day forecasting) or "all" '
                            'to do it for all elections (including "none"). '
                            'Default is "none"', default='none')
        parser.add_argument('-c', '--calibrate-pollsters', action='store_true',
                            help='If set, will run in pollster calibration '
                            'mode. This will exclude each pollster from '
                            'calculations so that their polls can be calibrated '
                            'using the trend from the other polls.', default='none')
        self.election_instructions = parser.parse_args().election.lower()
        self.prepare_election_list()

    def prepare_election_list(self):
        with open('./Data/polled-elections.csv', 'r') as f:
            elections = ElectionCode.load_elections_from_file(f)
        with open('./Data/future-elections.csv', 'r') as f:
            elections += ElectionCode.load_elections_from_file(f)
        if self.election_instructions == 'all':
            self.elections = elections + [no_target_election_marker]
        elif self.election_instructions == 'none':
            self.elections = [no_target_election_marker]
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
                    self.elections = (elections[elections.index(code):]
                        + [no_target_election_marker])
                except ValueError:
                    raise ConfigError('Error in "elections" argument: '
                                  'value given did not match any election '
                                  'given in Data/polled-elections.csv')
            else:
                raise ConfigError('Invalid instruction in "elections"'
                                  'argument.')


class ModellingData:
    def __init__(self, config):
            
        # Load the file containing a list of significant parties for each election
        # and arrange the data for the rest of the program to efficiently use
        with open('./Data/significant-parties.csv', 'r') as f:
            self.parties = {
                (a[0], a[1]): a[2:] for a in
                [b.strip().split(',') for b in f.readlines()]}

        with open('./Data/preference-estimates.csv', 'r') as f:
            self.preference_flows = {
                (a[0], a[1], a[2]): float(a[3]) * 0.01 for a in
                [b.strip().split(',') for b in f.readlines()]}

        # Load the file containing prior results for each election
        with open('./Data/prior-results.csv', 'r') as f:
            self.prior_results = {((a[0], a[1]), a[2]): float(a[3]) for a in
                            [b.strip().split(',') for b in
                            f.readlines()]}

        # Discontinuities for leader changes
        # or other exceptionally significant events
        with open('./Data/discontinuities.csv', 'r') as f:
            self.discontinuities = {a[0]: a[1:] for a in [b.strip().split(',')
                            for b in f.readlines()]}

        # Number of iterations to run for each model
        # (note: half of the iterations will be warm-up)
        # At least 300 is recommended, more will make the
        # path more consistent at the cost of taking more time
        # Sparsely polled periods take more time as the model has more freedom
        with open('./Data/desired-iterations.csv', 'r') as f:
            self.desired_iterations = {(a[0], a[1]): int(a[2]) for a in [
                                b.strip().split(',') for b in f.readlines()]}

        # Load the dates of next and previous elections
        # We will only model polls between those two dates
        with open('./Data/election-cycles.csv', 'r') as f:
            self.election_cycles = {(a[0], a[1]):
                            (pd.Period(a[2], freq='D'),
                                pd.Period(a[3], freq='D'))
                            for a in [b.strip().split(',')
                            for b in f.readlines()]}

        # Load the list of credentialled pollsters for each election
        # The house-effect sum-to-zero constraint will only apply
        # for these pollsters
        # Membership is based on track record of having similar or
        # better results than the consensus, no notable house
        # effect (including minor party effects), and compliance with
        # any industry standards
        # Rules may be relaxed somewhat while there are very few
        # pollsters covering a race (e.g. ResolvePM for NSW-2023)
        with open('./Data/anchoring-pollsters.csv', 'r') as f:
            self.anchoring_pollsters = {(a[0], a[1]): a[2:]
                            for a in [b.strip().split(',')
                            for b in f.readlines()]}


class ElectionData:
    def __init__(self, m_data, desired_election):
        self.e_tuple = (str(desired_election.year()),
                          desired_election.region())           
        tup = self.e_tuple
        self.others_medians = {}

        # collect the model data
        self.base_df = pd.read_csv(data_source[tup[1]])

        # drop data not in range of this election period
        self.base_df['MidDate'] = [pd.Period(date, freq='D')
                            for date in self.base_df['MidDate']]
        self.base_df = self.base_df[self.base_df['MidDate'] >=
                                    m_data.election_cycles[tup][0]]
        self.base_df = self.base_df[self.base_df['MidDate'] <=
                                    m_data.election_cycles[tup][1]]

        # convert dates to days from start
        # do this before removing polls with N/A values so that
        # start times are consistent amongst series
        # (otherwise, a poll missing some parties could cause inconsistent
        # date indexing)
        self.start = self.base_df['MidDate'].min()  # day zero
        # day number for each poll
        self.base_df['Day'] = self.base_df['MidDate'] - self.start
        self.n_days = self.base_df['Day'].max().n + 1

        # store the election day for when the model needs it later
        self.election_day = (m_data.election_cycles[tup][1] - self.start).n

        self.all_houses = self.base_df['Firm'].unique().tolist()

        self.create_tpp_series(m_data=m_data, 
                               desired_election=desired_election, 
                               df=self.base_df)

    def create_tpp_series(self, m_data, desired_election, df):
        df['old_tpp'] = df['@TPP']
        num_polls = len(df['@TPP'].values.tolist())
        # print(num_polls)
        min_index = df.index.values.tolist()[0]
        # print(min_index)
        adjustments = {a + min_index: 0 for a in range(0, num_polls)}
        for others_party in others_parties + ['GRN FP']:
            days = df['Day'].values.tolist()
            if others_party in df and others_party in self.others_medians:
                pref_tuple = (self.e_tuple[0], self.e_tuple[1], others_party)
                oth_tuple = (self.e_tuple[0], self.e_tuple[1], 'OTH FP')
                polled_percent = df[others_party].values.tolist()
                adj_flow = (m_data.preference_flows[pref_tuple] -
                            m_data.preference_flows[oth_tuple])
                for a in range(0, num_polls):
                    if math.isnan(polled_percent[a]):
                        day = days[a]
                        # print(day.n)
                        estimated_fp = self.others_medians[others_party][day.n]
                        # print(estimated_fp)
                        pref_adjust = estimated_fp * adj_flow
                        # print(pref_adjust)
                        adjustments[a + min_index] += pref_adjust
                        # print(a + min_index)
                        # print(adjustments[a + min_index])
                # print(others_party)
                # print(polled_percent)
        # print(adjustments)
        adjustment_series = pd.Series(data=adjustments)
        df['Total'] = df['ALP FP'] + df['LIB FP' if 'LIB FP' in df else 'LNP FP']
        # print(adjustment_series)
        df['@TPP'] = df['ALP FP']
        for column in df:
            pref_tuple = (self.e_tuple[0], self.e_tuple[1], column)
            if pref_tuple not in m_data.preference_flows:
                continue
            preference_flow = m_data.preference_flows[pref_tuple]
            if column == 'OTH FP':
                lnp_col = 'LIB FP' if 'LIB FP' in df else 'LNP FP'
                df['OTH FP'] = df.apply(
                    lambda row: (
                        100 - row['ALP FP'] - row[lnp_col] - 
                        (row['GRN FP'] if not math.isnan(row['GRN FP']) else 0)
                        if pd.isnull(row['OTH FP']) else row['OTH FP']
                        ),
                    axis=1
                )
            pref_col = df[column].fillna(0)
            # print(column)
            # print(pref_col)
            df['@TPP'] += pref_col * preference_flow
            df['Total'] += pref_col
        # print(df['@TPP'].to_string())
        df['@TPP'] += adjustment_series
        print(df['Total'])
        df['@TPP'] /= (df['Total'] * 0.01)
        # print(df['@TPP'].to_string())
        if desired_election.region() == 'fed':
            df['@TPP'] += 0.1  # leakage in LIB/NAT seats


def run_models():

    try:
        config = Config()
    except ConfigError as e:
        print('Could not process configuration due to the following issue:')
        print(str(e))
        return

    # check version information
    print('Python version: {}'.format(sys.version))
    print('pystan version: {}'.format(pystan.__version__))

    m_data = ModellingData(config)

    # Load the list of election periods we want to model
    desired_elections = config.elections

    for desired_election in desired_elections:

        e_data = ElectionData(m_data=m_data, desired_election=desired_election)

        for party in m_data.parties[e_data.e_tuple]:

            df = e_data.base_df.copy()

            # drop any rows with N/A values for the current party
            df = df.dropna(subset=[party])

            # push misc parties into Others, as explained above
            for others_party in others_parties:
                try:
                    # make sure any N/A values do not get
                    # propagated into the Others data
                    tempCol = df[others_party].fillna(0)
                    df['OTH FP'] = df['OTH FP'] + tempCol
                except KeyError:
                    pass  # it's expected that some parties aren't in the file
            n_polls = len(df)

            # Get the prior result, or a small vote share if
            # the prior result is not given
            if (e_data.e_tuple, party) in m_data.prior_results:
                prior_result = max(0.25, m_data.prior_results[(e_data.e_tuple, party)])
            elif party == '@TPP':
                prior_result = 50  # placeholder TPP
            else:
                prior_result = 0.25  # percentage

            # Get a series for any missing data
            missing = df[party].apply(lambda x: 1 if np.isnan(x) else 0)
            y = df[party].fillna(prior_result)
            y = y.apply(lambda x: max(x, 0.01))

            # We are excluding some houses
            # from the sum to zero constraint because
            # they have unusual or infrequent poll results compared
            # with other pollsters
            # Organise the polling houses so that the pollsters
            # included in the sum-to-zero are first, and then the
            # others follow
            houses = e_data.all_houses.copy()
            houseCounts = df['Firm'].value_counts()
            whitelist = m_data.anchoring_pollsters[e_data.e_tuple]
            exclusions = set([h for h in houses if h not in whitelist])
            print(f'Pollsters included in anchoring: {[h for h in houses if h not in exclusions]}')
            print(f'Pollsters not included in anchoring: {exclusions}')
            for h in houses:
                if houseCounts[h] < 1:
                    exclusions.add(h)
            remove_exclusions = []
            for e in exclusions:
                if e in houses:
                    houses.remove(e)
                else:
                    remove_exclusions.append(e)
            for e in remove_exclusions:
                exclusions.remove(e)
            houses = houses + list(exclusions)
            house_map = dict(zip(houses, range(1, len(houses)+1)))
            df['House'] = df['Firm'].map(house_map)
            n_houses = len(df['House'].unique())
            n_exclude = len(exclusions)

            # Convert "days" objects into raw numerical data
            # that Stan can accept
            for i in df.index:
                df.loc[i, 'Day'] = df.loc[i, 'Day'].n + 1

            # Transform discontinuities from dates to raw numbers
            discontinuities_filtered = m_data.discontinuities[e_data.e_tuple[1]]
            discontinuities_filtered = \
                [(pd.to_datetime(date).to_period('D') - e_data.start).n + 1
                 for date in discontinuities_filtered]

            # Remove discontinuities outside of the election period
            discontinuities_filtered = \
                [date for date in discontinuities_filtered
                 if date >= 0 and date < e_data.n_days]

            # Stan doesn't like zero-length arrays so put in a dummy value
            # if there are no discontinuities
            if not discontinuities_filtered:
                discontinuities_filtered.append(0)

            # quality adjustment for polls
            sample_size = 1000  # treat good quality polls as being this size
            # adjust effective sample size according to quality
            sigmas = df['Quality Adjustment'].apply(
                lambda x: np.sqrt((50 * 50) / (sample_size * 0.6 ** x)))

            houseEffectOld = 240
            houseEffectNew = 120

            # get the Stan model code
            with open("./Models/fp_model.stan", "r") as f:
                model = f.read()

            # Prepare the data for Stan to process
            stan_data = {
                'dayCount': e_data.n_days,
                'pollCount': n_polls,
                'houseCount': n_houses,
                'discontinuityCount': len(discontinuities_filtered),
                'priorResult': prior_result,

                'pollObservations': y.values,
                'missingObservations': missing.values,
                'pollHouse': df['House'].values.tolist(),
                'pollDay': df['Day'].values.tolist(),
                'discontinuities': discontinuities_filtered,
                'sigmas': sigmas.values,
                'excludeCount': n_exclude,

                'electionDay': e_data.election_day,

                # distributions for the daily change in vote share
                # higher values during campaigns, since it's more likely
                # people are paying attention and changing their mind then
                'dailySigma': 0.35,
                'campaignSigma': 0.7,
                'finalSigma': 1.2,

                # prior distribution for each house effect
                # modelled as a double exponential to avoid
                # easily giving a large house effect, but
                # still giving a big one when it's really warranted
                'houseEffectSigma': 1.0,

                # prior distribution for sum of house effects
                # keep this very small, we will deal with systemic bias
                # in the main program, so for now keep the sum of house
                # effects at approximately zero
                'houseEffectSumSigma': 0.001,

                # prior distribution for each day's vote share
                'priorVoteShareSigma': 200.0,

                # Bounds for the transition between
                'houseEffectOld': houseEffectOld,
                'houseEffectNew': houseEffectNew
            }

            # encode the STAN model in C++ or retrieve it if already cached
            sm = stan_cache(model_code=model)

            # Report dates for model, this means we can easily check if new
            # data has actually been saved without waiting for model to run
            print('Beginning sampling for ' + party + ' ...')
            end = e_data.start + timedelta(days=e_data.n_days)
            print('Start date of model: ' + e_data.start.strftime('%Y-%m-%d\n'))
            print('End date of model: ' + end.strftime('%Y-%m-%d\n'))

            # Stan model configuration
            chains = 8
            iterations = m_data.desired_iterations[e_data.e_tuple]

            # Do model sampling. Time for diagnostic purposes
            start_time = perf_counter()
            fit = sm.sampling(data=stan_data,
                              iter=iterations,
                              chains=chains,
                              control={'max_treedepth': 16,
                                       'adapt_delta': 0.8})
            finish_time = perf_counter()
            print('Time elapsed: ' + format(finish_time - start_time, '.2f')
                  + ' seconds')
            print('Stan Finished ...')

            # Check technical model diagnostics
            import pystan.diagnostics as psd
            print(psd.check_hmc_diagnostics(fit))

            # construct the file names to output to
            output_trend = './Outputs/fp_trend_' + ''.join(e_data.e_tuple) \
                + '_' + party + '.csv'
            output_polls = './Outputs/fp_polls_' + ''.join(e_data.e_tuple) \
                + '_' + party + '.csv'
            output_house_effects = './Outputs/fp_house_effects_' + \
                ''.join(e_data.e_tuple) + '_' + party + '.csv'

            if party in others_parties or party == 'GRN FP':
                e_data.others_medians[party] = {}

            # Extract trend data from model summary and write to file
            probs_list = [0.001]
            for i in range(1, 100):
                probs_list.append(i * 0.01)
            probs_list.append(0.999)
            output_probs = tuple(probs_list)
            summary = fit.summary(probs=output_probs)['summary']
            trend_file = open(output_trend, 'w')
            trend_file.write('Start date day,Month,Year\n')
            trend_file.write(e_data.start.strftime('%d,%m,%Y\n'))
            trend_file.write('Day,Party')
            for prob in output_probs:
                trend_file.write(',' + str(round(prob * 100)) + "%")
            trend_file.write('\n')
            # need to get past the centered values and house effects
            # this is where the actual FP trend starts
            offset = e_data.n_days + n_houses * 2
            for summaryDay in range(0, e_data.n_days):
                table_index = summaryDay + offset
                trend_file.write(str(summaryDay) + ",")
                trend_file.write(party + ",")
                for col in range(3, 3+len(output_probs)-1):
                    trend_value = summary[table_index][col]
                    trend_file.write(str(trend_value) + ',')
                if party in others_parties or party == 'GRN FP':
                    # Average of first and last
                    median_col = math.floor((4+len(output_probs)) / 2)
                    median_val = summary[table_index][median_col]
                    e_data.others_medians[party][summaryDay] = median_val
                trend_file.write(
                    str(summary[table_index][3+len(output_probs)-1]) + '\n')
            trend_file.close()
            print('Saved trend file at ' + output_trend)

            # Extract house effect data from model summary
            new_house_effects = []
            old_house_effects = []
            offset = e_data.n_days
            for house in range(0, n_houses):
                new_house_effects.append(summary[offset + house, 0])
                old_house_effects.append(summary[offset + n_houses + house, 0])

            # Write poll data to file, giving both raw and
            # house effect adjusted values
            polls_file = open(output_polls, 'w')
            polls_file.write('Firm,Day')
            polls_file.write(',' + party)
            polls_file.write(',' + party + ' adj')
            if party == "@TPP":
                polls_file.write(',' + party + ' reported')
            polls_file.write('\n')
            for poll_index in df.index:
                polls_file.write(str(df.loc[poll_index, 'Firm']))
                day = df.loc[poll_index, 'Day']
                days_ago = e_data.n_days - day
                polls_file.write(',' + str(day))
                fp = df.loc[poll_index, party]
                new_he = new_house_effects[df.loc[poll_index, 'House'] - 1]
                old_he = old_house_effects[df.loc[poll_index, 'House'] - 1]
                old_factor = ((days_ago - houseEffectNew) /
                             (houseEffectOld - houseEffectNew))
                old_factor = max(min(old_factor, 1), 0)
                mixed_he = (old_factor * old_he +
                            (1 - old_factor) * new_he)
                adjusted_fp = fp - mixed_he
                polls_file.write(',' + str(fp))
                polls_file.write(',' + str(adjusted_fp))
                if party == "@TPP":
                    polls_file.write(',' + str(df.loc[poll_index, 'old_tpp']))
                polls_file.write('\n')
            polls_file.close()
            print('Saved polls file at ' + output_polls)

            # Extract house effect data from model summary and write to file
            probs_list = []
            probs_list.append(0.001)
            for i in range(1, 10):
                probs_list.append(i * 0.1)
            probs_list.append(0.999)
            output_probs = tuple(probs_list)
            summary = fit.summary(probs=output_probs)['summary']
            house_effects_file = open(output_house_effects, 'w')
            house_effects_file.write('House,Party')
            for prob in output_probs:
                house_effects_file.write(',' + str(round(prob * 100)) + "%")
            house_effects_file.write('\n')
            house_effects_file.write('New house effects\n')
            offset = e_data.n_days
            for house_index in range(0, n_houses):
                house_effects_file.write(houses[house_index])
                table_index = offset + house_index
                house_effects_file.write("," + party)
                for col in range(3, 3+len(output_probs)):
                    house_effects_file.write(
                        ',' + str(summary[table_index][col]))
                house_effects_file.write('\n')
            offset = e_data.n_days + n_houses
            house_effects_file.write('Old house effects\n')
            for house_index in range(0, n_houses):
                house_effects_file.write(houses[house_index])
                table_index = offset + house_index
                house_effects_file.write("," + party)
                for col in range(3, 3+len(output_probs)):
                    house_effects_file.write(
                        ',' + str(summary[table_index][col]))
                house_effects_file.write('\n')

            house_effects_file.close()
            print('Saved house effects file at ' + output_house_effects)


if __name__ == '__main__':
    run_models()
