
# To keep analysis simple, and maintain 
party_groups = {
    'ALP' : ['ALP FP'],
    'LNP' : ['LNP FP', 'LIB FP'],
    # Minor "constituency" parties that represent a particular group/viewpoint
    # Fairly stable, slow moving vote shares
    'Misc-c' : ['GRN FP', 'NAT FP'],
    # Minor "reactionary" parties that are primarily anti-political,
    # defining themselves as being against the current system
    # Volatile, rapidly changing vote shares
    'Misc-r' : ['UAP FP', 'ONP FP', 'SAB FP', 'SFF FP', 'DEM FP', 'KAP FP'],
    # General others: Anything not ALP/LIB/NAT/GRN (or their equivalents)
    'OTH' : ['OTH FP'],
    # Unnamed others: Anything not listed at all
    'xOTH' : ['xOTH FP']
}

# Parties that shouldn't be included as part of the OTH FP
# when calculating exclusive-others vote shares
not_others = ['ALP FP', 'LNP FP', 'LIB FP', 'NAT FP', 'GRN FP', 'OTH FP']

def import_trend_file(filename):
    debug = (filename == './Outputs/fp_trend_2019fed_ALP FP.csv')
    with open(filename, 'r') as f:
        lines = f.readlines()
    lines = [line.strip().split(',')[2:] for line in lines[3:]]
    lines = [[float(a) for a in line] for line in lines]
    lines.reverse()
    return lines

# Create exclusive others raw series
def create_exclusive_others_series(election_data, e_p):
    series = []
    unnamed_others_base = 3  # Base of 3% for unnamed others mirrors the C++
    for day in range(0, len(election_data[(e_p[0][0], e_p[0][1], e_p[1][0])])):
        median = 0  # Median values for minor parties
        for party in e_p[1]:
            if party not in not_others:
                median += election_data[(e_p[0][0], e_p[0][1], party)][day][50]
        oth_median = election_data[(e_p[0][0], e_p[0][1], 'OTH FP')][day][50]
        modified_oth_median = max(oth_median, median + unnamed_others_base)
        xoth_proportion = 1 - median / modified_oth_median
        spread = []
        for value in range(0, 101):
            spread.append(election_data[(e_p[0][0], e_p[0][1],
                          'OTH FP')][day][value] * xoth_proportion)
        series.append(spread)
    return series

def trend_adjust():
    with open('./Data/ordered-elections.csv', 'r') as f:
        elections = [(a[0], a[1]) for a in
                  [b.strip().split(',') for b in f.readlines()]]
    with open('./Data/significant-parties.csv', 'r') as f:
        parties = {(a[0], a[1]): a[2:] for a in
                   [b.strip().split(',') for b in f.readlines()]}
    with open('./Data/eventual-results.csv', 'r') as f:
        eventual_results = {(a[0], a[1], a[2]) : a[3] for a in
                  [b.strip().split(',') for b in f.readlines()]}
    with open('./Data/prior-results.csv', 'r') as f:
        prior_results = {(a[0], a[1], a[2]) : a[3] for a in
                  [b.strip().split(',') for b in f.readlines()]}
    election_parties = {(e[0], e[1]): parties[(e[0], e[1])]
                        for e in elections}
    election_data = {}
    for e_p in election_parties.items():
        for party in e_p[1]:
            trend_filename = './Outputs/fp_trend_' + e_p[0][0] + e_p[0][1] + \
                             '_' + party + '.csv'
            print(trend_filename)
            data = import_trend_file(trend_filename)
            election_data[(e_p[0][0], e_p[0][1], party)] = data
        election_data[(e_p[0][0], e_p[0][1], party)]
        election_data[(e_p[0][0], e_p[0][1], 'xOTH FP')] = \
            create_exclusive_others_series (election_data, e_p)
        
    day = 0
    #trendline = election_data[('2019','fed','xOTH FP')]
    #for day, trend in enumerate(trendline):
    #    print(str(day) + ": " + str(trend[50]))
    for election in elections:
        print(f'{election[0]}, {election[1]}')
        for party in election_parties[election]:
            print(party)
            data_key = (election[0], election[1], party)
            if not data_key in prior_results:
                prior_results[data_key] = 0
                print(f'Info: prior result not found for: ' + 
                      f'{election[0]}, {election[1]}, {party}')
            if not data_key in eventual_results:
                eventual_results[data_key] = 0
                print(f'Info: eventual result not found for: ' + 
                      f'{election[0]}, {election[1]}, {party}')
            print(f'Prior result: {prior_results[data_key]}')
            print(f'Final poll trend: {election_data[data_key][0][50]}')
            print(f'Eventual result: {eventual_results[data_key]}')

if __name__ == '__main__':
    trend_adjust()
