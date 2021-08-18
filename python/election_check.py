from election_data import AllElections
from election_code import ElectionCode


def check_seat_numbers(elections):
    for code, election in elections.elections.items():
        seat_count = len(election.seat_results)
        print(f'Election {election.name}: Number of seats is {seat_count}')


def check_seat_percent(elections):
    for code, election in elections.elections.items():
        for seat_result in election.seat_results:
            total_percent = sum(x.percent for x in seat_result.fp)
            if abs(total_percent - 100) > 0.21:
                print(f'{election.name} - {seat_result.name}: total %: {total_percent}')


if __name__ == '__main__':
    elections = AllElections()
    check_seat_numbers(elections)
    check_seat_percent(elections)
