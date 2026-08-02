import pickle
import argparse
import requests
from bs4 import BeautifulSoup
import numpy
import statsmodels.api as sm

class FederalStateError(ValueError):
    pass


class ConfigError(FederalStateError):
    pass


class MappingError(FederalStateError):
    pass


class ResultsError(FederalStateError):
    pass


overall_tpp_swings = {
                      '2027nsw': 3.85,
                      '2026vic': 1.52,
                      '2026sa': 5.23,
                      '2023nsw': 3.2,
                      '2022vic': 1.69,
                      '2022sa': 3.26,
                      '2019nsw': -1.17,
                      '2018vic': 1.31,
                      '2018sa': -1.56}
overall_grn_swings = {
                      '2027nsw': 1.04,
                      '2026vic': -0.15,
                      '2026sa': 0.65,
                      '2023nsw': 1.31,
                      '2022vic': 1.85,
                      '2022sa': 3.16,
                      '2019nsw': 0.17,
                      '2018vic': -1.24,
                      '2018sa': 3.4}
base_url = 'https://results.aec.gov.au'
aec_election_code = {
    '2027nsw': 31496,
    '2026vic': 31496,
    '2026sa': 31496,
    '2023nsw': 27966,
    '2022vic': 27966,
    '2022sa': 27966,
    '2019nsw': 24310,
    '2018vic': 24310,
    '2018sa': 24310,
}

alp_name = {
    '2027nsw': 'Labor',
    '2026vic': 'Australian Labor Party',
    '2026sa': 'Australian Labor Party',
    '2023nsw': 'Labor',
    '2022vic': 'Australian Labor Party',
    '2022sa': 'Australian Labor Party',
    '2019nsw': 'Labor',
    '2018vic': 'Australian Labor Party',
    '2018sa': 'Australian Labor Party',
}

grn_name = {
    '2027nsw': 'The Greens',
    '2026vic': 'The Greens',
    '2026sa': 'The Greens',
    '2023nsw': 'The Greens',
    '2022vic': 'The Greens',
    '2022sa': 'The Greens',
    '2019nsw': 'The Greens',
    '2018vic': 'The Greens (VIC)',
    '2018sa': 'The Greens',
}


