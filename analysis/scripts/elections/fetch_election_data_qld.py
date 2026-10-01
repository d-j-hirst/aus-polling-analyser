import argparse
import time
import json
import os.path

election = '2020qld'
driver = None
By = None
StaleElementReferenceException = None

urls = {
  '2020qld': 'https://results.elections.qld.gov.au/state2020',
}

def create_driver():
  global By, StaleElementReferenceException
  from selenium import webdriver
  from selenium.common.exceptions import (
      StaleElementReferenceException as SeleniumStaleElementReferenceException,
  )
  from selenium.webdriver.chrome.service import Service
  from selenium.webdriver.chrome.options import Options
  from selenium.webdriver.common.by import By as SeleniumBy

  By = SeleniumBy
  StaleElementReferenceException = SeleniumStaleElementReferenceException
  chrome_options = Options()
  chrome_options.add_argument('--headless=new')
  chrome_options.add_argument("--no-sandbox")
  # This assumes running Linux/wsl2 and you have installed chromedriver
  # according to instructions here:
  # https://cloudbytes.dev/snippets/run-selenium-and-chrome-on-wsl2
  homedir = os.path.expanduser("~")
  chrome_options.binary_location = f"{homedir}/chrome-linux64/chrome"
  webdriver_service = Service(f"{homedir}/chromedriver-linux64/chromedriver")
  return webdriver.Chrome(service=webdriver_service, options=chrome_options)

skip_booths = [
  # 'Absent Early Voting',
  # 'Absent Election Day',
  # 'Brisbane CBD Early Voting',
  # 'In Person Declaration Votes',
  # 'Mobile Polling',
  # 'Postal Declaration Votes',
  # 'Telephone Voting',
  # 'Telephone Voting - Early Voting',
]

def replace_booth_name(booth_name):
  booth_name = booth_name.replace('CBD Early Voting', 'CBD Early Voting Centre')
  return booth_name


def candidate_index_for_tcp(seat_name, candidate_name):
  normalized = candidate_name.replace("Declared: ", "").strip().casefold()
  candidates = candidate_map[seat_name]
  exact = [
      index for index, candidate in candidates.items()
      if candidate['name'].strip().casefold() == normalized
  ]
  if len(exact) == 1:
    return exact[0]
  surname = normalized.split()[-1]
  surname_matches = [
      index for index, candidate in candidates.items()
      if candidate['name'].strip().casefold().split()[-1] == surname
  ]
  if len(surname_matches) == 1:
    return surname_matches[0]
  raise ValueError(
      f'Could not match TCP candidate {candidate_name!r} in {seat_name}: '
      f'{candidates!r}')


def get_electorate_links():
  driver.get(urls[election])
  time.sleep(1)
  electorate_table = driver.find_element(By.ID, 'electorateList')
  links = electorate_table.find_elements(By.TAG_NAME, 'a')
  seat_names = [link.text for link in links]
  hrefs = [link.get_attribute('href') for link in links]
  return hrefs, seat_names

def get_candidate_names(link):
  driver.get(link)
  time.sleep(1)
  for attempt in range(5):
    try:
      candidate_elements = driver.find_elements(By.CLASS_NAME, 'candidateName')
      names = [
          element.text.replace("Declared: ", "").strip()
          for element in candidate_elements
      ]
      party_elements = driver.find_elements(By.CLASS_NAME, 'candidateParty')
      parties = [element.text.strip() for element in party_elements]
      if names and len(names) == len(parties):
        break
      if attempt == 4:
        raise ValueError(
            f'Candidate table did not load for {link}')
      time.sleep(1)
    except StaleElementReferenceException:
      if attempt == 4:
        raise
      time.sleep(1)
  parties = ["Independent" if party == "" else party for party in parties]
  return {num: {'name': name, 'party': party} for num, (name, party) in enumerate(zip(names, parties))}

def extract_full_name(seat_name, name, party):
  return next((
    full_name for full_name, full_party
    in candidate_map[seat_name]
    if party == full_party and name in full_name
  ), name)

