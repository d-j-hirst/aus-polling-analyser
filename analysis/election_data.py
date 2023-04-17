import pickle
import re
import requests
from election_code import ElectionCode


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

previous_names = [
    {'Newtown', 'Marrickville'}
]

class ElectionResults:
    def __init__(self, name, download):
        self.name = name
        self.seat_results = download()
        self.calculate_totals_by_party()

    def __repr__(self):
        repr = f'\n*** Election: {self.name} ***\n\n'
        for seat_result in self.seat_results:
            repr += f'{seat_result}\n'
        return repr
    
    def seat_names(self, include_name_changes=False):
        names = [a.name for a in self.seat_results]
        extra_names = []
        if include_name_changes:
            for name in names:
                for name_set in previous_names:
                    if name in name_set:
                        extra_names += [a for a in name_set
                                        if a != name]
        return names + extra_names
    
    def seat_by_name(self, name, include_name_changes=False):
        if include_name_changes:
            for actual_name in [a.name for a in self.seat_results]:
                if name == actual_name:
                    return self.seat_by_name(actual_name,
                                             include_name_changes=False)
                for name_set in previous_names:
                    if ((name in name_set and actual_name in name_set)
                        or name == actual_name):
                        return self.seat_by_name(actual_name,
                                                include_name_changes=False)
        else:
            return next((a for a in self.seat_results
                    if a.name == name), None)
    
    # return total count of fp votes in this election
    def total_fp_votes(self):
        return self.total_votes
    
    # return total count of fp votes in this election
    def total_fp_votes_party(self, party):
        if party not in self.fp_by_party:
            return 0
        return self.fp_by_party[party]
    
    # return total percentage of fp votes in this election
    # going to the given party
    def total_fp_percentage_party(self, party):
        return self.total_fp_votes_party(party) / self.total_fp_votes() * 100
    
    def calculate_totals_by_party(self):
        self.fp_by_party = {}
        self.candidates_by_party = {}
        self.total_votes = 0
        for seat in self.seat_results:
            for candidate in seat.fp:
                self.total_votes += candidate.votes
                if candidate.party in self.fp_by_party:
                    self.fp_by_party[candidate.party] += candidate.votes
                    self.candidates_by_party[candidate.party] += 1
                else:
                    self.fp_by_party[candidate.party] = candidate.votes
                    self.candidates_by_party[candidate.party] = 1


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

    def party_percent(self, party):
        return sum(x.percent for x in self.fp
                   if x.party == party)

    def party_votes(self, party):
        return sum(x.votes for x in self.fp
                   if x.party == party)

    def party_swing(self, party):
        # This is more complicated because there is potential
        # for two categorised parties to have different swings -
        # so take the average weighted by the number of votes
        return (sum(x.swing * x.votes for x in self.fp
                if x.party == party and x.swing is not None) / 
                sum(x.votes for x in self.fp
                if x.party == party and x.swing is not None))


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


