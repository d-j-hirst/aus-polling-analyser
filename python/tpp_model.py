# -*- coding: utf-8 -*-
#Created on Sun Sep 13 00:29:58 2020

# PYTHON: analyse TPP poll data

import pandas as pd
import numpy as np
import pystan

import sys
from stan_cache import stan_cache

# --- version information
print('Python version: {}'.format(sys.version))
print('pystan version: {}'.format(pystan.__version__))

desired_election = '2022fed'

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
    '2004fed': './Data/poll-data.xlsx',
    '2007fed': './Data/poll-data.xlsx',
    '2010fed': './Data/poll-data.xlsx',
    '2013fed': './Data/poll-data.xlsx',
    '2016fed': './Data/poll-data.xlsx',
    '2019fed': './Data/poll-data.xlsx',
    '2022fed': './Data/poll-data.xlsx',
}

output_trend = {
    '2004fed': './Outputs/tpp_trend_2004.csv',
    '2007fed': './Outputs/tpp_trend_2007.csv',
    '2010fed': './Outputs/tpp_trend_2010.csv',
    '2013fed': './Outputs/tpp_trend_2013.csv',
    '2016fed': './Outputs/tpp_trend_2016.csv',
    '2019fed': './Outputs/tpp_trend_2019.csv',
    '2022fed': './Outputs/tpp_trend_2022.csv',
}

output_polls = {
    '2004fed': './Outputs/tpp_polls_2004.csv',
    '2007fed': './Outputs/tpp_polls_2007.csv',
    '2010fed': './Outputs/tpp_polls_2010.csv',
    '2013fed': './Outputs/tpp_polls_2013.csv',
    '2016fed': './Outputs/tpp_polls_2016.csv',
    '2019fed': './Outputs/tpp_polls_2019.csv',
    '2022fed': './Outputs/tpp_polls_2022.csv',
}

output_house_effects = {
    '2004fed': './Outputs/tpp_house_effects_2004.csv',
    '2007fed': './Outputs/tpp_house_effects_2007.csv',
    '2010fed': './Outputs/tpp_house_effects_2010.csv',
    '2013fed': './Outputs/tpp_house_effects_2013.csv',
    '2016fed': './Outputs/tpp_house_effects_2016.csv',
    '2019fed': './Outputs/tpp_house_effects_2019.csv',
    '2022fed': './Outputs/tpp_house_effects_2022.csv',
}

def main():
    # --- key inputs to model
    sample_size = 1000 # treat all polls as being of this size
    pseudo_sample_sigma = np.sqrt((0.5 * 0.5) / sample_size) * 100
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
    
    # add polling house data to the mix
    # make sure the sum-to-zero exclusions are last in the list
    # exclude from sum-to-zero on the basis of being outliers to the other houses
    houses = df['Firm'].unique().tolist()
    exclusions = ['Roy Morgan', 'ANU', 'YouGov']
    for e in exclusions:
        if e in houses:
            houses.remove(e)
        else:
            exclusions.remove(e)
    houses = houses + exclusions
    house_map = dict(zip(houses, range(1, len(houses)+1)))
    df['House'] = df['Firm'].map(house_map)
    n_houses = len(df['House'].unique())
    n_exclude = len(exclusions)
    
    for i in range(0, n_polls):
        df.loc[i,'Day'] = df.loc[i,'Day'].n
    
    # batch up
    data = {
        'n_days': n_days.n,
        'n_polls': n_polls,
        'n_houses': n_houses,
        'pseudoSampleSigma': pseudo_sample_sigma,
        
        'y': (df['TPP ALP']).values,
        'house': df['House'].astype(int).values,
        'day': df['Day'].astype(int).values,
        
        # uncomment these lines if an event worthy of a discontinuity occurs
        #'discontinuity': discontinuity,
        #'stability': stability,
        'n_exclude': n_exclude
    }
    
    # --- get the STAN model 
    with open ('./Models/tpp_model.stan', 'r') as f:
        model = f.read()
        f.close()
    
    # --- compile/retrieve model and run samples
    sm = stan_cache(model_code=model)
    fit = sm.sampling(data=data, iter=iterations, 
        chains=chains, control={'max_treedepth':12})
    
    # --- check diagnostics
    print(fit.stansummary())
    import pystan.diagnostics as psd
    print(psd.check_hmc_diagnostics(fit))
        
    output_probs = (0.005,0.025,0.1,0.25,0.5,0.75,0.9,0.975,0.995)
    summary = fit.summary(probs=output_probs)['summary']
    trend_file = open(output_trend[desired_election], 'w')
    trend_file.write('Day,0.5%,2.5%,10%,25%,50%,75%,90%,97.5%,99.5%\n')
    for summaryDay in range(0, n_days.n):
        trend_file.write(str(summaryDay) + ",")
        for col in range(3,3+len(output_probs)-1):
            trend_file.write(str(summary[summaryDay][col]) + ',')
        trend_file.write(str(summary[summaryDay][3+len(output_probs)-1]) + '\n')
    trend_file.close()
    
    house_effects = summary[n_days.n + n_houses:n_days.n + n_houses * 2,0]
    
    polls_file = open(output_polls[desired_election], 'w')
    polls_file.write('Firm,Day,Raw ALP TPP,Adjusted ALP TPP\n')
    for poll_index in range(0, n_polls):
        polls_file.write(str(df.loc[poll_index, 'Firm']) + ',')
        polls_file.write(str(df.loc[poll_index, 'Day']) + ',')
        tpp = df.loc[poll_index, 'TPP ALP']
        adjusted_tpp = tpp - house_effects[df.loc[poll_index, 'House'] - 1]
        polls_file.write(str(tpp) + ',')
        polls_file.write(str(adjusted_tpp) + '\n')
    polls_file.close()
    
    house_effects_file = open(output_house_effects[desired_election], 'w')
    house_effects_file.write('Day,0.5%,2.5%,10%,25%,50%,75%,90%,97.5%,99.5%\n')
    for house_index in range(0, n_houses):
        table_index = n_days.n + n_houses + house_index;
        house_effects_file.write(houses[house_index] + ",")
        for col in range(3,3+len(output_probs)-1):
            house_effects_file.write(str(summary[table_index][col]) + ',')
        house_effects_file.write(str(summary[table_index][3+len(output_probs)-1]) + '\n')
    house_effects_file.close()

if __name__ == '__main__':
    main()