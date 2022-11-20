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

url = 'https://www.vec.vic.gov.au/results/state-election-results/2018-state-election'

options = Options()
options.add_argument('headless')
options.add_argument("no-sandbox")
options.add_argument('window-size=1920x1080')
options.add_argument("disable-gpu")
# This assumes running Linux/wsl2 and you have installed chromedriver
# according to instructions here:
# https://cloudbytes.dev/snippets/run-selenium-and-chrome-on-wsl2
webdriver_service = Service(f"/home/{env.str('USERNAME')}/chromedriver/stable/chromedriver")
driver = webdriver.Chrome(service=webdriver_service, options=options)

# Only for local debug display use
party_translate = {
    "ANIMAL JUSTICE PARTY": "AJP",
    "LIBERAL": "LIB",
    "FIONA PATTEN'S REASON PARTY": "REA",
    "SUSTAINABLE AUSTRALIA": "SUS",
    "AUSTRALIAN GREENS": "GRN",
    "AUSTRALIAN LABOR PARTY": "ALP",
    "LABOUR DLP": "DLP",
    "SHOOTERS, FISHERS & FARMERS VIC": "SFF",
    "VICTORIAN SOCIALISTS": "VS",
    "THE NATIONALS": "NAT",
    "DERRYN HINCH'S JUSTICE PARTY": "DHJ",
    "LIBERAL DEMOCRATS": "LDP",
    "TRANSPORT MATTERS": "TM",
    "AUSSIE BATTLER PARTY": "ABP",
    "AUSTRALIAN COUNTRY PARTY": "ACP",
    "AUSTRALIAN LIBERTY ALLIANCE": "ALA",
    "": "IND"
}

def space(s=""):
    return s.ljust(16)[:16]

def get_fp_results(link):
    
    link.click()
    time.sleep(0.5)  # Workaround for issue where elements don't click properly

    headings = driver.find_elements(By.TAG_NAME, 'h3')
    fp_heading = next(x for x in headings if 
                      "Recheck first preference votes" in x.text)
    fp_parent = fp_heading.find_element(By.XPATH, value='..')
    fp_rows = fp_parent.find_elements(By.TAG_NAME, 'tr')

    names = []
    parties = []
    fp_booths = {}

    name_row = fp_rows[0]
    name_els = name_row.find_elements(By.TAG_NAME, 'th')[1:-2]
    for name_el in name_els:
        names.append(name_el.text)

    party_row = fp_rows[1]
    party_els = party_row.find_elements(By.TAG_NAME, 'th')[1:-2]
    for party_el in party_els:
        parties.append(party_el.text)

    for booth_row in fp_rows[2:]:
        booth_name = booth_row.find_elements(By.TAG_NAME, 'th')
        if len(booth_name) != 1: continue
        booth_name = booth_name[0].text.strip()
        booth_els = booth_row.find_elements(By.TAG_NAME, 'td')[0:-2]
        if len(booth_els) < 2: continue # no actual data here
        if booth_name == "Ordinary Votes Total": continue # don't need this
        if booth_name == "Total": break # end of data that is needed
        fp_booths[booth_name] = []
        for booth_el in booth_els:
            fp_booths[booth_name].append(int(booth_el.text))
    
    name_cols = ' '.join(space(name) for name in names)
    print(f'{space()} {name_cols}')
    party_cols = ' '.join(space(party_translate[party]) for party in parties)
    print(f'{space()} {party_cols}')
    for booth_name, votes in fp_booths.items():
        vote_cols = ' '.join(space(str(vote)) for vote in votes)
        print(f'{space(booth_name)} {vote_cols}')
    
    # start to pack into results
    index_to_name = {a: b for a, b in enumerate(names)}
    name_to_index = {b: a for a, b in enumerate(names)}
    index_to_party = {a: b if len(b) > 0 else "INDEPENDENT" for a, b in enumerate(parties)}
    cand_info = {a: {"name": index_to_name[a], "party": index_to_party[a]} for a in index_to_name}
    booth_info = {name: {"fp": {n: x for n, x in enumerate(results)}} for name, results in fp_booths.items()}
    
    back_el = driver.find_element(By.TAG_NAME, 'a')
    back_el.click()

    return (name_to_index, cand_info, booth_info)