class AllElections:
    def __init__(self):
        self.elections = {}
        fed_years = [2022, 2019, 2016, 2013, 2010, 2007, 2004, 2001, 1998, 1996,
                    1993, 1990, 1987, 1984, 1983, 1980]
        self.elections.update({
            ElectionCode(year=year, region='fed'): 
            ElectionResults(f'{year} Federal Election',
            lambda: generic_download('fed', year))
            for year in fed_years})
        nsw_years = [2019, 2015, 2011, 2007, 2003, 1999, 1995, 1991, 1988,
                    1984, 1981]
        self.elections.update({
            ElectionCode(year=year, region='nsw'): 
            ElectionResults(f'{year} NSW Election',
            lambda: generic_download('nsw', year))
            for year in nsw_years})
        vic_years = [2022, 2018, 2014, 2010, 2006, 2002, 1999, 1996, 1992]
        self.elections.update({
            ElectionCode(year=year, region='vic'): 
            ElectionResults(f'{year} VIC Election',
            lambda: generic_download('vic', year))
            for year in vic_years})
        qld_years = [2020, 2017, 2015, 2012, 2009, 2006, 2004, 2001, 1998,
                    1995, 1992, 1989, 1986, 1983, 1980]
        self.elections.update({
            ElectionCode(year=year, region='qld'): 
            ElectionResults(f'{year} QLD Election',
            lambda: generic_download('qld', year))
            for year in qld_years})
        sa_years = [2022, 2018, 2014, 2010, 2006, 2002, 1997]
        self.elections.update({
            ElectionCode(year=year, region='sa'): 
            ElectionResults(f'{year} SA Election',
            lambda: generic_download('sa', year))
            for year in sa_years})
        wa_years = [2021, 2017, 2013, 2008, 2005, 2001, 1996, 1993, 1989,
                    1986, 1983, 1980]
        self.elections.update({
            ElectionCode(year=year, region='wa'): 
            ElectionResults(f'{year} WA Election',
            lambda: generic_download('wa', year))
            for year in wa_years})
    
    def __getitem__(self, key):
        return self.elections[key]

    def items(self):
        return self.elections.items()

    def keys(self):
        return self.elections.keys()

    # return an ordered list of election codes for
    # elections following the given one
    def next_elections(self, current_election):
        return sorted([a for a in self.elections.keys()
                       if a.region() == current_election.region() and
                       a.year() > current_election.year()],
                      key=lambda x: x.year())

    # return an ordered list of election codes for
    # elections preceding the given one
    def previous_elections(self, current_election):
        return sorted([a for a in self.elections.keys()
                       if a.region() == current_election.region() and
                       a.year() < current_election.year()],
                      key=lambda x: x.year())


def collect_seat_urls(seat_url_dict, url, pattern):
    content_category = str(requests.get(url).content)
    content_category = content_category.split('div class="mw-category mw-category-columns"')[1].split('<noscript>')[0]
    matches_category = re.findall(pattern, content_category)
    for match in matches_category:
        name = match[1].split(" (")[0].replace('&#039;', "'")
        if name == 'Maneroo':
            continue
        url = match[0]
        if name in seat_url_dict:
            seat_url_dict[name].add(url)
        else:
            seat_url_dict[name] = {url}


def fetch_seat_urls_state(state):
    seat_urls = {}  # key is seat name, value is url
    state_pattern = r'<a href="([^"?]*)"[^>]*>[^>]*district of ([^<]*)<'
    if state == 'fed':
        federal_pattern = r'<a href="([^"?]*)"[^>]*>[^>]*Division of ([^<]*)<'
        # As Namadgi was only contested for one election, it doesn't have a
        # dedicated results page, so add it separately
        seat_urls['Namadgi'] = {'/wiki/Division_of_Namadgi'}
        collect_seat_urls(seat_urls,
                          f'https://en.wikipedia.org/wiki/Category:Australian_federal_electoral_results_by_division',
                          federal_pattern)
        collect_seat_urls(seat_urls,
                          f'https://en.wikipedia.org/w/index.php?title=Category:Australian_federal_electoral_results_by_division&pagefrom=Perth%0AElectoral+results+for+the+Division+of+Perth#mw-pages',
                          federal_pattern)
    elif state == 'nsw':
        collect_seat_urls(seat_urls,
                          f'https://en.wikipedia.org/w/index.php?title=Category:New_South_Wales_state_electoral_results_by_district',
                          state_pattern)
        collect_seat_urls(seat_urls,
                          f'https://en.wikipedia.org/w/index.php?title=Category:New_South_Wales_state_electoral_results_by_district&pagefrom=Marrickville',
                          state_pattern)
    elif state == 'vic':
        collect_seat_urls(seat_urls,
                          f'https://en.wikipedia.org/w/index.php?title=Category:Victoria_(Australia)_state_electoral_results_by_district',
                          state_pattern)
        collect_seat_urls(seat_urls,
                          f'https://en.wikipedia.org/w/index.php?title=Category:Victoria_(Australia)_state_electoral_results_by_district&pagefrom=Rainbow%0AElectoral+results+for+the+district+of+Rainbow#mw-pages',
                          state_pattern)
    else:
        collect_seat_urls(seat_urls,
                          f'https://en.wikipedia.org/wiki/Category:{state_page[state]}_state_electoral_results_by_district',
                          state_pattern)
    return seat_urls


