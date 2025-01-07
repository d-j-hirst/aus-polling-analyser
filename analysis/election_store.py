from election_check import get_checked_elections


def write_candidate_to_file(file, c):
    file.write(f'{c.name},{c.party},{c.votes},{c.percent},{c.swing}\n')


def write_election_to_file(file, election_code, election_results):
    file.write(f'{election_results.name}\n')
    for seat in election_results.seat_results:
        file.write(f'Seat,{seat.name}\n')
        file.write(f'fp\n')
        for fp in seat.fp:
            write_candidate_to_file(file, fp)
        file.write(f'tcp\n')
        for tcp in seat.tcp:
            write_candidate_to_file(file, tcp)


def store_elections(elections):
    for election_code, election_results in elections.items():
        filename = (f'./elections/results_{election_code.year()}'
                    f'{election_code.region()}.csv')
        print(filename)
        with open(filename, 'w') as file:
            write_election_to_file(file, election_code, election_results)


if __name__ == '__main__':
    elections = get_checked_elections()
    store_elections(elections)