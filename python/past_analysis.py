from copy import deepcopy
import pickle
import re
import requests
from functools import reduce


state_page = {'nsw': 'New South Wales',
              'vic': "Victoria (Australia)",
              'qld': "Queensland",
              'sa': "South Australian",
              'wa': "Western Australian",
              }

state_election_name = {'fed': 'Australian federal election',
                       'nsw': 'New South Wales state election',
                       'vic': 'Victorian state election',
                       'qld': 'Queensland state election',
                       'sa': 'South Australian state election',
                       'wa': 'Western Australian state election',
                      }

class ElectionResults:
    def __init__(self, name, download):
        self.name = name
        self.seat_results = download()

    def __repr__(self):
        repr = f'\n*** Election: {self.name} ***\n\n'
        for seat_result in self.seat_results:
            repr += f'{seat_result}\n'
        return repr


class SavedResults:
    def __init__(self):
        self.results = []

class SeatResults:
    def __init__(self, name):
        self.name = name
        self.tcp = []
        self.fp = []

    def order(self, tcp_by_percent=False):
        self.fp.sort(key=lambda x: x.votes, reverse=True)
        if not tcp_by_percent:
            self.tcp.sort(key=lambda x: x.votes, reverse=True)
        else:
            self.tcp.sort(key=lambda x: x.percent, reverse=True)

    
    def __repr__(self):
        repr = f'{self.name}\n Two-candidate preferred votes:\n'
        for tcp in self.tcp:
            repr += f'  {tcp}\n'
        repr += f'\n First preference votes:\n'
        for fp in self.fp:
            repr += f'  {fp}\n'
        return repr


class CandidateResult:
    def __init__(self, name, party, votes, percent, swing):
        self.name = name
        self.party = party
        self.votes = votes
        self.percent = percent
        self.swing = swing
    
    def __repr__(self):
        return (f'{self.name} ({self.party}) - Votes: {self.votes},'
               f' Vote %: {self.percent}, Swing: {self.swing}')


def fetch_seat_urls_modern_fed(code):
    r = requests.get(f'https://results.aec.gov.au/{code}/Website/HouseDivisionalResults-{code}.htm')
    content = str(r.content)
    pattern = r'<td headers="dDiv" class="filterDivision"><a href="([^"]*)">([^<]*)</a></td>'
    seat_urls = {}  # key is seat name, value is url
    while True:
        match = re.search(pattern, content)
        if match:
            seat_urls[match.group(2)] = match.group(1)
            content = content[match.end():]
        else:
            break
    return seat_urls


def modern_fed_download(code):
    filename = f'{code}_results.pkl'
    try:
        with open(filename, 'rb') as pkl:
            all_results = pickle.load(pkl)
    except FileNotFoundError:
        all_results = SavedResults()
        seat_urls = fetch_seat_urls_modern_fed(code)
        for seat_name, url in seat_urls.items():
            seat_results = SeatResults(seat_name)
            full_url = f'https://results.aec.gov.au/{code}/Website/{url}'
            content = str(requests.get(full_url).content)
            while True:
                match = re.search(r'<td headers="tcpCnd">([^<]*)</td>', content)
                if not match:
                    break
                name = match.group(1)
                party = re.search(r'<td headers="tcpPty">([^<]*)</td>', content).group(1)
                votes_str = re.search(r'<td headers="tcpVot" class="row-right">([^<]*)</td>', content).group(1)
                votes = int(votes_str.replace(',', ''))
                percent = float(re.search(r'<td headers="tcpTE" class="row-right">([^<]*)</td>', content).group(1))
                swing_match = re.search(r'<td headers="tcpSwg" class="row-right">([^<]*)</td>', content)
                swing = float(swing_match.group(1))
                candidate = CandidateResult(name=name,
                                            party=party,
                                            votes=votes,
                                            percent=percent,
                                            swing=swing) 
                seat_results.tcp.append(candidate)
                content = content[swing_match.end():]
            while True:
                match = re.search(r'<td headers="fpCan">([^<]*)</td>', content)
                if not match:
                    break
                if match.group(1) == '......' or match.group(1) == "Formal":
                    break
                name = match.group(1)
                party = re.search(r'<td headers="fpPty">([^<]*)</td>', content).group(1)
                votes_str = re.search(r'<td headers="fpVot" class="row-right">([^<]*)</td>', content).group(1)
                votes = int(votes_str.replace(',', ''))
                percent = float(re.search(r'<td headers="fpPct" class="row-right">([^<]*)</td>', content).group(1))
                swing_match = re.search(r'<td headers="fpSwg" class="row-right">([^<]*)</td>', content)
                swing = float(swing_match.group(1))
                candidate = CandidateResult(name=name,
                                            party=party,
                                            votes=votes,
                                            percent=percent,
                                            swing=swing) 
                seat_results.fp.append(candidate)
                content = content[swing_match.end():]
            seat_results.order()
            all_results.results.append(seat_results)
        with open(filename, 'wb') as pkl:
            pickle.dump(all_results, pkl, pickle.HIGHEST_PROTOCOL)
    return all_results.results


def fetch_seat_urls_old_fed(code):
    r = requests.get(f'https://results.aec.gov.au/{code}/Website/HouseDivisionMenu-{code}-NAT.htm')
    content = str(r.content)
    
    pattern = r'_hyperlinkDivision\d?" href="([^"]*)">([^(]*) \('
    seat_urls = {}  # key is seat name, value is url
    while True:
        match = re.search(pattern, content)
        if match:
            seat_urls[match.group(2)] = match.group(1)
            content = content[match.end():]
        else:
            break
    return seat_urls


def fetch_seat_urls_2004_fed():
    seat_urls = {}  # key is seat name, value is url
    state_codes = ['NSW', 'VIC', 'QLD', 'WA', 'SA', 'TAS', 'ACT', 'NT']
    for state_code in state_codes:
        r = requests.get(f'https://results.aec.gov.au/12246/results/HouseDivisionMenu-12246-{state_code}.htm')
        content = str(r.content)
        pattern = r'_hyperlinkDivision\d?" href="([^"]*)">([^<]*)<'
        while True:
            match = re.search(pattern, content)
            if match:
                seat_urls[match.group(2)] = match.group(1)
                content = content[match.end():]
            else:
                break
    return seat_urls


