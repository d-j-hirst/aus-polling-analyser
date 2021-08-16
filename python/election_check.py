from election_data import AllElections
from election_code import ElectionCode


def check_seat_numbers(elections):
    for code, election in elections.elections.items():
        seat_count = len(election.seat_results)
        print(f'Election {election.name}: Number of seats is {seat_count}')
        if code == ElectionCode(2019, 'fed'):
            for seat in election.seat_results:
                print(seat.name)


if __name__ == '__main__':
    elections = AllElections()
    check_seat_numbers(elections)