tpp_swings = {
    '2022sa': {'Adelaide': 7.1, 'Badcoe': 10.1, 'Black': 6.5, 'Bragg': 8.8,
               'Chaffey': 0.8, 'Cheltenham': 2.4, 'Colton': 1.4,
               'Croydon': 1.5, 'Davenport': 11.6, 'Dunstan': 6.9, 'Elder': 7.5,
               'Elizabeth': 3.3, 'Enfield': 8.3, 'Finniss': 12.1,
               'Flinders': 5.7, 'Florey': -0.6, 'Frome': 10, 'Gibson': 12.5,
               'Giles': 6.1, 'Hammond': 11.7, 'Hartley': 3, 'Heysen': 5.7,
               'Hurtle Vale': 7.2, 'Kaurna': 4.1, 'Kavel': 9.8, 'King': 3.5,
               'Lee': 5.9, 'Light': 11.1, 'MacKillop': 2.6, 'Mawson': 13.1,
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
                'Wyong': 3.72},
    '2018vic': {'Albert Park': 10.17, 'Altona': 2.01, 'Bass': 6.94,
                'Bayswater': 5.01, 'Bellarine': 6.61, 'Benambra': 0.76,
                'Bendigo East': 7.12, 'Bendigo West': 6.37, 'Bentleigh': 11.27,
                'Box Hill': 7.8, 'Brighton': 8.66, 'Broadmeadows': 2.45,
                'Brunswick': 4.98, 'Bulleen': 4.79, 'Bundoora': 5.2,
                'Buninyong': 5.88, 'Burwood': 6.47, 'Carrum': 11.23,
                'Caulfield': 4.61, 'Clarinda': 1.58, 'Cranbourne': 8.65,
                'Croydon': 7.18, 'Dandenong': 11.05, 'Eildon': 1.32,
                'Eltham': 6.37, 'Essendon': 7.2, 'Euroa': -0.97,
                'Evelyn': 6.94, 'Ferntree Gully': 6.1, 'Footscray': 13.62,
                'Forest Hill': 3.67, 'Frankston': 9.26, 'Geelong': 4.11,
                'Gembrook': 8.16, 'Gippsland East': 0.32, 'Gippsland South': 0.34,
                'Hastings': 6.59, 'Hawthorn': 9.01, 'Ivanhoe': 8.96,
                'Kew': 5.87, 'Keysborough': 2.95, 'Kororoit': 5.67,
                'Lara': 2.03, 'Lowan': -2.22, 'Macedon': 9.4,
                'Malvern': 10.15, 'Melbourne': 4.79, 'Melton': -6.93,
                'Mildura': 14.61, 'Mill Park': 5.04, 'Monbulk': 3.65,
                'Mordialloc': 10.87, 'Mornington': 7.61, 'Morwell': 4.21,
                'Mount Waverley': 6.44, 'Mulgrave': 8.23, 'Murray Plains': -1.59,
                'Narracan': 4.03, 'Narre Warren North': 5.19,
                'Narre Warren South': 1.4, 'Nepean': 8.54, 'Niddrie': 4.88,
                'Northcote': 3.41, 'Oakleigh': 7.6, 'Ovens Valley': 3.94,
                'Pascoe Vale': 1.55, 'Polwarth': 5.24, 'Prahran': 7.58,
                'Preston': 3.85, 'Richmond': 5.3, 'Ringwood': 7.89,
                'Ripon': 0.99, 'Rowville': 2.72, 'Sandringham': 6.69,
                'Shepparton': 1.56, 'South Barwon': 7.46, 'South-West Coast': 8.66,
                'St Albans': 4.04, 'Sunbury': 10.03, 'Sydenham': 1.6,
                'Tarneit': 3.44, 'Thomastown': -1.25, 'Warrandyte': 7.72,
                'Wendouree': 4.47, 'Werribee': -3.1, 'Williamstown': 5.53,
                'Yan Yean': 13.37, 'Yuroke': 1.74
    },
    '2018sa': {'Adelaide': 2, 'Badcoe': 1.4, 'Black': -6.4, 'Bragg': -0.3,
               'Chaffey': 6.7, 'Cheltenham': 1.5, 'Colton': -4,
               'Croydon': 3.2, 'Davenport': 0.7, 'Dunstan': -2.5, 'Elder': -0.3,
               'Elizabeth': 3.3, 'Enfield': 2.3, 'Finniss': -1.1,
               'Flinders': 2.6, 'Florey': 1.9, 'Frome': -1.3, 'Gibson': -5.6,
               'Giles': 10, 'Hammond': -2.7, 'Hartley': -4.7, 'Heysen': 3.8,
               'Hurtle Vale': 4, 'Kaurna': 5.7, 'Kavel': -1, 'King': -0.7,
               'Lee': 2.3, 'Light': 5.9, 'MacKillop': 1.7, 'Mawson': 4.5,
               'Morialta': 1.5, 'Morphett': -2.6, 'Mount Gambier': 2.9,
               'Narungga': -3.3, 'Newland': -1.8, 'Playford': 4.6,
               'Port Adelaide': 2.8, 'Ramsay': 1.2, 'Reynell': 3.4,
               'Schubert': 2, 'Stuart': -3, 'Taylor': 2.3, 'Torrens': 2.1,
               'Unley': -2.4, 'Waite': 2.3, 'West Torrens': 0.9, 'Wright': -0.8},
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
               'Wyong': 2.8},
    '2018vic': {'Albert Park': -0.21, 'Altona': -1.04, 'Bass': -3.41,
                'Bayswater': -0.33, 'Bellarine': -0.43, 'Benambra': -5.31,
                'Bendigo East': 0.76, 'Bendigo West': 1.3, 'Bentleigh': -3.05,
                'Box Hill': 0.22, 'Brighton': -2.28, 'Broadmeadows': 2.49,
                'Brunswick': 0.42, 'Bulleen': 1.55, 'Bundoora': -2.2,
                'Buninyong': -1.77, 'Burwood': -0.8, 'Carrum': -1.71,
                'Caulfield': -2.15, 'Clarinda': -0.76, 'Cranbourne': -0.24,
                'Croydon': 0.31, 'Dandenong': -0.2, 'Eildon': -1.26,
                'Eltham': -0.96, 'Essendon': -1.22, 'Euroa': 0.09,
                'Evelyn': 3.44, 'Ferntree Gully': 0.49, 'Footscray': -0.46,
                'Forest Hill': -0.08, 'Frankston': -0.4, 'Geelong': -2.51,
                'Gembrook': 2.16, 'Gippsland East': -1.76, 'Gippsland South': 0.08,
                'Hastings': 1.23, 'Hawthorn': -3.07, 'Ivanhoe': -0.91,
                'Kew': -0.94, 'Keysborough': -0.79, 'Kororoit': 1.21,
                'Lara': -1.41, 'Lowan': -3.18, 'Macedon': -5.58,
                'Malvern': -3.42, 'Melbourne': -2.59, 'Melton': -2.56,
                'Mildura': 1.96, 'Mill Park': -1.63, 'Monbulk': 1.94,
                'Mordialloc': -0.04, 'Mornington': -1.91, 'Morwell': -1.08,
                'Mount Waverley': 0.67, 'Mulgrave': -0.82, 'Murray Plains': 0.22,
                'Narracan': -2.99, 'Narre Warren North': 0.51,
                'Narre Warren South': 0.43, 'Nepean': 0.18, 'Niddrie': -1.26,
                'Northcote': 3.24, 'Oakleigh': -2.41, 'Ovens Valley': -5.54,
                'Pascoe Vale': -3.34, 'Polwarth': -1.24, 'Prahran': 3.32,
                'Preston': -1.05, 'Richmond': 2.77, 'Ringwood': 2.95,
                'Ripon': -2.97, 'Rowville': 2.63, 'Sandringham': -5.62,
                'Shepparton': -0.96, 'South Barwon': -3.7, 'South-West Coast': -3.61,
                'St Albans': 3.13, 'Sunbury': -0.09, 'Sydenham': -0.71,
                'Tarneit': -1.4, 'Thomastown': -0.95, 'Warrandyte': -0.07,
                'Wendouree': -0.03, 'Werribee': -1.96, 'Williamstown': -4.67,
                'Yan Yean': -0.42, 'Yuroke': 0.38},
    '2018sa': {'Adelaide': 0.7, 'Badcoe': -3.9, 'Black': -0.8, 'Bragg': -2.1,
               'Chaffey': -3.6, 'Cheltenham': -2.5, 'Colton': -1.7,
               'Croydon': -3.3, 'Davenport': -0.2, 'Dunstan': -2.7, 'Elder': -3.5,
               'Elizabeth': -0.7, 'Enfield': 0, 'Finniss': -4.3,
               'Flinders': 0.9, 'Florey': -1.8, 'Gibson': -3,
               'Giles': -1.3, 'Hammond': -1.3, 'Hartley': -3.1, 'Heysen': -8.3,
               'Hurtle Vale': -1.8, 'Kaurna': 1.7, 'Kavel': -7.2, 'King': -0.6,
               'Lee': -1.6, 'Light': 1.3, 'MacKillop': -3.3, 'Mawson': -3.4,
               'Morialta': -4.3, 'Morphett': -4.9,
               'Newland': -2, 'Playford': -1.1,
               'Port Adelaide': -4, 'Ramsay': -2.6, 'Reynell': -2.3,
               'Schubert': -4.2, 'Stuart': 2.4, 'Taylor': -0.5, 'Torrens': -1.7,
               'Unley': -2, 'Waite': -5, 'West Torrens': -0.1, 'Wright': -1.2},
}