def old_fed_download(code):
    filename = f'{code}_results.pkl'
    try:
        with open(filename, 'rb') as pkl:
            all_results = pickle.load(pkl)
    except FileNotFoundError:
        all_results = SavedResults()
        seat_urls = (fetch_seat_urls_old_fed(code)
            if int(code) > 12246
            else fetch_seat_urls_2004_fed())
        for seat_name, url in seat_urls.items():
            seat_results = SeatResults(seat_name)
            folder = 'Website' if int(code) > 12246 else 'results'
            full_url = f'https://results.aec.gov.au/{code}/{folder}/{url}'
            content = str(requests.get(full_url).content)
            content_fp, content_tcp = content.split('<div id="panelTCP">')
            while True:
                match = re.search(r'<TD nowrap="nowrap" align="left">["]?([^"<]*)["<]', content_fp)
                if not match:
                    break
                if match.group(1) == '......':
                    break
                name = match.group(1)
                party = re.search(r'<TD align="left">([^<]*)</TD>', content_fp).group(1)
                stats_match = re.search(r'<TD>([^<]*)</TD>[^<]*<TD>([^<]*)</TD>[^<]*<TD>([^<]*)</TD>[^<]*', content_fp)
                votes = int(stats_match.group(1).replace(',', ''))
                percent = float(stats_match.group(2))
                swing = float(stats_match.group(3))
                candidate = CandidateResult(name=name,
                                            party=party,
                                            votes=votes,
                                            percent=percent,
                                            swing=swing) 
                seat_results.fp.append(candidate)
                content_fp = content_fp[stats_match.end():]
            while True:
                match = re.search(r'<TD nowrap="nowrap" align="left">["]?([^"<]*)["<]', content_tcp)
                if not match:
                    break
                name = match.group(1)
                party = re.search(r'<TD align="left">([^<]*)</TD>', content_tcp).group(1)
                offset = 0 if int(code) < 17496 else 1
                stats_match = re.search(r'<TD align="right">([^<]*)</TD>[^<]*' * (4 + offset), content_tcp)
                votes = int(stats_match.group(1).replace(',', ''))
                percent = float(stats_match.group(2 + offset))
                swing = float(stats_match.group(4 + offset))
                candidate = CandidateResult(name=name,
                                            party=party,
                                            votes=votes,
                                            percent=percent,
                                            swing=swing) 
                seat_results.tcp.append(candidate)
                content_tcp = content_tcp[stats_match.end():]
            seat_results.order()
            all_results.results.append(seat_results)
        with open(filename, 'wb') as pkl:
            pickle.dump(all_results, pkl, pickle.HIGHEST_PROTOCOL)
    return all_results.results


def fetch_seat_urls_2001_fed():
    seat_urls = {}  # key is seat name, value is url
    state_codes = ['nsw', 'vic', 'qld', 'wa', 'sa', 'tas', 'act', 'nt']
    for state_code in state_codes:
        r = requests.get(f'https://results.aec.gov.au/10822/Website/div_{state_code}.htm')
        content = str(r.content)
        content = content.split('<td width="150" class="line">')[0]
        pattern = r'<a href="([^"]*)">([^<]*)<'
        while True:
            match = re.search(pattern, content)
            if match:
                seat_urls[match.group(2).strip()] = match.group(1)
                content = content[match.end():]
            else:
                break
    return seat_urls

def fed_download_2001(code):
    filename = f'{code}_results.pkl'
    try:
        with open(filename, 'rb') as pkl:
            all_results = pickle.load(pkl)
    except FileNotFoundError:
        all_results = SavedResults()
        seat_urls = fetch_seat_urls_2001_fed()
        for seat_name, url in seat_urls.items():
            seat_results = SeatResults(seat_name)
            full_url = f'https://results.aec.gov.au/10822/Website/{url}'
            content = str(requests.get(full_url).content)
            content_fp, content_tcp = content.split('cellspacing="0" callpadding="0" width="100%">')
            while True:
                match = re.search(r'<TD align=left noWrap>([^<]*)<', content_fp)
                if not match:
                    break
                if match.group(1) == '.....' or match.group(1) == 'FORMAL':
                    break
                name = match.group(1)
                party = re.search(r'<TD align=left>([^<]*)</TD>', content_fp).group(1)
                stats_match = re.search(r'<TD>([^<]*)</TD>[^<]*<TD>([^<]*)</TD>[^<]*<TD>([^<]*)</TD>[^<]*', content_fp)
                votes = int(stats_match.group(1).replace(',', ''))
                percent = float(stats_match.group(2))
                swing = float(stats_match.group(3))
                candidate = CandidateResult(name=name,
                                            party=party,
                                            votes=votes,
                                            percent=percent,
                                            swing=swing) 
                seat_results.fp.append(candidate)
                content_fp = content_fp[stats_match.end():]
            while True:
                # Some really gnarly regexes in here as the 2001 results
                # have a lot of inconsistent formatting (e.g. Hunter)
                match = re.search(r'<TD align=left noWrap>(?:<b>)?([^<]*)(?:</b>)?<', content_tcp)
                if not match:
                    break
                if match.group(1) == 'No results available':
                    break
                name = match.group(1)
                party = re.search(r'<(?:TD|div align="left")>(?:<b>)?([^<]*)(?:</b>)?</(?:TD|div)>', content_tcp).group(1)
                stats_match = re.search(r'<TD align=right>(?:<b>)?([^%]*)%(?:</b>)?</TD>[^<]*' * 3, content_tcp)
                if stats_match is None:
                    stats_match = re.search(r'<TD align="?right"?>(?:<b>)?([^-<%]*)%?(?:</b>)?</TD>', content_tcp)
                    swing = None
                else:
                    swing = float(stats_match.group(3))
                percent = float(stats_match.group(1))
                votes = None
                candidate = CandidateResult(name=name,
                                            party=party,
                                            votes=votes,
                                            percent=percent,
                                            swing=swing)
                seat_results.tcp.append(candidate)
                content_tcp = content_tcp[stats_match.end():]
            seat_results.order()
            all_results.results.append(seat_results)
        with open(filename, 'wb') as pkl:
            pickle.dump(all_results, pkl, pickle.HIGHEST_PROTOCOL)
    return all_results.results


