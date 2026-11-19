# Live publication examples

These fictional packages illustrate the client/server handoff without uploading
real forecasts. `package.json` is an election-night report; `morning-package.json`
is its next-morning continuation with previous, today and since-start comparisons.
The latter retains yesterday by pointing to the former's since-start window.
There is no new yesterday commentary request. The source report can arrive at a
server after the morning package without replacing the server's latest forecast.

The morning example repeats the same fictional numerical report, so its previous
and today comparisons contain local no-count notices. Its since-start comparison
and evidence still describe the accumulated earlier change. The examples do not
assert that a whole real reporting day ended at the source capture's time.

`statewide-preference-flows.json` is a minimal live report for testing the
preference-flow display. Its fictional row gives Labor first-preference ballots
a median 60% flow to Liberal against One Nation. The 25th to 75th percentiles
are 54% and 65%. The example uses the same percentile positions as vote shares.