def _get_fps_once(fp_link):
  driver.get(fp_link)
  time.sleep(1)
  content = driver.find_element(By.ID, 'resultTable')
  row = content.find_element(By.TAG_NAME, 'tr')
  tbody = content.find_element(By.TAG_NAME, 'tbody')
  booth_rows = tbody.find_elements(By.TAG_NAME, 'tr')
  booths = {}
  for booth_row in booth_rows:
    booth_cells = booth_row.find_elements(By.TAG_NAME, 'td')
    booth_name = booth_cells[0].text
    booth_name = replace_booth_name(booth_name)
    if booth_name in skip_booths: continue
    booths[booth_name] = {"fp": {}, "tcp": {}}
    # cycles through td elements 2, 4, 6, etc.
    vote_cells = booth_cells[1:-5:2]
    for index, vote_cell in enumerate(vote_cells):
      booths[booth_name]["fp"][index] = int(vote_cell.text.replace(',', ''))
  return booths


def get_fps(fp_link):
  for attempt in range(5):
    try:
      return _get_fps_once(fp_link)
    except StaleElementReferenceException:
      if attempt == 4:
        raise
      time.sleep(1)


def _add_tcps_once(tcp_link, seat_name):
  driver.get(tcp_link)
  time.sleep(1)
  content = driver.find_element(By.ID, 'resultTable')
  row = content.find_element(By.TAG_NAME, 'tr')
  headings = row.find_elements(By.TAG_NAME, 'th')
  candidate_names = [heading.text.strip() for heading in headings[1:-1]]
  tbody = content.find_element(By.TAG_NAME, 'tbody')
  booth_rows = tbody.find_elements(By.TAG_NAME, 'tr')
  for booth_row in booth_rows:
    booth_cells = booth_row.find_elements(By.TAG_NAME, 'td')
    booth_name = booth_cells[0].text
    booth_name = replace_booth_name(booth_name)
    if booth_name in skip_booths: continue
    if booth_name not in all_results[seat_name]["booths"]:
      # Some declaration categories exist only in the TCP table. They cannot
      # form a complete previous-election booth record without FP votes.
      continue
    # cycles through td elements 2, 4, 6, etc.
    vote_cells = booth_cells[1:-1:2]
    for vote_cell, candidate in zip(vote_cells, candidate_names):
      candidate_index = candidate_index_for_tcp(seat_name, candidate)
      all_results[seat_name]["booths"][booth_name]["tcp"][candidate_index] = int(vote_cell.text.replace(',', ''))


def add_tcps(tcp_link, seat_name):
  for attempt in range(5):
    try:
      return _add_tcps_once(tcp_link, seat_name)
    except StaleElementReferenceException:
      if attempt == 4:
        raise
      time.sleep(1)


all_results = {}
candidate_map = {}


def main(argv=None):
  global driver, election, all_results, candidate_map
  parser = argparse.ArgumentParser(
      description='Fetch archived Queensland booth results.')
  parser.add_argument('--election', choices=sorted(urls), default=election)
  args = parser.parse_args(argv)
  election = args.election
  all_results = {}
  candidate_map = {}
  driver = create_driver()
  try:
    electorate_links, seat_names = get_electorate_links()
    for electorate_link, seat_name in zip(electorate_links, seat_names):
      candidates = get_candidate_names(electorate_link)
      candidate_map[seat_name] = candidates

    fp_links = [link + '/table/primary' for link in electorate_links]
    for fp_link, seat_name in zip(fp_links, seat_names):
      seat_info = get_fps(fp_link)
      all_results[seat_name] = {}
      all_results[seat_name]['candidates'] = candidate_map[seat_name]
      all_results[seat_name]['booths'] = seat_info

    tcp_links = [link + '/table/preference' for link in electorate_links]
    for tcp_link, seat_name in zip(tcp_links, seat_names):
      add_tcps(tcp_link, seat_name)

    with open(f'Booth Results/{election}.json', 'w') as f:
      json.dump(all_results, f, indent=4)
      f.write('\n')
  finally:
    driver.quit()
  return 0


if __name__ == '__main__':
  raise SystemExit(main())
