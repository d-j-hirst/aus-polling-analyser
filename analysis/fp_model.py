import argparse
import math
import numpy as np
import pandas as pd
import pystan
import sys
import statistics
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
                            ' e.g. 2013-fed. Write "all" '
                            'to do it for all elections.')
        parser.add_argument('-c', '--calibrate', action='store_true',
                            help='If set, will run in pollster calibration '
                            'mode. This will exclude each pollster from '
                            'calculations so that their polls can be '
                            'calibrated using the trend from the other polls.')
        parser.add_argument('-b', '--bias', action='store_true',
                            help='If set, will run in bias calibration '
                            'mode. This will record relative bias for each '
                            'pollster that can then be used to calibrate '
                            'the house effects in actual forecast runs. '
                            'Ignored if --calibrate is also used.')
        parser.add_argument('--cutoff', action='store', type=int,
                            help='Exclude polls occurring fewer than this many'
                            'days before an election. Useful for creating'
                            'hindcasts for previous elections.', 
                            default=0)
        self.election_instructions = parser.parse_args().election.lower()
        self.calibrate_pollsters = parser.parse_args().calibrate == True
        self.calibrate_bias = (not self.calibrate_pollsters and 
                               parser.parse_args().bias == True)
        self.cutoff = parser.parse_args().cutoff
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
                (a[0], a[1], a[2]): (float(a[3]) * 0.01,
                float(a[4]) * 0.01 if len(a) > 4 and a[4][0] != "#" else 0)
                for a in [b.strip().split(',') for b in f.readlines()]}

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

        with open('./Outputs/Calibration/variability.csv', 'r') as f:
            self.pollster_sigmas = {(a[0], a[1]): float(a[2])
                            for a in [b.strip().split(',')
                            for b in f.readlines()]}

        with open('./Outputs/Calibration/he_weighting.csv', 'r') as f:
            self.pollster_he_weights = {(a[0], a[1]): float(a[2])
                            for a in [b.strip().split(',')
                            for b in f.readlines()]}

        with open('./Outputs/Calibration/biases.csv', 'r') as f:
            self.pollster_biases = {((a[0], a[1]), a[2], a[3]): (float(a[4]), float(a[5]))
                            for a in [b.strip().split(',')
                            for b in f.readlines()]}