def gen_fed_url(election):
    return (f'{base_url}/{aec_election_code[election]}/Website/'
            f'HouseDivisionalResults-{aec_election_code[election]}.htm')

fed_results_urls = {
    '2027nsw': (gen_fed_url("2027nsw")),
    '2026vic': (gen_fed_url("2026vic")),
    '2026sa': (gen_fed_url("2026sa")),
    '2023nsw': (gen_fed_url("2023nsw")),
    '2022vic': (gen_fed_url("2022vic")),
    '2022sa': (gen_fed_url("2022sa")),
    '2019nsw': (gen_fed_url("2019nsw")),
    '2018vic': (gen_fed_url("2018vic")),
    '2018sa': (gen_fed_url("2018sa"))
}

ignore_greens_seats_election = {
    '2027nsw': {'Calare'},
    '2026vic': {'Flinders', 'Monash'},
    '2026sa': {},
    # Ignore Greens totals in these seats due to new prominent independents distorting their
    # natural vote
    '2023nsw': {'North Sydney', 'Mackellar', 'Bradfield', 'Cowper', 'Calare'},
    # Ignore Greens total in Melbourne due to disendorsement of previous member
    # and in Goldstein/Kooyong due to prominent independents distorting their
    # natural vote
    '2022vic': {'Goldstein', 'Kooyong', 'Melbourne'},
    # Sharkie is already incumbent in Mayo so no need to exclude it
    '2022sa': {},
    '2019nsw': {'Warringah'},
    '2018vic': {},
    '2018sa': {},
}

