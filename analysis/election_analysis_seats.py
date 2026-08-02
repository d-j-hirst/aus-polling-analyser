"""Seat swing, Green-independent correlation and Coalition-allocation analysis.

Parent: election_analysis.py coordinates this seat-level component with the
party and regional analyses before certifying their shared output bundle.

Main functions:
* ``analyse_seat_swings`` calculates seat-swing distributions and contextual
  modifiers used by the C++ seat simulation.
* ``analyse_green_independent_correlation`` estimates the joint behaviour of
  Green and independent support.
* ``analyse_nationals`` derives Coalition-allocation inputs.
* ``get_all_elections`` loads the auxiliary results required by the last step.
"""

import statistics

import numpy
import statsmodels.api as sm
from sklearn.linear_model import LinearRegression

from election_analysis_common import (
    collect_election_data,
    has_material_independent_vote,
)
from election_code import ElectionCode
from poll_transform import detransform_vote_share, transform_vote_share
from sample_kurtosis import calc_rmse, one_tail_kurtosis, two_tail_kurtosis


# Core seat-level historical calculations

def analyse_seat_swings(elections, seat_types, seat_regions, by_elections):
    alp_swings = {}
    federals = {}
    margins = {}
    incumbent_retirement_urbans = {}
    incumbent_retirement_regionals = {}
    sophomore_candidate_urbans = {}
    sophomore_candidate_regionals = {}
    sophomore_party_urbans = {}
    sophomore_party_regionals = {}
    previous_swings = {}
    by_election_swings = {}
    names = {}
    for this_election, this_results in elections.items():
        previous_elections = elections.previous_elections(this_election)
        if len(previous_elections) > 0:
            previous_election = previous_elections[-1]
            previous_results = elections[previous_election]
        else:
            previous_results = None
        if len(previous_elections) > 1:
            old_election = previous_elections[-2]
            old_results = elections[old_election]
        else:
            old_results = None
        for this_seat_name in this_results.seat_names():
            # Fraser appears federally in both VIC and ACT (both safe seats)
            # since VIC is more recent, pretend the ACT results don't exist
            if this_seat_name == "Fraser" and this_election.year() < 2019:
                continue
            # Northern Territory is the only seat in its region,
            # so we can't analyse how it compares to its state
            if this_seat_name == "Northern Territory":
                continue
            this_seat_result = this_results.seat_by_name(this_seat_name)
            # These automatically gives None if no seat is found
            previous_seat_result = (previous_results.seat_by_name(
                this_seat_name,
                include_name_changes=True
            ) if previous_results is not None else None)
            old_seat_result = (old_results.seat_by_name(this_seat_name,
                               include_name_changes=True)
                               if old_results is not None else None)
            # Check seat has a classic 2cp swing
            if len(this_seat_result.tcp) != 2:
                continue
            if this_seat_result.tcp[0].party not in majors:
                continue
            if this_seat_result.tcp[1].party not in majors:
                continue
            if 'Labor' not in (this_seat_result.tcp[0].party,
                                this_seat_result.tcp[1].party):
                continue
            if this_seat_result.tcp[0].swing is None:
                continue

            # Check previous results has a classic 2cp swing
            if previous_seat_result is not None:
                if len(previous_seat_result.tcp) != 2:
                    previous_seat_result = None
                elif previous_seat_result.tcp[0].party not in majors:
                    previous_seat_result = None
                elif previous_seat_result.tcp[1].party not in majors:
                    previous_seat_result = None
                elif 'Labor' not in (previous_seat_result.tcp[0].party,
                                    previous_seat_result.tcp[1].party):
                    previous_seat_result = None
                elif previous_seat_result.tcp[0].swing is None:
                    previous_seat_result = None

            # Check old results has a classic 2cp swing
            if old_seat_result is not None:
                if len(old_seat_result.tcp) != 2:
                    old_seat_result = None
                elif old_seat_result.tcp[0].party not in majors:
                    old_seat_result = None
                elif old_seat_result.tcp[1].party not in majors:
                    old_seat_result = None
                elif 'Labor' not in (old_seat_result.tcp[0].party,
                                    old_seat_result.tcp[1].party):
                    old_seat_result = None
                elif old_seat_result.tcp[0].swing is None:
                    old_seat_result = None

            if (this_seat_name, this_election.region()) in seat_regions:
                this_seat_region = seat_regions[(this_seat_name, this_election.region())]
            else:
                this_seat_region = 'none'
            if this_election not in alp_swings:
                alp_swings[this_election] = {}
                federals[this_election] = {}
                margins[this_election] = {}
                incumbent_retirement_urbans[this_election] = {}
                incumbent_retirement_regionals[this_election] = {}
                sophomore_candidate_urbans[this_election] = {}
                sophomore_candidate_regionals[this_election] = {}
                sophomore_party_urbans[this_election] = {}
                sophomore_party_regionals[this_election] = {}
                previous_swings[this_election] = {}
                by_election_swings[this_election] = {}
                names[this_election] = {}
            if this_seat_region not in alp_swings[this_election]:
                alp_swings[this_election][this_seat_region] = []
                federals[this_election][this_seat_region] = []
                margins[this_election][this_seat_region] = []
                incumbent_retirement_urbans[this_election][this_seat_region] = []
                incumbent_retirement_regionals[this_election][this_seat_region] = []
                sophomore_candidate_urbans[this_election][this_seat_region] = []
                sophomore_candidate_regionals[this_election][this_seat_region] = []
                sophomore_party_urbans[this_election][this_seat_region] = []
                sophomore_party_regionals[this_election][this_seat_region] = []
                previous_swings[this_election][this_seat_region] = []
                by_election_swings[this_election][this_seat_region] = []
                names[this_election][this_seat_region] = []

            temp_incumbent_retirement = 0
            if previous_seat_result is not None and len(previous_seat_result.tcp) == 2:
                previous_winner_name = previous_seat_result.tcp[0].name
                if len([a for a in this_seat_result.fp if a.name == previous_winner_name]) == 0:
                    temp_incumbent_retirement = 1 if previous_seat_result.tcp[0].party == 'Labor' else -1

            temp_sophomore_candidate = 0
            if (previous_seat_result is not None and old_seat_result is not None
                 and len(previous_seat_result.tcp) == 2 and len(old_seat_result.tcp)) == 2:
                old_winner_name = old_seat_result.tcp[0].name
                previous_winner_name = previous_seat_result.tcp[0].name
                if old_winner_name != previous_winner_name:
                    if len([a for a in this_seat_result.fp if a.name == previous_winner_name]) != 0:
                        temp_sophomore_candidate = 1 if previous_seat_result.tcp[0].party == 'Labor' else -1

            temp_sophomore_party = 0
            if (previous_seat_result is not None and old_seat_result is not None
                 and len(previous_seat_result.tcp) == 2 and len(old_seat_result.tcp)) == 2:
                old_winner_party = (old_seat_result.tcp[0].party == 'Labor')
                previous_winner_party = (previous_seat_result.tcp[0].party == 'Labor')
                if old_winner_party != previous_winner_party:
                    temp_sophomore_party = 1 if previous_seat_result.tcp[0].party == 'Labor' else -1

            temp_previous_swing = None
            if previous_seat_result is not None and len(previous_seat_result.tcp) == 2:
                if previous_seat_result.tcp[0].swing is not None:
                    temp_previous_swing = previous_seat_result.tcp[0].swing
                    if previous_seat_result.tcp[0].party != 'Labor':
                        temp_previous_swing *= -1

            temp_by_election_swing = 0
            by_election_tag = (this_election.year(), this_seat_name)
            if by_election_tag in by_elections:
                temp_by_election_swing = by_elections[by_election_tag]

            # This code is here to look for movements to and from
            # three-cornered ALP/Lib/Nat contests. It didn't find a strong
            # enough connection to be worth using for now, except perhaps
            # in NSW.

            # temp_lnp_contest = 0
            # if previous_seat_result is not None:
            #     # exclude these states as the dynamics of three-cornered contests is different under OPV
            #     if this_election.region() != 'qld' and this_election.region() != 'nsw':
            #         if ('National' in (a.party for a in this_seat_result.fp)
            #                 and 'Liberal' in (a.party for a in this_seat_result.fp)):
            #             if ('National' not in (a.party for a in previous_seat_result.fp)
            #                 or 'Liberal' not in (a.party for a in previous_seat_result.fp)):
            #                 if this_election.region() != 'fed': continue
            #                 third_party_result = min([a.percent for a in this_seat_result.fp
            #                     if a.party == "Liberal" or a.party == "National"])
            #                 print(f'New LNP contest in {this_seat_name} for election {this_election}, lower fp vote {third_party_result}')
            #                 temp_lnp_contest = third_party_result


            alp_swing = (this_seat_result.tcp[0].swing
                         if this_seat_result.tcp[0].party == 'Labor'
                         else -this_seat_result.tcp[0].swing)
            alp_swings[this_election][this_seat_region].append(alp_swing)
            federals[this_election][this_seat_region].append(1 if this_election.region() == "fed" else 0)
            margins[this_election][this_seat_region].append(abs(this_seat_result.tcp[0].percent - 50))
            incumbent_retirement_urbans[this_election][this_seat_region].append(temp_incumbent_retirement
                if seat_types[(this_seat_name, this_election.region())] <= 1 else 0)
            incumbent_retirement_regionals[this_election][this_seat_region].append(temp_incumbent_retirement
                if seat_types[(this_seat_name, this_election.region())] >= 2 else 0)
            sophomore_candidate_urbans[this_election][this_seat_region].append(temp_sophomore_candidate
                if seat_types[(this_seat_name, this_election.region())] <= 1 else 0)
            sophomore_candidate_regionals[this_election][this_seat_region].append(temp_sophomore_candidate
                if seat_types[(this_seat_name, this_election.region())] >= 2 else 0)
            sophomore_party_urbans[this_election][this_seat_region].append(temp_sophomore_party
                if seat_types[(this_seat_name, this_election.region())] <= 1 else 0)
            sophomore_party_regionals[this_election][this_seat_region].append(temp_sophomore_party
                if seat_types[(this_seat_name, this_election.region())] >= 2 else 0)
            previous_swings[this_election][this_seat_region].append(temp_previous_swing)
            by_election_swings[this_election][this_seat_region].append(temp_by_election_swing)
            names[this_election][this_seat_region].append(this_seat_name)
    region_averages = {election: {region: statistics.mean(x)
                                  for region, x in a.items()}
                       for election, a in alp_swings.items()}
    swing_deviations = {election: {region: [seat_swing - region_averages[election][region] for seat_swing in seat_swings]
                                   for region, seat_swings in election_regions.items()}
                        for election, election_regions in alp_swings.items()}
    region_swings = {election: {region: [region_averages[election][region] for a in seat_regions]
                                   for region, seat_regions in election_regions.items()}
                        for election, election_regions in alp_swings.items()}
    previous_swing_deviations = {}
    for election, regions in previous_swings.items():
        previous_election = (elections.previous_elections(election)[-1]
                             if len(elections.previous_elections(election)) > 0 else None)
        if previous_election is not None:
            previous_average = region_averages[previous_election]
            previous_swing_deviations[election] = {region_code:
                    [a - region_averages[previous_election][region_code]
                    if a is not None else 0 for a in previous_swings]
                for region_code, previous_swings in regions.items()}
        else:
            previous_swing_deviations[election] = {region_code:
                    [0 for a in previous_swings]
                for region_code, previous_swings in regions.items()}

    alp_swings_flat = []
    alp_deviations_flat = []
    federal_flat = []
    region_swings_flat = []
    margins_flat = []
    incumbent_retirement_urban_flat = []
    incumbent_retirement_regional_flat = []
    sophomore_candidate_urban_flat = []
    sophomore_candidate_regional_flat = []
    sophomore_party_urban_flat = []
    sophomore_party_regional_flat = []
    previous_swing_deviations_flat = []
    by_election_swing_flat = []
    names_flat = []
    election_regions_flat = []
    regions_flat = []
    for election_code, election in swing_deviations.items():
        for region_code, region in election.items():
            alp_swings_flat += alp_swings[election_code][region_code]
            alp_deviations_flat += region
            federal_flat += federals[election_code][region_code]
            region_swings_flat += region_swings[election_code][region_code]
            margins_flat += margins[election_code][region_code]
            incumbent_retirement_urban_flat += incumbent_retirement_urbans[election_code][region_code]
            incumbent_retirement_regional_flat += incumbent_retirement_regionals[election_code][region_code]
            sophomore_candidate_urban_flat += sophomore_candidate_urbans[election_code][region_code]
            sophomore_candidate_regional_flat += sophomore_candidate_regionals[election_code][region_code]
            sophomore_party_urban_flat += sophomore_party_urbans[election_code][region_code]
            sophomore_party_regional_flat += sophomore_party_regionals[election_code][region_code]
            previous_swing_deviations_flat += previous_swing_deviations[election_code][region_code]
            by_election_swing_flat += by_election_swings[election_code][region_code]
            names_flat += names[election_code][region_code]
            election_regions_flat += [election_code.region()] * len(region)
            regions_flat += [region_code] * len(region)
    abs_swings_flat = [abs(x) for x in alp_deviations_flat]

    # Analysis of swing *direction* factors
    inputs_array = numpy.transpose(numpy.array([incumbent_retirement_urban_flat,
                                                incumbent_retirement_regional_flat,
                                                sophomore_candidate_urban_flat,
                                                sophomore_candidate_regional_flat,
                                                sophomore_party_urban_flat,
                                                sophomore_party_regional_flat,
                                                previous_swing_deviations_flat,
                                                by_election_swing_flat]))
    results_array = numpy.array(alp_deviations_flat)
    reg = LinearRegression().fit(inputs_array, results_array)
    retirement_urban = reg.coef_[0]
    retirement_regional = reg.coef_[1]
    sophomore_candidate_urban = reg.coef_[2]
    sophomore_candidate_regional = reg.coef_[3]
    sophomore_party_urban = reg.coef_[4]
    sophomore_party_regional = reg.coef_[5]
    previous_swing_modifier = reg.coef_[6]
    by_election_modifier = reg.coef_[7]

    # Analysis of swing *magnitude* factors
    inputs_array = numpy.transpose(numpy.array([federal_flat, region_swings_flat, margins_flat]))
    results_array = numpy.array(abs_swings_flat)
    reg = LinearRegression().fit(inputs_array, results_array)

    mean_swing_deviation = reg.intercept_
    federal_modifier = reg.coef_[0]
    region_swing_effect = reg.coef_[1]
    margin_effect = reg.coef_[2]
    swing_kurtosis = one_tail_kurtosis(abs_swings_flat)

    filename = (f'./Seat Statistics/tpp-swing-factors.csv')
    with open(filename, 'w') as f:
        f.write(f'mean-swing-deviation,{mean_swing_deviation}\n')
        f.write(f'swing-kurtosis,{swing_kurtosis}\n')
        f.write(f'federal-modifier,{federal_modifier}\n')
        f.write(f'retirement-urban,{retirement_urban}\n')
        f.write(f'retirement-regional,{retirement_regional}\n')
        f.write(f'sophomore-candidate-urban,{sophomore_candidate_urban}\n')
        f.write(f'sophomore-candidate-regional,{sophomore_candidate_regional}\n')
        f.write(f'sophomore-party-urban,{sophomore_party_urban}\n')
        f.write(f'sophomore-party-regional,{sophomore_party_regional}\n')
        f.write(f'previous-swing-modifier,{previous_swing_modifier}\n')
        f.write(f'by-election-modifier,{by_election_modifier}\n')

    individual_infos = {}
    for i in range(0, len(names_flat)):
        key = (names_flat[i], election_regions_flat[i], regions_flat[i])
        vals = (alp_swings_flat[i], region_swings_flat[i])
        if key not in individual_infos:
            individual_infos[key] = []
        individual_infos[key].append(vals)

    filename = (f'./Seat Statistics/individual-seat-factors.csv')
    with open(filename, 'w') as f:
        # mix_factor = 0.38
        elasticity_factor = 0.38
        # base_errors = []
        # predicted_errors = []
        # mixed_errors = []
        for key, values in individual_infos.items():
            if (len(values) < 10 and key[1] != 'vic') or len(values) < 7:
                continue
            inputs_array = numpy.transpose(numpy.array(
                [[a[1] for a in values]]))
            results_array = numpy.array([a[0] for a in values])
            reg = LinearRegression(fit_intercept=False).fit(inputs_array, results_array)
            elasticity = reg.coef_[0]
            trend = 0  # set to zero as it seems to harm predictiveness so far
            residuals = [a[0] - elasticity * a[1] - trend for a in values]
            volatility = calc_rmse(residuals)
            # The elasticity from the regression significantly overestimates
            # seat elasticity in new samples. A factor of 0.38 was found to
            # provided the optimum predictiveness for sample sizes of 10 and up.
            adjusted_elasticity = (elasticity - 1) * elasticity_factor + 1
            # High trend/low volatility values are likely artifacts of small
            # sample sizes, so cap them
            limited_trend = min(max(trend, -2.5), 2.5)
            limited_volatility = max(volatility, 2)
            f.write(f'{key[0]},{key[1]},{key[2]},{adjusted_elasticity},{limited_trend},{limited_volatility}\n')

            # code for testing elasticity predictiveness

            # for remove in range(0, len(values)):
            #     new_values = [a for ind, a in enumerate(values) if ind != remove]
            #     inputs_array = numpy.transpose(numpy.array(
            #         [[a[1] for a in new_values]]))
            #     results_array = numpy.array([a[0] for a in new_values])
            #     reg = LinearRegression(fit_intercept=False).fit(inputs_array, results_array)
            #     new_elasticity = reg.coef_[0]
            #     new_trend = reg.intercept_
            #     observed_swing = values[remove][0]
            #     predicted_swing = values[remove][1] * new_elasticity + new_trend
            #     mixed_swing = predicted_swing * mix_factor + values[remove][1] * (1 - mix_factor)
            #     base_error = abs(observed_swing - values[remove][1])
            #     predicted_error = abs(observed_swing - predicted_swing)
            #     mixed_error = abs(observed_swing - mixed_swing)
            #     base_errors.append(base_error)
            #     predicted_errors.append(predicted_error)
            #     mixed_errors.append(mixed_error)
        # print("Overall errors:")
        # print(mix_factor)
        # print(len(base_errors))
        # print(statistics.mean(base_errors))
        # print(statistics.mean(predicted_errors))
        # print(statistics.mean(mixed_errors))


