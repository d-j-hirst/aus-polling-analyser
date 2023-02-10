import numpy as np
import pandas as pd
import statsmodels.api as sm
import statsmodels.formula.api as smf

df = pd.read_csv('./Data/by-elections.csv')

print('Assessing: by-election swing correlations')

df['swingdev'] = df['Eventual swing'] - df['Statewide swing']
df['byelecswing'] = df['By-elec swing']

def report(df):
    byelection_swing = np.array(df['byelecswing'].tolist())
    swing_dev = np.array(df['swingdev'].tolist())

    ols_model = sm.OLS(swing_dev, byelection_swing)
    ols_results = ols_model.fit()
    print(ols_results.summary())

    quantile_model = smf.quantreg('swingdev ~ byelecswing', df)
    quantile_results = quantile_model.fit(q=0.5)
    print(quantile_results.summary())

print('*' * 60)
print('The following reports are for: all by-elections')
print('*' * 60)
report(df)

flipped_df = df[(df['Party change']) == True]

print('*' * 60)
print('The following reports are for: by-elections with a party change')
print('*' * 60)
report(flipped_df)

alp_df = df[(df['Government']) == 'ALP']

print('*' * 60)
print('The following reports are for: by-elections with ALP in government')
print('*' * 60)
report(alp_df)

lnp_df = df[(df['Government']) == 'LNP']

print('*' * 60)
print('The following reports are for: by-elections with Coalition in government')
print('*' * 60)
report(lnp_df)