assume_tpp_seats_election = {
    '2027nsw': {
        'Bradfield': 1.18,
        'Calare': 3.19,
        'Cowper': 0.45,
        'Farrer': 3.46,
        'Fowler': 12.29,
        'Grayndler': 3.55,
        'Hunter': 4.70,
        'Mackellar': 4.21,
        'Newcastle': 2.85,
        'Warringah': 5.25,
        'Watson': 7.70,
        'Wentworth': 1.72,
        'Wills': 3.77,
    },
    '2026vic': {
        'Bendigo': -9.81,
        'Calwell': 2.32,
        'Cooper': 2.82,
        'Flinders': 1.38,
        'Fraser': 5.46,
        'Goldstein': -0.25,
        'Indi': -1.40,
        'Kooyong': 1.37,
        'Melbourne': 0.88,
        'Monash': 3.39,
        'Nicholls': -1.87,
        'Wannon': -2.27,
        'Wills': 3.77,
    },
    '2026sa': {'Mayo': 3.39},
    '2023nsw': {
        'Bradfield': 10.01,
        'Calare': -2.16,
        'Cowper': 2.41,
        'Fowler': -8.27,
        'Grayndler': 5.02,
        'Mackellar': 4.62,
        'North Sydney': 8.01,
        'Sydney': 6.9,
        'Warringah': 0.69,
        'Wentworth': 3.93,
    },
    '2022vic': {
        'Cooper': -0.75,
        'Goldstein': 2.99,
        'Indi': 7.47,
        'Kooyong': 2.21,
        'Melbourne': 7.61,
        'Nicholls': 2.88,
        'Wannon': 1.13,
        'Wills': 2.56
    },
    '2022sa': {'Mayo': 4.13},
    '2019nsw': {
        'Cowper': 0.7,
        'Farrer': 0.7,
        'Grayndler': 1.47,
        'New England': -1.21,
        'Warringah': 8.97,
        'Wentworth': 7.9,
        'Whitlam': -2.81
    },
    '2018vic': {
        'Cooper': 4.23,
        'Indi': -7.72,
        'Kooyong': 6.14,
        'Wills': 4.18,
        'Melbourne': 0.07,
    },
    '2018sa': {'Mayo': 0.73},
}

adjust_tpp_state = {
    '2022sa': {
        'King': -1.96,
        'Colton': -1.96,
        'Elder': -1.96,
        'Newland': -1.96,
        'Croydon': 0.72,
        'Croydon': 0.72,
        'Badcoe': 0.72,
        'Wright': 0.72,
        'Playford': 0.72,
        'Taylor': -1.22,
        'Schubert': 1.3,
        'Flinders': 1.3,
        'Kavel': -0.72,
        'Narungga': -0.72,
        'Finniss': -0.72,
        'Heysen': -0.72,
        'MacKillop': -0.72,
    },
    '2019nsw': {
        'Blue Mountains': 1.9,
        'Gosford': 1.9,
        'Campbelltown': 1.96,
        'Granville': 1.96,
        'Londonderry': 1.96,
        'Rockdale': 1.96,
        'Strathfield': 1.96,
        'Maitland': 1.9,
        'Port Stephens': 1.9,
        'Swansea': 1.9,
        'The Entrance': 1.9,
        'Prospect': 1.96,
        'Wyong': 1.9,
        'Seven Hills': -1.96,
        'Miranda': -1.96,
        'Auburn': -1.22,
        'Wollondilly': -1.3,
        'Albury': 1.3,
        'East Hills': 1.22,
        'Goulburn': 1.3,
        'Coffs Harbour': 1.3,
        'Lismore': 1.3,
        'Dubbo': 1.3,
        'Barwon': 1.3,
        'Camden': 1.22,
        'Ku-ring-gai': -0.72,
        'Epping': -0.72,
        'Upper Hunter': -1.04,
        'Oxley': -1.04,
        'Terrigal': -1.04,
        'Macquarie Fields': 0.72,
        'Summer Hill': 0.72,
        'Mount Druitt': 0.72,
        'Kogarah': 0.72,
        'Lakemba': 0.72,
    },
    '2018vic': {
        'Bentleigh': 1.96,
        'Carrum': 1.96,
        'Mordialloc': 1.96,
        'Frankston': 1.96,
        'Ripon': 1.9,
        'Narre Warren South': -1.22,
        'Buninyong': -1.3,
        'Wendouree': -1.3,
        'Tarneit': -1.22,
        'Clarinda': -1.22,
        'Williamstown': -1.22,
        'Cranbourne': -1.22,
        'Footscray': -1.22,
        'Melton': -1.22,
        'Brighton': 1.22,
        'Nepean': 1.22,
        'Evelyn': 1.22,
        'Sandringham': 1.22,
        'Oakleigh': 0.72,
        'Yuroke': 0.72,
        'Pascoe Vale': 0.72,
        'Essendon': 0.72,
        'Dandenong': 0.72,
        'Geelong': 0.72,
        'Macedon': 1.04,
        'Hawthorn': -0.72,
        'Bulleen': -0.72,
        'Kew': -0.72,
        'Bass': -1.04,
        'Lowan': -1.04,
        'Euroa': -1.04,
        'Yan Yean': 5,
    },
    '2018sa': {
        'Croydon': -1.22,
        'Colton': -1.22,
        'Ashford': -1.22,
        'Wright': -1.22,
        'Playford': -1.22,
        'Taylor': -1.22,
        'Kavel': 1.22,
        'Goyder': 1.22,
        'Finniss': 1.22,
        'Heysen': 1.22,
        'MacKillop': 1.22,
        'Giles': 0.72,
        'Elder': 0.72,
        'Torrens': 0.72,
        'Kaurna': 0.72,
        'Napier': 0.72,
        'Reynell': 0.72,
        'Lee': 0.72,
        'Schubert': -0.72,
    }
}