def fed_download_psephos_archive(year):
    filename = f'{year}_results.pkl'
    try:
        with open(filename, 'rb') as pkl:
            all_results = pickle.load(pkl)
    except FileNotFoundError:
        single_divider = '-' * 68
        all_results = SavedResults()
        menu_url = f'http://psephos.adam-carr.net/countries/a/australia/{year}/{year}reps.shtml'
        content = str(requests.get(menu_url).content)
        split_content = content.split('<li class="data_desc">')[1:]
        urls = ['http://psephos.adam-carr.net' + 
            re.search(r'<a href="([^"]*)"', a).group(1)
            for a in split_content]
        for url in urls:
            state_content = str(requests.get(url).content)
            state_content = state_content.replace('\\r','\r').replace('\\n','\n').replace('\\x92',"'")
            state_content = re.search(r'VOTING BY DIVISION\s*=+[^=]+=+\s*([\s\S]*)', state_content).group(1)
            seats = []
            while True:
                seat_match = re.search(r'([^\n]+\s+=+[^=]*)(?:\n[^\n=]+\s+=|[^=]*$)', state_content)
                if seat_match is None:
                    break
                seatData = seat_match.group(1)
                seats.append(seatData)
                state_content = state_content[seat_match.end(1):]
            for seat in seats:
                # "Northern Territory" seat skips the state name,
                # so add another word so that it parses properly
                seat_name = ' '.join(seat.split('\n')[0].strip().replace("TERRITORY",'TERRITORY t').split(' ')[:-1]).replace(',','').title()
                seat_results = SeatResults(seat_name)
                if 'Total' in seat:
                    fp_content = seat.split('Candidate')[1].split('Total')[0].split(single_divider)[-2]
                    fp_lines = fp_content.split('\r\n')[1:-1]
                    for line in fp_lines:
                        name = line[:31].replace('*', '').replace('+', '').strip().title()
                        if name == 'Anthony Peterson' and seat_name == 'Gippsland' and year == '1983':
                            name = 'Anthony Petersen'
                        party = line[31:40].strip()
                        if len(party) == 0:
                            party = "IND"
                        votes_str = line[40:48].strip().replace(',','').replace('.','').replace(' ','')
                        if len(votes_str) == 0:
                            continue
                        votes = int(votes_str)
                        percent = float(line[48:54].strip())
                        swing_str = line[54:].strip()
                        if len(swing_str) == 0:
                            swing = None
                        else:
                            swing = float(swing_str)
                        candidate = CandidateResult(name=name,
                                                    party=party,
                                                    votes=votes,
                                                    percent=percent,
                                                    swing=swing)                        
                        seat_results.fp.append(candidate)
                    if seat.count('Total') > 1:
                        tcp_content = seat.split('Total')[-2].split(single_divider)[-2]
                        tcp_lines = tcp_content.split('\r\n')[1:-1]
                        for line in tcp_lines:
                            name = line[:17].replace('*', '').replace('+', '').strip().title()
                            if name == '(Exhausted':
                                break
                            if name == 'King' and seat_name == 'Griffith' and year == '1993':
                                continue
                            if name == 'Proctor' and seat_name == 'Forrest' and year == '1993':
                                name = 'Procter'
                            # print(seat_name)
                            # print(name)
                            # print(seat_results.fp)
                            # print('-----')
                            candidate_list = [a for a in seat_results.fp
                                            if name.split(' ')[-1].lower() ==
                                            a.name.split(' ')[-1].lower()]
                            candidate_match = candidate_list[0]
                            name = candidate_match.name
                            party = candidate_match.party
                            votes_str = line[40:48].strip().replace(',','').replace('.','')
                            votes = int(votes_str)
                            percent = float(line[48:54].strip())
                            swing = None
                            candidate = CandidateResult(name=name,
                                                        party=party,
                                                        votes=votes,
                                                        percent=percent,
                                                        swing=swing)                        
                            seat_results.tcp.append(candidate)
                    else:
                        seat_results.tcp = deepcopy(seat_results.fp)
                        for a in seat_results.tcp:
                           a.swing = None
                    seat_results.order()
                all_results.results.append(seat_results)
        with open(filename, 'wb') as pkl:
            pickle.dump(all_results, pkl, pickle.HIGHEST_PROTOCOL)
    return all_results.results


