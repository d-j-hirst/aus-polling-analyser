import requests
import time
import json
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

url = 'https://pastvtr.elections.nsw.gov.au/SGE2015/la-home.htm'

options = Options()
options.add_argument('headless')
options.add_argument("no-sandbox")
options.add_argument('window-size=1920x1080')
options.add_argument("disable-gpu")
# This assumes running Linux/wsl2 and you have installed chromedriver
# according to instructions here:
# https://cloudbytes.dev/snippets/run-selenium-and-chrome-on-wsl2
service_path = f"/home/{env.str('USERNAME')}/chromedriver/stable/chromedriver"
webdriver_service = Service(service_path)
driver = webdriver.Chrome(service=webdriver_service, options=options)

skip_booths = [
    'Polling Places',
    'Total Polling Place Ordinary Votes',
    'Pre-Poll Venues',
    'Total Pre-Poll Ordinary Votes',
    'Declaration Votes',
    'Total Declaration Votes',
    'Total Votes / Ballot Papers',
    '% of Formal Votes / Ballot Papers',
    '% of Total Votes / Ballot Papers',
    'Total Votes',
    '% of Candidate Votes'
]

tcps_2015 = {
    'Ballina': ('NP', 'GRN'),
    'Balmain': ('LAB', 'GRN'),
    'Davidson': ('LIB', 'GRN'),
    'Lake Macquarie': ('LAB', 'PIPER Greg'),
    'Lismore': ('NP', 'GRN'),
    'Manly': ('LIB', 'GRN'),
    'Murray': ('NP', 'DALTON Helen'),
    'Newtown': ('LAB', 'GRN'),
    'North Shore': ('LIB', 'GRN'),
    'Pittwater': ('LIB', 'GRN'),
    'Summer Hill': ('LAB', 'GRN'),
    'Sydney': ('LIB', 'GREENWICH Alex'),
    'Tamworth': ('NP', 'DRAPER Peter'),
    'Vaucluse': ('LIB', 'GRN'),
    'Willoughby': ('LIB', 'GRN'),
    'Wollongong': ('LAB', 'RORRIS Arthur'),
}


all_results = {}

def get_fp_list():
    driver.get(url)
    links = driver.find_elements(By.LINK_TEXT, 'LA FP')
    return [link.get_attribute('href') for link in links]

def get_fps(link):
    driver.get(link)
    prcc = driver.find_element(By.ID, 'prcc-report')
    heading = prcc.find_element(By.TAG_NAME, 'h2')
    seat_name = heading.text.split(' of ')[1]
    print(f'Loading FP results for {seat_name}')
    votes_table = prcc.find_element(By.CLASS_NAME, 'cc-votes')
    votes_rows = votes_table.find_elements(By.TAG_NAME, 'tr')
    candidates = []
    booths = {}
    for cell in votes_rows[0].find_elements(By.TAG_NAME, 'th')[1:]:
        if 'Formal' in cell.text: break
        if '(' not in cell.text:
            name, party = cell.text, 'IND'
        else:
            name, party = cell.text.split(' (')
            party = party.split(')')[0]
        candidates.append((name, party))
    index_to_candidate = {a: b for a, b in enumerate(candidates)}
    candidate_to_index = {b: a for a, b in enumerate(candidates)}
    for vote_row in votes_rows[1:]:
        cells = vote_row.find_elements(By.TAG_NAME, 'td')
        if not len(cells): continue
        booth_name = cells[0].text
        if booth_name in skip_booths: continue
        booths[booth_name] = {"fp": {}, "tcp": {}}
        for cell, candidate in zip(cells[1:len(candidates) + 1], candidates):
            votes = int(cell.text.replace(',',''))
            booths[booth_name]["fp"][candidate_to_index[candidate]] = votes
    cand_info = {a: {
        "name": index_to_candidate[a][0],
        "party": index_to_candidate[a][1]
    } for a in index_to_candidate}
    seat_info = {"candidates": cand_info, "booths": booths}
    return (seat_name, seat_info)
    
all_results = {}
fp_links = get_fp_list()
for fp_link in fp_links:
    seat_name, seat_info = get_fps(fp_link)
    all_results[seat_name] = seat_info

def get_tcp_list():
    driver.get(url)
    links = driver.find_elements(By.LINK_TEXT, 'LA TCP')
    return [link.get_attribute('href') for link in links]

def add_tcps(tcp_link):
    driver.get(tcp_link)
    district_title = driver.find_element(By.ID, 'election-title')
    name_element = district_title.find_element(By.TAG_NAME, 'h3')
    seat_name = name_element.text.split(' of ')[1]
    print(seat_name)
    selector = driver.find_element(By.TAG_NAME, 'select')
    options = selector.find_elements(By.TAG_NAME, 'option')
    booths = {}
    for option in options:
        text = option.text
        types = []
        if (
            ("(LAB)" in text or "(CLP)" in text) and
            ("(LIB)" in text or "(NP)" in text)
        ):
            types.append('tpp')
        if seat_name in tcps_2015 and (
            (tcps_2015[seat_name][0] in text) and
            (tcps_2015[seat_name][1] in text)
        ):
            types.append('tcp')
        elif seat_name not in tcps_2015 and (
            ("(LAB)" in text or "(CLP)" in text) and
            ("(LIB)" in text or "(NP)" in text)
        ):
            types.append('tcp')
        if len(types) == 0: continue
        option.click()
        driver.find_element(By.ID, 'view-tcp').click()
        time.sleep(1)  # Make sure element has had time to load
        vote_table = driver.find_element(By.ID, 'vote-table')
        headers = vote_table.find_elements(By.TAG_NAME, 'th')[1:3]
        candidates = [header.text for header in headers]
        rows = vote_table.find_elements(By.TAG_NAME, 'tr')[1:]
        for row in rows:
            cells = row.find_elements(By.TAG_NAME, 'td')
            texts = [cell.text.replace(',', '') for cell in cells]
            # The replacement is for "Provisional/Silent"
            # which is formatted differently for tcp vs. fp
            name = texts[0].replace('Provisional/Silent','Provisional / Silent')
            if name in skip_booths: continue
            if name not in booths: booths[name] = {'tcp': {}, 'tpp': {}}
            print(all_results[seat_name]['candidates'])
            print(candidates)
            for type in types:
                candidate_nums = [
                    next(
                        num for num, a in all_results[seat_name]['candidates'].items()
                        if a["name"].upper() in candidate.upper() and a["party"] in candidate
                    )
                    for candidate in candidates
                ]
                booths[name][type][candidate_nums[0]] = int(texts[1])
                booths[name][type][candidate_nums[1]] = int(texts[2])
    print(booths)
    for booth_name, booth in booths.items():
        all_results[seat_name]['booths'][booth_name]['tcp'] = booth['tcp']
        all_results[seat_name]['booths'][booth_name]['tpp'] = booth['tpp']
       

tcp_links = get_tcp_list()
for tcp_link in tcp_links:
    add_tcps(tcp_link)


with open('Booth Results/2015nsw.json', 'w') as f:
    json.dump(all_results, f, indent=4)