adjust_tpp_federal = {
    '2027nsw': {
        'Barton': -1.2,
        'Bennelong': 2.0,
        'Bradfield': 1.2,
        'Fowler': -1.0,
        'Lyne': 1.2,
        'Mackellar': 1.0,
        'Parkes': 1.2,
        'Reid': 2.0,
        'Robertson': 2.0,
        'Wentworth': 1.0,
        'Whitlam': -1.2,
    },
    '2026vic': {
        'Aston': 2.0,
        'Calwell': -1.2,
        'Casey': 1.2,
        'Chisholm': 2.0,
        'Flinders': -1.1,
        'Gorton': -1.2,
        'Hawke': 1.1,
        'Holt': 0.7,
        'Maribyrnong': -1.2,
        'Menzies': -0.7,
    },
    '2026sa': {
        'Boothby': 1.96,
        'Grey': 1.04,
        'Spence': 0.72,
    },
    '2023nsw': {
        'Cunningham': -1.3,
        'Hunter': -1.3,
        'Fowler': -1.22,
        'Parramatta': -1.22,
        'Bennelong': 1.22,
        'Gilmore': 1.9,
        'Lindsay': -1.96,
        'Reid': -0.72,
        'Cowper': -0.72,
    },
    '2022vic': {
        'Casey': 1.22,
        'Chisholm': 0.72,
        'Corangamite': 1.9,
        'Dunkley': 1.96,
        'Flinders': 1.3,
        'Hawke': -1.22,
        'Higgins': 0.72,
        'Holt': -1.22,
        'Isaacs': -2.5,
        'Jagajaga': 0.72,
        'Macnamara': 0.72,
        'Mallee': -1.04,
        'Melbourne': 2.5,
        'Menzies': 1.22,
        'Nicholls': 1.3,
        'Scullin': -2.5,
    },
    '2022sa': {
        'Adelaide': 0.72,
        'Sturt': -0.72,
        'Boothby': 1.22,
    },
    '2019nsw': {
        'Barton': 1.96,
        'Macarthur': 1.96,
        'Dobell': 1.9,
        'Eden-Monaro': 1.9,
        'Macquarie': 1.9,
        'Paterson': 1.9,
        'Lindsay': 1.2,
        'Reid': 1.22,
        'Gilmore': 1.3,
        'Cowper': 1.3,
        'Farrer': 2.5,
    },
    '2018vic': {
        'Macnamara': -1.22,
        'Higgins': 1.22,
        'Mallee': 1.3,
        'Jagajaga': -1.22,
        'Isaacs': 5,
        'Scullin': 5,
        'Wills': 5,
        'Melbourne': -5,
        'Chisholm': -1.96,
        'Dunkley': -0.72,
        'Bruce': 0.72,
        'Goldstein': -0.72,
        'Nicholls': -1.04,
    },
    '2018sa': {
        'Adelaide': -1.22,
        'Sturt': 1.22,
        'Boothby': -0.72,
    },
}


