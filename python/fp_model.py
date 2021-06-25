# -*- coding: utf-8 -*-
#Created on Sun Sep 15 14:19:58 2020

# PYTHON: analyse primary poll data



def main():
    import sys
    
    import numpy as np
    import pandas as pd
    import pystan
    from time import perf_counter
    from datetime import timedelta
    
    sys.path.append( './bin' )
    from stan_cache import stan_cache
    
    # --- check version information
    print('Python version: {}'.format(sys.version))
    print('pystan version: {}'.format(pystan.__version__))
    
    # N.B. The "Others" (OTH) "party" values include votes for UAP and ONP, so these
    # are effectively counted twice. The reason for this is that many polls do 
    # not report separate UAP/ONP figures, so they are aggregated from the polls that do,
    # count them together with the other "others" under OTH, and then (in the main program)
    # subtract the UAP and ONP from the OTH value to get the true non-UAP/ONP others value
    
    parties = {
        ('2022','fed') : ['LNP FP', 'ALP FP', 'GRN FP', 'ONP FP', 'OTH FP'],
        ('2019','fed') : ['LNP FP', 'ALP FP', 'GRN FP', 'ONP FP', 'UAP FP', 'OTH FP'],
        ('2016','fed') : ['LNP FP', 'ALP FP', 'GRN FP', 'UAP FP', 'OTH FP'],
        ('2013','fed') : ['LNP FP', 'ALP FP', 'GRN FP', 'UAP FP', 'OTH FP'],
        ('2010','fed') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2007','fed') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2004','fed') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2023','nsw') : ['LNP FP', 'ALP FP', 'GRN FP', 'SFF FP', 'ONP FP', 'OTH FP'],
        ('2019','nsw') : ['LNP FP', 'ALP FP', 'GRN FP', 'SFF FP', 'ONP FP', 'OTH FP'],
        ('2015','nsw') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2011','nsw') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2007','nsw') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2022','vic') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2018','vic') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2014','vic') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2010','vic') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2006','vic') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2020','qld') : ['LNP FP', 'ALP FP', 'GRN FP', 'ONP FP', 'KAP FP', 'OTH FP'],
        ('2017','qld') : ['LNP FP', 'ALP FP', 'GRN FP', 'ONP FP', 'KAP FP', 'OTH FP'],
        ('2015','qld') : ['LNP FP', 'ALP FP', 'GRN FP', 'UAP FP', 'KAP FP', 'OTH FP'],
        ('2012','qld') : ['LNP FP', 'ALP FP', 'GRN FP', 'KAP FP', 'OTH FP'],
        ('2009','qld') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2006','qld') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2021','wa') : ['LIB FP', 'ALP FP', 'GRN FP', 'NAT FP', 'ONP FP', 'OTH FP'],
        ('2017','wa') : ['LIB FP', 'ALP FP', 'GRN FP', 'NAT FP', 'ONP FP', 'OTH FP'],
        ('2013','wa') : ['LIB FP', 'ALP FP', 'GRN FP', 'NAT FP', 'OTH FP'],
        ('2008','wa') : ['LIB FP', 'ALP FP', 'GRN FP', 'NAT FP', 'OTH FP'],
        ('2022','sa') : ['LNP FP', 'ALP FP', 'GRN FP', 'SAB FP', 'OTH FP'],
        ('2018','sa') : ['LNP FP', 'ALP FP', 'GRN FP', 'SAB FP', 'OTH FP'],
        ('2014','sa') : ['LNP FP', 'ALP FP', 'GRN FP', 'OTH FP'],
        ('2010','sa') : ['LNP FP', 'ALP FP', 'GRN FP', 'DEM FP', 'FF FP', 'OTH FP'],
        ('2006','sa') : ['LNP FP', 'ALP FP', 'GRN FP', 'DEM FP', 'FF FP', 'OTH FP']
    }
    
    others_parties = ['ONP FP', 'UAP FP', 'SFF FP', 'CA FP', 'KAP FP', 'SAB FP', 'DEM FP', 'FF FP']
    
    prior_results = {
             (('2022','fed'), 'LNP FP'): 41.44,
             (('2022','fed'), 'ALP FP'): 33.34,
             (('2022','fed'), 'GRN FP'): 10.4,
             (('2022','fed'), 'ONP FP'): 3.08,
             (('2022','fed'), 'UAP FP'): 3.43,
             (('2022','fed'), 'OTH FP'): 14.82,
             
             (('2019','fed'), 'LNP FP'): 42.04,
             (('2019','fed'), 'ALP FP'): 34.73,
             (('2019','fed'), 'GRN FP'): 10.23,
             (('2019','fed'), 'OTH FP'): 13.0,
             
             (('2016','fed'), 'LNP FP'): 45.55,
             (('2016','fed'), 'ALP FP'): 33.38,
             (('2016','fed'), 'GRN FP'): 8.65,
             (('2016','fed'), 'UAP FP'): 5.49,
             (('2016','fed'), 'OTH FP'): 12.42,
             
             (('2013','fed'), 'LNP FP'): 43.66,
             (('2013','fed'), 'ALP FP'): 37.99,
             (('2013','fed'), 'GRN FP'): 11.76,
             (('2013','fed'), 'OTH FP'): 6.63,
             
             (('2010','fed'), 'LNP FP'): 42.09,
             (('2010','fed'), 'ALP FP'): 43.38,
             (('2010','fed'), 'GRN FP'): 7.79,
             (('2010','fed'), 'OTH FP'): 6.73,
             
             (('2007','fed'), 'LNP FP'): 46.71,
             (('2007','fed'), 'ALP FP'): 37.63,
             (('2007','fed'), 'GRN FP'): 7.19,
             (('2007','fed'), 'OTH FP'): 8.47,
             
             (('2004','fed'), 'LNP FP'): 42.92,
             (('2004','fed'), 'ALP FP'): 37.84,
             (('2004','fed'), 'GRN FP'): 4.96,
             (('2004','fed'), 'OTH FP'): 14.28,
             
             (('2019','nsw'), 'LNP FP'): 45.63,
             (('2019','nsw'), 'ALP FP'): 34.08,
             (('2019','nsw'), 'GRN FP'): 10.29,
             (('2019','nsw'), 'OTH FP'): 10.0,
             
             (('2015','nsw'), 'LNP FP'): 51.15,
             (('2015','nsw'), 'ALP FP'): 25.55,
             (('2015','nsw'), 'GRN FP'): 10.28,
             (('2015','nsw'), 'OTH FP'): 13.02,
             
             (('2011','nsw'), 'LNP FP'): 38.98,
             (('2011','nsw'), 'ALP FP'): 36.99,
             (('2011','nsw'), 'GRN FP'): 8.95,
             (('2011','nsw'), 'OTH FP'): 15.08,
             
             (('2007','nsw'), 'LNP FP'): 34.35,
             (('2007','nsw'), 'ALP FP'): 42.68,
             (('2007','nsw'), 'GRN FP'): 8.25,
             (('2007','nsw'), 'OTH FP'): 14.42,
             
             (('2022','vic'), 'LNP FP'): 35.19,
             (('2022','vic'), 'ALP FP'): 42.86,
             (('2022','vic'), 'GRN FP'): 10.71,
             (('2022','vic'), 'OTH FP'): 11.24,
             
             (('2018','vic'), 'LNP FP'): 42.0,
             (('2018','vic'), 'ALP FP'): 38.1,
             (('2018','vic'), 'GRN FP'): 11.48,
             (('2018','vic'), 'OTH FP'): 8.42,
             
             (('2014','vic'), 'LNP FP'): 44.78,
             (('2014','vic'), 'ALP FP'): 36.25,
             (('2014','vic'), 'GRN FP'): 11.21,
             (('2014','vic'), 'OTH FP'): 7.76,
             
             (('2010','vic'), 'LNP FP'): 39.61,
             (('2010','vic'), 'ALP FP'): 43.06,
             (('2010','vic'), 'GRN FP'): 10.04,
             (('2010','vic'), 'OTH FP'): 7.29,
             
             (('2006','vic'), 'LNP FP'): 38.31,
             (('2006','vic'), 'ALP FP'): 47.95,
             (('2006','vic'), 'GRN FP'): 9.73,
             (('2006','vic'), 'OTH FP'): 4.01,
             
             (('2020','qld'), 'LNP FP'): 33.69,
             (('2020','qld'), 'ALP FP'): 35.43,
             (('2020','qld'), 'GRN FP'): 10.0,
             (('2020','qld'), 'ONP FP'): 13.73,
             (('2020','qld'), 'KAP FP'): 2.32,
             (('2020','qld'), 'OTH FP'): 4.83,
             
             (('2017','qld'), 'LNP FP'): 41.32,
             (('2017','qld'), 'ALP FP'): 37.47,
             (('2017','qld'), 'GRN FP'): 8.43,
             (('2017','qld'), 'ONP FP'): 0.92,
             (('2017','qld'), 'KAP FP'): 1.93,
             (('2017','qld'), 'OTH FP'): 9.93,
             
             (('2015','qld'), 'LNP FP'): 49.66,
             (('2015','qld'), 'ALP FP'): 26.66,
             (('2015','qld'), 'GRN FP'): 7.53,
             (('2015','qld'), 'KAP FP'): 11.53,
             (('2015','qld'), 'OTH FP'): 4.62,
             
             (('2012','qld'), 'LNP FP'): 41.6,
             (('2012','qld'), 'ALP FP'): 42.25,
             (('2012','qld'), 'GRN FP'): 8.37,
             (('2012','qld'), 'OTH FP'): 7.78,
             
             (('2009','qld'), 'LNP FP'): 38.92,
             (('2009','qld'), 'ALP FP'): 46.92,
             (('2009','qld'), 'GRN FP'): 7.99,
             (('2009','qld'), 'OTH FP'): 6.17,
             
             (('2006','qld'), 'LNP FP'): 35.46,
             (('2006','qld'), 'ALP FP'): 47.01,
             (('2006','qld'), 'GRN FP'): 6.76,
             (('2006','qld'), 'OTH FP'): 10.77,
             
             (('2021','wa'), 'LIB FP'): 31.23,
             (('2021','wa'), 'ALP FP'): 42.2,
             (('2021','wa'), 'GRN FP'): 8.91,
             (('2021','wa'), 'NAT FP'): 5.4,
             (('2021','wa'), 'ONP FP'): 4.93,
             (('2021','wa'), 'OTH FP'): 12.26,
             
             (('2017','wa'), 'LIB FP'): 47.1,
             (('2017','wa'), 'ALP FP'): 33.13,
             (('2017','wa'), 'GRN FP'): 8.39,
             (('2017','wa'), 'NAT FP'): 6.05,
             (('2017','wa'), 'OTH FP'): 5.33,
             
             (('2013','wa'), 'LIB FP'): 38.39,
             (('2013','wa'), 'ALP FP'): 35.84,
             (('2013','wa'), 'GRN FP'): 11.92,
             (('2013','wa'), 'NAT FP'): 4.87,
             (('2013','wa'), 'OTH FP'): 8.98,
             
             (('2008','wa'), 'LIB FP'): 37.12,
             (('2008','wa'), 'ALP FP'): 43.35,
             (('2008','wa'), 'GRN FP'): 7.52,
             (('2008','wa'), 'NAT FP'): 2.19,
             (('2008','wa'), 'OTH FP'): 9.82,
             
             (('2022','sa'), 'LNP FP'): 38.0,
             (('2022','sa'), 'ALP FP'): 32.8,
             (('2022','sa'), 'GRN FP'): 6.7,
             (('2022','sa'), 'SAB FP'): 14.2,
             (('2022','sa'), 'OTH FP'): 8.3,
             
             (('2018','sa'), 'LNP FP'): 44.78,
             (('2018','sa'), 'ALP FP'): 35.8,
             (('2018','sa'), 'GRN FP'): 8.7,
             (('2018','sa'), 'OTH FP'): 10.72,
             
             (('2014','sa'), 'LNP FP'): 41.65,
             (('2014','sa'), 'ALP FP'): 37.47,
             (('2014','sa'), 'GRN FP'): 8.11,
             (('2014','sa'), 'OTH FP'): 12.77,
             
             (('2010','sa'), 'LNP FP'): 33.97,
             (('2010','sa'), 'ALP FP'): 45.22,
             (('2010','sa'), 'GRN FP'): 6.49,
             (('2010','sa'), 'FF FP'): 5.88,
             (('2010','sa'), 'DEM FP'): 2.89,
             (('2010','sa'), 'OTH FP'): 5.55,
             
             (('2006','sa'), 'LNP FP'): 41.42,
             (('2006','sa'), 'ALP FP'): 36.34,
             (('2006','sa'), 'GRN FP'): 2.36,
             (('2006','sa'), 'FF FP'): 2.64,
             (('2006','sa'), 'DEM FP'): 7.49,
             (('2006','sa'), 'OTH FP'): 9.75,
        }
    
    # Set up discontinuity data
    fed_d = ['2005-01-28', '2006-12-04', '2008-09-16', '2009-12-01',
             '2010-06-24', '2013-06-26', '2015-09-14', '2018-08-24']
    nsw_d = ['2005-08-03', '2005-09-01', '2008-09-05', '2009-12-04', '2014-04-17',
             '2015-01-05', '2017-01-12', '2018-11-10']
    vic_d = ['2006-05-08', '2013-03-06', '2007-07-30']
    qld_d = ['2007-09-13', '2016-05-06']
    wa_d = ['2006-03-24', '2008-01-17', '2008-08-04', '2012-01-23', '2019-06-13']
    sa_d = ['2007-04-12', '2009-07-08', '2011-10-21', '2013-01-31']
    discontinuities = {
        'fed': fed_d,
        'nsw': nsw_d,
        'vic': vic_d,
        'qld': qld_d,
        'wa': wa_d,
        'sa': sa_d
    }
    
    all_iterations = {
        'fed': 2000,
        'nsw': 500,
        'vic': 500,
        'qld': 2000,
        'wa': 300,
        'sa': 500
    }
    
    election_cycles = {
        ('2004','fed'): (pd.Period('2001-11-11', freq='D'), pd.Period('2004-10-09', freq='D')),
        ('2007','fed'): (pd.Period('2004-10-10', freq='D'), pd.Period('2007-11-24', freq='D')),
        ('2010','fed'): (pd.Period('2007-11-25', freq='D'), pd.Period('2010-08-21', freq='D')),
        ('2013','fed'): (pd.Period('2010-08-22', freq='D'), pd.Period('2013-09-07', freq='D')),
        ('2016','fed'): (pd.Period('2013-09-08', freq='D'), pd.Period('2016-07-02', freq='D')),
        ('2019','fed'): (pd.Period('2016-07-03', freq='D'), pd.Period('2019-05-18', freq='D')),
        ('2022','fed'): (pd.Period('2019-05-19', freq='D'), pd.Period('2022-12-12', freq='D')),
        ('2007','nsw'): (pd.Period('2003-03-23', freq='D'), pd.Period('2007-03-24', freq='D')),
        ('2011','nsw'): (pd.Period('2007-03-25', freq='D'), pd.Period('2011-03-26', freq='D')),
        ('2015','nsw'): (pd.Period('2011-03-27', freq='D'), pd.Period('2015-03-28', freq='D')),
        ('2019','nsw'): (pd.Period('2015-03-29', freq='D'), pd.Period('2019-03-23', freq='D')),
        ('2023','nsw'): (pd.Period('2019-03-24', freq='D'), pd.Period('2023-03-25', freq='D')),
        ('2006','vic'): (pd.Period('2002-12-01', freq='D'), pd.Period('2006-11-25', freq='D')),
        ('2010','vic'): (pd.Period('2006-11-26', freq='D'), pd.Period('2010-11-27', freq='D')),
        ('2014','vic'): (pd.Period('2010-11-28', freq='D'), pd.Period('2014-11-29', freq='D')),
        ('2018','vic'): (pd.Period('2014-11-30', freq='D'), pd.Period('2018-11-24', freq='D')),
        ('2022','vic'): (pd.Period('2018-11-25', freq='D'), pd.Period('2022-11-26', freq='D')),
        ('2006','qld'): (pd.Period('2004-02-08', freq='D'), pd.Period('2006-09-09', freq='D')),
        ('2009','qld'): (pd.Period('2006-09-10', freq='D'), pd.Period('2009-03-21', freq='D')),
        ('2012','qld'): (pd.Period('2009-03-22', freq='D'), pd.Period('2012-03-24', freq='D')),
        ('2015','qld'): (pd.Period('2012-03-25', freq='D'), pd.Period('2015-01-31', freq='D')),
        ('2017','qld'): (pd.Period('2015-02-01', freq='D'), pd.Period('2017-11-25', freq='D')),
        ('2020','qld'): (pd.Period('2017-11-26', freq='D'), pd.Period('2020-10-31', freq='D')),
        ('2008','wa'): (pd.Period('2005-02-27', freq='D'), pd.Period('2008-09-06', freq='D')),
        ('2013','wa'): (pd.Period('2008-09-07', freq='D'), pd.Period('2013-07-06', freq='D')),
        ('2017','wa'): (pd.Period('2013-07-07', freq='D'), pd.Period('2017-03-11', freq='D')),
        ('2021','wa'): (pd.Period('2017-03-12', freq='D'), pd.Period('2021-03-13', freq='D')),
        ('2006','sa'): (pd.Period('2002-02-09', freq='D'), pd.Period('2006-03-18', freq='D')),
        ('2010','sa'): (pd.Period('2006-03-19', freq='D'), pd.Period('2010-03-20', freq='D')),
        ('2014','sa'): (pd.Period('2010-03-21', freq='D'), pd.Period('2014-03-15', freq='D')),
        ('2018','sa'): (pd.Period('2014-03-16', freq='D'), pd.Period('2018-03-17', freq='D')),
        ('2022','sa'): (pd.Period('2018-03-18', freq='D'), pd.Period('2022-03-19', freq='D'))
    }
    
    data_source = {
        'fed': './Data/poll-data-fed.xlsx',
        'nsw': './Data/poll-data-nsw.xlsx',
        'vic': './Data/poll-data-vic.xlsx',
        'qld': './Data/poll-data-qld.xlsx',
        'wa': './Data/poll-data-wa.xlsx',
        'sa': './Data/poll-data-sa.xlsx',
    }
    
    config = open('config.txt', 'r')
    lines = config.readlines()
    config.close()
    
    desired_elections = [tuple([val.strip() for val in line.split('-')]) for line in lines]
    
    for desired_election in desired_elections:
        for party in parties[desired_election]:
            
            output_trend = './Outputs/fp_trend_' + ''.join(desired_election) + '_' + party + '.csv'
            output_polls = './Outputs/fp_polls_' + ''.join(desired_election) + '_' + party + '.csv'
            output_house_effects = './Outputs/fp_house_effects_' + ''.join(desired_election) + '_' + party + '.csv'
        
            # --- key inputs to model
            sample_size = 1000 # treat all polls as being of this size
            pseudo_sample_sigma = np.sqrt((50 * 50) / sample_size) 
            chains = 6
            iterations = all_iterations[desired_election[1]]
            # Note: half of the iterations will be warm-up
            
            # --- collect the model data
            # the XL data file was extracted from the Wikipedia
            # page on next Australian Federal Election
            workbook = pd.ExcelFile(data_source[desired_election[1]])
            df = workbook.parse('Data')
            
            # drop data not in range of this election period
            df['MidDate'] = [pd.Period(date, freq='D') for date in df['MidDate']]
            df = df[df['MidDate'] >= election_cycles[desired_election][0]] 
            df = df[df['MidDate'] <= election_cycles[desired_election][1]] 
            
            # convert dates to days from start
            # do this before removing polls with N/A values so that
            # start times are consistent amongst series
            start = election_cycles[desired_election][0] # day zero
            df['Day'] = df['MidDate'] - start # day number for each poll
            n_days = df['Day'].max().n + 1
            
            election_day = (election_cycles[desired_election][1] - start).n
            
            df = df.dropna(subset=[party])
            
            # push misc parties into Others
            for others_party in others_parties:
                try:
                    tempCol = df[others_party].fillna(0)
                    df['OTH FP'] = df['OTH FP'] + tempCol
                except KeyError:
                    pass #it's expected that not all parties will be in the data file
            n_polls = len(df)
            
            # treat later Newspoll as a seperate series 
            df['Firm'] = df['Firm'].where((df['MidDate'] < 
                pd.Period('2015-06-20', freq='D')) |
                (df['Firm'] != 'Newspoll'), other='Newspoll2')
            
            if (desired_election, party) in prior_results:
                prior_result = prior_results[(desired_election, party)]
            else:
                prior_result = 0.25
            
            # manipulate polling data ... 
            missing = df[party].apply(lambda x: 1 if np.isnan(x) else 0)
            y = df[party].fillna(prior_result)
            y = y.apply(lambda x: max(x, 0.01) )
            
            #centre_track[d add polling house data to the mix
            # make sure the "sum to zero" exclusions are 
            # last in the list
            # Note: we are excluding some houses
            # from the sum to zero constraint because 
            # they have unusual or infrequent poll results compared 
            # with other pollsters
            houses = df['Firm'].unique().tolist()
            houseCounts = df['Firm'].value_counts()
            exclusions = set(['ANU', 'YouGov', 'Lonergan', 'AMR', 'F2F Morgan', 'SMS Morgan', 'Saulwick', 'McNair', 'Taverner'])
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
            
            for i in df.index:
                df.loc[i,'Day'] = df.loc[i,'Day'].n + 1
                
            discontinuities_filtered = discontinuities[desired_election[1]]
            
            discontinuities_filtered = [(pd.to_datetime(date).to_period('D') - start).n + 1 
                               for date in discontinuities_filtered]
            
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
                    
                    'electionDay': election_day,
                    
                    'dailySigma': 0.2,
                    'campaignSigma': 0.35,
                    'finalSigma': 0.5
            }
            
            # encode the STAN model in C++ 
            sm = stan_cache(model_code=model)
            
            print('Beginning sampling for ' + party + ' ...')
            end = start + timedelta(days = n_days)
            print('Start date of model: ' + start.strftime('%Y-%m-%d\n'))
            print('End date of model: ' + end.strftime('%Y-%m-%d\n'))
            
            start_time = perf_counter()
            fit = sm.sampling(data=data,
                              iter=iterations, 
                              chains=chains,
                              control={'max_treedepth':16,
                                       'adapt_delta':0.8})
            finish_time = perf_counter()
            
            print('Time elapsed: ' + format(finish_time - start_time, '.2f') + ' seconds')
            print('Stan Finished ...')
            
            #--- check diagnostics
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
            offset = n_days + n_houses
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
            offset = n_days
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
