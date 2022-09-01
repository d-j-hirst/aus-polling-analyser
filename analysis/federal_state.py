import pickle
import argparse
import requests
from bs4 import BeautifulSoup


class ConfigError(ValueError):
    pass


overall_greens_swings = {'2022vic': 1.85}
overall_tpp_swings = {'2022vic': 1.69}
base_url = 'https://results.aec.gov.au'
aec_election_code = {
    '2022vic': 27966
}
fed_results_urls = {
    '2022vic': (f'{base_url}/{aec_election_code["2022vic"]}/Website/'
                f'HouseDivisionalResults-{aec_election_code["2022vic"]}.htm')
}
ignore_greens_seats_election = {
    # Ignore Greens total in Melbourne due to disendorsement of previous member
    # and in Goldstein/Kooyong due to 
    '2022vic': {'Goldstein', 'Kooyong', 'Melbourne'}
}
assume_tpp_seats_election = {
    # Melbourne and Isaacs are set to state average as the previous
    # election's disendorsement distorts the result.
    '2022vic': {'Cooper': -0.75,
                'Goldstein': 2.99,
                'Indi': 7.47,
                'Isaacs': overall_tpp_swings['2022vic'],
                'Kooyong': 2.21,
                'Melbourne': overall_tpp_swings['2022vic'],
                'Nicholls': 2.88,
                'Wannon': 1.13,
                'Wills': 0.06}
}


class Config:
    def __init__(self):
        parser = argparse.ArgumentParser(
            description='Determine trend adjustment parameters')
        parser.add_argument('--election', action='store', type=str,
                            help='Generate federal comparisons for this state '
                            'election. Enter as 1234-xxx format,'
                            ' e.g. 2013-fed.')
        self.election = parser.parse_args().election.lower().replace('-', '')


class Results:
    def __init__(self):
        self.greens_swings = {}
        self.tpp_swings = {}
        self.vote_totals = {}


def election_filename(election):
    return f'Federal-State/{election}.pkl'


def fetch_results(election):
    URL = fed_results_urls[election]
    aec_code = aec_election_code[election]
    page = requests.get(URL)

    soup = BeautifulSoup(page.content, 'html.parser')

    seat_els = soup.find_all('td', class_='filterDivision')

    current_state = election[4:].upper()

    overall_greens_swing = overall_greens_swings[election]
    ignore_greens_seats = ignore_greens_seats_election[election]
    assume_tpp_seats = assume_tpp_seats_election[election]

    results = Results()

    for seat_el in seat_els:
        state = seat_el.next_sibling.next_sibling.text
        if state != current_state: continue
        seat_link = seat_el.find('a')
        seat_name = seat_el.text
        seat_path = seat_link['href']
        seat_URL = f'{base_url}/{aec_code}/Website/{seat_path}'
        seat_page = requests.get(seat_URL)
        seat_soup = BeautifulSoup(seat_page.content, 'html.parser')
        booth_els = seat_soup.find_all('td', headers='ppPp')
        for booth_el in booth_els:
            booth_name = booth_el.text
            if (seat_name == 'Mallee'):
                print(booth_name)
            booth_link = booth_el.find('a')
            booth_path = booth_link['href']
            booth_URL = f'{base_url}/{aec_code}/Website/{booth_path}'
            booth_page = requests.get(booth_URL)
            booth_soup = BeautifulSoup(booth_page.content, 'html.parser')
            fp_greens_el = booth_soup.find('td', headers='fpPty',
                                            string="The Greens")
            fp_greens_swing = float(fp_greens_el.find_next_sibling(
                                    'td', headers='fpSwg').text)
            formal_el = booth_soup.find('td', headers='fpCan',
                                        string="Formal")
            fp_formal_text = formal_el.find_next_sibling(
                                        'td', headers='fpVot').text
            fp_formal_int = int(fp_formal_text.replace(',', ''))
            if seat_name in ignore_greens_seats:
                fp_greens_swing = overall_greens_swing
            if seat_name in assume_tpp_seats:
                tpp_alp_swing = assume_tpp_seats[seat_name]
            else:
                tpp_alp_el = booth_soup.find('td',
                                            headers='tcpPty',
                                             text="Australian Labor Party")
                tpp_alp_pct = float(tpp_alp_el.find_next_sibling(
                                        'td', headers='tcpPct').text)
                tpp_alp_swing = float(tpp_alp_el.find_next_sibling(
                                        'td', headers='tcpSwg').text)
            
            if abs(tpp_alp_swing - tpp_alp_pct) < 0.02: continue
            booth_key = (seat_name, booth_name)
            results.greens_swings[booth_key] = fp_greens_swing
            results.tpp_swings[booth_key] = tpp_alp_swing
            results.vote_totals[booth_key] = fp_formal_int
        print(f'Downloaded booths for seat: {seat_name}')
    filename = election_filename(election)
    with open(filename, 'wb') as pkl:
        pickle.dump(results, pkl, pickle.HIGHEST_PROTOCOL)
    return results


def obtain_results(election):
    filename = election_filename(election)
    greens_swings = {}
    tpp_swings = {}
    vote_totals = {}
    results = None
    try:
        with open(filename, 'rb') as pkl:
            results = pickle.load(pkl)
    except FileNotFoundError:
        pass

    if results is None:
        results = fetch_results(election)
    return results


def parse_booth_file(election):
    seat_booths = {}
    with open(f'Federal-State/booths-{election}.txt') as f:
        lines = f.readlines()
        current_seat = None
        for line in lines:
            if line[0] == '#':
                current_seat = line[1:].strip()
                seat_booths[current_seat] = set()
            else:
                booth = tuple(line.strip().split(','))
                seat_booths[current_seat].add(booth)
    return seat_booths


def add_weighted_swings(seat_booths, results, election):
    weighted_greens_swings = {}
    weighted_tpp_swings = {}
    total_weights = {}
    for seat, booth_keys in seat_booths.items():
        for booth_key in booth_keys:
            if booth_key not in results.vote_totals:
                print(f'Warning: booth {booth_key[1]} '
                      f'in seat {booth_key[0]} not found!')
                continue
            greens_swing = results.greens_swings[booth_key]
            tpp_swing = results.tpp_swings[booth_key]
            vote_total = results.vote_totals[booth_key]
            if seat not in weighted_greens_swings:
                weighted_greens_swings[seat] = 0
                weighted_tpp_swings[seat] = 0
                total_weights[seat] = 0
            weighted_greens_swings[seat] += greens_swing * vote_total
            weighted_tpp_swings[seat] += tpp_swing * vote_total
            total_weights[seat] += vote_total
    return (weighted_greens_swings, weighted_tpp_swings, total_weights)


def calculate_deviations(seat_booths, results, election):

    weighted_greens_swings, weighted_tpp_swings, total_weights = \
        add_weighted_swings(seat_booths, results, election)

    overall_greens_swing = overall_greens_swings[election]
    overall_tpp_swing = overall_tpp_swings[election]

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

def analyse_specific_election(election):

    results = obtain_results(election)

    seat_booths = parse_booth_file(election)

    calculate_deviations(seat_booths, results, election)


def analyse():
    try:
        config = Config()
    except ConfigError as e:
        print('Could not process configuration due to the following issue:')
        print(str(e))
        return
    analyse_specific_election(config.election)


if __name__ == '__main__':
    analyse()