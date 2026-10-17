"""Download the enumerated official sources used by category_tpp_history.py."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from urllib.parse import urljoin

from bs4 import BeautifulSoup

from scripts.diagnostics.category_sources import fetch
from scripts.turnout.turnout_aec import ELECTIONS as AEC
from scripts.turnout.turnout_vic import ELECTIONS as VEC, discover_district_urls
from scripts.turnout.turnout_nsw import ELECTIONS as NSW
from scripts.turnout.turnout_qld import ELECTIONS as QLD


def source_urls():
    for election in AEC.values():
        stems = ['HouseTppByPollingPlace', 'HouseTcpByCandidateByVoteType',
                 'GeneralPollingPlaces']
        if int(election.election_code[:4]) >= 2016:
            stems.append('HouseTppByDivisionByVoteType')
        for stem in stems:
            yield (f'https://results.aec.gov.au/{election.event_id}/'
                   f'{election.download_path}/{stem}Download-'
                   f'{election.event_id}.csv')
    for election in VEC.values():
        index = fetch(election.index_url, True)
        for url in discover_district_urls(election, index):
            if election.page_style == 'historical':
                replacement = ('tcpbyvc' if election.election_code[:4]
                               in ('2006', '2010') else 'tcpbyvotingcentre')
                yield url.replace('fpvbyvotingcentre', replacement)
            else:
                yield url.replace('-district-results-by-voting-centre',
                                  '-2cp-results-by-voting-centre')
    for election in QLD.values():
        yield election.archive_url
    for year in (2017, 2021, 2025):
        base = f'http://media.waec.wa.gov.au/Archive/{year}%20SGE%20Final/'
        soup = BeautifulSoup(fetch(base, True), 'html.parser')
        for link in soup.find_all('a', href=True):
            if any(label in link.text for label in ('CANDIDATE SETUP', 'LA VERBOSE', 'POLLING PLACE SETUP')):
                yield urljoin(base, link['href'])


def collect_one(url):
    try:
        return None if fetch(url, True) else (url, 'Empty response')
    except Exception as error:
        return url, str(error)


def nsw_tool_urls(election, download=False):
    soup = BeautifulSoup(fetch(election.results_url, download), 'html.parser')
    return sorted({urljoin(election.results_url, a['href'])
                   for a in soup.find_all('a', href=True)
                   if a.get_text(strip=True) == 'LA TCP'})


def nsw_table_url(tool_url, download=False):
    soup = BeautifulSoup(fetch(tool_url, download), 'html.parser')
    options = []
    for option in soup.find_all('option', value=True):
        # Older pages omit closing option tags; only the immediate text is
        # the label, not all the subsequently nested options.
        text = str(option.find(string=True, recursive=False) or '')
        # CLP means Country Labor in these NSW pages, not the NT's Country
        # Liberal Party. Keep this alias confined to the NSW adapter.
        if any(p in text for p in ('(ALP)', '(LAB)', '(CLP)')) and any(
                p in text for p in ('(LIB)', '(NAT)', '(NP)')):
            options.append(option['value'])
    if len(options) != 1:
        return None
    pair = options[0].replace('(', '').replace(')', '').replace('--', '-')
    if 'SGE2015' in tool_url:
        return 'https://pastvtr.elections.nsw.gov.au/SGE2015/data/' + pair + '/tcp_table.html'
    return tool_url.rstrip('/') + '/' + pair + '/tcp_table.html'


def collect_nsw_tool(url):
    try:
        table = nsw_table_url(url, True)
        return collect_one(table) if table else (url, 'No unique Labor-Coalition pair')
    except Exception as error:
        return url, str(error)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download', action='store_true', required=True)
    parser.parse_args()
    urls = list(source_urls())
    with ThreadPoolExecutor(max_workers=4) as executor:
        failures = [result for result in executor.map(collect_one, urls) if result]
    print(f'{len(urls)} sources; {len(failures)} failures')
    for failure in failures:
        print(failure)
    for election in NSW.values():
        urls = nsw_tool_urls(election, True)
        with ThreadPoolExecutor(max_workers=4) as executor:
            failures = [r for r in executor.map(collect_nsw_tool, urls) if r]
        print(election.election_code, len(urls), 'districts;', len(failures), 'failures')
        for failure in failures[:5]:
            print(failure)


if __name__ == '__main__':
    main()
