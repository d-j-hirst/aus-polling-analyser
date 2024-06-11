import argparse
import math
import numpy as np
import pandas as pd
import pystan
from election_code import ElectionCode
from datetime import timedelta
from time import perf_counter

from stan_cache import stan_cache


regions = ['NSW', 'VIC', 'QLD', 'WA', 'SA', 'WSTAN']


class ConfigError(ValueError):
  pass


class Config:
  def __init__(self):
    parser = argparse.ArgumentParser(
      description='Determine trend adjustment parameters')
    parser.add_argument(
      '--election',
      action='store',
      type=str,
      help='Generate regional trends for this election.'
           'Enter as 1234-xxx format, e.g. 2013-fed. Write "all" '
           'to do it for all elections.')
    self.election_instructions = parser.parse_args().election.lower()
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
        raise ConfigError(
          'Error in "elections" argument: given value did not have two parts '
          'separated by a hyphen (e.g. 2013-fed)'
        )
      try:
        code = ElectionCode(parts[0], parts[1])
      except ValueError:
        raise ConfigError(
          'Error in "elections" argument: first part of election name could'
          'not be converted into an integer'
        )
      if code not in elections:
        raise ConfigError(
          'Error in "elections" argument: value given did not match any '
          'election given in Data/polled-elections.csv'
        )
      if len(parts) == 2:
        self.elections = [code]
      elif parts[2] == 'onwards':
        try:
          self.elections = (elections[elections.index(code):])
        except ValueError:
          raise ConfigError(
            'Error in "elections" argument: value given did not match any '
            'election given in Data/polled-elections.csv'
          )
      else:
        raise ConfigError('Invalid instruction in "elections" argument.')
      self.elections = [e for e in self.elections if e.region() == 'fed']


class ModellingData:
  def __init__(self):
    # Load the file containing prior results for each election
    with open('./Data/prior-results.csv', 'r') as f:
      self.prior_results = {
        ((a[0], a[1]), a[2]): float(a[3])
        for a in [
          b.strip().split(',') for b in f.readlines()
        ]
    }
        
    # Load the dates of next and previous elections
    # We will only model polls between those two dates
    with open('./Data/election-cycles.csv', 'r') as f:
      self.election_cycles = {
        (a[0], a[1]): (
          pd.Timestamp(a[2]),
          pd.Timestamp(a[3])
        )
        for a in [
          b.strip().split(',')
          for b in f.readlines()
        ]
      }
            

class ElectionData:
  def __init__(self, config, m_data, desired_election):
    self.e_tuple = (str(desired_election.year()),
                      desired_election.region())           
    tup = self.e_tuple
    self.others_medians = {}

    # collect the model data
    filename = f'./Regional/{desired_election.short()}-polls.csv'
    self.base_df = pd.read_csv(filename)

    self.previous_results = (
      self.base_df[self.base_df.Firm == 'Election'].to_dict('records')[0]
    )

    self.base_df = self.base_df[self.base_df.Firm != 'Election']

    # convert dates to days from start
    self.base_df['StartDate'] = [
      pd.Timestamp(date) for date in self.base_df['StartDate']
    ]
    self.base_df['EndDate'] = [
      pd.Timestamp(date) for date in self.base_df['EndDate']
    ]
    self.base_df['MidDate'] = (
      self.base_df['StartDate'] + (
        self.base_df['EndDate'] - self.base_df['StartDate']
      ) / 2
    )
    # day number for each poll period
    self.start = self.base_df['StartDate'].min()  # day zero
    self.base_df['StartDay'] = (self.base_df['StartDate'] - self.start).dt.days
    self.base_df['MidDay'] = (self.base_df['MidDate'] - self.start).dt.days
    self.base_df['EndDay'] = (self.base_df['EndDate'] - self.start).dt.days
    self.n_days = self.base_df['EndDay'].max() + 1

    # store the election day for when the model needs it later
    self.election_day = (m_data.election_cycles[tup][1] - self.start).days

    self.all_houses = self.base_df['Firm'].unique().tolist()

    self.create_day_series()

    print(self.base_df)

  def create_day_series(self):
    # Convert "days" objects into raw numerical data
    # that Stan can accept
    for i in self.base_df.index:
      self.base_df.loc[i, 'StartDayNum'] = int(self.base_df.loc[i, 'StartDay'] + 1)
      self.base_df.loc[i, 'MidDayNum'] = int(self.base_df.loc[i, 'MidDay'] + 1)
      self.base_df.loc[i, 'EndDayNum'] = int(self.base_df.loc[i, 'EndDay'] + 1)