class Config:
    def __init__(self):
        parser = argparse.ArgumentParser(
            description='Determine federal-state comparisons')
        parser.add_argument('--election', action='store', type=str,
                            help='Generate federal comparisons for this state '
                            'election. Enter as 1234-xxx format,'
                            ' e.g. 2027-nsw.')
        parser.add_argument('--hideseats', action='store_true',
                            help='Hide individual seat output')
        arguments = parser.parse_args()
        if not arguments.election:
            raise ConfigError('An election must be provided with --election.')

        self.election = arguments.election.lower().replace('-', '')
        if self.election not in overall_tpp_swings:
            available = ', '.join(sorted(overall_tpp_swings))
            raise ConfigError(
                f'Unknown election {arguments.election!r}. Available elections: '
                f'{available}.'
            )
        self.hide_seats = arguments.hideseats


class Results:
    """Compatibility type for the previous derived-result cache format."""

    def __init__(self):
        self.greens_swings = {}
        self.tpp_swings = {}
        self.vote_totals = {}


class RawResults:
    """AEC booth data before local assumptions and manual corrections."""

    def __init__(self):
        self.greens_swings = {}
        self.tpp_swings = {}
        self.tpp_percentages = {}
        self.vote_totals = {}


def election_filename(election):
    return f'Federal-State/{election}.pkl'


def fetch_page(url, description):
    try:
        response = requests.get(url, timeout=30)
        response.raise_for_status()
    except requests.RequestException as error:
        raise ResultsError(f'Could not download {description}: {error}') from error
    return BeautifulSoup(response.content, 'html.parser')


def required_sibling(element, headers, description):
    if element is None:
        raise ResultsError(f'Could not find {description}.')
    sibling = element.find_next_sibling('td', headers=headers)
    if sibling is None:
        raise ResultsError(f'Could not find {description} value.')
    return sibling


def parse_float(cell, description):
    try:
        return float(cell.get_text(strip=True))
    except ValueError as error:
        raise ResultsError(f'Could not parse {description}.') from error


def fetch_results(election):
    """Download raw AEC booth data without applying local assumptions."""
    URL = fed_results_urls[election]
    aec_code = aec_election_code[election]
    soup = fetch_page(URL, f'{election} federal division results')

    seat_els = soup.find_all('td', class_='filterDivision')

    current_state = election[4:].upper()

    results = RawResults()

    for seat_el in seat_els:
        state_element = seat_el.find_next_sibling('td')
        if state_element is None:
            raise ResultsError('Could not find a federal division state.')
        state = state_element.get_text(strip=True)
        if state != current_state:
            continue
        seat_link = seat_el.find('a')
        if seat_link is None:
            raise ResultsError('Could not find a federal division result link.')
        seat_name = seat_el.get_text(strip=True)
        seat_path = seat_link['href']
        seat_URL = f'{base_url}/{aec_code}/Website/{seat_path}'
        seat_soup = fetch_page(seat_URL, f'{seat_name} division results')
        booth_els = seat_soup.find_all('td', headers='ppPp')
        for booth_el in booth_els:
            booth_name = booth_el.get_text(strip=True)
            booth_link = booth_el.find('a')
            if booth_link is None:
                raise ResultsError(
                    f'Could not find the link for {seat_name} booth {booth_name}.'
                )
            booth_path = booth_link['href']
            booth_URL = f'{base_url}/{aec_code}/Website/{booth_path}'
            booth_soup = fetch_page(
                booth_URL, f'{seat_name} booth {booth_name} results'
            )
            fp_greens_el = booth_soup.find('td', headers='fpPty',
                                            string=grn_name[election])
            fp_greens_swing = parse_float(
                required_sibling(
                    fp_greens_el, 'fpSwg',
                    f'{seat_name} booth {booth_name} Greens swing',
                ),
                f'{seat_name} booth {booth_name} Greens swing',
            )
            formal_el = booth_soup.find('td', headers='fpCan',
                                        string="Formal")
            fp_formal_text = required_sibling(
                formal_el, 'fpVot', f'{seat_name} booth {booth_name} formal vote'
            ).get_text(strip=True)
            try:
                fp_formal_int = int(fp_formal_text.replace(',', ''))
            except ValueError as error:
                raise ResultsError(
                    f'Could not parse {seat_name} booth {booth_name} formal vote.'
                ) from error

            tpp_alp_el = booth_soup.find(
                'td', headers='tcpPty', string=alp_name[election]
            )
            tpp_alp_pct = None
            tpp_alp_swing = None
            if tpp_alp_el is not None:
                tpp_alp_pct = parse_float(
                    required_sibling(
                        tpp_alp_el, 'tcpPct',
                        f'{seat_name} booth {booth_name} TPP percentage',
                    ),
                    f'{seat_name} booth {booth_name} TPP percentage',
                )
                tpp_alp_swing = parse_float(
                    required_sibling(
                        tpp_alp_el, 'tcpSwg',
                        f'{seat_name} booth {booth_name} TPP swing',
                    ),
                    f'{seat_name} booth {booth_name} TPP swing',
                )

            booth_key = (seat_name, booth_name)
            results.greens_swings[booth_key] = fp_greens_swing
            results.tpp_swings[booth_key] = tpp_alp_swing
            results.tpp_percentages[booth_key] = tpp_alp_pct
            results.vote_totals[booth_key] = fp_formal_int
        print(f'Downloaded booths for seat: {seat_name}')
    filename = election_filename(election)
    with open(filename, 'wb') as pkl:
        pickle.dump(results, pkl, pickle.HIGHEST_PROTOCOL)
    return results