all_results = {}
for seat_index in range(0, 88):
    driver.get(url)

    iframe = driver.find_element(By.TAG_NAME, 'iframe')

    driver.switch_to.frame(iframe)

    seats_list_header = driver.find_element(By.ID, 'district')
    seats_list_div = seats_list_header.find_element(By.XPATH, value='..')
    seats_list = seats_list_div.find_elements(By.TAG_NAME, 'a')

    seat_name = seats_list[seat_index].text.split('District')[0]
    print(seat_name)
    seats_list[seat_index].click()

    # --- RETRIEVE FP RESULTS

    links = driver.find_elements(By.TAG_NAME, 'a')
    try:
        link = next(x for x in links if "Recheck results" in x.text)
        name_to_index, cand_info, booth_info = get_fp_results(link)
    except StopIteration:
        # Unfortunately the recount in Ripon means that the FP
        # recheck results have been removed ... there is still
        # an Excel file which this information is sourced from.
        cand_info = {0: {"name": "GIBBS, Sandra", "party": "DERRYN HINCH'S JUSTICE PARTY"},
                     1: {"name": "TRUSCOTT, Jeff", "party": "INDEPENDENT"},
                     2: {"name": "JENNINGS, Bronwyn", "party": "VICTORIAN SOCIALISTS"},
                     3: {"name": "HILLS, Anna", "party": "ANIMAL JUSTICE PARTY"},
                     4: {"name": "MULCAHY, Peter", "party": "LABOUR DLP"}, 
                     5: {"name": "STALEY, Louise", "party": "LIBERAL"}, 
                     6: {"name": "DE SANTIS, Sarah", "party": "AUSTRALIAN LABOR PARTY"}, 
                     7: {"name": "FAVA, Peter", "party": "SHOOTERS, FISHERS & FARMERS VIC"},
                     8: {"name": "SIMIC, Serge", "party": "AUSTRALIAN GREENS"},
                     9: {"name": "MAYER, Maria", "party": "INDEPENDENT"}}
        name_to_index = {"STALEY, Louise": 5,
                         "DE SANTIS, Sarah": 6}
        booth_info = {"Alfredton West": {"fp": {0: 24, 1: 3, 2: 3, 3: 20, 4: 12, 5: 185, 6: 310, 7: 23, 8: 34, 9: 0}},
                      "Amphitheatre": {"fp": {0: 9, 1: 1, 2: 0, 3: 4, 4: 0, 5: 77, 6: 57, 7: 10, 8: 2, 9: 0}},
                      "Ararat": {"fp": {0: 103, 1: 14, 2: 10, 3: 31, 4: 118, 5: 624, 6: 895, 7: 161, 8: 53, 9: 22}},
                      "Ararat West": {"fp": {0: 65, 1: 7, 2: 5, 3: 20, 4: 56, 5: 499, 6: 556, 7: 75, 8: 67, 9: 5}},
                      "Ascot": {"fp": {0: 5, 1: 0, 2: 0, 3: 0, 4: 1, 5: 86, 6: 25, 7: 6, 8: 7, 9: 0}},
                      "Avoca": {"fp": {0: 28, 1: 17, 2: 4, 3: 13, 4: 13, 5: 328, 6: 291, 7: 59, 8: 27, 9: 6}},
                      "Bealiba": {"fp": {0: 9, 1: 0, 2: 1, 3: 3, 4: 3, 5: 88, 6: 51, 7: 10, 8: 3, 9: 1}},
                      "Beaufort": {"fp": {0: 54, 1: 12, 2: 5, 3: 13, 4: 24, 5: 375, 6: 381, 7: 60, 8: 37, 9: 8}},
                      "Bowenvale": {"fp": {0: 20, 1: 1, 2: 2, 3: 6, 4: 6, 5: 180, 6: 202, 7: 38, 8: 10, 9: 0}},
                      "Bridgewater": {"fp": {0: 12, 1: 2, 2: 2, 3: 7, 4: 2, 5: 157, 6: 82, 7: 16, 8: 5, 9: 0}},
                      "Buangor": {"fp": {0: 4, 1: 0, 2: 0, 3: 2, 4: 6, 5: 72, 6: 39, 7: 11, 8: 0, 9: 0}},
                      "Carisbrook": {"fp": {0: 31, 1: 3, 2: 7, 3: 21, 4: 23, 5: 295, 6: 334, 7: 52, 8: 24, 9: 4}},
                      "Charlton": {"fp": {0: 21, 1: 1, 2: 1, 3: 10, 4: 16, 5: 294, 6: 159, 7: 32, 8: 18, 9: 2}},
                      "Clunes": {"fp": {0: 40, 1: 4, 2: 4, 3: 18, 4: 15, 5: 188, 6: 424, 7: 55, 8: 73, 9: 5}},
                      "Concongella": {"fp": {0: 1, 1: 0, 2: 2, 3: 3, 4: 3, 5: 57, 6: 30, 7: 9, 8: 2, 9: 0}},
                      "Creswick": {"fp": {0: 46, 1: 35, 2: 11, 3: 45, 4: 47, 5: 374, 6: 741, 7: 75, 8: 123, 9: 4}},
                      "Donald": {"fp": {0: 38, 1: 5, 2: 2, 3: 10, 4: 14, 5: 403, 6: 174, 7: 49, 8: 22, 9: 8}},
                      "Dunolly": {"fp": {0: 23, 1: 21, 2: 0, 3: 17, 4: 14, 5: 255, 6: 229, 7: 57, 8: 34, 9: 4}},
                      "Elmhurst": {"fp": {0: 6, 1: 0, 2: 1, 3: 3, 4: 8, 5: 64, 6: 54, 7: 13, 8: 3, 9: 2}},
                      "Glenorchy": {"fp": {0: 2, 1: 0, 2: 2, 3: 0, 4: 1, 5: 27, 6: 21, 7: 6, 8: 2, 9: 2}},
                      "Great Western": {"fp": {0: 10, 1: 1, 2: 1, 3: 2, 4: 2, 5: 103, 6: 74, 7: 13, 8: 2, 9: 0}},
                      "Inglewood": {"fp": {0: 25, 1: 7, 2: 4, 3: 19, 4: 16, 5: 215, 6: 222, 7: 37, 8: 13, 9: 4}},
                      "Korong Vale": {"fp": {0: 7, 1: 2, 2: 1, 3: 1, 4: 6, 5: 62, 6: 37, 7: 8, 8: 1, 9: 1}},
                      "Landsborough": {"fp": {0: 6, 1: 0, 2: 0, 3: 0, 4: 6, 5: 74, 6: 41, 7: 6, 8: 0, 9: 0}},
                      "Learmonth": {"fp": {0: 10, 1: 0, 2: 3, 3: 5, 4: 5, 5: 158, 6: 116, 7: 19, 8: 15, 9: 1}},
                      "Lexton": {"fp": {0: 9, 1: 0, 2: 1, 3: 5, 4: 4, 5: 73, 6: 53, 7: 13, 8: 3, 9: 1}},
                      "Litchfield": {"fp": {0: 5, 1: 0, 2: 0, 3: 0, 4: 0, 5: 36, 6: 5, 7: 12, 8: 2, 9: 1}},
                      "Marnoo": {"fp": {0: 5, 1: 0, 2: 0, 3: 4, 4: 4, 5: 69, 6: 17, 7: 4, 8: 1, 9: 0}},
                      "Maryborough": {"fp": {0: 85, 1: 29, 2: 14, 3: 25, 4: 54, 5: 612, 6: 891, 7: 107, 8: 62, 9: 19}},
                      "Maryborough East": {"fp": {0: 59, 1: 25, 2: 6, 3: 20, 4: 41, 5: 429, 6: 675, 7: 115, 8: 42, 9: 3}},
                      "Miners Rest": {"fp": {0: 60, 1: 4, 2: 7, 3: 35, 4: 39, 5: 384, 6: 562, 7: 72, 8: 53, 9: 1}},
                      "Moonambel": {"fp": {0: 3, 1: 0, 2: 3, 3: 1, 4: 0, 5: 67, 6: 75, 7: 6, 8: 8, 9: 1}},
                      "Newbridge": {"fp": {0: 15, 1: 3, 2: 0, 3: 12, 4: 6, 5: 80, 6: 82, 7: 20, 8: 3, 9: 0}},
                      "Newlyn": {"fp": {0: 14, 1: 0, 2: 2, 3: 4, 4: 11, 5: 163, 6: 151, 7: 22, 8: 34, 9: 2}},
                      "Smeaton": {"fp": {0: 5, 1: 1, 2: 2, 3: 4, 4: 4, 5: 90, 6: 55, 7: 8, 8: 7, 9: 1}},
                      "Snake Valley": {"fp": {0: 24, 1: 2, 2: 3, 3: 10, 4: 21, 5: 119, 6: 177, 7: 68, 8: 27, 9: 4}},
                      "St Arnaud": {"fp": {0: 47, 1: 38, 2: 1, 3: 16, 4: 38, 5: 654, 6: 365, 7: 95, 8: 35, 9: 2}},
                      "Stawell": {"fp": {0: 35, 1: 5, 2: 9, 3: 18, 4: 18, 5: 237, 6: 184, 7: 40, 8: 23, 9: 4}},
                      "Stawell West": {"fp": {0: 13, 1: 1, 2: 3, 3: 5, 4: 11, 5: 133, 6: 144, 7: 26, 8: 24, 9: 0}},
                      "Stuart Mill": {"fp": {0: 7, 1: 1, 2: 1, 3: 0, 4: 0, 5: 54, 6: 20, 7: 6, 8: 2, 9: 0}},
                      "Talbot": {"fp": {0: 33, 1: 3, 2: 5, 3: 6, 4: 14, 5: 189, 6: 285, 7: 34, 8: 46, 9: 2}},
                      "Tarnagulla": {"fp": {0: 6, 1: 1, 2: 0, 3: 2, 4: 6, 5: 46, 6: 75, 7: 17, 8: 3, 9: 1}},
                      "Waubra": {"fp": {0: 7, 1: 4, 2: 2, 3: 4, 4: 4, 5: 88, 6: 54, 7: 10, 8: 3, 9: 2}},
                      "Wedderburn": {"fp": {0: 32, 1: 27, 2: 4, 3: 5, 4: 8, 5: 206, 6: 181, 7: 31, 8: 13, 9: 8}},
                      "Absent Votes": {"fp": {0: 151, 1: 19, 2: 27, 3: 96, 4: 145, 5: 886, 6: 820, 7: 233, 8: 177, 9: 16}},
                      "Early Votes": {"fp": {0: 433, 1: 43, 2: 43, 3: 194, 4: 356, 5: 3190, 6: 3132, 7: 521, 8: 363, 9: 46}},
                      "Marked As Voted Votes": {"fp": {0: 1, 1: 0, 2: 0, 3: 1, 4: 0, 5: 1, 6: 0, 7: 0, 8: 0, 9: 0}},
                      "Postal Votes": {"fp": {0: 267, 1: 30, 2: 12, 3: 83, 4: 98, 5: 2476, 6: 1643, 7: 231, 8: 138, 9: 23}},
                      "Provisional Votes": {"fp": {0: 18, 1: 2, 2: 4, 3: 16, 4: 17, 5: 75, 6: 88, 7: 30, 8: 22, 9: 2}}}


    # --- RETRIEVE TCP RESULTS

    names = []
    parties = []
    tcp_booths = {}

    links = driver.find_elements(By.TAG_NAME, 'a')
    link = next(x for x in links if "Two candidate preferred" in x.text)
    link.click()

    headings = driver.find_elements(By.TAG_NAME, 'h3')
    tcp_heading = next(x for x in headings if 
                       "Two candidate preferred votes" in x.text)
    tcp_parent = tcp_heading.find_element(By.XPATH, value='..')
    tcp_rows = tcp_parent.find_elements(By.TAG_NAME, 'tr')
    
    name_row = tcp_rows[0]
    name_els = name_row.find_elements(By.TAG_NAME, 'th')[:2]
    for name_el in name_els:
        names.append(name_el.text)

    party_row = tcp_rows[1]
    party_els = party_row.find_elements(By.TAG_NAME, 'th')[1:3]
    for party_el in party_els:
        parties.append(party_el.text)

    for booth_row in tcp_rows[2:]:
        booth_name = booth_row.find_elements(By.TAG_NAME, 'th')
        if len(booth_name) != 1: continue
        booth_name = booth_name[0].text.strip()
        booth_els = booth_row.find_elements(By.TAG_NAME, 'td')[:2]
        if len(booth_els) < 2: continue # no actual data here
        if booth_name == "All Votes Votes": continue # causes errors if not skipped
        if booth_name == "Ordinary Votes Total": continue # don't need this
        if booth_name == "Total": break # end of data that is needed
        tcp_booths[booth_name] = []
        for booth_el in booth_els:
            tcp_booths[booth_name].append(int(booth_el.text))
    
    name_cols = ' '.join(space(name) for name in names)
    print(f'{space()} {name_cols}')
    party_cols = ' '.join(space(party_translate[party]) for party in parties)
    print(f'{space()} {party_cols}')

    if len(name_to_index) == 0: 
        name_to_index = {b: a for a, b in enumerate(names)}

    for booth_name, votes in tcp_booths.items():
        vote_cols = ' '.join(space(str(vote)) for vote in votes)
        print(f'{space(booth_name)} {vote_cols}')
        booth_info[booth_name]['tcp'] = {name_to_index[names[index]]: results for index, results in enumerate(votes)}

    all_results[seat_name] = {"candidates": cand_info, "booths": booth_info}

driver.close()

with open('Booth Results/2018vic.json', 'w') as f:
    json.dump(all_results, f, indent=4)