def analyse_green_independent_correlation(elections):
    d = {
        'greens_swings': [],
        'ind_swings': [],
        'party': 'Greens'
    }
    def func(d):
        if 'Greens' in [a.party for a in d['this_seat_results'].fp]:
            this_greens = sum(x.percent for x in d['this_seat_results'].fp
                                if x.party == 'Greens')
        else:
            return
        if 'Greens' in [a.party for a in d['next_seat_results'].fp]:
            next_greens = sum(x.percent for x in d['next_seat_results'].fp
                                if x.party == 'Greens')
        else:
            return
        this_ind = sum(x.percent for x in d['this_seat_results'].fp
                                if x.party == 'Independent')
        next_ind = sum(x.percent for x in d['next_seat_results'].fp
                                if x.party == 'Independent')
        if not has_material_independent_vote(this_ind, next_ind):
            return
        greens_swing = (transform_vote_share(next_greens)
                        - transform_vote_share(this_greens)
                        - d['election_swing'])
        # Can't run this under transformation as it may be zero
        ind_swing = next_ind - this_ind
        d['greens_swings'].append(greens_swing)
        d['ind_swings'].append(ind_swing)
    
    collect_election_data(elections, d, func, use_previous = False)
    
    inputs_np = numpy.transpose(numpy.array([d['ind_swings']]))
    results_np = numpy.array(d['greens_swings'])
    mod = sm.OLS(results_np, inputs_np)
    fit = mod.fit()

    filename = (f'./Seat Statistics/GRN_IND_correlation.csv')
    with open(filename, 'w') as f:
        f.write(f'{fit.params[0]}')


