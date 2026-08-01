"""Optional historical diagnostics for saved trend adjustments.

Parent: trend_adjust.py coordinates generation; this optional module checks
the predictiveness of the saved adjustment outputs.
"""

import math
import os
import statistics

from election_code import no_target_election_marker
from poll_transform import transform_vote_share, detransform_vote_share
from trend_adjust_cutoffs import CutoffTrendData
from trend_adjust_io import load_adjustment_data, adjustment_parameters_at


def check_poll_predictiveness(config):
    """Compare saved TPP methods at the user-requested forecast horizon."""

    for poll_day in [config.check_day]:
        baseline_errors = []
        poll_errors = []
        fundamentals_errors = []
        mixed_errors = []
        for election in config.elections:
            if election == no_target_election_marker:
                continue
            if config.check_region == "nofed" and election.region() == "fed":
                continue
            elif (config.check_region != "" and config.check_region != "nofed"
                and election.region() != config.check_region):
                continue
            party_group = "TPP"
            party = "@TPP"
            adjust_filename = (f'./Adjustments/adjust_{election.year()}'
                        f'{election.region()}_{party_group}.csv')
            cutoff_filename = (
                f'./Outputs/Cutoffs/cutoffs_{election.short()}.csv'
            )
            if not os.path.isfile(cutoff_filename):
                continue
            trend_data = CutoffTrendData(cutoff_filename)
            poll_trend = trend_data.value_at(
                party, poll_day, 50, default_value=None
            )
            if poll_trend is None:
                continue
            adjustment_data = load_adjustment_data(adjust_filename)
            parameters = adjustment_parameters_at(
                adjustment_data,
                transform_vote_share(poll_trend),
                poll_day)
            (poll_bias, fund_bias, mixed_bias, lower_error, upper_error,
             lower_kurtosis, upper_kurtosis, mix_factor) = parameters
            fundamentals_filename = (f'./Fundamentals/fundamentals_{election.year()}'
                        f'{election.region()}.csv')
            with open(fundamentals_filename, 'r') as f:
                fundamentals = next(float(obj.split(',')[1]) for obj in f.readlines()
                                    if obj.split(',')[0] == "@TPP")
            poll_adjusted = transform_vote_share(poll_trend) - poll_bias
            fund_adjusted = transform_vote_share(fundamentals) - fund_bias
            mixed_transformed = (
                poll_adjusted * mix_factor
                + fund_adjusted * (1 - mix_factor)
                - mixed_bias)
            mixed = detransform_vote_share(mixed_transformed)
            try:
                with open('./Data/eventual-results.csv', 'r') as f:
                    eventual_result = next(float(a.split(",")[3]) for a in f.readlines()
                                        if int(a.split(",")[0]) == election.year()
                                        and a.split(",")[1] == election.region()
                                        and a.split(",")[2] == party)
            except StopIteration:
                continue
            baseline_errors.append(50 - eventual_result)
            poll_errors.append(poll_trend - eventual_result)
            fundamentals_errors.append(fundamentals - eventual_result)
            mixed_errors.append(mixed - eventual_result)
            # print(party_group)
            # print(f"poll_bias: {poll_bias}")
            # print(f"fund_bias: {fund_bias}")
            # print(f"mixed_bias: {mixed_bias}")
            # print(f"lower_error: {lower_error}")
            # print(f"upper_error: {upper_error}")
            # print(f"lower_kurtosis: {lower_kurtosis}")
            # print(f"upper_kurtosis: {upper_kurtosis}")
            # print(f"mix_factor: {mix_factor}")
            # print(f"poll trend: {poll_trend}")
            # print(f"fundamentals: {fundamentals}")
            # print(f"mixed: {mixed}")
            # print(f"eventual_result: {eventual_result}")
        
        try:
            print(f"poll day: {poll_day}")
            print(f"Average baseline error:      {statistics.mean([abs(a) for a in baseline_errors])}")
            print(f"Average poll error:          {statistics.mean([abs(a) for a in poll_errors])}")
            print(f"Average fundamentals error:  {statistics.mean([abs(a) for a in fundamentals_errors])}")
            print(f"Average mixed error:         {statistics.mean([abs(a) for a in mixed_errors])}")
            print(f"Median baseline error:      {statistics.median([abs(a) for a in baseline_errors])}")
            print(f"Median poll error:          {statistics.median([abs(a) for a in poll_errors])}")
            print(f"Median fundamentals error:  {statistics.median([abs(a) for a in fundamentals_errors])}")
            print(f"Median mixed error:         {statistics.median([abs(a) for a in mixed_errors])}")
            print(f"baseline RMSE:      {math.sqrt(statistics.mean([abs(a) ** 2 for a in baseline_errors]))}")
            print(f"poll RMSE:          {math.sqrt(statistics.mean([abs(a) ** 2 for a in poll_errors]))}")
            print(f"fundamentals RMSE:  {math.sqrt(statistics.mean([abs(a) ** 2 for a in fundamentals_errors]))}")
            print(f"mixed RMSE:         {math.sqrt(statistics.mean([abs(a) ** 2 for a in mixed_errors]))}")
        except statistics.StatisticsError:
            print("Could not check statistics as there were no data. Make sure you use --election all so that the program uses all available elections")
