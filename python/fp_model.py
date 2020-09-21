# -*- coding: utf-8 -*-
#Created on Sun Sep 15 14:19:58 2020

# PYTHON: analyse primary poll data


import sys

import numpy as np
import pandas as pd
import pystan

sys.path.append( './bin' )
from stan_cache import stan_cache

# --- check version information
print('Python version: {}'.format(sys.version))
print('pystan version: {}'.format(pystan.__version__))

parties = ['L_NP FP', 'ALP FP', 'GRN FP', 'ONP FP', 'UAP FP', 'OTH FP']
n_parties = len(parties)

desired_election = '2019fed'

def main():
    for party in parties:
        election_cycles = {
            '2004fed': (pd.Period('2001-11-11', freq='D'), pd.Period('2004-10-09', freq='D')),
            '2007fed': (pd.Period('2004-10-10', freq='D'), pd.Period('2007-11-24', freq='D')),
            '2010fed': (pd.Period('2007-11-25', freq='D'), pd.Period('2010-08-21', freq='D')),
            '2013fed': (pd.Period('2010-08-22', freq='D'), pd.Period('2013-09-07', freq='D')),
            '2016fed': (pd.Period('2013-09-08', freq='D'), pd.Period('2016-07-02', freq='D')),
            '2019fed': (pd.Period('2016-07-03', freq='D'), pd.Period('2019-05-18', freq='D')),
            '2022fed': (pd.Period('2019-05-19', freq='D'), pd.Period('2022-12-12', freq='D'))
        }
        
        data_source = {
            '2004fed': './Data/poll-data-fed.xlsx',
            '2007fed': './Data/poll-data-fed.xlsx',
            '2010fed': './Data/poll-data-fed.xlsx',
            '2013fed': './Data/poll-data-fed.xlsx',
            '2016fed': './Data/poll-data-fed.xlsx',
            '2019fed': './Data/poll-data-fed.xlsx',
            '2022fed': './Data/poll-data-fed.xlsx',
        }
        
        output_trend = './Outputs/fp_trend_' + desired_election + '_' + party + '.csv'
        output_polls = './Outputs/fp_polls_' + desired_election + '_' + party + '.csv'
        output_house_effects = './Outputs/fp_house_effects_' + desired_election + '_' + party + '.csv'
    
        # --- key inputs to model
        sampleSize = 1000 # treat all polls as being of this size
        pseudoSampleSigma = np.sqrt((50 * 50) / sampleSize) 
        chains = 5
        iterations = 2000
        # Note: half of the iterations will be warm-up
        
        # --- collect the model data
        # the XL data file was extracted from the Wikipedia
        # page on next Australian Federal Election
        workbook = pd.ExcelFile(data_source[desired_election])
        df = workbook.parse('Data')
        
        # drop data not in range of this election period
        df['MidDate'] = [pd.Period(date, freq='D') for date in df['MidDate']]
        df = df[df['MidDate'] > pd.Period(election_cycles[desired_election][0], freq='D')] 
        df = df[df['MidDate'] < pd.Period(election_cycles[desired_election][1], freq='D')] 
        
        # push One Nation into Other 
        tempCol = df['ONP FP'].fillna(0)
        df['OTH FP'] = df['OTH FP'] + tempCol
        tempCol = df['UAP FP'].fillna(0)
        df['OTH FP'] = df['OTH FP'] + tempCol
        
        # covert dates to days from start
        start = df['MidDate'].min() - 1 # day zero
        df['Day'] = df['MidDate'] - start # day number for each poll
        n_days = df['Day'].max()
        n_polls = len(df)
        
        # uncomment these lines if an event worthy of a discontinuity occurs
        #discontinuity = (pd.Period('2020-01-01', freq='D') - start).n
        #stability = (pd.Period('2020-01-02', freq='D') - start).n
        
        # treat later Newspoll as a seperate series 
        # [Because newspoll changed its preference allocation methodology]
        df['Firm'] = df['Firm'].where((df['MidDate'] < 
            pd.Period('2017-12-01', freq='D')) |
            (df['Firm'] != 'Newspoll'), other='Newspoll2')
        
        # manipulate polling data ... 
        missing = df[party].apply(lambda x: 1 if np.isnan(x) else 0)
        y = df[party].fillna(0.25)
        
        #centre_track[d add polling house data to the mix
        # make sure the "sum to zero" exclusions are 
        # last in the list
        houses = df['Firm'].unique().tolist()
        exclusions = ['Roy Morgan', 'ANU', 'YouGov', 'Ipsos']
        # Note: we are excluding YouGov and Ipsos 
        # from the sum to zero constraint because 
        # they have unusual poll results compared 
        # with other pollsters
        remove_exclusions = []
        for e in exclusions:
            if e in houses:
                houses.remove(e)
            else:
                remove_exclusions.append(e)
        for e in remove_exclusions:
            exclusions.remove(e)
        houses = houses + exclusions
        house_map = dict(zip(houses, range(1, len(houses)+1)))
        df['House'] = df['Firm'].map(house_map)
        n_houses = len(df['House'].unique())
        n_exclude = len(exclusions)
        
        for i in df.index:
            df.loc[i,'Day'] = df.loc[i,'Day'].n
        
        # quality adjustment for polls
        df['poll_qual_adj'] = 0.0
        df['poll_qual_adj'] = pd.Series(2.0, index=df.index
            ).where(df['Firm'].str.contains('Ipsos|YouGov|Roy Morgan|ANU'),
            other=0.0)
        
        # --- compile model
        
        # get the STAN model 
        with open ("./Models/fp_model.stan", "r") as f:
            model = f.read()
            f.close()
        
        data = {
                'n_days': n_days.n,
                'n_polls': n_polls,
                'n_houses': n_houses,
                'pseudoSampleSigma': pseudoSampleSigma,
            
                'obs_y': y.values, 
                'missing_y': missing.values, 
                'poll_day': df['Day'].values.tolist(),
                'house': df['House'].values.tolist(), 
                'poll_qual_adj': df['poll_qual_adj'].values,
                'n_exclude': n_exclude,
                
                # let's set the day-to-day smoothing 
                'sigma': 0.15,
                'sigma_volatile': 0.4,
        }
        
        # encode the STAN model in C++ 
        sm = stan_cache(model_code=model)
            
        fit = sm.sampling(data=data,
                          iter=iterations, 
                          chains=chains,
                          verbose=True,
                          refresh=10,
                          control={'max_treedepth':13})
        
        # --- check diagnostics
        print('Stan Finished ...')
        import pystan.diagnostics as psd
        print(psd.check_hmc_diagnostics(fit))
            
        output_probs = (0.005,0.025,0.1,0.25,0.5,0.75,0.9,0.975,0.995)
        summary = fit.summary(probs=output_probs)['summary']
        print('Got Summary ...')
        trend_file = open(output_trend, 'w')
        trend_file.write('Start date day,Month,Year\n')
        trend_file.write(start.strftime('%d,%m,%Y\n'))
        trend_file.write('Day,Party,0.5%,2.5%,10%,25%,50%,75%,90%,97.5%,99.5%\n')
        # need to get past the centered values and house effects
        # this is where the actual FP trend starts
        offset = n_days.n + n_houses * 2
        for summaryDay in range(0, n_days.n):
            table_index = summaryDay + offset
            trend_file.write(str(summaryDay) + ",")
            trend_file.write(party + ",")
            for col in range(3,3+len(output_probs)-1):
                trendValue = summary[table_index][col]
                trend_file.write(str(trendValue) + ',')
            trend_file.write(str(summary[table_index][3+len(output_probs)-1]) + '\n')
        trend_file.close()
        print('Saved trend file at ' + output_trend)
        
        house_effects = []
        offset = n_days.n + n_houses
        for house in range(0, n_houses):
            house_effects.append(summary[offset + house,0])
        
        polls_file = open(output_polls, 'w')
        polls_file.write('Firm,Day')
        polls_file.write(',' + party)
        polls_file.write(',' + party + ' adj')
        polls_file.write('\n')
        for poll_index in df.index:
            polls_file.write(str(df.loc[poll_index, 'Firm']))
            polls_file.write(',' + str(df.loc[poll_index, 'Day']))
            fp = df.loc[poll_index, party]
            adjusted_fp = fp - house_effects[df.loc[poll_index, 'House'] - 1]
            polls_file.write(',' + str(fp))
            polls_file.write(',' + str(adjusted_fp))
            polls_file.write('\n')
        polls_file.close()
        print('Saved polls file at ' + output_polls)
        
        house_effects_file = open(output_house_effects, 'w')
        house_effects_file.write('House,Party,0.5%,2.5%,10%,25%,50%,75%,90%,97.5%,99.5%\n')
        offset = n_days.n
        for house_index in range(0, n_houses):
            house_effects_file.write(houses[house_index])
            table_index = offset + house_index
            house_effects_file.write("," + party)
            for col in range(3,3+len(output_probs)):
                house_effects_file.write(',' + str(summary[table_index][col]))
            house_effects_file.write('\n')
        house_effects_file.close()
        print('Saved house effects file at ' + output_house_effects)

if __name__ == '__main__':
    main()