def nsw_download_psephos_archive(year):

    filename = f'{year}nsw_results.pkl'
    try:
        with open(filename, 'rb') as pkl:
            all_results = pickle.load(pkl)
    except FileNotFoundError:
        length = 68 if int(year) > 2003 else 67
        single_divider = '-' * length
        double_divider = '=' * length
        all_results = SavedResults()
        suffix = ('2' if int(year) > 2003 else 
                  ('1' if int(year) > 1999 else '-1'))
        menu_url = f'http://psephos.adam-carr.net/countries/a/australia/states/nsw/nsw{year}{suffix}.txt'
        content = str(requests.get(menu_url).content)
        content = content.replace('\\r','\r').replace('\\n','\n').replace('\\x92',"'")
        seats_marker = ('VOTING BY CONSTITUENCY'
                        if int(year) > 2007
                        else 'VOTING BY DISTRICT')
        content = re.search(seats_marker + r'\s*=+\s*([\s\S]*)', content).group(1)
        seats = []
        while True:
            seat_match = re.search(r'([^\n]+\s+=+[^=]*)(?:\n[^\n=]+\s+=|[^=]*$)', content)
            if seat_match is None:
                break
            seatData = seat_match.group(1)
            seats.append(seatData)
            content = content[seat_match.end(1):]
        for seat in seats:
            seat = seat.replace('FINAL COUNT', 'Final count')
            seat_name = seat.split('\n')[0].split('  ')[0].strip().replace(',','').title()
            seat_results = SeatResults(seat_name)
            if 'Total' in seat:
                fp_content = seat.split('Candidate')[1].split('Total')[0].split(single_divider)[1]
                separator = '\r\n' if int(year) > 2011 else '\n'
                offset = 0 if int(year) > 2011 else -10
                fp_lines = fp_content.split(separator)[1:-1]
                for line in fp_lines:
                    name = line[:25].replace('*', '').replace('+', '').strip().title()
                    party = line[25:48 + offset].strip()
                    if len(party) == 0:
                        party = "IND"
                    votes_str = line[48 + offset:56 + offset].strip().replace(',','').replace('.','').replace(' ','')
                    if len(votes_str) == 0:
                        continue
                    # Account for error in 2019 election page
                    line = line.replace('0-1.5','-01.5')
                    votes = int(votes_str)
                    percent_str = line[56 + offset:61 + offset].strip()
                    if len(percent_str) == 0:
                        percent = None
                    else:
                        percent = float(percent_str)
                    swing_str = line[61 + offset:].strip().replace('(', '').replace(')', '')
                    if len(swing_str) == 0:
                        swing = None
                    else:
                        swing = float(swing_str)
                    candidate = CandidateResult(name=name,
                                                party=party,
                                                votes=votes,
                                                percent=percent,
                                                swing=swing)                        
                    seat_results.fp.append(candidate)
                tcp_split = ('Two-candidate preferred'
                             if int(year) > 2011
                             else 'Final count')
                tcp_content = seat.split(tcp_split)[1].split('Total')[0].split(single_divider)[1]
                tcp_lines = tcp_content.split(separator)[1:-1]
                for line in tcp_lines:
                    name = line[:25].replace('*', '').replace('+', '').strip().title()
                    party = line[25:48 + offset].strip()
                    if len(party) == 0:
                        party = "IND"
                    votes_str = line[48 + offset:56 + offset].strip().replace(',','').replace('.','').replace(' ','')
                    if len(votes_str) == 0:
                        continue
                    votes = int(votes_str)
                    percent = float(line[56 + offset:61 + offset].strip())
                    swing_str = line[61 + offset:].strip().replace('(', '').replace(')', '')
                    if len(swing_str) == 0:
                        swing = None
                    else:
                        swing = float(swing_str)
                    candidate = CandidateResult(name=name,
                                                party=party,
                                                votes=votes,
                                                percent=percent,
                                                swing=swing)                        
                    seat_results.tcp.append(candidate)
                seat_results.order()
            all_results.results.append(seat_results)
        with open(filename, 'wb') as pkl:
            pickle.dump(all_results, pkl, pickle.HIGHEST_PROTOCOL)
    return all_results.results


def nsw_download_psephos_archive_1999():
    filename = f'1999nsw_results.pkl'
    try:
        with open(filename, 'rb') as pkl:
            all_results = pickle.load(pkl)
    except FileNotFoundError:
        length = 67
        single_divider = '-' * length
        double_divider = '=' * length
        all_results = SavedResults()
        menu_url = 'http://psephos.adam-carr.net/countries/a/australia/states/nsw/nsw-1999-1.txt'
        content = str(requests.get(menu_url).content)
        content = content.replace('\\r','\r').replace('\\n','\n').replace('\\x92',"'")
        content = re.search(r'District[^\n]+\s*=+\s*([\s\S]*)', content).group(1)
        seats = []
        while True:
            seat_match = re.search(r'([\s\S]*?\w[\s\S]*?)(?:\n\s+\n)', content)
            if seat_match is None:
                break
            seatData = seat_match.group(1)
            seats.append(seatData)
            content = content[seat_match.end(1):]
        for seat in seats:
            seat_name = seat.split('(')[0].strip().title()
            seat_results = SeatResults(seat_name)
            fp_content = seat.split(single_divider)[1]
            fp_lines = fp_content.split('\r\n')[1:-1]
            for line in fp_lines:
                name = line[:25].replace('*', '').replace('+', '').strip().title()
                party = line[25:32].strip()
                if len(party) == 0:
                    party = "IND"
                votes_str = line[32:40].strip().replace(',','').replace('.','').replace(' ','')
                if len(votes_str) == 0:
                    continue
                votes = int(votes_str)
                percent_str = line[40:46].strip()
                if len(percent_str) == 0:
                    percent = None
                else:
                    percent = float(percent_str)
                swing = None
                fp_candidate = CandidateResult(name=name,
                                               party=party,
                                               votes=votes,
                                               percent=percent,
                                               swing=swing)                        
                seat_results.fp.append(fp_candidate)
                tcp_votes_str = line[46:53].strip().replace(',','').replace('.','').replace(' ','')
                if len(tcp_votes_str) > 0:
                    tcp_votes = int(tcp_votes_str)
                    tcp_percent_str = line[53:58].strip().replace(',','').replace(' ','')
                    tcp_percent = float(tcp_percent_str)
                    tcp_swing_str = line[58:].strip().replace(',','').replace(' ','').replace('(','').replace(')','')
                    if len(tcp_swing_str) > 0:
                        tcp_swing = float(tcp_swing_str)
                    else:
                        tcp_swing = None
                    tcp_candidate = CandidateResult(name=name,
                                                    party=party,
                                                    votes=tcp_votes,
                                                    percent=tcp_percent,
                                                    swing=tcp_swing)                   
                    seat_results.tcp.append(tcp_candidate)
            seat_results.order()
            all_results.results.append(seat_results)
        # with open(filename, 'wb') as pkl:
        #     pickle.dump(all_results, pkl, pickle.HIGHEST_PROTOCOL)
    return all_results.results


