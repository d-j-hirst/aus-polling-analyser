import pickle
import re
import requests

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

    def order(self):
        self.tcp.sort(key=lambda x: x.percent, reverse=True)
        self.fp.sort(key=lambda x: x.percent, reverse=True)
    
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
    print(seat_urls)
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
            print(seat_name)
            seat_results = SeatResults(seat_name)
            full_url = f'https://results.aec.gov.au/10822/Website/{url}'
            content = str(requests.get(full_url).content)
            content_fp, content_tcp = content.split('cellspacing="0" callpadding="0" width="100%">')
            while True:
                # if seat_name == 'Banks':
                    # print(content_fp)
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
                if seat_name == "Hunter":
                    print(content_tcp)
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


if __name__ == '__main__':
    election_2019 = ElectionResults('2019 Federal Election',
                                    election_2019fed_download)
    election_2016 = ElectionResults('2016 Federal Election',
                                    election_2016fed_download)
    election_2013 = ElectionResults('2013 Federal Election',
                                    election_2013fed_download)
    election_2010 = ElectionResults('2010 Federal Election',
                                    election_2010fed_download)
    election_2007 = ElectionResults('2007 Federal Election',
                                    election_2007fed_download)
    election_2004 = ElectionResults('2004 Federal Election',
                                    election_2004fed_download)
    election_2001 = ElectionResults('2001 Federal Election',
                                    election_2001fed_download)
    print(election_2001)
