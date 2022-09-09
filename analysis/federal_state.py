import pickle
import argparse
import requests
from bs4 import BeautifulSoup
from sklearn.linear_model import LinearRegression, QuantileRegressor
import numpy
import statsmodels.api as sm

class ConfigError(ValueError):
    pass


warnings = ''


overall_greens_swings = {'2022vic': 1.85,
                         '2022sa': 3.16,
                         '2019nsw': -0.24}
overall_tpp_swings = {'2022vic': 1.69,
                      '2022sa': 3.26,
                      '2019nsw': -1.25}
base_url = 'https://results.aec.gov.au'
aec_election_code = {
    '2022vic': 27966,
    '2022sa': 27966,
    '2019nsw': 24310
}

alp_name = {
    '2022vic': 'Australian Labor Party',
    '2022sa': 'Australian Labor Party',
    '2019nsw': 'Labor',
}


tpp_swings = {
    '2022sa': {'Adelaide': 7.1, 'Badcoe': 10.1, 'Black': 6.5, 'Bragg': 8.8,
               'Chaffey': 0.8, 'Cheltenham': 2.4, 'Colton': 1.4,
               'Croydon': 1.5, 'Davenport': 11.6, 'Dunstan': 6.9, 'Elder': 7.5,
               'Elizabeth': 3.3, 'Enfield': 8.3, 'Finniss': 12.1,
               'Flinders': 5.7, 'Florey': -0.6, 'Frome': 10, 'Gibson': 12.5,
               'Giles': 6.1, 'Hammond': 11.7, 'Hartley': 3, 'Heysen': 5.7,
               'Hurtle Vale': 7.2, 'Kaurna': 4.1, 'Kavel': 9.8, 'King': 3.5,
               'Lee': 5.9, 'Light': 11.1, 'Mackillop': 2.6, 'Mawson': 13.1,
               'Morialta': 8, 'Morphett': 6.4, 'Mount Gambier': 4.4,
               'Narungga': 4.1, 'Newland': 5.4, 'Playford': -2.7,
               'Port Adelaide': 5, 'Ramsay': 1.4, 'Reynell': 7.3,
               'Schubert': 3.8, 'Stuart': 14.7, 'Taylor': 7.8, 'Torrens': 4.3,
               'Unley': 9.4, 'Waite': 11.4, 'West Torrens': 4.5, 'Wright': 8.8},
    '2019nsw': {'Albury': -2.75, 'Auburn': 3.2, 'Ballina': 1.28, 'Balmain': 4.7,
               'Bankstown': -0.15, 'Barwon': 10.37, 'Bathurst': -2.08,
               'Baulkham Hills': 3.11, 'Bega': 1.26, 'Blacktown': 4.55,
               'Blue Mountains': 6.71, 'Cabramatta': 8.33, 'Camden': 10.72,
               'Campbelltown': 9.68, 'Canterbury': -2.66, 'Castle Hill': 4.72,
               'Cessnock': -2.67, 'Charlestown': -0.55, 'Clarence': -4.79,
               'Coffs Harbour': 3.53, 'Coogee': 4.56, 'Cootamundra': -6.65,
               'Cronulla': 1.32, 'Davidson': 3.52, 'Drummoyne': 3.78,
               'Dubbo': 2.27, 'East Hills': -0.08, 'Epping': 3.85,
               'Fairfield': 0.14, 'Gosford': 7.04, 'Goulburn': 2.89,
               'Granville': 5.53, 'Hawkesbury': 0.3, 'Heathcote': 2.63,
               'Heffron': 1.03, 'Holsworthy': 3.41, 'Hornsby': 2.64,
               'Keira': 2.35, 'Kiama': -3.35, 'Kogarah': -5.09,
               'Ku-ring-gai': 2.46, 'Lake Macquarie': -5.79, 'Lakemba': 0.86,
               'Lane Cove': 3.47, 'Lismore': 1.57, 'Liverpool': -4.17,
               'Londonderry': -2.37, 'Macquarie Fields': 6.66,
               'Maitland': -0.62, 'Manly': 13.7, 'Maroubra': -2.38,
               'Miranda': -1.61, 'Monaro': -9.08, 'Mount Druitt': 0.97,
               'Mulgoa': -1.67, 'Murray': 0.29, 'Myall Lakes': -0.45,
               'Newcastle': 10.33, 'Newtown': 3.24, 'North Shore': 4.07,
               'Northern Tablelands': -5.77, 'Oatley': -3.93, 'Orange': 6.65,
               'Oxley': -3.98, 'Parramatta': 2.21, 'Penrith': 4.9,
               'Pittwater': 5.53, 'Port Macquarie': -1.32,
               'Port Stephens': 1.03, 'Prospect': 7.26, 'Riverstone': 5.9,
               'Rockdale': 4.8, 'Ryde': 2.55, 'Seven Hills': 2.39,
               'Shellharbour': 1.3, 'South Coast': -0.93, 'Strathfield': 3.22,
               'Summer Hill': 2.16, 'Swansea': -2.42, 'Sydney': 3.7,
               'Tamworth': -0.53, 'Terrigal': -3.32, 'The Entrance': 4.84,
               'Tweed': -1.78, 'Upper Hunter': -0.35, 'Vaucluse': 4.72,
               'Wagga Wagga': 5.38, 'Wakehurst': 4.24, 'Wallsend': 4.64,
               'Willoughby': 3.44, 'Wollondilly': 3.46, 'Wollongong': 7.93,
               'Wyong': 3.72}
}