def nsw_download_psephos_archive_early(year):
    filename = f'{year}nsw_results.pkl'
    try:
        with open(filename, 'rb') as pkl:
            all_results = pickle.load(pkl)
    except FileNotFoundError:
        all_results = SavedResults()
        tcp_offset = 0 if year == '1995' else -5
        newline = '\r\n' if year == '1995' else '\n'
        menu_url = ('http://psephos.adam-carr.net/countries/a/australia/states/nsw/1995-nsw-assembly.txt'
                    if year == '1995' else
                    'http://psephos.adam-carr.net/countries/a/australia/states/nsw/nsw-91-assembly.txt')
        content = str(requests.get(menu_url).content)
        content = content.replace('\\r','\r').replace('\\n','\n').replace('\\x92',"'").replace('\\xb9',"'")
        content = re.search(r'\n\s*\n([\s\S]*)', content).group(1)
        seats = []
        while True:
            seat_match = re.search(r'([\s\S]*?\d\s*)\n\s*\n[\s\S]*', content)
            if seat_match is None:
                break
            seatData = seat_match.group(1)
            seats.append(seatData)
            content = content[seat_match.end(1):]
        for seat in seats:
            seat_name = seat.split('(')[0].strip().title()
            seat_results = SeatResults(seat_name)
            seat_lines = seat.split(newline)[2:-1]
            for line in seat_lines:
                name = line[:25].replace('*', '').replace('+', '').strip().title()
                party = line[25:32].strip()
                if len(party) == 0:
                    party = "IND"
                votes_str = line[32:40].strip().replace(',','').replace('.','').replace(' ','')
                if len(votes_str) == 0:
                    continue
                votes = int(votes_str)
                percent_str = line[40:45].strip()
                if len(percent_str) == 0:
                    percent = None
                else:
                    percent = float(percent_str)
                swing_str = line[45:53].strip().replace('(','').replace(')','')
                if len(swing_str) == 0 or year == '1991':
                    swing = None
                else:
                    swing = float(swing_str)
                swing = None
                fp_candidate = CandidateResult(name=name,
                                               party=party,
                                               votes=votes,
                                               percent=percent,
                                               swing=swing)                        
                seat_results.fp.append(fp_candidate)
                tcp_percent_str = line[52 + tcp_offset:58 + tcp_offset].strip().replace(',','').replace(' ','')
                if len(tcp_percent_str) > 0:
                    tcp_percent = float(tcp_percent_str)
                    tcp_swing_str = line[58 + tcp_offset:].strip().replace(',','').replace(' ','').replace('(','').replace(')','')
                    if len(tcp_swing_str) > 0:
                        tcp_swing = float(tcp_swing_str)
                    else:
                        tcp_swing = None
                    tcp_candidate = CandidateResult(name=name,
                                                    party=party,
                                                    votes=None,
                                                    percent=tcp_percent,
                                                    swing=tcp_swing)                   
                    seat_results.tcp.append(tcp_candidate)
            seat_results.order(tcp_by_percent=True)
            all_results.results.append(seat_results)
        with open(filename, 'wb') as pkl:
            pickle.dump(all_results, pkl, pickle.HIGHEST_PROTOCOL)
    return all_results.results


def fetch_seat_urls_vic(code):
    summary = ('summary' if int(code) > 2010
               else f'state{code}resultsummary')
    url = f'https://itsitecoreblobvecprd.blob.core.windows.net/public-files/historical-results/state{code}/{summary}.html'
    r = requests.get(url)
    content = str(r.content)
    content = content.replace('  results', ' results')
    content = content.lower().split('individual lower house results')[1].split('overall upper house results')[0]
    pattern = r'<a href="([^"]*)">([^<]*)<'
    seat_urls = {}  # key is seat name, value is url
    while True:
        match = re.search(pattern, content)
        if match:
            seat_urls[match.group(2).title().split(' District')[0]] = match.group(1)
            content = content[match.end():]
        else:
            break
    return seat_urls


def vic_download(code):
    filename = f'{code}vic_results.pkl'
    try:
        with open(filename, 'rb') as pkl:
            all_results = pickle.load(pkl)
    except FileNotFoundError:
        all_results = SavedResults()
        seat_urls = fetch_seat_urls_vic(code)
        for seat_name, url in seat_urls.items():
            seat_results = SeatResults(seat_name)
            full_url = f'https://itsitecoreblobvecprd.blob.core.windows.net/public-files/historical-results/state{code}/{url}'
            content = str(requests.get(full_url).content)
            content = content.replace('\\r','\r').replace('\\n','\n').replace("\\'","'").replace("&amp;","&")
            tcp_split = ('<h3>Results after distribution'
                if '<h3>Results after distribution' in content
                else '<h3>Two candidate preferred')
            fp_content = content.split('first preference votes')[1].split(tcp_split)[0]
            pattern = r'<td>([^<]*)<[^<]*<td(?: colspan=4)?>([^<]*)<[^<]*<td>([^<]*)<[\s\S]*?width="([^"]*)"'
            while True:
                match = re.search(pattern, fp_content)
                if not match:
                    break
                name = match.group(1).strip().title()
                party = match.group(2).strip().title()
                party = party if len(party) > 0 else 'IND'
                votes = int(match.group(3))
                percent = float(match.group(4))
                fp_candidate = CandidateResult(name=name,
                                            party=party,
                                            votes=votes,
                                            percent=percent,
                                            swing=None)
                seat_results.fp.append(fp_candidate)
                fp_content = fp_content[match.end():]
            tcp_link = re.search(r'a href="([^"]*)">(?:Click here for )?[Tt]wo candidate preferred', content).group(1)
            tcp_url = f'https://itsitecoreblobvecprd.blob.core.windows.net/public-files/historical-results/state{code}/{tcp_link}'
            tcp_content = str(requests.get(tcp_url).content)
            tcp_content = tcp_content.replace('\\r','\r').replace('\\n','\n').replace("\\'","'").replace("&amp;","&")
            names = re.search(r'candidate1">([^<]*)<[^c]*candidate2">([^<]*)<', tcp_content)
            name_list = [names.group(1).strip().title(), names.group(2).strip().title()]
            parties = re.search(r'Voting [Cc]entres?\s*<[^<]*<th[^>]*>([^<]*)<[^<]*<th[^>]*>([^<]*)<', tcp_content)
            party_list = [parties.group(1).strip().title(), parties.group(2).strip().title()]
            party_list = [a if len(a) > 0 else 'IND' for a in party_list]
            votes = re.search(r'>Total<[^<]*<td[^>]*>([^<]*)<[^<]*<td[^>]*>([^<]*)<', tcp_content)
            votes_list = [int(votes.group(1)), int(votes.group(2))]
            percent = re.search(r'polled by candidate<[^<]*<td[^>]*>([^<%]*)%\s*<[^<]*<td[^>]*>([^<%]*)%\s*<', tcp_content)
            percent_list = [float(percent.group(1)), float(percent.group(2))]
            for i in (0, 1):
                tcp_candidate = CandidateResult(name=name_list[i],
                                                party=party_list[i],
                                                votes=votes_list[i],
                                                percent=percent_list[i],
                                                swing=None)
                seat_results.tcp.append(tcp_candidate)
            seat_results.order()
            all_results.results.append(seat_results)
        with open(filename, 'wb') as pkl:
            pickle.dump(all_results, pkl, pickle.HIGHEST_PROTOCOL)
    return all_results.results