def is_raw_results(results):
    return isinstance(results, RawResults) and hasattr(results, 'tpp_percentages')


def obtain_results(election):
    filename = election_filename(election)
    results = None
    try:
        with open(filename, 'rb') as pkl:
            results = pickle.load(pkl)
    except (AttributeError, EOFError, FileNotFoundError, pickle.UnpicklingError):
        pass

    if not is_raw_results(results):
        if results is not None:
            print('Refreshing legacy derived booth cache as raw AEC data.')
        results = fetch_results(election)
    return results


def apply_local_assumptions(raw_results, election):
    """Apply local exceptions after loading cached AEC data."""
    results = Results()
    overall_greens_swing = overall_grn_swings[election]
    ignored_greens_seats = ignore_greens_seats_election[election]
    assumed_tpp_seats = assume_tpp_seats_election[election]

    for booth_key, formal_votes in raw_results.vote_totals.items():
        federal_seat = booth_key[0]
        greens_swing = raw_results.greens_swings[booth_key]
        if federal_seat in ignored_greens_seats:
            greens_swing = overall_greens_swing

        tpp_swing = raw_results.tpp_swings[booth_key]
        tpp_percentage = raw_results.tpp_percentages[booth_key]
        if federal_seat in assumed_tpp_seats:
            tpp_swing = assumed_tpp_seats[federal_seat]
            tpp_percentage = None
        elif tpp_swing is None or tpp_percentage is None:
            raise ResultsError(
                f'{federal_seat} booth {booth_key[1]} lacks an ALP TPP result '
                'and has no configured assumption.'
            )

        # AEC reports a percentage as the swing for booths without a comparable
        # predecessor. Retain the booth in the raw cache but exclude it here.
        if (tpp_percentage is not None and
                (abs(tpp_swing - tpp_percentage) < 0.02 or formal_votes == 0)):
            formal_votes = 0
            tpp_swing = 0
        elif federal_seat in adjust_tpp_federal[election]:
            tpp_swing -= adjust_tpp_federal[election][federal_seat]

        results.greens_swings[booth_key] = greens_swing
        results.tpp_swings[booth_key] = tpp_swing
        results.vote_totals[booth_key] = formal_votes
    return results


def parse_booth_mapping(mapping_filename):
    seat_booths = {}
    current_seat = None
    with open(mapping_filename, encoding='utf-8') as mapping_file:
        for line_number, raw_line in enumerate(mapping_file, start=1):
            line = raw_line.strip()
            if not line:
                continue
            if line.startswith('#'):
                current_seat = line[1:].strip()
                if not current_seat:
                    raise MappingError(
                        f'{mapping_filename}:{line_number} has an empty state seat.'
                    )
                if current_seat in seat_booths:
                    raise MappingError(
                        f'{mapping_filename}:{line_number} repeats state seat '
                        f'{current_seat!r}.'
                    )
                seat_booths[current_seat] = set()
                continue

            booth = tuple(part.strip() for part in line.split(','))
            if current_seat is None:
                raise MappingError(
                    f'{mapping_filename}:{line_number} appears before a state seat.'
                )
            if len(booth) != 2 or not all(booth):
                raise MappingError(
                    f'{mapping_filename}:{line_number} must be "Federal seat,booth".'
                )
            if booth in seat_booths[current_seat]:
                raise MappingError(
                    f'{mapping_filename}:{line_number} repeats booth {booth!r} '
                    f'for {current_seat}.'
                )
            seat_booths[current_seat].add(booth)

    if not seat_booths:
        raise MappingError(f'{mapping_filename} contains no state-seat mappings.')
    return seat_booths


