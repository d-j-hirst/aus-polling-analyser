from sklearn import datasets, linear_model

unnamed_others_code = 'xOTH FP'

# To keep analysis simple, and maintain decent sample sizes, group
# polled parties into categories with similar expected behaviour.
party_groups = {
    'ALP' : ['ALP FP'],
    'LNP' : ['LNP FP', 'LIB FP'],
    # Minor "constituency" parties that represent a particular group/viewpoint
    # Fairly stable, slow moving vote shares
    'Misc-c' : ['GRN FP', 'NAT FP', 'FF FP'],
    # Minor "protest" parties that are primarily anti-political,
    # defining themselves as being against the current system
    # Volatile, rapidly changing vote shares
    'Misc-p' : ['UAP FP', 'ONP FP', 'SAB FP', 'SFF FP', 'DEM FP', 'KAP FP'],
    # General others: Anything not ALP/LIB/NAT/GRN (or their equivalents)
    'OTH' : ['OTH FP'],
    # Unnamed others: Anything not listed at all
    'xOTH' : [unnamed_others_code]
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
    # [0] year of election, [1] region of election
    with open('./Data/ordered-elections.csv', 'r') as f:
        elections = [(a[0], a[1]) for a in
                  [b.strip().split(',') for b in f.readlines()]]
    # key: [0] year of election, [1] region of election
    # value: list of significant party codes modelled in that election
    with open('./Data/significant-parties.csv', 'r') as f:
        parties = {(a[0], a[1]): a[2:] for a in
                   [b.strip().split(',') for b in f.readlines()]}
    # key: [0] year of election, [1] region of election, [2] party code
    # value: primary vote recorded in this election
    with open('./Data/eventual-results.csv', 'r') as f:
        eventual_results = {(a[0], a[1], a[2]) : float(a[3]) for a in
                  [b.strip().split(',') for b in f.readlines()]}
    # key: [0] year of election, [1] region of election, [2] party code
    # value: primary vote recorded in the previous election
    with open('./Data/prior-results.csv', 'r') as f:
        linelists = [b.strip().split(',') for b in f.readlines()]
        prior_results = {(a[0], a[1], a[2]) : [float(x) for x in a[3:]]
            for a in linelists}
    # stores: first incumbent party, then main opposition party,
    # finally years incumbent party has been in power
    with open('./Data/incumbency.csv', 'r') as f:
        incumbency = {(a[0], a[1]) : (a[2], a[3], float(a[4])) for a in
                  [b.strip().split(',') for b in f.readlines()]}
    # Trim party list so that we only store it for completed elections
    parties = {(e[0], e[1]): parties[(e[0], e[1])]
                        for e in elections}
    # Create averages of prior results
    avg_prior_results = {k: sum(v[:6]) / 6
        for k, v in prior_results.items()}
    election_data = {}
    for e_p in parties.items():
        for party in e_p[1]:
            trend_filename = './Outputs/fp_trend_' + e_p[0][0] + e_p[0][1] + \
                             '_' + party + '.csv'
            print(trend_filename)
            data = import_trend_file(trend_filename)
            election_data[(e_p[0][0], e_p[0][1], party)] = data
        election_data[(e_p[0][0], e_p[0][1], party)]
        election_data[(e_p[0][0], e_p[0][1], unnamed_others_code)] = \
            create_exclusive_others_series (election_data, e_p)
    for e in elections:
        eventual_others = eventual_results[(e[0], e[1], 'OTH FP')]
        eventual_named = 0
        for p in parties[e]:
            if p not in not_others and (e[0], e[1], p) in eventual_results:
                eventual_named += eventual_results[(e[0], e[1], p)]
        eventual_unnamed = eventual_others - eventual_named
        eventual_results[(e[0], e[1], unnamed_others_code)] = eventual_unnamed
        parties[e].append(unnamed_others_code)
        
        
    day = 0
    #trendline = election_data[('2019','fed','xOTH FP')]
    #for day, trend in enumerate(trendline):
    #    print(str(day) + ": " + str(trend[50]))
    info = {}
    results = {}
    stored_info = {}
    for election in elections:
        print(f'{election[0]}, {election[1]}')
        for party in parties[election]:
            print(party)
            party_group = ''
            for group, group_list in party_groups.items():
                if party in group_list:
                    party_group = group
                    break
            if party_group == '':
                print(f'Warning: {party} not categorised!')
                continue
            if party_group not in info:
                info[party_group] = []
            if party_group not in results:
                results[party_group] = []
            data_key = (election[0], election[1], party)
            if not data_key in prior_results:
                prior_results[data_key] = [0]
                avg_prior_results[data_key] = 0
                print(f'Info: prior result not found for: ' + 
                      f'{election[0]}, {election[1]}, {party}')
            if not data_key in eventual_results:
                eventual_results[data_key] = 0
                print(f'Info: eventual result not found for: ' + 
                      f'{election[0]}, {election[1]}, {party}')
            print(f'Prior result: {prior_results[data_key][0]}')
            print(f'Prior average: {avg_prior_results[data_key]}')
            print(f'Final poll trend: {election_data[data_key][0][50]}')
            print(f'Eventual result: {eventual_results[data_key]}')
            incumbent = (incumbency[data_key[0], data_key[1]][0] == party)
            opposition = (incumbency[data_key[0], data_key[1]][1] == party)
            government_length = incumbency[data_key[0], data_key[1]][2]
            # should actually randomply sample poll results from distribution
            poll_trend = election_data[data_key][day][50] \
                if day < len(election_data[data_key]) \
                else prior_results[data_key]
            federal = (data_key[1] == 'fed')
            this_info = [
                avg_prior_results[data_key],
                poll_trend,
                1 if incumbent else 0,
                1 if federal else 0,
                government_length if incumbent else 0,
                government_length if opposition else 0,
                float(data_key[0]) - 2020
            ]
            stored_info[data_key] = this_info
            info[party_group].append(this_info)
            results[party_group].append([eventual_results[data_key]])

    coeffs = {}
    for party_group in info.keys():
        regr = linear_model.LinearRegression(fit_intercept=False)
        print(party_group)
        print(info[party_group])
        print(results[party_group])
        regr.fit(info[party_group], results[party_group])
        this_coeffs = [round(x, 3) for x in regr.coef_[0]]
        print('Coefficients: \n', this_coeffs)
        print('Coefficients: \n', regr.intercept_)
        coeffs[party_group] = this_coeffs

    for election in elections:
        print(f'{election[0]}, {election[1]}')
        for party in parties[election]:
            print(party)
            party_group = ''
            for group, group_list in party_groups.items():
                if party in group_list:
                    party_group = group
                    break
            if party_group == '':
                continue
            data_key = (election[0], election[1], party)
            zipped = zip(stored_info[data_key], coeffs[party_group])
            estimated = sum([a[0] * a[1] for a in zipped])
            print(f'Prior result: {prior_results[data_key][0]}')
            print(f'Prior average: {avg_prior_results[data_key]}')
            print(f'Final poll trend: {election_data[data_key][0][50]}')
            print(f'Estimated result: {estimated}')
            print(f'Eventual result: {eventual_results[data_key]}')



if __name__ == '__main__':
    trend_adjust()