def vic_download_psephos(year):
    filename = f'{year}vic_results.pkl'
    try:
        with open(filename, 'rb') as pkl:
            all_results = pickle.load(pkl)
    except FileNotFoundError:
        length = 68
        single_divider = '-' * length
        double_divider = '=' * length
        all_results = SavedResults()
        menu_url = f'http://psephos.adam-carr.net/countries/a/australia/states/vic/historic/{year}assembly.txt'
        content = str(requests.get(menu_url).content)
        content = content.replace('\\r','\r').replace('\\n','\n').replace('\\x92',"'")
        seats_marker = 'VOTING BY CONSTITUENCY'
        content = re.search(seats_marker + r'\s*=+\s*([\s\S]*)', content).group(1)
        content = content.split('BY-ELECTIONS')[0]
        seats = []
        while True:
            seat_match = re.search(r'([^\n]+\s+=+[^=]*)(?:\n[^\n=]+\s+=|[^=]*$)', content)
            if seat_match is None:
                break
            seatData = seat_match.group(1)
            seats.append(seatData)
            content = content[seat_match.end(1):]
        for seat in seats:
            seat_name = seat.split('\n')[0].split('  ')[0].strip().replace(',','').title()
            if int(year) < 1996 and int(year) > 1985:
                if 'distributed' in seat or 'challenged' in seat:
                    tcp_match = re.search(r'(?:distributed|challenged)[^>]*>([^\n]*)\r\n[^>]*$', seat)
                    tcp_test = re.search(r'(?:distributed|challenged)([^>]*)>[^>]*$', seat).group(1)
                    candidate_lines = len(re.findall('\n[^->\d\r\n\()]', tcp_test))
                    if candidate_lines > 2 and tcp_match.group(1)[-2] != 'e':
                        insertion_point = tcp_match.end(1)
                        seat = seat[:insertion_point] + ' e' + seat[insertion_point:]
                else:
                    tcp_match = re.search(r'(?:informal)([^\n]*)\r\n', seat)
                    insertion_point = tcp_match.end(1)
                    seat = seat[:insertion_point] + ' e' + seat[insertion_point:]
            seat_results = SeatResults(seat_name)
            if 'informal' in seat:
                fp_content = seat.split('Candidate')[1].split('informal')[0].split(single_divider)[1]
                separator = '\r\n'
                offset = 0
                fp_lines = fp_content.split(separator)[1:-1]
                for line in fp_lines:
                    name = line[:26].replace('*', '').replace('+', '').strip().title()
                    if name == '(Challenged Votes':
                        continue
                    party = line[26:46 + offset].strip()
                    if len(party) == 0:
                        party = "IND"
                    votes_str = line[46 + offset:54 + offset].strip().replace(',','').replace('.','').replace(' ','')
                    if len(votes_str) == 0:
                        continue
                    votes = int(votes_str)
                    percent_str = line[54 + offset:59 + offset].strip()
                    if len(percent_str) == 0:
                        percent = None
                    else:
                        percent = float(percent_str)
                    swing_str = line[60 + offset:].strip().replace('(', '').replace(')', '')
                    if len(swing_str) == 0:
                        swing = None
                    else:
                        swing = float(swing_str)
                    candidate = CandidateResult(name=name,
                                                party=party,
                                                votes=votes,
                                                percent=percent,
                                                swing=swing)
                    seat_results.fp.append(candidate)
                seat_results.order()
                if len(seat_results.fp) == 2:
                    seat_results.tcp = deepcopy(seat_results.fp)
                    all_results.results.append(seat_results)
                    continue

                # Correction for erroneous 1999 Williamstown entry
                seat = seat.replace('30,686  16.7', '30,686  18.5 e')
                # Corrections for erroneous 1992 Essendon entry
                seat = seat.replace('14,429  51.2', '14,429  48.7')
                seat = seat.replace('15,159  48.7', '15,159  51.2')

                tcp_est = re.search(' ([\d,]+)\s+([\d.]+) +e\r\n', seat)
                primary_leader = seat_results.fp[0]
                primary_second = seat_results.fp[1]
                if tcp_est is not None:
                    # If there is an estimate, there won't be an
                    # official two-party preferred result, and vice versa
                    # Give the two-party-preferred to the leaders in
                    # primary votes
                    leader_tpp = round(float(tcp_est.group(2)) + 50, 1)
                    total_votes = int(tcp_est.group(1).replace(',',''))
                    leader_votes = int(leader_tpp * total_votes / 100)
                    lead_tcp = CandidateResult(name=primary_leader.name,
                                               party=primary_leader.party,
                                               votes=leader_votes,
                                               percent=leader_tpp,
                                               swing=None)                     
                    seat_results.tcp.append(lead_tcp)
                    second_tcp = CandidateResult(name=primary_second.name,
                                                 party=primary_second.party,
                                                 votes=total_votes - leader_votes,
                                                 percent=round(100 - leader_tpp, 1),
                                                 swing=None)                     
                    seat_results.tcp.append(second_tcp)
                else:
                    # If there is no estimate, look for the final distribution
                    # of preferences
                    tcp_results = re.search(r'(?:distributed|challenged)([^>]*)>[^>]*$', seat).group(1)
                    for line in tcp_results.split('\n')[2:-2]:
                        name = line[:17].replace('*', '').replace('+', '').strip().title()
                        if name == '(Exhausted' or name == "Informal":
                            continue
                        votes_str = line[46 + offset:54 + offset].strip().replace(',','').replace('.','').replace(' ','')
                        if len(votes_str) == 0:
                            continue
                        votes = int(votes_str)
                        percent_str = line[54 + offset:59 + offset].strip()
                        if len(percent_str) == 0:
                            percent = None
                        else:
                            percent = float(percent_str)
                        swing_str = line[59 + offset:].strip().replace('(', '').replace(')', '')
                        if len(swing_str) == 0:
                            swing = None
                        else:
                            swing = float(swing_str)
                        party = 'IND'
                        for fp_c in seat_results.fp:
                            if name.lower() in fp_c.name.lower():
                                name = fp_c.name
                                party = fp_c.party
                        candidate = CandidateResult(name=name,
                                                    party=party,
                                                    votes=votes,
                                                    percent=percent,
                                                    swing=swing)               
                        seat_results.tcp.append(candidate)
                seat_results.order()
            all_results.results.append(seat_results)
        with open(filename, 'wb') as pkl:
            pickle.dump(all_results, pkl, pickle.HIGHEST_PROTOCOL)
    return all_results.results    


