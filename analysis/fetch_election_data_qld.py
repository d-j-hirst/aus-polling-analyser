import requests
import time
import json
import os.path
import datetime
import environ
from selenium import webdriver
from selenium.webdriver.chrome.service import Service
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.common.by import By
from selenium.webdriver.common.keys import Keys
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions
from selenium.common.exceptions import NoSuchElementException

import environ

env = environ.Env(
  DEBUG=(int, 0)
)

# reading .env file
environ.Env.read_env('fetch_election_data.env')

election = '2020qld'

urls = {
  '2020qld': 'https://results.elections.qld.gov.au/state2020',
}

chrome_options = Options()
chrome_options.add_argument('--headless=new')
chrome_options.add_argument("--no-sandbox")
# This assumes running Linux/wsl2 and you have installed chromedriver
# according to instructions here:
# https://cloudbytes.dev/snippets/run-selenium-and-chrome-on-wsl2
homedir = os.path.expanduser("~")
chrome_options.binary_location = f"{homedir}/chrome-linux64/chrome"
webdriver_service = Service(f"{homedir}/chromedriver-linux64/chromedriver")
driver = webdriver.Chrome(service=webdriver_service, options=chrome_options)

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

def get_electorate_links():
  driver.get(urls[election])
  time.sleep(2)
  electorate_table = driver.find_element(By.ID, 'electorateList')
  links = electorate_table.find_elements(By.TAG_NAME, 'a')
  seat_names = [link.text for link in links]
  hrefs = [link.get_attribute('href') for link in links]
  return hrefs, seat_names

def get_candidate_names(link):
  driver.get(link)
  time.sleep(2)
  candidate_elements = driver.find_elements(By.CLASS_NAME, 'candidateName')
  names = [element.text.replace("Declared: ", "").strip() for element in candidate_elements]
  party_elements = driver.find_elements(By.CLASS_NAME, 'candidateParty')
  parties = [element.text.strip() for element in party_elements]
  parties = ["Independent" if party == "" else party for party in parties]
  return {num: {'name': name, 'party': party} for num, (name, party) in enumerate(zip(names, parties))}

def extract_full_name(seat_name, name, party):
  return next((
    full_name for full_name, full_party
    in candidate_map[seat_name]
    if party == full_party and name in full_name
  ), name)

def get_fps(fp_link):
  driver.get(fp_link)
  time.sleep(2)
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

def add_tcps(tcp_link, seat_name):
  driver.get(tcp_link)
  time.sleep(2)
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
    # cycles through td elements 2, 4, 6, etc.
    vote_cells = booth_cells[1:-1:2]
    for vote_cell, candidate in zip(vote_cells, candidate_names):
      candidate_index = next(i for i, (num, val) in enumerate(candidate_map[seat_name].items()) if val['name'] == candidate)
      all_results[seat_name]["booths"][booth_name]["tcp"][candidate_index] = int(vote_cell.text.replace(',', ''))

all_results = {}
candidate_map = {}

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
