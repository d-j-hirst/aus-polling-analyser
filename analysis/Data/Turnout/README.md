# Normalized Turnout Evidence

The JSON files in this directory contain validated official turnout evidence
for modelling and coverage analysis. They are source-derived data, not model
outputs.

Regenerate the federal and NSW files from the commissions' final results with:

```bash
cd analysis
./env/bin/python -B turnout_aec.py --election all
./env/bin/python -B turnout_nsw.py --election all
./env/bin/python -B turnout_vic.py --election all
```

Each adapter validates its official inputs before replacing an election file.
AEC ordinary votes are classified as election-day votes before 2010 and as a
combined election-day/early category from 2010. NSW 2015 retains separate
enrolment and provisional categories; later combined categories remain
combined, and no inferred split is stored.
VEC early votes remain combined and its small Marked As Voted category remains
separate from 2010 onward; the 2006 Declaration Votes aggregate remains
combined. VEC did not publish voting-centre figures for the 2014 Prahran and
2018 Ripon recounts. VEC's linked 2018 Brunswick voting-centre page contains
stale totals inconsistent with its district and statewide summaries. Final
district totals are retained for all three, but their unreliable or unavailable
vote-type partitions are deliberately absent.

See [`docs/turnout-data-foundation.md`](../../../docs/turnout-data-foundation.md)
for the shared schema, category policy and planned state-election adapters.
