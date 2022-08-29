import pickle
import requests
from bs4 import BeautifulSoup

filename = f'Federal-State/victoria.pkl'
vic_results = None
greens_swings = {}
tpp_swings = {}
vote_totals = {}
try:
    with open(filename, 'rb') as pkl:
        vic_results = pickle.load(pkl)
    greens_swings, tpp_swings, vote_totals = vic_results
except FileNotFoundError:
    pass

overall_greens_swing = 1.85
overall_tpp_swing = 1.69

if vic_results is None:
    URL = 'https://results.aec.gov.au/27966/Website/HouseDivisionalResults-27966.htm'
    page = requests.get(URL)

    soup = BeautifulSoup(page.content, 'html.parser')

    seat_els = soup.find_all('td', class_='filterDivision')

    current_state = 'VIC'

    # Ignore Greens total in Melbourne due to disendorsement of previous member
    ignore_greens_seats = {'Goldstein', 'Kooyong', 'Melbourne'}
    # Melbourne and Isaacs are ignored due to previous disendorsement
    assume_tpp_seats = {'Melbourne': overall_tpp_swing,
                        'Wills': 0.06,
                        'Cooper': -0.75,
                        'Goldstein': 2.99,
                        'Indi': 7.47,
                        'Isaacs': overall_tpp_swing,
                        'Kooyong': 2.21,
                        'Nicholls': 2.88,
                        'Wannon': 1.13}

    for seat_el in seat_els:
        state = seat_el.next_sibling.next_sibling.text
        if state != current_state: continue
        seat_link = seat_el.find('a')
        seat_name = seat_el.text
        seat_path = seat_link['href']
        seat_URL = f'https://results.aec.gov.au/27966/Website/{seat_path}'
        seat_page = requests.get(seat_URL)
        seat_soup = BeautifulSoup(seat_page.content, 'html.parser')
        booth_els = seat_soup.find_all('td', headers='ppPp')
        for booth_el in booth_els:
            booth_name = booth_el.text
            booth_link = booth_el.find('a')
            booth_path = booth_link['href']
            booth_URL = f'https://results.aec.gov.au/27966/Website/{booth_path}'
            booth_page = requests.get(booth_URL)
            booth_soup = BeautifulSoup(booth_page.content, 'html.parser')
            fp_greens_el = booth_soup.find('td', headers='fpPty', string="The Greens")
            fp_greens_pct = float(fp_greens_el.find_next_sibling('td', headers='fpPct').text)
            fp_greens_swing = float(fp_greens_el.find_next_sibling('td', headers='fpSwg').text)
            if abs(fp_greens_swing - fp_greens_pct) < 0.02: continue
            formal_el = booth_soup.find('td', headers='fpCan', string="Formal")
            fp_formal_text = formal_el.find_next_sibling('td', headers='fpVot').text
            fp_formal_int = int(fp_formal_text.replace(',', ''))
            if seat_name in ignore_greens_seats: fp_greens_swing = overall_greens_swing
            if seat_name in assume_tpp_seats:
                tpp_alp_swing = assume_tpp_seats[seat_name]
            else:
                tpp_alp_el = booth_soup.find('td', headers='tcpPty', text="Australian Labor Party")
                tpp_alp_swing = float(tpp_alp_el.find_next_sibling('td', headers='tcpSwg').text)
            booth_key = (seat_name, booth_name)
            greens_swings[booth_key] = fp_greens_swing
            tpp_swings[booth_key] = tpp_alp_swing
            vote_totals[booth_key] = fp_formal_int
        print(f'Downloaded booths for seat: {seat_name}')
    full_results = (greens_swings, tpp_swings, vote_totals)
    with open(filename, 'wb') as pkl:
        pickle.dump(full_results, pkl, pickle.HIGHEST_PROTOCOL)


seat_booths = {}
with open('Federal-State/booths-vic.txt') as f:
    lines = f.readlines()
    current_seat = None
    for line in lines:
        if line[0] == '#':
            current_seat = line[1:].strip()
            seat_booths[current_seat] = set()
        else:
            booth = tuple(line.strip().split(','))
            seat_booths[current_seat].add(booth)


weighted_greens_swings = {}
weighted_tpp_swings = {}
total_weights = {}
for seat, booth_keys in seat_booths.items():
    for booth_key in booth_keys:
        if booth_key not in vote_totals:
            print(f'Warning: booth {booth_key[1]} in seat {booth_key[0]} not found!')
            continue
        greens_swing = greens_swings[booth_key]
        tpp_swing = tpp_swings[booth_key]
        vote_total = vote_totals[booth_key]
        if seat not in weighted_greens_swings:
            weighted_greens_swings[seat] = 0
            weighted_tpp_swings[seat] = 0
            total_weights[seat] = 0
        weighted_greens_swings[seat] += greens_swing * vote_total
        weighted_tpp_swings[seat] += tpp_swing * vote_total
        total_weights[seat] += vote_total

tpp_list = []
grn_list = []
for seat, weighted_greens_swing in weighted_greens_swings.items():
    total_weight = total_weights[seat]
    weighted_tpp_swing = weighted_tpp_swings[seat]
    greens_swing = weighted_greens_swing / total_weight
    tpp_swing = weighted_tpp_swing / total_weight
    tpp_deviation = tpp_swing - overall_tpp_swing
    greens_deviation = greens_swing - overall_greens_swing
    print(f'{seat} federal greens deviation: {greens_deviation}')
    print(f'{seat} federal tpp deviation: {tpp_deviation}')
    tpp_list.append((seat, tpp_deviation))
    grn_list.append((seat, greens_deviation))

for seat_name, tpp_dev in sorted(tpp_list, key=lambda x: x[1]):
    print(f'{seat_name} federal tpp deviation: {tpp_dev}')

for seat_name, grn_dev in sorted(grn_list, key=lambda x: x[1]):
    print(f'{seat_name} federal GRN deviation: {grn_dev}')

