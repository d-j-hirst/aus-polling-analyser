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

election = '2015nsw'

urls = {
    '2015nsw': 'https://pastvtr.elections.nsw.gov.au/SGE2015/la-home.htm',
    '2019nsw': 'https://pastvtr.elections.nsw.gov.au/sg1901/la/results',
}

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
    'Voting Centres',
    'Total Polling Place Ordinary Votes',
    'Total Voting Centre Ordinary Votes',
    'Pre-Poll Venues',
    'Early Voting Centres',
    'Total Pre-Poll Ordinary Votes',
    'Total Early Voting Centre Ordinary Votes',
    'Declaration Votes',
    'Total Declaration Votes',
    'Total Votes / Ballot Papers',
    '% of Formal Votes / Ballot Papers',
    '% of Total Votes / Ballot Papers',
    'Total Votes',
    '% of Candidate Votes',
]

append_booths = [
    'Sydney Town Hall Pre-Poll',
    'Declared Facility',
]

append_votes = [
    'Absent',
    'Postal',
    'Provisional',
    'iVote',
]

tcps = {
    '2015nsw': {
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
    },
    '2019nsw': {
        'Ballina': ('NAT', 'GRN'),
        'Balmain': ('LAB', 'GRN'),
        'Barwon': ('NAT', 'SFF'),
        'Cabramatta': ('LAB', 'LE Dai'),
        'Coffs Harbour': ('NAT', 'TOWNLEY Sally'),
        'Davidson': ('LIB', 'GRN'),
        'Dubbo': ('NAT', 'DICKERSON Mathew'),
        'Lake Macquarie': ('LAB', 'PIPER Greg'),
        'Lismore': ('NP', 'GRN'),
        'Manly': ('LIB', 'GRN'),
        'Murray': ('NAT', 'SFF'),
        'Newtown': ('LAB', 'GRN'),
        'North Shore': ('LIB', 'CORRIGAN Carolyn'),
        'Orange': ('NAT', 'SFF'),
        'Pittwater': ('LIB', 'GRN'),
        'Sydney': ('LIB', 'GREENWICH Alex'),
        'Tamworth': ('NAT', 'RODDA Mark'),
        'Vaucluse': ('LIB', 'GRN'),
        'Wagga Wagga': ('NAT', 'McGIRR Joe'),
        'Wollondilly': ('LAB', 'HANNAN Judy'),
    }
}

prcc_report_name = {
    '2015nsw': 'prcc-report',
    '2019nsw': 'prccReport',
}

def process_booth_name(booth_name):
    if booth_name == 'Enrolment': return 'Provisional Votes'
    booth_name = booth_name.replace(
        'Provisional/Silent',
        'Provisional'
    ).replace(
        'Provisional / Silent',
        'Provisional'
    ).replace(
        'Enrolment/Provisional',
        'Provisional'
    ).replace(
        'Enrolment / Provisional',
        'Provisional'
    ).replace(
        'RO Pre-Poll',
        'EM Office'
    ).replace(
        'Pre-Poll',
        'EVC'
    ).replace(
        'Sydney Town Hall EVC',
        'Sydney Town Hall Pre-Poll'
    ).replace(
        'Declared Institution',
        'Declared Facility'
    )
    if booth_name in append_votes:
        booth_name = f'{booth_name} Votes'
    return booth_name

def get_dop_list():
    driver.get(urls[election])
    links = driver.find_elements(By.LINK_TEXT, 'LA DoP')
    return [link.get_attribute('href') for link in links]

def get_candidate_names(link):
    driver.get(link)
    content = driver.find_element(By.ID, prcc_report_name[election])
    heading = content.find_element(By.TAG_NAME, 'h2')
    seat_name = heading.text.split(' of ')[1].strip()
    print(f'Loading full candidate names for {seat_name}')
    rows = content.find_elements(By.TAG_NAME, 'tr')
    names = []
    for row in rows[1:]:
        cell = row.find_element(By.TAG_NAME, 'td')
        candidate_name = cell.text.strip()
        if 'Candidates' in candidate_name or len(candidate_name) == 0: continue
        if 'Total' in candidate_name: break
        names.append((candidate_name[:-3].strip(), candidate_name[-3:]))
    return seat_name, names

def extract_full_name(seat_name, name, party):
    return next((
        full_name for full_name, full_party
        in candidate_map[seat_name]
        if party == full_party and name in full_name
    ), name)