grn_swings = {
    '2022sa': {'Adelaide': 0.8, 'Badcoe': 3.9, 'Black': 5.2, 'Bragg': 4.1,
               'Chaffey': 4, 'Cheltenham': 4.6, 'Colton': 5.6,
               'Croydon': 4.6, 'Davenport': 2.2, 'Dunstan': 4.7, 'Elder': 2.3,
               'Elizabeth': 1.2, 'Enfield': 2.4, 'Finniss': -0.9,
               'Flinders': -2.1, 'Florey': 4.5, 'Gibson': 5.5,
               'Giles': -0.2, 'Hammond': 0.5, 'Hartley': 6.3, 'Heysen': 8.7,
               'Hurtle Vale': 1.2, 'Kaurna': 1.7, 'Kavel': -1, 'King': -0.2,
               'Lee': 4, 'Light': -0.3, 'Mawson': 0.7,
               'Morialta': 4.8, 'Morphett': 7.3,
               'Newland': 0.2, 'Playford': 4.6,
               'Port Adelaide': 4.4, 'Ramsay': 1.3, 'Reynell': 5.5,
               'Schubert': 4.8, 'Stuart': -1, 'Taylor': -1, 'Torrens': 3.3,
               'Unley': 9.6, 'Waite': 0.9, 'West Torrens': 5.8, 'Wright': 2.7},
    '2019nsw': {'Albury': 3.6, 'Auburn': 1.4, 'Ballina': 4.7, 'Balmain': 5.3,
               'Bankstown': 1.7, 'Barwon': -3.2, 'Bathurst': -3.6,
               'Baulkham Hills': -0.1, 'Bega': -0.4, 'Blacktown': 0,
               'Blue Mountains': -4.1, 'Cabramatta': -0.1, 'Camden': -1.5,
               'Campbelltown': -0.4, 'Canterbury': 2.9, 'Castle Hill': 0.9,
               'Cessnock': -2.7, 'Charlestown': -0.6, 'Clarence': -4.8,
               'Coffs Harbour': -7, 'Coogee': -4.1, 'Cootamundra': -0.6,
               'Cronulla': -1.7, 'Davidson': 0.1, 'Drummoyne': -1.6,
               'Dubbo': -0.8, 'East Hills': -1.8, 'Epping': -3.7,
               'Fairfield': 2.4, 'Gosford': 0.1, 'Goulburn': 0.2,
               'Granville': -1.7, 'Hawkesbury': -1.2, 'Heathcote': -0.3,
               'Heffron': -2, 'Holsworthy': 0, 'Hornsby': -1.6,
               'Keira': 1.8, 'Kiama': 0.8, 'Kogarah': 0,
               'Ku-ring-gai': -2.8, 'Lake Macquarie': 0.1, 'Lakemba': -2.9,
               'Lane Cove': -4.1, 'Lismore': -2.1, 'Liverpool': 0.9,
               'Londonderry': 0.2, 'Macquarie Fields': 0.5,
               'Maitland': 0.1, 'Manly': 1.8, 'Maroubra': -1.6,
               'Miranda': -0.2, 'Monaro': 0.1, 'Mount Druitt': 1.8,
               'Mulgoa': 1.4, 'Murray': 0.4, 'Myall Lakes': -1,
               'Newcastle': -1.9, 'Newtown': 0.5, 'North Shore': -3.3,
               'Northern Tablelands': -1.4, 'Oatley': -1, 'Orange': -1.6,
               'Oxley': -2.4, 'Parramatta': -0.9, 'Penrith': -0.5,
               'Pittwater': -0.8, 'Port Macquarie': -1.1,
               'Port Stephens': -2.9, 'Prospect': -1.8, 'Riverstone': 0.6,
               'Rockdale': 1.1, 'Ryde': -2.8, 'Seven Hills': -0.7,
               'Shellharbour': 0.1, 'South Coast': 0.3, 'Strathfield': -0.4,
               'Summer Hill': -6.7, 'Swansea': 1.1, 'Sydney': -0.2,
               'Tamworth': 0.6, 'Terrigal': -1.7, 'The Entrance': -1.7,
               'Tweed': 0.6, 'Upper Hunter': -0.7, 'Vaucluse': -4.4,
               'Wagga Wagga': -2.2, 'Wakehurst': -2, 'Wallsend': -1.5,
               'Willoughby': -4.7, 'Wollondilly': -2.8, 'Wollongong': 3.3,
               'Wyong': 2.8}
}


