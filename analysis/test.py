
from sklearn.linear_model import LinearRegression, QuantileRegressor
import numpy as np

input_array = np.array([[0],[1],[2],[3],[4],[5],[6],[7]])
dependent_array = np.array([0,1,2,3,4,5,6,7])
reg_l = LinearRegression().fit(input_array, dependent_array)
reg_q = QuantileRegressor(quantile=0.5, alpha=0).fit(input_array, dependent_array)
print(input_array)
print(dependent_array)
print(f'Coeffs: {reg_l.coef_}\n '
        f'Intercept: {reg_l.intercept_}')
print(f'Coeffs: {reg_q.coef_}\n '
        f'Intercept: {reg_q.intercept_}')

rng = np.random.RandomState(42)
x = np.linspace(start=0, stop=10, num=100)
X = x[:, np.newaxis]
y_true_mean = 10 + 0.5 * x
y_normal = y_true_mean + rng.normal(loc=0, scale=0.5 * x, size=x.shape[0])

print(X)
print(y_normal)

quantiles = [0.05, 0.5, 0.95]
predictions = {}
out_bounds_predictions = np.zeros_like(y_true_mean, dtype=np.bool_)
for quantile in quantiles:
    qr = QuantileRegressor(quantile=quantile, alpha=0)
    y_pred = qr.fit(X, y_normal).predict(X)
    print(qr.coef_)
    print(qr.intercept_)
    print(y_pred)
    predictions[quantile] = y_pred

    if quantile == min(quantiles):
        out_bounds_predictions = np.logical_or(
            out_bounds_predictions, y_pred >= y_normal
        )
    elif quantile == max(quantiles):
        out_bounds_predictions = np.logical_or(
            out_bounds_predictions, y_pred <= y_normal
        )