class ElectionData:
    def __init__(self, config, m_data, desired_election):
        self.e_tuple = (str(desired_election.year()),
                          desired_election.region())           
        tup = self.e_tuple
        self.others_medians = {}

        # collect the model data
        self.base_df = pd.read_csv(data_source[tup[1]])

        # drop data not in range of this election period
        self.base_df['MidDate'] = [pd.Period(date, freq='D')
                            for date in self.base_df['MidDate']]
        start_date = m_data.election_cycles[tup][0]
        end_date = (m_data.election_cycles[tup][1] - 
                    pd.to_timedelta(config.cutoff, unit="D"))
        self.base_df = self.base_df[self.base_df['MidDate'] >= start_date]
        self.base_df = self.base_df[self.base_df['MidDate'] <= end_date]

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

        if config.calibrate_pollsters:
            # Don't run calibration for any pollsters with only one poll
            # in this election period as at least two polls are required
            self.pollster_exclusions = \
                [a for a in self.all_houses if
                 list(self.base_df['Firm']).count(a) > 1]

            self.pollster_exclusions += ['']

            self.poll_calibrations = {}
        else:
            self.pollster_exclusions = ['']

        self.create_tpp_series(m_data=m_data, 
                               desired_election=desired_election, 
                               df=self.base_df)
        
        self.combine_others_parties()

        self.create_day_series()

    def create_tpp_series(self, m_data, desired_election, df):
        df['old_tpp'] = df['@TPP']
        num_polls = len(df['@TPP'].values.tolist())
        min_index = df.index.values.tolist()[0]
        adjustments = {a + min_index: 0 for a in range(0, num_polls)}
        for others_party in others_parties + ['GRN FP']:
            days = df['Day'].values.tolist()
            if others_party in df and others_party in self.others_medians:
                pref_tuple = (self.e_tuple[0], self.e_tuple[1], others_party)
                oth_tuple = (self.e_tuple[0], self.e_tuple[1], 'OTH FP')
                polled_percent = df[others_party].values.tolist()
                adj_flow = (m_data.preference_flows[pref_tuple][0] -
                            m_data.preference_flows[oth_tuple][0])
                for a in range(0, num_polls):
                    if math.isnan(polled_percent[a]):
                        day = days[a]
                        estimated_fp = self.others_medians[others_party][day.n]
                        pref_adjust = estimated_fp * adj_flow
                        adjustments[a + min_index] += pref_adjust
        adjustment_series = pd.Series(data=adjustments)
        df['Total'] = (df['ALP FP'] + df['LIB FP'
                       if 'LIB FP' in df else 'LNP FP'])
        df['@TPP'] = df['ALP FP']
        for column in df:
            pref_tuple = (self.e_tuple[0], self.e_tuple[1], column)
            if pref_tuple not in m_data.preference_flows:
                continue
            preference_flow = m_data.preference_flows[pref_tuple][0]
            preference_survival = 1 - m_data.preference_flows[pref_tuple][1]
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
            df['@TPP'] += pref_col * preference_flow * preference_survival
            df['Total'] += pref_col * preference_survival
            print(pref_col)
            print(preference_flow)
            print(preference_survival)
            print(df['@TPP'])
            print(df['Total'])
        df['@TPP'] += adjustment_series
        df['@TPP'] /= (df['Total'] * 0.01)
        if desired_election.region() == 'fed':
            df['@TPP'] += 0.1  # leakage in LIB/NAT seats
        print(df['@TPP'])
    
    def combine_others_parties(self):
        # push misc parties into Others, as explained above
        for others_party in others_parties:
            try:
                # make sure any N/A values do not get
                # propagated into the Others data
                tempCol = self.base_df[others_party].fillna(0)
                self.base_df['OTH FP'] = self.base_df['OTH FP'] + tempCol
            except KeyError:
                pass  # it's expected that some parties aren't in the file

    def create_day_series(self):
        # Convert "days" objects into raw numerical data
        # that Stan can accept
        for i in self.base_df.index:
            self.base_df.loc[i, 'Day'] = self.base_df.loc[i, 'Day'].n + 1


def calibrate_pollsters(e_data, exc_polls, excluded_pollster, party, summary,
                        n_houses, output_probs, df):
    exc_poll_data = [a for a in zip(exc_polls['Day'], exc_polls[party],
                     exc_polls.axes[0], exc_polls['Firm'])]
    if len(exc_poll_data) <= 1: return
    print(f'Trend closeness statistics for {excluded_pollster}')
    offset = e_data.n_days + n_houses * 2 - 1
    median_col = 3 + output_probs.index(0.5)
    diff_sum = {}
    pollster_count = {}
    house_effects = {}
    for a in exc_poll_data:
        day, vote, pollster = a[0], a[1], a[3]
        table_index = day + offset
        trend_value = summary[table_index][median_col]
        if pollster not in diff_sum:
            diff_sum[pollster] = 0
            pollster_count[pollster] = 0
        diff_sum[pollster] += vote - trend_value
        pollster_count[pollster] += 1
    for key in diff_sum.keys():
        house_effects[key] = diff_sum[key] / pollster_count[key]
        
    deviations = []
    prob_deviations = []
    for a in exc_poll_data:
        day, vote, poll_index, pollster = a[0], a[1], a[2], a[3]
        table_index = day + offset
        trend_median = summary[table_index][median_col]
        eff_house_effect = house_effects[pollster]
        adj_poll = vote - eff_house_effect
        # for the case where the poll is higher than any
        # probability threshold, have this as the default value
        percentile = 0.999 
        for index, upper_prob in enumerate(output_probs):
            upper_value = summary[table_index][index + 3]
            if adj_poll < upper_value:
                if index == 0:
                    percentile = 0.001
                else:
                    lower_value = summary[table_index][index + 2]
                    lower_prob = output_probs[index - 1]
                    lerp = ((adj_poll - lower_value) /
                            (upper_value - lower_value))
                    percentile = (lower_prob + lerp * 
                                (upper_prob - lower_prob))
                break
            upper_value = summary[table_index][index + 3]
        deviation = adj_poll - trend_median
        prob_deviation = abs(percentile - 0.5)
        neighbours = sum([min(1, 2 ** (-abs(day - other_day) / 20) * 0.5)
                      for other_day in df['Day']
                     ])
        e_data.poll_calibrations[(excluded_pollster, day,
                                  party, poll_index)] = \
            (vote, trend_median, adj_poll, 
             percentile, deviation, prob_deviation, neighbours)
        deviations.append(deviation)
        prob_deviations.append(prob_deviation)
    std_dev = statistics.stdev(deviations)
    prob_dev_avg = statistics.mean(prob_deviations)
    print(f'Overall ({excluded_pollster}, {party}):'
          f' standard deviation from trend median: {std_dev}'
          f' average probability deviation: {prob_dev_avg}')