def gen_fed_url(election):
    return (f'{base_url}/{aec_election_code[election]}/Website/'
            f'HouseDivisionalResults-{aec_election_code[election]}.htm')

fed_results_urls = {
    '2022vic': (gen_fed_url("2022vic")),
    '2022sa': (gen_fed_url("2022sa")),
    '2019nsw': (gen_fed_url("2019nsw"))
}
ignore_greens_seats_election = {
    # Ignore Greens total in Melbourne due to disendorsement of previous member
    # and in Goldstein/Kooyong due to prominent independents distorting their
    # natural vote
    '2022vic': {'Goldstein', 'Kooyong', 'Melbourne'},
    # Sharkie is already incumbent in Mayo so no need to exclude it
    '2022sa': {},
    # Sharkie is already incumbent in Mayo so no need to exclude it
    '2019nsw': {'Warringah'}
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
                'Wills': 0.06},
    '2022sa': {'Mayo': 4.13},
    '2019nsw': {'Cowper': 0.7, 
                'Farrer': 0.7, 
                'Grayndler': 1.47,
                'New England': -1.21,
                'Warringah': 8.97,
                'Wentworth': 7.9,
                'Whitlam': -2.81}
}


class Config:
    def __init__(self):
        parser = argparse.ArgumentParser(
            description='Determine trend adjustment parameters')
        parser.add_argument('--election', action='store', type=str,
                            help='Generate federal comparisons for this state '
                            'election. Enter as 1234-xxx format,'
                            ' e.g. 2013-fed.')
        parser.add_argument('--hideseats', action='store_true',
                            help='Hide individual seat output')
        self.election = parser.parse_args().election.lower().replace('-', '')
        self.hide_seats = parser.parse_args().hideseats


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
                                             text=alp_name[election])
                tpp_alp_pct = float(tpp_alp_el.find_next_sibling(
                                        'td', headers='tcpPct').text)
                tpp_alp_swing = float(tpp_alp_el.find_next_sibling(
                                        'td', headers='tcpSwg').text)
            
            if (abs(tpp_alp_swing - tpp_alp_pct) < 0.02 or
                fp_formal_int == 0): continue
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
    global warnings
    weighted_greens_swings = {}
    weighted_tpp_swings = {}
    total_weights = {}
    booth_usage = {key: 0 for key in results.vote_totals.keys()}
    for seat, booth_keys in seat_booths.items():
        for booth_key in booth_keys:
            if booth_key not in results.vote_totals:
                warnings += (f'Warning: booth {booth_key[1]} '
                             f'in seat {booth_key[0]} not found!\n')
                continue
            booth_usage[booth_key] += 1
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
    unused_booths = [a for a, b in booth_usage.items() if b == 0
        and 'EAV' not in a[1] and ' Team' not in a[1]
        and 'Divisional Office' not in a[1] and 'BLV' not in a[1] and
        'Adelaide (' not in a[1] and 'Melbourne (' not in a[1]
        and 'Sydney (' not in a[1]
        and not (('Adelaide ') in a[1] and (' PPVC') in a[1])
        and not (('Melbourne ') in a[1] and (' PPVC') in a[1])
        and not (('Sydney ') in a[1] and (' PPVC') in a[1])]
    duplicated_booths = [a for a, b in booth_usage.items() if b > 1]
    warnings += (f'Unused booths: {unused_booths}\n')
    warnings += (f'Duplicated booths: {duplicated_booths}\n')
    return (weighted_greens_swings, weighted_tpp_swings, total_weights)