def get_fp_list():
    driver.get(urls[election])
    links = driver.find_elements(By.LINK_TEXT, 'LA FP')
    return [link.get_attribute('href') for link in links]

def get_fps(link):
    driver.get(link)
    content = driver.find_element(By.ID, prcc_report_name[election])
    heading = content.find_element(By.TAG_NAME, 'h2')
    seat_name = heading.text.split(' of ')[1].strip()
    print(f'Loading FP results for {seat_name}')
    if election == '2015nsw':
        votes_table = content.find_element(By.CLASS_NAME, 'cc-votes')
    elif election == '2019nsw':
        votes_table = content.find_elements(By.TAG_NAME, 'table')[1]
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
        actual_name = extract_full_name(seat_name, name, party)
        candidates.append((actual_name, party))
    index_to_candidate = {a: b for a, b in enumerate(candidates)}
    candidate_to_index = {b: a for a, b in enumerate(candidates)}
    for vote_row in votes_rows[1:]:
        cells = vote_row.find_elements(By.TAG_NAME, 'td')
        if not len(cells): continue
        booth_name = cells[0].text
        if booth_name in skip_booths: continue
        booth_name = process_booth_name(booth_name)
        if booth_name in append_booths: booth_name += f' ({seat_name})'
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

def get_tcp_list():
    driver.get(urls[election])
    links = driver.find_elements(By.LINK_TEXT, 'LA TCP')
    return [link.get_attribute('href') for link in links]

def add_tcps(tcp_link):
    driver.get(tcp_link)
    if election == '2015nsw':
        upper_section = driver.find_element(By.ID, 'election-title')
    elif election == '2019nsw':
        upper_section = driver.find_element(By.ID, 'tcpCandidates')
    name_element = upper_section.find_element(By.TAG_NAME, 'h3')
    seat_name = name_element.text.split(' of ')[1]
    print(f'Loading TCP/TPP results for {seat_name}')
    selector = driver.find_element(By.TAG_NAME, 'select')
    options = selector.find_elements(By.TAG_NAME, 'option')
    booths = {}
    for option in options:
        text = option.text
        types = []
        if (
            ("(LAB)" in text or "(CLP)" in text) and
            ("(LIB)" in text or "(NP)" in text or "(NAT)" in text)
        ):
            types.append('tpp')
        if seat_name in tcps[election] and (
            (tcps[election][seat_name][0] in text) and
            (tcps[election][seat_name][1] in text)
        ):
            types.append('tcp')
        elif seat_name not in tcps[election] and (
            ("(LAB)" in text or "(CLP)" in text) and
            ("(LIB)" in text or "(NP)" in text or "(NAT)" in text)
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
            if texts[0] in skip_booths: continue
            # The replacement is for "Provisional/Silent"
            # which is formatted differently for tcp vs. fp
            name = process_booth_name(texts[0])
            if name in append_booths: name += f' ({seat_name})'
            if name not in booths: booths[name] = {'tcp': {}, 'tpp': {}}
            for type in types:
                candidate_nums = [
                    # "upper" required because NSWEC is inconsistent
                    # for names like "McDONALD"
                    next(
                        num for num, a
                        in all_results[seat_name]['candidates'].items()
                        if candidate.split('(')[0].strip().upper()
                        in a["name"].upper()
                        and a["party"] in candidate
                    )
                    for candidate in candidates
                ]
                if candidate_nums[0] not in booths[name][type]:
                    booths[name][type][candidate_nums[0]] = 0
                if candidate_nums[1] not in booths[name][type]:
                    booths[name][type][candidate_nums[1]] = 0
                booths[name][type][candidate_nums[0]] += int(texts[1])
                booths[name][type][candidate_nums[1]] += int(texts[2])
    for booth_name, booth in booths.items():
        all_results[seat_name]['booths'][booth_name]['tcp'] = booth['tcp']
        all_results[seat_name]['booths'][booth_name]['tpp'] = booth['tpp']

all_results = {}
candidate_map = {}

dop_links = get_dop_list()
for dop_link in dop_links:
    seat_name, candidates = get_candidate_names(dop_link)
    candidate_map[seat_name] = candidates

fp_links = get_fp_list()
for fp_link in fp_links:
    seat_name, seat_info = get_fps(fp_link)
    all_results[seat_name] = seat_info
       
tcp_links = get_tcp_list()
for tcp_link in tcp_links:
    add_tcps(tcp_link)

with open(f'Booth Results/{election}.json', 'w') as f:
    json.dump(all_results, f, indent=4)