def run_individual_party(config, m_data, e_data,
                         excluded_pollster, party):

    df = e_data.base_df.copy()

    # drop any rows with N/A values for the current party
    df = df.dropna(subset=[party])

    # If we're not excluding any pollster then we want to record
    # calibration stats for all pollsters (so that they may be
    # compared to those with pollsters excluded)
    if excluded_pollster != '':
        exc_polls = df[df.Firm == excluded_pollster]
        if exc_polls.empty:
            print(f'No polls by {excluded_pollster} for {party}'
                  f', skipping round')
            return
    elif config.calibrate_pollsters:
        exc_polls = df

    # if we're excluding a pollster for calibrations
    # remove their polls now
    df = df[df.Firm != excluded_pollster]
    n_polls = len(df)
    # It's possible for there to actually be no polls at all if
    # the party hasn't been polled before the cutoff date
    if n_polls == 0:
        print(f'No polls for party {party} at all, skipping round')
        return

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
    houses = df['Firm'].unique().tolist()
    house_map = dict(zip(houses, range(1, len(houses)+1)))
    df['House'] = df['Firm'].map(house_map)
    n_houses = len(df['House'].unique())

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

    # Have a standard sigma for calibrating pollsters,
    # otherwise used the observed sigmas
    sample_size = 1000
    calibration_sigma = np.sqrt((50 * 50) / (sample_size))
    sigmas = df['Firm'].apply(
        lambda x: calibration_sigma if config.calibrate_pollsters else
        m_data.pollster_sigmas[(x, party)] if
        (x, party) in m_data.pollster_sigmas else 3
    )

    # Equal weights for house effects when calibrating,
    # use determined house effect weights when running forecasts
    he_weights = [
        1 if config.calibrate_pollsters or config.calibrate_bias else
        4 / (m_data.pollster_biases[(e_data.e_tuple, x, party)][1] ** 2) if
        (e_data.e_tuple, x, party) in m_data.pollster_biases else 0.05
        for x in houses
    ]

    # Equal weights for house effects when calibrating,
    # use determined house effect weights when running forecasts
    biases = [
        0 if config.calibrate_pollsters or config.calibrate_bias else
        m_data.pollster_biases[(e_data.e_tuple, x, party)][0] if
        (e_data.e_tuple, x, party) in m_data.pollster_biases else 0
        for x in houses
    ]

    houseEffectOld = 240
    houseEffectNew = 120

    # Print an estimate for the expected house effect sum
    # (doesn't have any impact on subsequent calculations)
    weightedBiasSum = 0
    housePollCount = [0 for a in houses]
    houseWeight = [0 for a in houses]
    houseList = df['House'].values.tolist()
    for poll in range(0, n_polls):
        housePollCount[houseList[poll] - 1] = housePollCount[houseList[poll] - 1] + 1
    for house in range(0, n_houses):
        houseWeight[house] = he_weights[house]
        weightedBiasSum += biases[house] * houseWeight[house]
    totalHouseWeight = sum(houseWeight)
    weightedBias = weightedBiasSum / totalHouseWeight
    print(f'Expected house effect sum: {weightedBias}')
    print(f'House effect weights: {houseWeight} for {houses}')

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
        'heWeights': he_weights,
        'biases': biases,

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
        'houseEffectSigma': 2,

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

    # construct the file names that the script will output results into
    # put calibration files in calibration folder, with the file name
    # appended with the pollster name if calibrated for a pollster's variance
    # or "biascal" if calibrating for bias.
    pollster_append = (f'_{excluded_pollster}' if 
                       excluded_pollster != '' else
                       f'_biascal' if config.calibrate_bias else '')
    e_tag = ''.join(e_data.e_tuple)
    calib_str = ("Calibration/" if config.calibrate_pollsters
                 or config.calibrate_bias else "")
    folder = (f'./Outputs/{calib_str}')
    cutoff_append = f'_{config.cutoff}d' if config.cutoff > 0 else ''

    output_trend = (f'{folder}fp_trend_{e_tag}_{party}{pollster_append}'
                    f'{cutoff_append}.csv')
    output_polls = (f'{folder}fp_polls_{e_tag}_{party}{pollster_append}'
                    f'{cutoff_append}.csv')
    output_house_effects = (f'{folder}fp_house_effects_{e_tag}_'
        f'{party}{pollster_append}{cutoff_append}.csv')

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
    
    if config.calibrate_pollsters:
        calibrate_pollsters(e_data=e_data,
                            exc_polls=exc_polls,
                            excluded_pollster=excluded_pollster,
                            party=party,
                            summary=summary,
                            n_houses=n_houses,
                            output_probs=output_probs,
                            df=df
                           )


