# 2026 SA turnout inputs

These recorded counts document early voting and postal applications before
the SA 2026 election. They provide a reproducible record of the published
operational evidence.

`turnout-inputs.csv` stores the ECSA district counts and their quantity dates.
It is retained source data and is not currently consumed by the forecast.

- `early_voting_mark_offs` is the number of electors marked off as voting early
  through 20 March 2026. These are ballots issued, not formal votes, and 2026
  early-voting-centre ballots were ordinary rather than declaration votes.
- `postal_vote_applications` is the number of applications received through
  16 March 2026. It is not the number of packs issued, votes returned, votes
  accepted, or formal votes counted.

Source: <https://www.ecsa.sa.gov.au/se2026-daily-tally>

The published early-markoff district rows sum to 454,860, while ECSA's displayed
total is 454,862. The CSV preserves the published district rows. Postal district
rows sum to ECSA's displayed total of 174,121. The Lee postal total is displayed
as `3.833` in the HTML; it is recorded as 3,833 because the row values and the
state total establish that the full stop is a thousands-separator typo.

The results feed separates named early-voting-centre counts from
`Early Voting Absent Ordinary Votes`, used when an elector voted early outside
their enrolled district, and from the much smaller
`Early Voting Declaration Votes` category. The normalized final turnout
dataset compares the mark-offs with all applicable early-vote categories,
including votes cast outside the elector's district, rather than named EVCs alone.

## Known polling-place changes

ECSA advised before polling day that Whyalla Norrie North, Whyalla Norrie
North-West and Willsden would not open because of late staffing illness. Their
electors were redirected to nearby booths. The ECSA loader therefore excludes
these three Giles booths from the 2026 live baseline rather than treating their
zero returns as votes still to come.

Leigh Creek was not cancelled: a mobile team was expected to take ordinary
votes and report them later. The unexplained zero-vote Port Augusta Early Voting
Centre in Stuart is also retained, because there is not enough evidence to
classify it as cancelled.