def run_model(config, m_data, e_data):
  df = e_data.base_df.copy()

  df['NatSwing'] = df['National'] - e_data.previous_results['National']

  for region in regions:
    df[f'{region}_SwingDev'] = (
      df[f'{region}'] - e_data.previous_results[f'{region}'] - df['NatSwing']
    )

  pollDays = (
    [int(a) for a in df['StartDayNum'].values] +
    [int(a) for a in df['MidDayNum'].values] +
    [int(a) for a in df['EndDayNum'].values]
  )
  df.fillna(-10000, inplace=True)

  # Modify poll "days" to be more efficient
  modified_day_count = max(math.floor(e_data.n_days / 5), 1)
  modified_poll_days = [min(modified_day_count, max(1, math.floor(a / 5))) for a in pollDays]

  stan_data = {
    'pollCount': df.shape[0] * 3,
    'dayCount': modified_day_count, # scale for efficiency
    'pollDay': modified_poll_days,
    'nswSwingDevPoll': df['NSW_SwingDev'].tolist() * 3,
    'vicSwingDevPoll': df['VIC_SwingDev'].tolist() * 3,
    'qldSwingDevPoll': df['QLD_SwingDev'].tolist() * 3,
    'waSwingDevPoll': df['WA_SwingDev'].tolist() * 3,
    'saSwingDevPoll': df['SA_SwingDev'].tolist() * 3,
    'wstanSwingDevPoll': df['WSTAN_SwingDev'].tolist() * 3,
    'pollSize': df['Size'].tolist() * 3,
  }

  print(stan_data)

  # get the Stan model code
  with open("./Models/region_model.stan", "r") as f:
    model = f.read()

  # encode the STAN model in C++ or retrieve it if already cached
  sm = stan_cache(model_code=model)

  # Report dates for model, this means we can easily check if new
  # data has actually been saved without waiting for model to run
  print('Beginning sampling ...')
  end = e_data.start + timedelta(days=int(e_data.n_days))
  print('Start date of model: ' + e_data.start.strftime('%Y-%m-%d\n'))
  print('End date of model: ' + end.strftime('%Y-%m-%d\n'))

  # Stan model configuration
  chains = 6
  iterations = 300

  # Do model sampling. Time for diagnostic purposes
  start_time = perf_counter()
  fit = sm.sampling(data=stan_data,
                      iter=iterations,
                      chains=chains,
                      control={'max_treedepth': 18,
                              'adapt_delta': 0.8})
  finish_time = perf_counter()
  print('Time elapsed: ' + format(finish_time - start_time, '.2f')
          + ' seconds')
  print('Stan Finished ...')

  # Check technical model diagnostics
  import pystan.diagnostics as psd
  print(psd.check_hmc_diagnostics(fit))

  probs_list = [0.001]
  for i in range(1, 100):
      probs_list.append(i * 0.01)
  probs_list.append(0.999)
  probs_list = [0.5]
  output_probs = tuple(probs_list)
  summary = fit.summary(probs=output_probs)

  required_rows = [modified_day_count * a - 1 for a in range(1, 7)]
  state_vals = [summary['summary'].tolist()[a][0] for a in required_rows]
  print(state_vals)
  with open(
    f'./Regional/{e_data.e_tuple[0]}{e_data.e_tuple[1]}-swing-deviations.csv', 'w'
  ) as f:
    f.write('nsw,vic,qld,wa,sa,tan\n')
    f.write(','.join([str(a) for a in state_vals]))

  for offset in reversed(range(0, 5)):
    required_rows = [modified_day_count * a - offset for a in range(1, 7)]
    state_vals = [summary['summary'].tolist()[a][0] for a in required_rows]
    print(state_vals)


def run_models():
  try:
    config = Config()
  except ConfigError as e:
    print('Could not process configuration due to the following issue:')
    print(str(e))
    return

  m_data = ModellingData()

  # Load the list of election periods we want to model
  desired_elections = config.elections

  print(desired_elections)

  for desired_election in desired_elections:
    e_data = ElectionData(
      config=config,
      m_data=m_data,
      desired_election=desired_election
    )

    run_model(
      config=config,
      m_data=m_data,
      e_data=e_data,
    )


if __name__ == '__main__':
    run_models()