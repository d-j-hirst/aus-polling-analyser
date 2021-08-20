from election_data import AllElections
from election_code import ElectionCode


def check_seat_numbers(elections):
    for code, election in elections.elections.items():
        seat_count = len(election.seat_results)
        print(f'Election {election.name}: Number of seats is {seat_count}')


def check_seat_percent_total(elections):
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            if len(seat_result.fp) < 2:
                continue  # Don't need to flag some old uncontested seats
            total_percent = sum(x.percent for x in seat_result.fp)
            if abs(total_percent - 100) > 0.21:
                print(f'{election.name} - {seat_result.name}: total %: {total_percent}')


def check_vote_percent_calc(elections):
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            if len(seat_result.fp) < 2:
                continue  # Don't need to flag some old uncontested seats
            total_votes = sum(x.votes for x in seat_result.fp)
            for candidate in seat_result.fp:
                calc_percent = candidate.votes / total_votes * 100
                if abs(calc_percent - candidate.percent) > 0.06:
                    print(f'{election.name} - {seat_result.name} - {candidate.name} - recorded %: {candidate.percent}, calc %: {calc_percent}')



if __name__ == '__main__':
    elections = AllElections()
    check_seat_numbers(elections)
    check_seat_percent_total(elections)
    check_vote_percent_calc(elections)