def generic_download(state, year):
    filename = f'elections/{year}{state}_results.pkl'
    try:
        with open(filename, 'rb') as pkl:
            all_results = pickle.load(pkl)
        return all_results.results
    except FileNotFoundError:
        pass
    all_results = SavedResults()
    seat_urls = fetch_seat_urls_state(state)
    for seat_name, url_list in seat_urls.items():
        for url in url_list:
            seat_results = SeatResults(seat_name)
            full_url = f'https://en.wikipedia.org/{url}'
            # This lines makes sure we don't get old data
            headers = {
                'User-Agent' : 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/71.0.3578.98 Safari/537.36',
                'Cache-Control': 'no-cache'
            }
            content = str(requests.get(full_url, headers).content)
            content = content.replace('\\r','\r').replace('\\n','\n').replace("\\'","'")
            content = content.replace('&amp;','&').replace('\\xe2\\x88\\x92', '-')
            content = content.replace('\\xe2\\x80\\x93', '-')
            content = content.replace('&#039;', "'")
            election_marker = f'>{year} {state_election_name[state]}<'
            if election_marker not in content:
                print(f'Seat not present in election: {seat_name}')
                continue
            print(f'Seat results found: {seat_name}')
            election_content = content.split(election_marker)[1].split('</table>')[0]
            if seat_name == "Narungga":
                print(url)
                print(election_content)
            if '>Two-' in election_content:
                fp_content = election_content.split('>Two-')[0]
                tcp_content = election_content.split('>Two-')[-1]
            else:
                fp_content = election_content
                tcp_content = None
            fp_content = fp_content.split('Notional')[0]
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
                    name=match[1].strip(),
                    party=match[0].strip(),
                    votes=int('0'+match[2].replace(',','').replace('.','').strip()),
                    percent=float(match[3].replace(',','.').strip()),
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
                        name=match[1].strip(),
                        party=match[0].strip(),
                        votes=int('0'+match[2].replace(',','').replace('.','').strip()),
                        percent=float(match[3].strip()),
                        swing=swing))
                if seat_name == 'Barambah' and year == 1989:
                    seat_results.tcp[0].votes = 8497
                    seat_results.tcp[1].votes = 3404
                elif seat_name == 'Bowen' and year == 1989:
                    seat_results.tcp[0].votes = 7524
                    seat_results.tcp[1].votes = 3134
                elif ((state == 'nsw' and year <= 1984 and seat_results.tcp[0].votes == 0) or 
                    (state == 'fed' and year <= 1983 and seat_results.tcp[0].votes == 0) or 
                    (seat_name == 'Pearce' and year == 2001) or
                    (seat_name == 'Newcastle' and year == 1987)):
                    total_votes = sum(x.votes for x in seat_results.fp)
                    seat_results.tcp[0].votes = round(seat_results.tcp[0].percent * 0.01 * total_votes)
                    seat_results.tcp[1].votes = round(seat_results.tcp[1].percent * 0.01 * total_votes)
                elif seat_name == 'Hammond' and year == 2006:
                    seat_results.tcp[0].swing = -4.2
                elif seat_results.tcp[0].votes == 0:
                    print(seat_results)
                    raise ValueError('Missing votes data - needs attention')
                try:
                    # If one of the swing values is equal to the vote percent,
                    # then it's not actually a legitimate swing and the
                    # swing values should be set to "None" to represent this
                    if (abs(seat_results.tcp[0].swing - seat_results.tcp[0].percent) < 0.06
                        or abs(seat_results.tcp[1].swing - seat_results.tcp[1].percent) < 0.06):
                        seat_results.tcp[0].swing = None
                        seat_results.tcp[1].swing = None
                except TypeError:
                    pass  # If one of the above values is none,
                        # it's fine to just skip the check altogether
            else:
                seat_results.tcp = seat_results.fp
                if None not in (x.swing for x in seat_results.tcp):
                    # Remove swing where the tcp swing isn't the same
                    # as fp swing (evidenced by it not adding to 0)
                    if sum(x.swing for x in seat_results.tcp) != 0:
                        for x in seat_results.tcp:
                            x.swing = None
            seat_results.order()
            all_results.results.append(seat_results)
    with open(filename, 'wb') as pkl:
        pickle.dump(all_results, pkl, pickle.HIGHEST_PROTOCOL)
    print(f'Downloaded election from Wikipedia: {year}{state}')
    return all_results.results


if __name__ == '__main__':
    elections = AllElections()