# Auxiliary input loading

def get_all_elections():
    with open('./Data/polled-elections.csv', 'r') as f:
        polled_elections = ElectionCode.load_elections_from_file(f)
    with open('./Data/old-elections.csv', 'r') as f:
        old_elections = ElectionCode.load_elections_from_file(f)
    with open('./Data/future-elections.csv', 'r') as f:
        future_elections = ElectionCode.load_elections_from_file(f)
    return polled_elections + old_elections + future_elections


def analyse_nationals(elections, all_elections):
    def get_nationals_share(results, seat_name):
        seat_result = (results.seat_by_name(
            seat_name,
            include_name_changes=True
        ) if results is not None else None)
        if seat_result is None:
            return None
        nationals_candidate = next((a for a in seat_result.fp if a.party == "National"), None)
        liberals_candidate = next((a for a in seat_result.fp if a.party == "Liberal"), None)
        nationals_percent = 0
        liberals_percent = 0
        if nationals_candidate is not None:
            nationals_percent = nationals_candidate.percent
        else:
            nationals_percent = 0
        if liberals_candidate is not None:
            liberals_percent = liberals_candidate.percent
        else:
            liberals_percent = 0
        if (nationals_percent + liberals_percent) == 0:
            return 0
        return nationals_percent / (nationals_percent + liberals_percent)
    
    for target_election in all_elections:
        transformed_nationals_shares = []
        transformed_previous_nationals_shares = []
        transformed_old_nationals_shares = []
        transformed_swing_averages = []
        for this_election, data in elections.items():
            if (
                int(this_election.year()) < int(target_election.year())
            ):
                previous_elections = elections.previous_elections(this_election)
                if len(previous_elections) > 0:
                    previous_election = previous_elections[-1]
                    previous_results = elections[previous_election]
                else:
                    continue
                if len(previous_elections) > 1:
                    old_election = previous_elections[-2]
                    old_results = elections[old_election]
                else:
                    continue

                swing_sum = 0
                swing_count = 0
                for seat in data.seat_results:
                    this_nationals_share = get_nationals_share(data, seat.name)
                    if (
                        this_nationals_share is None
                        or this_nationals_share == 0
                        or this_nationals_share == 1
                    ):
                        continue
                    if previous_results is not None:
                        previous_nationals_share = get_nationals_share(previous_results, seat.name)
                    if old_results is not None:
                        old_nationals_share = get_nationals_share(old_results, seat.name)
                    if (
                        this_nationals_share is not None
                        and previous_nationals_share is not None
                        and this_nationals_share != 0
                        and previous_nationals_share != 0
                        and this_nationals_share != 1
                        and previous_nationals_share != 1
                    ):
                        this_transformed = transform_vote_share(this_nationals_share * 100)
                        previous_transformed = transform_vote_share(previous_nationals_share * 100)
                        transformed_nationals_shares.append(this_transformed)
                        transformed_previous_nationals_shares.append(previous_transformed)
                        swing_sum += this_transformed - previous_transformed
                        swing_count += 1
                        if (
                            old_nationals_share is not None
                            and old_nationals_share != 0
                            and old_nationals_share != 1
                        ):
                            transformed_old_nationals_shares.append(transform_vote_share(old_nationals_share * 100))
                        else:
                            transformed_old_nationals_shares.append(transformed_previous_nationals_shares[-1])
                if (swing_count > 4): ## avoid using really small samples
                    transformed_swing_averages.append(swing_sum / swing_count)

        if len(transformed_nationals_shares) < 4:
            continue

        inputs_np = numpy.transpose(numpy.array([transformed_previous_nationals_shares, transformed_old_nationals_shares]))
        results_np = numpy.array(transformed_nationals_shares)
        reg = LinearRegression().fit(inputs_np, results_np)

        #calculate rmse of transformed_nationals_shares following this regression:
        predictions = [
            (reg.coef_[0] * transformed_previous_nationals_shares[i] +
            reg.coef_[1] * transformed_old_nationals_shares[i] +
            reg.intercept_)
            for i in range(len(transformed_nationals_shares))
        ]
        residuals = [
            transformed_nationals_shares[i] - predictions[i]
            for i in range(len(transformed_nationals_shares))
        ]
        mean_residual = numpy.mean(residuals)
        adjusted_residuals = [a - mean_residual for a in residuals]
        rmse = numpy.sqrt(numpy.mean(numpy.square(adjusted_residuals)))

        # calculate sample kurtosis of residuals
        this_kurtosis = two_tail_kurtosis(adjusted_residuals)

        swing_rmse = numpy.sqrt(numpy.mean([a * a for a in transformed_swing_averages]))
        swing_kurtosis = (
            two_tail_kurtosis(transformed_swing_averages)
            if len(transformed_swing_averages) > 4
            else 0
        )

        filename = (f'./Nationals/{target_election.year()}{target_election.region()}_stats.csv')
        with open(filename, 'w') as f:
            f.write(f'prev_coef,old_coef,intercept,seat_rmse,seat_kurtosis,overall_rmse,overall_kurtosis\n')
            f.write(f'{reg.coef_[0]},{reg.coef_[1]},{reg.intercept_},{rmse},{this_kurtosis},{swing_rmse},{swing_kurtosis}')

        previous_elections = elections.previous_elections(target_election)
        if len(previous_elections) > 0:
            previous_election = previous_elections[-1]
            previous_results = elections[previous_election]
        else:
            continue
        if len(previous_elections) > 1:
            old_election = previous_elections[-2]
            old_results = elections[old_election]
        else:
            continue

        # generate predictions for individual seats 
        predictions = []
        for seat in previous_results.seat_results:
            previous_nationals_share = 0
            if previous_results is not None:
                previous_nationals_share = get_nationals_share(previous_results, seat.name)
            if old_results is not None:
                old_nationals_share = get_nationals_share(old_results, seat.name)
            provisional_nats_share = previous_nationals_share
            if (
                previous_nationals_share is not None
                and old_nationals_share is not None
                and previous_nationals_share != 0
                and old_nationals_share != 0
                and previous_nationals_share != 1
                and old_nationals_share != 1
            ):
                transformed_previous_nationals_share = transform_vote_share(previous_nationals_share * 100)
                transformed_old_nationals_share = transform_vote_share(old_nationals_share * 100)   
                transformed_provisional_nats_share = transformed_previous_nationals_share * reg.coef_[0] + transformed_old_nationals_share * reg.coef_[1] + reg.intercept_
                provisional_nats_share = detransform_vote_share(transformed_provisional_nats_share) * 0.01
            # sanity checking from overambitious regression
            if provisional_nats_share < previous_nationals_share * 0.5:
                provisional_nats_share = previous_nationals_share * 0.5
            elif provisional_nats_share > (previous_nationals_share + 1) / 2:
                provisional_nats_share = (previous_nationals_share + 1) / 2
            # Andrew Lethlean is the confirmed Coalition candidate, while no
            # Liberal candidate is currently expected to contest Bendigo East.
            if (target_election == ElectionCode(2026, 'vic')
                    and seat.name == 'Bendigo East'):
                provisional_nats_share = 1.0
            predictions.append((seat.name, provisional_nats_share))
        
        filename = (f'./Nationals/{target_election.year()}{target_election.region()}_seats.csv')
        with open(filename, 'w') as f:
            f.write(f'seat,prediction\n')
            for i in range(len(predictions)):
                f.write(f'{predictions[i][0]},{predictions[i][1]}\n')
