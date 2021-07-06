
def import_trend_file(filename):
    debug = (filename == './Outputs/fp_trend_2019fed_ALP FP.csv')
    with open(filename, 'r') as f:
        lines = f.readlines()
    lines = [line.strip().split(',')[2:] for line in lines[3:]]
    lines = [[float(a) for a in line] for line in lines]
    lines.reverse()
    return lines

def trend_adjust():
    with open('./Data/ordered-elections.csv', 'r') as f:
        elections = [(a[0], a[1]) for a in
                  [b.strip().split(',') for b in f.readlines()]]
    with open('./Data/significant-parties.csv', 'r') as f:
        parties = {(a[0], a[1]): a[2:] for a in
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
    print(list(election_data.items())[0][0])
    trendline = list(election_data.items())[0][1]
    for day, trend in enumerate(trendline):
        print(str(day) + ": " + str(trend[50]))

if __name__ == '__main__':
    trend_adjust()