def fetch_seat_urls_state(state):
    seat_urls = {}  # key is seat name, value is url
    if state == 'fed':
        category_url = f'https://en.wikipedia.org/wiki/Category:Australian_federal_electoral_results_by_division'
        content_category = str(requests.get(category_url).content)
        content_category = content_category.split('div class="mw-category"')[1].split('<noscript>')[0]
        category_pattern = r'<a href="([^"?]*)"[^>]*>[^>]*Division of ([^<]*)<'
        matches_category = re.findall(category_pattern, content_category)
        seat_urls.update({match[1].split(" (")[0]: match[0] for match in matches_category})
        category_url = f'https://en.wikipedia.org/w/index.php?title=Category:Australian_federal_electoral_results_by_division&pagefrom=Perth%0AElectoral+results+for+the+Division+of+Perth#mw-pages'
        content_category = str(requests.get(category_url).content)
        content_category = content_category.split('div class="mw-category"')[1].split('<noscript>')[0]
        category_pattern = r'<a href="([^"?]*)"[^>]*>[^>]*Division of ([^<]*)<'
        matches_category = re.findall(category_pattern, content_category)
        seat_urls.update({match[1].split(" (")[0]: match[0] for match in matches_category})
    else:
        category_url = f'https://en.wikipedia.org/wiki/Category:{state_page[state]}_state_electoral_results_by_district'
        content_category = str(requests.get(category_url).content)
        content_category = content_category.split('div class="mw-category"')[1].split('<noscript>')[0]
        category_pattern = r'<a href="([^"?]*)"[^>]*>[^>]*district of ([^<]*)<'
        matches_category = re.findall(category_pattern, content_category)
        seat_urls.update({match[1].split(" (")[0]: match[0] for match in matches_category})
    return seat_urls


def generic_download(state, year):
    filename = f'{year}{state}_results.pkl'
    try:
        with open(filename, 'rb') as pkl:
            all_results = pickle.load(pkl)
    except FileNotFoundError:
        all_results = SavedResults()
        seat_urls = fetch_seat_urls_state(state)
        for seat_name, url in seat_urls.items():
            print(seat_name)
            seat_results = SeatResults(seat_name)
            full_url = f'https://en.wikipedia.org/{url}'
            content = str(requests.get(full_url).content)
            content = content.replace('\\r','\r').replace('\\n','\n').replace("\\'","'")
            content = content.replace('&amp;','&').replace('\\xe2\\x88\\x92', '-')
            content = content.replace('\\xe2\\x80\\x93', '-')
            election_marker = f'>{year} {state_election_name[state]}<'
            if election_marker not in content:
                continue
            election_content = content.split(election_marker)[1].split('</table>')[0]
            if '>Two-' in election_content:
                fp_content = election_content.split('>Two-')[0]
                tcp_content = election_content.split('>Two-')[-1]
            else:
                fp_content = election_content
                tcp_content = None
            pattern = (r'<tr class="vcard"[\s\S]*?class="org"[\s\S]*?>([^<]+)<'
                          + r'[\s\S]*?class="fn"[\s\S]*?>([^<]+)<'
                          + r'[\s\S]*?<td[\s\S]*?>([^<]+)<' * 3)
            fp_matches = re.findall(pattern, fp_content)
            for match in fp_matches:
                if len(match[3].strip()) == 0:
                    continue
                if len(match[4].strip()) > 0:
                    swing = float(match[4].strip())
                else:
                    swing = None
                seat_results.fp.append(CandidateResult(
                    name=match[0],
                    party=match[1].strip(),
                    votes=int('0'+match[2].replace(',','').replace('.','').strip()),
                    percent=float(match[3].strip()),
                    swing=swing))
            if tcp_content is not None:
                tcp_matches = re.findall(pattern, tcp_content)
                for match in tcp_matches:
                    swing_str = match[4].replace('N/A','').strip()
                    if len(swing_str) > 0:
                        swing = float(swing_str)
                    else:
                        swing = None
                    seat_results.tcp.append(CandidateResult(
                        name=match[0],
                        party=match[1].strip(),
                        votes=int('0'+match[2].replace(',','').replace('.','').strip()),
                        percent=float(match[3].strip()),
                        swing=swing))
                if seat_name == 'Barambah' and year == 1989:
                    seat_results.tcp[0].votes = 8497
                    seat_results.tcp[1].votes = 3404
                elif seat_name == 'Bowen' and year == 1989:
                    seat_results.tcp[0].votes = 7524
                    seat_results.tcp[1].votes = 3134
                elif state == 'nsw' and year <= 1984 and seat_results.tcp[0].votes == 0:
                    total_votes = sum(x.votes for x in seat_results.fp)
                    seat_results.tcp[0].votes = round(seat_results.tcp[0].percent * total_votes)
                    seat_results.tcp[1].votes = round(seat_results.tcp[1].percent * total_votes)
                elif ((seat_name == 'Pearce' and year == 2001) or
                      (seat_name == 'Newcastle' and year == 1987) or
                      (seat_name == 'Adelaide' and year == 1983) or
                      (seat_name == 'Ballarat' and year == 1983)):
                    total_votes = sum(x.votes for x in seat_results.fp)
                    seat_results.tcp[0].votes = round(seat_results.tcp[0].percent * total_votes)
                    seat_results.tcp[1].votes = round(seat_results.tcp[1].percent * total_votes)
                elif seat_results.tcp[0].votes == 0:
                    raise ValueError('Missing votes data - needs attention')
                try:
                    if (abs(seat_results.tcp[0].swing - seat_results.tcp[0].percent) < 0.01
                        or abs(seat_results.tcp[1].swing - seat_results.tcp[1].percent) < 0.01):
                        seat_results.tcp[0].swing = None
                        seat_results.tcp[1].swing = None
                except TypeError:
                    pass  # If one of the above values is none,
                          # it's fine to just skip the check altogether
            else:
                seat_results.tcp = seat_results.fp
            seat_results.order()
            all_results.results.append(seat_results)
        with open(filename, 'wb') as pkl:
            pickle.dump(all_results, pkl, pickle.HIGHEST_PROTOCOL)
    return all_results.results