def calculate_deviations(config, seat_booths, results, election):

    weighted_greens_swings, weighted_tpp_swings, total_weights = \
        add_weighted_swings(seat_booths, results, election)

    overall_greens_swing = overall_greens_swings[election]
    overall_tpp_swing = overall_tpp_swings[election]

    seat_names = []
    tpp_list = []
    grn_list = []
    for seat, weighted_greens_swing in weighted_greens_swings.items():
        total_weight = total_weights[seat]
        weighted_tpp_swing = weighted_tpp_swings[seat]
        greens_swing = weighted_greens_swing / total_weight
        tpp_swing = weighted_tpp_swing / total_weight
        tpp_deviation = tpp_swing - overall_tpp_swing
        greens_deviation = greens_swing - overall_greens_swing
        if not config.hide_seats:
            print(f'{seat} federal greens deviation: {greens_deviation}')
            print(f'{seat} federal tpp deviation: {tpp_deviation}')
        tpp_list.append((seat, tpp_deviation))
        grn_list.append((seat, greens_deviation))

    # for seat_name, tpp_dev in sorted(tpp_list, key=lambda x: x[1]):
    #     print(f'{seat_name} federal tpp deviation: {tpp_dev}')

    # for seat_name, grn_dev in sorted(grn_list, key=lambda x: x[1]):
    #     print(f'{seat_name} federal GRN deviation: {grn_dev}')

    if election in tpp_swings:
        fed_tpps = [tpp for seat, tpp in tpp_list]
        state_tpps = [tpp_swings[election][seat] for seat, tpp in tpp_list]

        inputs_array = numpy.transpose(numpy.array([fed_tpps]))
        outputs_array = numpy.array(state_tpps)

        sm_inputs = sm.add_constant(inputs_array)
        mod = sm.OLS(outputs_array, sm_inputs)
        fii = mod.fit()
        print(f'Statistics for federal-state TPP correlations - {election}')
        print(fii.summary())

        # for q in (0.1, 0.5, 0.9):
        #     q_reg = (QuantileRegressor(alpha=0, quantile=q)
        #                               .fit(inputs_array, outputs_array))
            
        #     q_coefficient = q_reg.coef_[0]
        #     q_intercept = q_reg.intercept_

        #     print(q)
        #     print(q_coefficient)
        #     print(q_intercept)
    
    if election in grn_swings:
        this_grns = grn_swings[election]
        fed_grn = [grn for seat, grn in grn_list if seat in this_grns]
        state_grn = [this_grns[seat] for seat, grn in grn_list 
                     if seat in this_grns]

        inputs_array = numpy.transpose(numpy.array([fed_grn]))
        outputs_array = numpy.array(state_grn)

        sm_inputs = sm.add_constant(inputs_array)
        mod = sm.OLS(outputs_array, sm_inputs)
        fii = mod.fit()
        print(f'Statistics for federal-state GRN correlations - {election}')
        print(fii.summary())


def analyse_specific_election(config):
    election = config.election

    results = obtain_results(election)

    seat_booths = parse_booth_file(election)

    calculate_deviations(config, seat_booths, results, election)


def analyse():
    try:
        config = Config()
    except ConfigError as e:
        print('Could not process configuration due to the following issue:')
        print(str(e))
        return
    analyse_specific_election(config)
    print(warnings.strip())


if __name__ == '__main__':
    analyse()