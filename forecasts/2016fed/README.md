# 2016 federal live replay

This configuration prepares Australian Election Forecasts for a diagnostic replay
of the 2 July 2016 election. It combines the existing national polling outputs
with a newly assembled seat configuration and the archived AEC House feed.
It does not reconstruct an original 2016 seat forecast: the old `.pol2` contains
a national model but no seats, regions, projections or simulations.

## Opening and initialising

Open `Federal Election 2016 turnout replay.pol2` from the repository root. The
original `Federal Election 2016.pol2` is preserved. The replay is a version-69
project with fresh reports and manual overrides cleared.

Run this macro once before starting the sequence:

```text
prepare-model:0;run-projection:0
```

`prepare-model` reads the already-generated CSV trends and adjustment parameters;
it does not fit Stan models. The projection makes those inputs available to the
simulations. The saved replay intentionally contains no fabricated fitted cache.
The existing CSV outputs end on 30 June 2016. They are reused as stored, without
refitting them to the current revision of the polling source files.

The project has Standard, Nowcast and Live simulations, each with 10,000
iterations. Only Live participates in an automatic snapshot sequence. Its output
folder is `2016fed-turnout2`. The sequence already contains 60 selected captures;
the same codes are in `forecasts/2016fed/live-snapshots/feeds/starter-codes.txt`.
Start it with **Run snapshot sequence** on Results after initialisation.

The sequence covers sparse election-night data, later eastern and WA reporting,
NXT/independent/Greens contests, declaration counting and the eventual final
return. In particular it includes both sides of the 11 July Bankstown account
replacement and the 12 July delayed Woden PPVC return. It is a configuration
diagnostic, rather than a substitute for the complete checkpoint.

## Seat and candidate evidence