def finalise_calibrations(e_data):
    polls_string = {}
    # for key, val in e_data.poll_calibrations.items():
    #     print(f'{key}: {val}')
    total_weight = {}
    total_weighted_dev = {}
    for key, val in e_data.poll_calibrations.items():
        if (key[0] != ''):
            full_val = e_data.poll_calibrations[('', key[1], key[2], key[3])]
            cal_deviation = val[4]
            full_deviation = full_val[4]
            difference = abs(cal_deviation) - abs(full_deviation)
            quotient = min(max(0.5, abs(full_deviation)) /
                           max(0.5, abs(cal_deviation)),
                           1)
            neighbours_weight = val[6]
            final_weight = min(quotient, neighbours_weight)
            new_key = (key[0], key[2])
            if new_key not in total_weight:
                total_weight[new_key] = 0
                total_weighted_dev[new_key] = 0
                polls_string[new_key] = ''
            total_weight[new_key] += final_weight
            total_weighted_dev[new_key] += final_weight * abs(cal_deviation)
            print(f'{key}: Calibrated deviation: {cal_deviation},'
                  f' full deviation: {full_deviation},'
                  f' difference: {difference}\n '
                  f' quotient weight: {quotient},'
                  f' neighbours weight: {neighbours_weight},'
                  f' final weight: {final_weight}')
            polls_string[new_key] += (f'{key[1]},{cal_deviation},{full_deviation},'
                             f'{final_weight}\n')
    for key, val in total_weighted_dev.items():
        weight = total_weight[key]
        if weight == 0: continue
        weighted_average_deviation = val / max(weight / 2, weight - 1)
        print(f'{key}: weighted avg deviation: {weighted_average_deviation}, '
              f'total weight: {weight}')
        filename = (f'./Outputs/Calibration/calib_'
                    f'{e_data.e_tuple[0]}{e_data.e_tuple[1]}_'
                    f'{key[0]}_{key[1]}.csv')
        with open(filename, 'w') as f:
            f.write(f'{weighted_average_deviation},'
                    f'{weight},\n{polls_string[key]}')


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

    m_data = ModellingData(config=config)

    # Load the list of election periods we want to model
    desired_elections = config.elections

    for desired_election in desired_elections:

        e_data = ElectionData(config=config,
                              m_data=m_data,
                              desired_election=desired_election)

        for excluded_pollster in e_data.pollster_exclusions:

            for party in m_data.parties[e_data.e_tuple]:

                if excluded_pollster != '':
                    print(f'Excluding pollster: {excluded_pollster}')
                else:
                    print('Not excluding any pollsters.')

                run_individual_party(config=config,
                                     m_data=m_data,
                                     e_data=e_data,
                                     excluded_pollster=excluded_pollster,
                                     party=party)

        if config.calibrate_pollsters:
            finalise_calibrations(e_data=e_data)

if __name__ == '__main__':
    run_models()