def election_2019fed_download():
    return modern_fed_download('24310')


def election_2016fed_download():
    return modern_fed_download('20499')


def election_2013fed_download():
    return old_fed_download('17496')


def election_2010fed_download():
    return old_fed_download('15508')


def election_2007fed_download():
    return old_fed_download('13745')


def election_2004fed_download():
    return old_fed_download('12246')


def election_2001fed_download():
    return fed_download_2001('10822')


def election_1998fed_download():
    return fed_download_psephos_archive('1998')


def election_1996fed_download():
    return fed_download_psephos_archive('1996')


def election_1993fed_download():
    return fed_download_psephos_archive('1993')


def election_1990fed_download():
    return fed_download_psephos_archive('1990')


def election_1987fed_download():
    return fed_download_psephos_archive('1987')


def election_1984fed_download():
    return fed_download_psephos_archive('1984')


def election_1983fed_download():
    return fed_download_psephos_archive('1983')


def election_1980fed_download():
    return fed_download_psephos_archive('1980')


def election_1977fed_download():
    return fed_download_psephos_archive('1977')


def election_1975fed_download():
    return fed_download_psephos_archive('1975')


def election_2019nsw_download():
    return nsw_download_psephos_archive('2019')


def election_2015nsw_download():
    return nsw_download_psephos_archive('2015')


def election_2011nsw_download():
    return nsw_download_psephos_archive('2011')


def election_2007nsw_download():
    return nsw_download_psephos_archive('2007')


def election_2003nsw_download():
    return nsw_download_psephos_archive('2003')


def election_1999nsw_download():
    return nsw_download_psephos_archive_1999()


def election_1995nsw_download():
    return nsw_download_psephos_archive_early('1995')


def election_1991nsw_download():
    return nsw_download_psephos_archive_early('1991')


def election_2018vic_download():
    return vic_download('2018')


def election_2014vic_download():
    return vic_download('2014')


def election_2010vic_download():
    return vic_download('2010')


def election_2006vic_download():
    return vic_download('2006')


def election_2002vic_download():
    return vic_download_psephos('2002')


def election_1999vic_download():
    return vic_download_psephos('1999')


def election_1996vic_download():
    return vic_download_psephos('1996')


def election_1992vic_download():
    return vic_download_psephos('1992')


def election_1988vic_download():
    return vic_download_psephos('1988')


def election_1985vic_download():
    return vic_download_psephos('1985')


def election_1982vic_download():
    return vic_download_psephos('1982')


if __name__ == '__main__':
    fed_years = [2019, 2016, 2013, 2010, 2007, 2004, 2001, 1998, 1996,
                 1993, 1990, 1987, 1984, 1983, 1980]
    fed_elections = {year: ElectionResults(f'{year} Federal Election',
                                           lambda: generic_download('fed', year))
                     for year in fed_years}
    nsw_years = [2019, 2015, 2011, 2007, 2003, 1999, 1995, 1991, 1988,
                 1984, 1981]
    nsw_elections = {year: ElectionResults(f'{year} NSW Election',
                                           lambda: generic_download('nsw', year))
                     for year in nsw_years}
    vic_years = [2018, 2014, 2010, 2006, 2002, 1999, 1996, 1992, 1988,
                 1985, 1982]
    vic_elections = {year: ElectionResults(f'{year} VIC Election',
                                           lambda: generic_download('vic', year))
                     for year in vic_years}
    qld_years = [2020, 2017, 2015, 2012, 2009, 2006, 2004, 2001, 1998,
                 1995, 1992, 1989, 1986, 1983, 1980]
    qld_elections = {year: ElectionResults(f'{year} QLD Election',
                                           lambda: generic_download('qld', year))
                     for year in qld_years}
    sa_years = [2018, 2014, 2010, 2006, 2002, 1997, 1993, 1989, 1985,
                 1982]
    sa_elections = {year: ElectionResults(f'{year} SA Election',
                                           lambda: generic_download('sa', year))
                     for year in sa_years}
    wa_years = [2021, 2017, 2013, 2008, 2005, 2001, 1996, 1993, 1989,
                 1986, 1983, 1980]
    wa_elections = {year: ElectionResults(f'{year} WA Election',
                                           lambda: generic_download('wa', year))
                     for year in wa_years}
    print(fed_elections[2019])
