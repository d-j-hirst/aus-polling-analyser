import requests
from bs4 import BeautifulSoup
from bs4.diagnose import diagnose

#URL = 'https://www.abc.net.au/news/elections/qld/2020/results'
URL = 'https://www.abc.net.au/news/elections/sa/2022/results'
page = requests.get(URL)

soup = BeautifulSoup(page.content, 'html.parser')

seatEls = soup.find_all('div', class_='_3UHiz')

results = {}
for seatEl in seatEls:
    name = seatEl.find('h2', class_='_1YbN5').text
    if name[-1] == '*': name = name[:-2]
    print('"' + name + '"')
    swingEl = seatEl.find('span', class_='_1hdDp')
    if swingEl is None: continue
    swingParts = swingEl.text.split(' ')
    if len(swingParts) < 2: continue
    swing = float(swingParts[0][:-1])
    party = swingParts[-1]
    if party != 'ALP': swing = -swing
    results[name] = [swing]

# electorates = ['alge', 'aspl', 'banc', 'barr', 'bonn', 'broa',
#                'bude', 'buli', 'bung', 'bunb', 'burd', 'burl',
#                'burn', 'cair', 'call', 'calo', 'capa', 'chat',
#                'clay', 'cond', 'cook', 'coom', 'coop', 'curr',
#                'ever', 'fern', 'gave', 'glad', 'glas', 'gree',
#                'greg', 'gymp', 'herv', 'hill', 'hinc', 'inal',
#                'ipsw', 'ipwe', 'jord', 'kawa', 'kepp', 'kurw',
#                'lock', 'loga', 'lytt', 'maca', 'mcco', 'mack',
#                'maiw', 'mans', 'maro', 'mary', 'merm', 'mill',
#                'mira', 'mogg', 'mora', 'momm', 'mudg', 'mulg',
#                'mund', 'murr', 'nana', 'nick', 'nind', 'noos',
#                'nudg', 'oodg', 'pine', 'pumi', 'redc', 'redl',
#                'rock', 'sand', 'scen', 'sbri', 'sdow', 'spor',
#                'spri', 'staf', 'stre', 'surf', 'theo', 'thur',
#                'tooh', 'tono', 'toso', 'town', 'trae', 'warr',
#                'wate', 'whit', 'wood']

# exclude = ['mira', 'hill', 'hinc', 'maiw', 'sbri', 'noos', 'trae']

electorates = ['adel', 'badc', 'blac', 'brag', 'chaf', 'chel',
              'colt', 'croy', 'dave', 'duns', 'elde', 'eliz',
              'enfi', 'finn', 'flin', 'flor', 'from', 'gibs',
              'gile', 'hamm', 'hart', 'heys', 'hurt', 'kaur',
              'kave', 'king', 'lee', 'ligh', 'mack', 'maws',
              'mori', 'morp', 'moun', 'naru', 'newl', 'play',
              'port', 'rams', 'reyn', 'schu', 'stua', 'tayl',
              'torr', 'unle', 'wait', 'west', 'wrig']
exclude = ['moun', 'kave', 'naru', 'wait', 'stua', 'finn', 'hamm', 'from']

# While this is technically inefficient as opposed to just directly
# removing the electorates from the above list, it's much more convenient
# for on-the-fly adjustments on election night
electorates = [a for a in electorates if a not in exclude]

for electorate in electorates:
    URL = 'https://www.abc.net.au/news/elections/sa/2022/guide/' + electorate
    page = requests.get(URL)
    soup = BeautifulSoup(page.content, 'html.parser')
    title = soup.find('h1').text
    title = title.split('(')[0].strip()
    if title[-1] == '*': name = title[-2]
    print(f'Getting results from page {title}')
    countedParent = soup.find('div', class_='_12fma')
    if countedParent is None: continue
    countedText = countedParent.find('strong').text
    try:
        countedPercent = float(countedText.split('%')[0])
        results[title].append(countedPercent)
    except ValueError:
        print(f"Warning: couldn't parse % counted for seat {title}")
        continue
    except KeyError:
        pass

results = [[a, b[0], b[1]] for a, b in results.items() if len(b) == 2]

print(results)

with open('output.csv', 'w') as f:
    for result in results:
        f.write(f'{result[0]},{result[1]},{result[2]}\n')