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


def fetchSeatUrls(code):
    r = requests.get(f'https://results.aec.gov.au/{code}/Website/HouseDivisionalResults-{code}.htm')
    content = str(r.content)
    pattern = '<td headers="dDiv" class="filterDivision"><a href="([^"]*)">([^<]*)</a></td>'
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
        seat_urls = fetchSeatUrls(code)
        for seat_name, url in seat_urls.items():
            seat_results = SeatResults(seat_name)
            full_url = f'https://results.aec.gov.au/{code}/Website/{url}'
            content = str(requests.get(full_url).content)
            while True:
                match = re.search('<td headers="tcpCnd">([^<]*)</td>', content)
                if not match:
                    break
                name = match.group(1)
                party = re.search('<td headers="tcpPty">([^<]*)</td>', content).group(1)
                votes_str = re.search('<td headers="tcpVot" class="row-right">([^<]*)</td>', content).group(1)
                votes = int(votes_str.replace(',', ''))
                percent = float(re.search('<td headers="tcpTE" class="row-right">([^<]*)</td>', content).group(1))
                swingMatch = re.search('<td headers="tcpSwg" class="row-right">([^<]*)</td>', content)
                swing = float(swingMatch.group(1))
                candidate = CandidateResult(name=name,
                                            party=party,
                                            votes=votes,
                                            percent=percent,
                                            swing=swing) 
                seat_results.tcp.append(candidate)
                content = content[swingMatch.end():]
            while True:
                match = re.search('<td headers="fpCan">([^<]*)</td>', content)
                if not match:
                    break
                if match.group(1) == '......' or match.group(1) == "Formal":
                    break
                name = match.group(1)
                party = re.search('<td headers="fpPty">([^<]*)</td>', content).group(1)
                votes_str = re.search('<td headers="fpVot" class="row-right">([^<]*)</td>', content).group(1)
                votes = int(votes_str.replace(',', ''))
                percent = float(re.search('<td headers="fpPct" class="row-right">([^<]*)</td>', content).group(1))
                swingMatch = re.search('<td headers="fpSwg" class="row-right">([^<]*)</td>', content)
                swing = float(swingMatch.group(1))
                candidate = CandidateResult(name=name,
                                            party=party,
                                            votes=votes,
                                            percent=percent,
                                            swing=swing) 
                seat_results.fp.append(candidate)
                content = content[swingMatch.end():]
            seat_results.order()
            all_results.results.append(seat_results)
        with open(filename, 'wb') as pkl:
            pickle.dump(all_results, pkl, pickle.HIGHEST_PROTOCOL)
    return all_results.results


def election_2019fed_download():
    return modern_fed_download('24310')


def election_2016fed_download():
    return modern_fed_download('20499')


if __name__ == '__main__':
    election_2019 = ElectionResults('2019 Federal Election',
                                    election_2019fed_download)
    print(election_2019)
    election_2016 = ElectionResults('2016 Federal Election',
                                    election_2016fed_download)
    print(election_2016)
