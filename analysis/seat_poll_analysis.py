
from sklearn.linear_model import QuantileRegressor

from numpy import array, transpose, dot, average

input_vals = [12.14, 35.7, 9, 43, 15, 30, 32.1, 36, 38, 25.6, 32.7, 25, 26.3, 25.6, 15.9, 17.9, 12.1]
dependent_vals = [48.5, 45.7, 8.98, 34.19, 24.67, 32.96, 26.29, 29.22, 34.86, 26.29, 27.74, 34.86, 29.2, 29.2, 29.2, 29.2, 29.2]
error_vals = [a - b for a ,b in zip(input_vals, dependent_vals)]

input_array=array([input_vals])
input_array = transpose(input_array)
error_array=array(error_vals)

reg = QuantileRegressor(alpha=0, quantile=0.5).fit(input_array, error_array)

projected_errors = []
for exclude in range(0, len(input_array)):
    temp_inputs = input_vals[:exclude] + input_vals[exclude + 1:]
    temp_errors = error_vals[:exclude] + error_vals[exclude + 1:]

    input_array=array([temp_inputs])
    input_array = transpose(input_array)
    error_array=array(temp_errors)  

    reg = QuantileRegressor(alpha=0, quantile=0.5).fit(input_array, error_array)

    coef = reg.coef_
    intercept = reg.intercept_

    excluded_input = input_vals[exclude]
    projected_error = coef * excluded_input + intercept
    actual_error = error_vals[exclude]

    projected_errors.append(projected_error[0])

projection_abs_error = [abs(a - b) for a, b in zip(projected_errors, error_vals)]
projection_abs_error_average = sum(projection_abs_error) / len(projection_abs_error)

baseline_abs_error = [abs(a) for a in error_vals]
baseline_abs_error_average = sum(baseline_abs_error) / len(error_vals)



mix_factor = 0.705
mixed_estimate = [a * mix_factor for a in projected_errors]
mixed_abs_error = [abs(a - b) for a, b in zip(mixed_estimate, error_vals)]
mixed_abs_error_average = sum(mixed_abs_error) / len(mixed_abs_error)

print(f'mix_factor: {mix_factor}')
print(f'projection_error_average: {projection_abs_error_average}')
print(f'baseline_error_average: {baseline_abs_error_average}')
print(f'mixed_error_average: {mixed_abs_error_average}')
print(f'coef: {reg.coef_}')
print(f'intercept: {reg.intercept_}')