from election_data import AllElections
from election_code import ElectionCode
import copy
import math
import numpy
import statistics
from sklearn.linear_model import LinearRegression
from poll_transform import transform_vote_share, detransform_vote_share, clamp

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


def analyse_greens(elections):
    bucket_min = -90
    bucket_base = 15
    bucket_max = -15
    this_buckets = {(-10000, bucket_min): []}
    this_buckets.update({(a, a + bucket_base): [] for a in range(bucket_min, bucket_max, bucket_base)})
    this_buckets.update({(bucket_max, 10000): []})
    swing_buckets = copy.deepcopy(this_buckets)
    party = 'Greens'
    for this_election, this_results in elections.items():
        if len(elections.next_elections(this_election)) == 0:
            continue
        next_election = elections.next_elections(this_election)[0]
        next_results = elections[next_election]
        election_swing = (next_results.total_fp_percentage_party(party)
                 - this_results.total_fp_percentage_party(party))
        print(f'{this_election.short()} - {next_election.short()} overall swing to greens: {election_swing}')
        for this_seat_name in this_results.seat_names():
            this_seat_results = this_results.seat_by_name(this_seat_name)
            if this_seat_name in next_results.seat_names():
                next_seat_results = next_results.seat_by_name(this_seat_name)
                if party in [a.party for a in this_seat_results.fp]:
                    this_greens = sum(x.percent for x in this_seat_results.fp
                                        if x.party == party)
                else:
                    continue
                if party in [a.party for a in next_seat_results.fp]:
                    next_greens = sum(x.percent for x in next_seat_results.fp
                                        if x.party == party)
                else:
                    continue
                this_greens = transform_vote_share(this_greens)
                next_greens = transform_vote_share(next_greens)
                greens_change = next_greens - this_greens
                this_bucket = next(a for a in this_buckets 
                                   if a[0] < this_greens
                                   and a[1] > this_greens)
                this_buckets[this_bucket].append(greens_change)
                swing_buckets[this_bucket].append(election_swing)
    bucket_coefficients = {}
    bucket_intercepts = {}
    bucket_residual_quantiles = {}
    n_quantiles = 25
    for bucket, results in this_buckets.items():
        if len(results) > 0:
            swings = swing_buckets[bucket]
            swing_array = numpy.array(swings).reshape(-1, 1)
            results_array = numpy.array(results)
            reg = LinearRegression().fit(swing_array, results_array)
            overall_coefficient = reg.coef_[0]
            overall_intercept = reg.intercept_
            residuals = [results[index] - (overall_coefficient * swings[index] + overall_intercept)
                         for index in range(0, len(results))
            ]
            bucket_coefficients[bucket] = (overall_coefficient)
            bucket_intercepts[bucket] = (overall_intercept)
            quantiles = [a / n_quantiles for a in range(0, n_quantiles + 1)]
            bucket_residual_quantiles[bucket] = {
                quantile: numpy.quantile(residuals, [quantile])[0]
                for quantile in quantiles
            }

    # This section takes sample (untransformed) values and
    # returns (untransformed) distributions based on those values
    # samples = [(1, 0),(2, 0),(3.5, 0),(5, 0),(6.5, 0),(8, 0),(10, 0),(12, 0),(15, 0),(20, 0),(25, 0),(30, 0),(35, 0),(40, 0),(45, 0),(50, 0)]
    samples = [(10, -3), (10, -2), (10, -1), (10, 0), (10, 1), (10, 2), (10, 3)]
    for sample, sample_swing in samples:
        sample_t = transform_vote_share(sample)
        midpoint = lambda x: (max(x[0], bucket_min - bucket_base) + min(x[1], bucket_max + bucket_base)) / 2
        lower_bucket = max((bucket for bucket in this_buckets.keys()
                        if (bucket[0] + bucket[1]) / 2 < sample_t),
                        key=midpoint)
        upper_bucket = min((bucket for bucket in this_buckets.keys()
                        if (bucket[0] + bucket[1]) / 2 > sample_t),
                        key=midpoint)
        mix_factor = clamp(((sample_t - midpoint(lower_bucket))
                            / (midpoint(upper_bucket) - midpoint(lower_bucket))),
                           0, 1)
        print(mix_factor)
        print(lower_bucket)
        print(upper_bucket)
        upper_coefficient = bucket_coefficients[upper_bucket]
        lower_coefficient = bucket_coefficients[lower_bucket]
        upper_intercept = bucket_intercepts[upper_bucket]
        lower_intercept = bucket_intercepts[lower_bucket]
        effective_coefficient = (upper_coefficient * mix_factor +
                                lower_coefficient * (1 - mix_factor))
        effective_intercept = (upper_intercept * mix_factor +
                            lower_intercept * (1 - mix_factor))
        sample_t_adj = sample_t + effective_coefficient * sample_swing + effective_intercept
        upper_quantiles = bucket_residual_quantiles[upper_bucket]
        lower_quantiles = bucket_residual_quantiles[lower_bucket]
        effective_quantiles = {quantile: upper_quantiles[quantile] * mix_factor +
                            lower_quantiles[quantile] * (1 - mix_factor)
                            for quantile in upper_quantiles.keys()}
        sample_t_quantiles = {quantile: sample_t_adj + value
                            for quantile, value in effective_quantiles.items()}
        sample_quantiles = {quantile: detransform_vote_share(value)
                            for quantile, value in sample_t_quantiles.items()}
        print(f'Expected distribution for original sample vote {sample} and swing {sample_swing}:')
        print('\n'.join(f'{quantile}: {value}' for quantile, value in sample_quantiles.items()))



if __name__ == '__main__':
    elections = AllElections()
    # check_seat_numbers(elections)
    check_fp_percent_total(elections)
    check_fp_percent_match(elections)
    check_fp_percent_calc(elections)
    check_tcp_percent_total(elections)
    check_tcp_percent_match(elections)
    combine_parties(elections)

    # display_parties(elections)
    # best_performances(elections)

    analyse_greens(elections)