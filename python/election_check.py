from election_data import AllElections

def check_seat_numbers(elections):
    for code, election in elections.elections.items():
        seat_count = len(election.seat_results)
        print(f'Election {election.name}: Number of seats is {seat_count}')


def check_fp_percent_total(elections):
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            if len(seat_result.fp) < 2:
                continue  # Don't need to flag some old uncontested seats
            total_percent = sum(x.percent for x in seat_result.fp)
            if abs(total_percent - 100) > 0.21:
                print(f'{election.name} - {seat_result.name}: total fp %: {total_percent}')


def check_fp_percent_match(elections):
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            if len(seat_result.fp) < 2:
                continue  # Don't need to flag some old uncontested seats
            total_votes = sum(x.votes for x in seat_result.fp)
            for candidate in seat_result.fp:
                calc_percent = candidate.votes / total_votes * 100
                if abs(calc_percent - candidate.percent) > 0.06:
                    print(f'{election.name} - {seat_result.name} - {candidate.name} - recorded fp %: {candidate.percent}, calc %: {calc_percent}')


def check_tcp_percent_total(elections):
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            if len(seat_result.tcp) < 2:
                # Don't need to flag some old uncontested seats 
                # or seats where TCP is not recorded
                continue
            if None in [x.percent for x in seat_result.tcp]:
                # If we are confirmed to not have a valid tcp count,
                # just skip the seat
                continue
            total_percent = sum(x.percent for x in seat_result.tcp)
            if abs(total_percent - 100) > 0.21:
                print(f'{election.name} - {seat_result.name}: total tcp %: {total_percent}')


def check_tcp_percent_match(elections):
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            if len(seat_result.tcp) < 2:
                # Don't need to flag some old uncontested seats 
                # or seats where TCP is not recorded
                continue 
            if None in [x.percent for x in seat_result.tcp]:
                # If we are confirmed to not have a valid tcp count,
                # just skip the seat
                continue
            total_votes = sum(x.votes for x in seat_result.tcp)
            for candidate in seat_result.tcp:
                calc_percent = candidate.votes / total_votes * 100
                if abs(calc_percent - candidate.percent) > 0.06:
                    print(f'{election.name} - {seat_result.name} - {candidate.name} - recorded tcp %: {candidate.percent}, calc %: {calc_percent}')


def check_tcp_swing_total(elections):
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            if len(seat_result.tcp) < 2:
                # Don't need to flag some old uncontested seats 
                # or seats where TCP is not recorded
                continue
            if None in [x.swing for x in seat_result.tcp]:
                # If we are confirmed to not have a valid tcp count,
                # just skip the seat
                continue
            total_swing = sum(x.swing for x in seat_result.tcp)
            if total_swing != 0:
                print(f'{election.name} - {seat_result.name}: total swing %: {total_swing}')


def check_fp_percent_calc(elections):
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            if None not in [a.votes for a in seat_result.fp]:
                if None in [a.percent for a in seat_result.fp]:
                    print(f'{election.name} - {seat_result.name} - has fp vote count data that can be converted to a percentage')


def check_tcp_percent_calc(elections):
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            if None not in [a.votes for a in seat_result.tcp]:
                if None in [a.percent for a in seat_result.tcp]:
                    print(f'{election.name} - {seat_result.name} - has tcp vote count data that can be converted to a percentage')


def combine_parties(elections):
    with open('./Data/party-simplification.csv', 'r') as f:
        conversions = {a[0].replace(';', ','): a[1] for a in
            [b.strip().split(',') for b in f.readlines()]}
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            for fp_candidate in seat_result.fp:
                if fp_candidate.party in conversions:
                    fp_candidate.party = conversions[fp_candidate.party]
            for tcp_candidate in seat_result.tcp:
                if tcp_candidate.party in conversions:
                    tcp_candidate.party = conversions[tcp_candidate.party]


def display_parties(elections):
    parties = {}
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            for fp_candidate in seat_result.fp:
                party = fp_candidate.party
                if party in parties:
                    parties[party][0] += 1
                    parties[party][1] = max(parties[party][1], fp_candidate.percent)
                else:
                    parties[party] = [1, fp_candidate.percent, 0, seat_result.name, code]
            for tcp_candidate in seat_result.tcp:
                party = tcp_candidate.party
                if party in parties:
                    parties[party][2] = max(parties[party][2], tcp_candidate.percent)
                else:
                    parties[party] = [0, 0, tcp_candidate.percent, seat_result.name, code]
    for party_name, party_info in parties.items():
        print(f'{party_name}: Seats contested (fp) - {party_info[0]}, best fp result - {party_info[1]}, best tcp result - {party_info[2]}, example seat - {party_info[3]}, example election - {party_info[4]}')


def best_performances(elections):
    best = {}
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            for fp_candidate in seat_result.fp:
                party = fp_candidate.party
                if party in best:
                    best[party].append((code, seat_result.name, fp_candidate.name, fp_candidate.percent))
                else:
                    best[party] = [(code, seat_result.name, fp_candidate.name, fp_candidate.percent)]
    for party, candidate_list in best.items():
        print(f'Party: {party}')
        candidate_list.sort(key=lambda x: x[3], reverse=True)
        for c in candidate_list[:10]:
            print(f'Election: {c[0].region()}{c[0].year()}, Seat: {c[1]}, Name: {c[2]}, fp %: {c[3]}')


def get_checked_elections():
    elections = AllElections()
    # Automatic checks that enforce consistency of election data
    check_fp_percent_total(elections)
    check_fp_percent_match(elections)
    check_fp_percent_calc(elections)
    check_tcp_percent_total(elections)
    check_tcp_percent_match(elections)
    check_tcp_swing_total(elections)
    # Combine micro parties into categories for better data processing
    combine_parties(elections)
    
    # Uncomment this to display seat numbers for each election in console
    # Results are only verified manually, so it's disabled by default
    # check_seat_numbers(elections)

    # Functions to do some interesting basic analysis, disable by default
    # display_parties(elections)
    # best_performances(elections)
    
    return elections


if __name__ == '__main__':
    get_checked_elections()