# -*- coding: utf-8 -*-
#Created on Sun Sep 15 14:19:58 2020

# PYTHON: analyse primary poll data



def main():
    import sys
    
    import numpy as np
    import pandas as pd
    import pystan
    
    sys.path.append( './bin' )
    from stan_cache import stan_cache
    
    # --- check version information
    print('Python version: {}'.format(sys.version))
    print('pystan version: {}'.format(pystan.__version__))
    
    config = open('config.txt', 'r')
    lines = config.readlines()
    config.close()
    
    desired_election = lines[0]
    
    # N.B. The "Others" (OTH) "party" values include votes for UAP and ONP, so these
    # are effectively counted twice. The reason for this is that many polls do 
    # not report separate UAP/ONP figures, so they are aggregated from the polls that do,
    # count them together with the other "others" under OTH, and then (in the main program)
    # subtract the UAP and ONP from the OTH value to get the true non-UAP/ONP others value
    
    parties = {
        '2022fed': ['LNP FP', 'ALP FP', 'GRN FP', 'ONP FP', 'UAP FP', 'OTH FP'],
        '2019fed': ['LNP FP', 'ALP FP', 'GRN FP', 'ONP FP', 'UAP FP', 'OTH FP'],
        '2016fed': ['LNP FP', 'ALP FP', 'GRN FP', 'UAP FP', 'OTH FP'],
        '2013fed': ['LNP FP', 'ALP FP', 'GRN FP', 'UAP FP', 'OTH FP'],
        '2010fed': ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        '2007fed': ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        '2004fed': ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP']
    }
    
    prior_results = {
             ('2022fed', 'LNP FP'): 41.44,
             ('2022fed', 'ALP FP'): 33.34,
             ('2022fed', 'GRN FP'): 10.4,
             ('2022fed', 'ONP FP'): 3.08,
             ('2022fed', 'UAP FP'): 3.43,
             ('2022fed', 'OTH FP'): 14.82,
             
             ('2019fed', 'LNP FP'): 42.04,
             ('2019fed', 'ALP FP'): 34.73,
             ('2019fed', 'GRN FP'): 10.23,
             ('2019fed', 'OTH FP'): 13.0,
             
             ('2016fed', 'LNP FP'): 45.55,
             ('2016fed', 'ALP FP'): 33.38,
             ('2016fed', 'GRN FP'): 8.65,
             ('2016fed', 'UAP FP'): 5.49,
             ('2016fed', 'OTH FP'): 12.42,
             
             ('2013fed', 'LNP FP'): 43.66,
             ('2013fed', 'ALP FP'): 37.99,
             ('2013fed', 'GRN FP'): 11.76,
             ('2013fed', 'OTH FP'): 6.63,
             
             ('2010fed', 'LNP FP'): 42.09,
             ('2010fed', 'ALP FP'): 43.38,
             ('2010fed', 'GRN FP'): 7.79,
             ('2010fed', 'OTH FP'): 6.73,
             
             ('2007fed', 'LNP FP'): 46.71,
             ('2007fed', 'ALP FP'): 37.63,
             ('2007fed', 'GRN FP'): 7.19,
             ('2007fed', 'OTH FP'): 8.47,
             
             ('2004fed', 'LNP FP'): 42.92,
             ('2004fed', 'ALP FP'): 37.84,
             ('2004fed', 'GRN FP'): 4.96,
             ('2004fed', 'OTH FP'): 14.28
        }
    
    discontinuities = ['2005-01-28', '2006-12-04', '2008-09-16', '2009-12-01',
                       '2010-06-24', '2013-06-26', '2015-09-14', '2018-08-24']
    
    for party in parties[desired_election]:
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
        sample_size = 1000 # treat all polls as being of this size
        pseudo_sample_sigma = np.sqrt((50 * 50) / sample_size) 
        chains = 8
        iterations = 4000
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
        
        # convert dates to days from start
        # do this before removing polls with N/A values so that
        # start times are consistent amongst series
        start = df['MidDate'].min() # day zero
        df['Day'] = df['MidDate'] - start # day number for each poll
        n_days = df['Day'].max().n + 1
        
        df = df.dropna(subset=[party])
        
        # push One Nation into Other 
        tempCol = df['ONP FP'].fillna(0)
        df['OTH FP'] = df['OTH FP'] + tempCol
        tempCol = df['UAP FP'].fillna(0)
        df['OTH FP'] = df['OTH FP'] + tempCol
        n_polls = len(df)
        
        # treat later Newspoll as a seperate series 
        df['Firm'] = df['Firm'].where((df['MidDate'] < 
            pd.Period('2015-06-20', freq='D')) |
            (df['Firm'] != 'Newspoll'), other='Newspoll2')
        
        if (desired_election, party) in prior_results:
            prior_result = prior_results[(desired_election, party)]
        else:
            prior_result = 0.1
        
        # manipulate polling data ... 
        missing = df[party].apply(lambda x: 1 if np.isnan(x) else 0)
        y = df[party].fillna(prior_result)
        y = y.apply(lambda x: max(x, 0.01) )
        
        #centre_track[d add polling house data to the mix
        # make sure the "sum to zero" exclusions are 
        # last in the list
        houses = df['Firm'].unique().tolist()
        houseCounts = df['Firm'].value_counts()
        exclusions = set(['ANU', 'YouGov', 'Lonergan', 'AMR', 'F2F Morgan'])
        for h in houses:
            if houseCounts[h] < 5:
                exclusions.add(h)
        # Note: we are excluding some houses
        # from the sum to zero constraint because 
        # they have unusual or infrequent poll results compared 
        # with other pollsters
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
        
        for i in df.index:
            df.loc[i,'Day'] = df.loc[i,'Day'].n + 1
        
        discontinuities_filtered = [(pd.to_datetime(date).to_period('D') - start).n + 1 
                           for date in discontinuities]
        
        discontinuities_filtered = [date for date in discontinuities_filtered if date >= 0 and date < n_days]
        
        # 
        if not discontinuities_filtered:
            discontinuities_filtered.append(0)
        
        # quality adjustment for polls
        # currently commented out as we don't have any particular opinions about qualities of particular polls
        df['poll_qual_adj'] = 0.0
        #df['poll_qual_adj'] = pd.Series(2.0, index=df.index
        #    ).where(df['Firm'].str.contains('Ipsos|YouGov|Roy Morgan|ANU'),
        #    other=0.0)
        
        # --- compile model
        
        # get the STAN model 
        with open ("./Models/fp_model.stan", "r") as f:
            model = f.read()
            f.close()
        
        data = {
                'dayCount': n_days,
                'pollCount': n_polls,
                'houseCount': n_houses,
                'discontinuityCount': len(discontinuities_filtered),
                'pseudoSampleSigma': pseudo_sample_sigma,
                'priorResult': prior_result,
            
                'pollObservations': y.values, 
                'missingObservations': missing.values, 
                'pollHouse': df['House'].values.tolist(), 
                'pollDay': df['Day'].values.tolist(),
                'discontinuities': discontinuities_filtered,
                'pollQualityAdjustment': df['poll_qual_adj'].values,
                'excludeCount': n_exclude,
                
                'dailySigma': 0.3
        }
        
        # encode the STAN model in C++ 
        sm = stan_cache(model_code=model)
        
        print('Beginning sampling ...')
            
        fit = sm.sampling(data=data,
                          iter=iterations, 
                          chains=chains,
                          verbose=True,
                          refresh=10,
                          control={'max_treedepth':15,
                                   'adapt_delta':0.8})
        
        # --- check diagnostics
        print('Stan Finished ...')
        import pystan.diagnostics as psd
        print(psd.check_hmc_diagnostics(fit))
        
        probs_list = [0.001];
        for i in range(1, 100):
            probs_list.append(i * 0.01)
        probs_list.append(0.999)
        output_probs = tuple(probs_list)
        summary = fit.summary(probs=output_probs)['summary']
        print('Got Summary ...')
        trend_file = open(output_trend, 'w')
        trend_file.write('Start date day,Month,Year\n')
        trend_file.write(start.strftime('%d,%m,%Y\n'))
        trend_file.write('Day,Party')
        for prob in output_probs:
            trend_file.write(',' + str(round(prob * 100)) + "%")
        trend_file.write('\n')
        # need to get past the centered values and house effects
        # this is where the actual FP trend starts
        offset = n_days + n_houses * 2
        for summaryDay in range(0, n_days):
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
        offset = n_days + n_houses
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
        offset = n_days
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