def parse_booth_file(election):
    return parse_booth_mapping(f'Federal-State/booths-{election}.txt')


def validate_mapping(seat_booths, results):
    """Validate authored mapping coverage before calculating any seat values."""
    booth_to_state_seats = {}
    missing_booths = []
    zero_weight_seats = []
    for state_seat, booth_keys in seat_booths.items():
        total_weight = 0
        for booth_key in booth_keys:
            booth_to_state_seats.setdefault(booth_key, []).append(state_seat)
            if booth_key not in results.vote_totals:
                missing_booths.append((state_seat, booth_key))
                continue
            total_weight += results.vote_totals[booth_key]
        if total_weight == 0:
            zero_weight_seats.append(state_seat)

    duplicate_booths = {
        booth_key: state_seats
        for booth_key, state_seats in booth_to_state_seats.items()
        if len(state_seats) > 1
    }
    problems = []
    if missing_booths:
        examples = ', '.join(
            f'{state_seat}: {booth_key[0]}/{booth_key[1]}'
            for state_seat, booth_key in missing_booths[:5]
        )
        problems.append(f'missing federal booth result(s): {examples}')
    if duplicate_booths:
        examples = ', '.join(
            f'{booth_key[0]}/{booth_key[1]} -> {", ".join(state_seats)}'
            for booth_key, state_seats in list(duplicate_booths.items())[:5]
        )
        problems.append(f'federal booth mapped to multiple state seats: {examples}')
    if zero_weight_seats:
        problems.append(
            'state seat(s) have no usable comparable federal votes: ' +
            ', '.join(zero_weight_seats[:5])
        )
    if problems:
        raise MappingError('; '.join(problems))


def mapping_diagnostics(seat_booths, results):
    mapped_booths = {
        booth_key for booth_keys in seat_booths.values() for booth_key in booth_keys
    }
    unused_booths = [
        booth_key for booth_key, vote_total in results.vote_totals.items()
        if (vote_total > 0 and booth_key not in mapped_booths and
            is_unexpected_unmapped_booth(booth_key[1]))
    ]
    if unused_booths:
        print(f'Unmapped comparison booths: {unused_booths}')


def is_unexpected_unmapped_booth(booth_name):
    """Exclude result groups that cannot be geographically assigned to one seat."""
    return not (
        'EAV' in booth_name or
        ' Team' in booth_name or
        'Divisional Office' in booth_name or
        'BLV' in booth_name or
        'Adelaide (' in booth_name or
        'Melbourne (' in booth_name or
        'Sydney (' in booth_name or
        'Ultimo (' in booth_name or
        'Wynyard (' in booth_name or
        'Polling Day' in booth_name or
        ('Adelaide ' in booth_name and ' PPVC' in booth_name) or
        ('Melbourne ' in booth_name and ' PPVC' in booth_name) or
        ('Sydney ' in booth_name and ' PPVC' in booth_name) or
        ('Haymarket ' in booth_name and ' PPVC' in booth_name)
    )


def add_weighted_swings(seat_booths, results):
    weighted_greens_swings = {}
    weighted_tpp_swings = {}
    total_weights = {}
    for seat, booth_keys in seat_booths.items():
        for booth_key in booth_keys:
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


def calculate_deviations(config, seat_booths, results, election):

    validate_mapping(seat_booths, results)
    mapping_diagnostics(seat_booths, results)
    weighted_greens_swings, weighted_tpp_swings, total_weights = \
        add_weighted_swings(seat_booths, results)

    overall_greens_swing = overall_grn_swings[election]
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
        state_tpps = [
            tpp_swings[election][seat] -
            (adjust_tpp_state[election][seat] if
             seat in adjust_tpp_state[election] else 0)
            for seat, tpp in tpp_list]

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

    raw_results = obtain_results(election)
    results = apply_local_assumptions(raw_results, election)

    seat_booths = parse_booth_file(election)

    calculate_deviations(config, seat_booths, results, election)


def analyse():
    try:
        config = Config()
        analyse_specific_election(config)
    except FederalStateError as e:
        print('Could not process configuration due to the following issue:')
        print(str(e))
        return


if __name__ == '__main__':
    analyse()