All 150 divisions and 994 House candidates come from the original
[AEC preload archive](ftp://mediafeedarchive.aec.gov.au/20499/Detailed/Preload/aec-mediafeed-Detailed-Preload-20499-20160629114751.zip).
The polling-district companion supplies booth classifications and locations.
Running-party entries include every candidate, with asterisks for repeated IND
and OTH entries. Named independent settings are restricted to incumbent members
Cathy McGowan and Andrew Wilkie, and the documented Windsor/Oakeshott campaigns.
Merely standing as an independent does not establish prominence or continuity.
Neither Windsor nor Oakeshott is marked as a returning 2013 independent.

Previous margins use the preload's historical Labor/Coalition TPP votes, including
the AEC's notional redistribution baselines. These are previous-election counts,
not final 2016 results. Fenner uses Fraser's history and Whitlam uses Throsby's.
Burt uses Canning for an initial FP distribution while retaining its own
new-boundary notional TPP margin. This approximation deserves checking early in
the diagnostic. Candidate retirement and first-term settings use earlier winners
and the current roster; Canning, Griffith and North Sydney account for their
intervening by-elections.

Liberals and Nationals have separate forecast groups. Queensland LNP and NT CLP
stay with the Liberal group. Where both Coalition parties stood previously, their
split comes from the preload's historical FP votes. Four new dual contests need
explicit assumptions: Nationals receive 55% of the combined Coalition primary in
open Murray and 10% in Ballarat, Indi and McEwen. These are editable starting
assumptions, not observed 2016 shares. The contemporaneous
[market summary](https://kevinbonham.blogspot.com/2016/06/seat-betting-watch-last-few-days.html?m=0)
reported the Nationals moving ahead in Murray.

## Prominent campaigns and preferences

NXT remains a separate party in seat results, but has no separate fitted national
trend. Its national support is included in Others. All South Australian NXT
candidates use the existing prominent-minor path. Mayo uses the requested $1.50
odds; Grey and Barker use broad assumed odds of $2.857 and $4 respectively. The
latter two are diagnostic assumptions informed by campaign evidence, not exact
archived market quotes. Other SA seats start with the generic prominent-minor
prior; the seven NXT campaigns outside SA have no separate prominent prior.
See contemporary reporting on the
[Grey campaign poll](https://www.abc.net.au/news/2016-06-20/nick-xenophon-team-polls-well-in-grey/7523816)
and the [late-campaign market assessment](https://kevinbonham.blogspot.com/2016/06/seat-betting-watch-last-few-days.html?m=0).

Cowper uses Oakeshott's reported 25% primary poll and $1.70 odds, within the
reported $1.65–$1.75 late market range. New England uses Windsor's 36% primary in
the June Newspoll, described in the
[contemporary poll roundup](https://kevinbonham.blogspot.com/2016/06/rolling-poll-roundup-reachtel-vs-rest.html).
Both polls use credibility setting 2, an explicit moderate-weight assumption.
Batman uses $2.00 for the Greens to represent the split late market.

The current direct seat-poll adjustment applies to independents. NXT polls inform
the broad starting assessment but are not entered as unused `sPolls=NXT` rows.
This setup does not extend that statistical mechanism. One Nation and the other
unmodelled minor parties remain in generic Others; ballot multiplicities are
retained. PUP is also generic Others: there is no PUP/UAP party, national target,
named Herbert candidate setting or separate series in the replay model. Its
Herbert ballot entry remains counted in the OTH multiplicity, and its actual
votes remain in the feed. Historical unmodelled parties use the ordinary capped
Others prior rather than transferring Palmer's personal campaign to a named
candidate in Fairfax.

The initial setup wrongly retained the old UAP national series despite PUP's
near-total withdrawal from the ballot. This caused unattainable national targets
and an extreme Herbert prior. The corrected setup removes UAP from the 2016
significant-party list and restores its historical component to the residual
Others prior. Non-Stan trend adjustments/fundamentals are regenerated for that
definition. Existing inclusive Others/major/Greens/TPP trend CSVs remain in use.
The unused UAP output CSVs may stay in the cache; the model reads only configured
series. Raw historical polling columns and published preference data remain as
evidence for reconstructing poll totals, with PUP using the same conversion rate
as Others. This does not create a PUP forecast target. Changes are registered in
source provenance; no stale Stan output is relabelled as freshly fitted.

Reopen the corrected project and initialise it with the macro above. Its cached
projection, odds calibration and live baseline are cleared, so they are rebuilt
under the corrected grouping. Keep the original `2016fed-turnout1` outputs for
diagnosis; the fresh sequence writes `2016fed-turnout2`.

Most non-classic preference defaults reuse the established federal settings,
with NXT replacing Centre Alliance. Liberal/National preferences between Greens
and Labor start at 31% to Greens, reflecting the 2013 Batman/Wills flows and the
announced continuation of Labor-first recommendations, rather than using final
2016 flows. See [the pre-election explanation](https://www.abc.net.au/news/2016-05-14/liberal-preferences-and-green-seat-prospects-at-the-2016-electio/9388778).

## Turnout and archived captures

The prepared turnout prior has 256 possible outcomes for 8,928 booth/category
accounts. Its calibration uses elections before 2016. Election-eve early
attendance and postal applications control expected formal counts; early
attendance is national because issuing centres do not identify voters' resident
divisions. New PPVC size estimates use 2013 versus 2010, not their eventual 2016
counts. The polling-place roll comes from the original preload.

The [AEC timetable](https://www.aec.gov.au/elections/federal_elections/2016/timetable.htm)
sets poll close at 18:00 on 2 July and the declaration receipt deadline at 18:00
on 15 July. Existing declaration-processing and unreported-place settings remain
in use; this preparation does not tune them to the eventual 2016 totals.
Election-specific seat volatility was regenerated using pre-2016 history. The
new regional polling calibration has fewer than its required four prior polling
elections for this target, so the engine's existing regional defaults apply.

The feed directory retains 1,149 captures with changed House accounts. Another
429 are preserved in `no-data-change`, and 259 captures of event 24310 (2019)
are preserved in `other-elections/2019fed`. Only consecutive equal House accounts
are filtered; reversals, withdrawals, restorations, count status and genuine
count changes remain. A hash manifest beside the directory records every move.
Neither original archive bytes nor source timestamps are rewritten.

Standalone production file readers checked the package, prior, preload and a
selection of election-night and postcount captures. The native model input reader
also prepared the existing trends successfully without generating forecast draws.
The GUI and full forecast have not been run as part of preparation.
