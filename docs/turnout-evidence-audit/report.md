# Turnout evidence audit

This report describes the published vote and application counts available for turnout analysis. It helps readers understand which counts can be compared with final election results and where the evidence has gaps.

The audit reads the normalized election datasets, selects exact counts and closely approximate counts, and checks their category and geographic coverage. It records the latest eligible count from each source and the final-result quantities that describe the same voting pool. Reproduction commands appear below.

Generated 2026-10-01T22:55:08.555611+00:00.

35 datasets; 35 elections with final seat evidence; 49996 operational rows; 3038 latest precise controls.

## Inclusion rule

Include reported exact point counts and counts reconstructed from published rates rounded to 0.1 percentage point or finer. Exclude forecasts, bounds and loose/unspecified approximations. Retain the last eligible observation per source, measure, geography, district and reconciliation status through polling day. Later rows stay in the source dataset but do not enter the pre-election controls.

The four reviewed one-decimal district tables retain that precision when trailing zeros disappear in ingestion. The JSON records rate precision, reference count and a conservative rounding half-width. Final-category quantities retain missing values as null.

| Selection | Rows |
| --- | ---: |
| after_polling_day | 5984 |
| bound | 8 |
| close_rate | 720 |
| coarse_or_unspecified_approximation | 14 |
| exact | 43267 |
| forecast | 3 |
| superseded_precise_rows | 40949 |

Exact/close-rate row counts above include superseded daily observations. Controls below are alternative source snapshots, not independent samples. Never add a state total to its districts or pool duplicate source reports. Applications, issues, returns, accepted and scrutiny-ready counts remain separate measures.

## Final evidence coverage

| Election | Final seats (all totals) | Category rows | Partial / missing category seats | Latest controls (close rate) | Final categories |
| --- | ---: | ---: | ---: | ---: | --- |
| 2004fed | 150 (150) | 750 | 0 / 0 | 0 (0) | absent, declaration_early, election_day_ordinary, postal, provisional |
| 2005wa | 57 (57) | 285 | 0 / 0 | 3 (0) | absent, early_in_person, election_day_ordinary, postal, provisional |
| 2006qld | 89 (89) | 535 | 0 / 0 | 0 (0) | absent, early_in_person, election_day_ordinary, mobile_or_institution, postal |
| 2006sa | 47 (47) | 94 | 0 / 0 | 5 (0) | declaration_combined, election_day_ordinary |
| 2006vic | 88 (88) | 435 | 0 / 1 | 2 (0) | absent, declaration_combined, early_combined, election_day_ordinary, postal |
| 2007fed | 150 (150) | 750 | 0 / 0 | 2 (0) | absent, declaration_early, election_day_ordinary, postal, provisional |
| 2008wa | 59 (59) | 295 | 0 / 0 | 2 (0) | absent, early_in_person, election_day_ordinary, postal, provisional |
| 2009qld | 89 (89) | 535 | 0 / 0 | 0 (0) | absent, early_in_person, election_day_ordinary, mobile_or_institution, postal |
| 2010fed | 150 (150) | 750 | 0 / 0 | 300 (0) | absent, declaration_early, ordinary_combined, postal, provisional |
| 2010sa | 47 (47) | 94 | 0 / 0 | 0 (0) | declaration_combined, election_day_ordinary |
| 2010vic | 88 (88) | 528 | 0 / 0 | 1 (0) | absent, early_combined, election_day_ordinary, marked_as_voted, postal, provisional |
| 2012qld | 89 (89) | 535 | 0 / 0 | 0 (0) | absent, early_in_person, election_day_ordinary, mobile_or_institution, postal |
| 2013fed | 150 (150) | 750 | 0 / 0 | 300 (0) | absent, declaration_early, ordinary_combined, postal, provisional |
| 2013wa | 59 (59) | 295 | 0 / 0 | 2 (0) | absent, early_in_person, election_day_ordinary, postal, provisional |
| 2014sa | 47 (47) | 94 | 0 / 0 | 1 (0) | declaration_combined, election_day_ordinary |
| 2014vic | 88 (88) | 522 | 0 / 1 | 88 (88) | absent, early_combined, election_day_ordinary, marked_as_voted, postal, provisional |
| 2015nsw | 93 (93) | 651 | 0 / 0 | 94 (0) | absent, early_combined, election_day_ordinary, enrolment, postal, provisional, remote_electronic |
| 2015qld | 89 (89) | 980 | 0 / 0 | 1 (0) | absent, declaration_early, early_in_person, election_day_ordinary, enrolment, mobile_or_institution, postal, provisional, remote_electronic, telephone |
| 2016fed | 150 (150) | 750 | 0 / 0 | 300 (0) | absent, declaration_early, ordinary_combined, postal, provisional |
| 2017qld | 93 (93) | 931 | 0 / 0 | 0 (0) | absent, declaration_early, early_in_person, election_day_ordinary, enrolment, mobile_or_institution, postal, provisional, remote_electronic, telephone |
| 2017wa | 59 (59) | 295 | 0 / 0 | 1 (0) | absent, early_in_person, election_day_ordinary, postal, provisional |
| 2018sa | 47 (47) | 94 | 0 / 0 | 3 (0) | declaration_combined, election_day_ordinary |
| 2018vic | 88 (88) | 516 | 0 / 2 | 2 (0) | absent, early_combined, election_day_ordinary, marked_as_voted, postal, provisional |
| 2019fed | 151 (151) | 755 | 0 / 0 | 302 (0) | absent, declaration_early, ordinary_combined, postal, provisional |
| 2019nsw | 93 (93) | 558 | 0 / 0 | 0 (0) | absent, early_combined, election_day_ordinary, enrolment_or_provisional, postal, remote_electronic |
| 2020qld | 93 (93) | 838 | 0 / 0 | 190 (0) | absent, declaration_combined, declaration_early, early_in_person, election_day_ordinary, mobile_or_institution, postal, telephone |
| 2021wa | 59 (59) | 295 | 0 / 0 | 180 (177) | absent, early_in_person, election_day_ordinary, postal, provisional |
| 2022fed | 151 (151) | 755 | 0 / 0 | 154 (0) | absent, declaration_early, ordinary_combined, postal, provisional |
| 2022sa | 47 (47) | 94 | 0 / 0 | 96 (94) | declaration_combined, election_day_ordinary |
| 2022vic | 87 (87) | 522 | 0 / 0 | 177 (174) | absent, early_combined, election_day_ordinary, marked_as_voted, postal, provisional |
| 2023nsw | 93 (93) | 465 | 0 / 0 | 189 (186) | absent, early_combined, election_day_ordinary, enrolment_or_provisional, postal |
| 2024qld | 93 (93) | 838 | 0 / 0 | 96 (1) | absent, declaration_combined, declaration_early, early_in_person, election_day_ordinary, mobile_or_institution, postal, telephone |
| 2025fed | 150 (150) | 750 | 0 / 0 | 450 (0) | absent, declaration_early, ordinary_combined, postal, provisional |
| 2025wa | 59 (59) | 354 | 0 / 0 | 1 (0) | absent, early_in_person, election_day_ordinary, mobile_or_institution, postal, provisional |
| 2026sa | 47 (47) | 796 | 0 / 0 | 96 (0) | absent, declaration_early, early_in_person, election_day_ordinary, mobile_or_institution, other, postal, provisional |

Districts with final totals but no category partition:
- 2006vic: Ferntree Gully District.
- 2014vic: Prahran District.
- 2018vic: Brunswick District, Ripon District.

## Excluded controls

Post-election operational quantities remain available for later progression/acceptance diagnostics, but do not substitute for a pre-election count. In particular, QLD 2024’s final postal workbook is dated after polling; its district issue/return/acceptance rows are not election-eve controls.

| Election / source | Measure | Reason | Rows | Quantity dates | Example label |
| --- | --- | --- | ---: | --- | --- |
| 2004fed / aec-2004-behind-the-scenes | postal_ballots_issued_cumulative | coarse_or_unspecified_approximation | 1 | 2004-10-08 to 2004-10-08 | Almost 760,000 postal votes issued |
| 2006qld / qsc-2009-ecq-postal-evidence | postal_applications_cumulative | coarse_or_unspecified_approximation | 1 | 2006-09-07 to 2006-09-07 | About 141,000 postal-vote applications received |
| 2008wa / waec-2008-annual-report | postal_applications_cumulative | bound | 1 | 2008-09-02 to 2008-09-02 | More than 65,000 postal-vote applications received |
| 2008wa / waec-2008-annual-report | prepoll_votes_cast_cumulative | bound | 1 | 2008-09-02 to 2008-09-02 | More than 60,000 early in-person votes cast |
| 2009qld / abc-2009qld-election-day | postal_applications_cumulative | coarse_or_unspecified_approximation | 1 | 2009-03-21T12:07:00+10:00 to 2009-03-21T12:07:00+10:00 | About 213,000 postal-vote requests received |
| 2010sa / abc-2010sa-postal-update | postal_applications_cumulative | bound | 1 | 2010-03-14 to 2010-03-14 | More than 80,000 postal-vote applications received |
| 2010vic / abc-2010vic-election-eve | prepoll_votes_cast_cumulative | coarse_or_unspecified_approximation | 1 | 2010-11-26T18:21:00+11:00 to 2010-11-26T18:21:00+11:00 | About half a million Victorians had voted early |
| 2014sa / abc-2014sa-election-eve | postal_applications_cumulative | coarse_or_unspecified_approximation | 1 | 2014-03-13 to 2014-03-13 | Approximately 86,000 postal-vote applications received |
| 2014sa / abc-2014sa-election-eve | pre_election_votes_cast_cumulative | forecast | 1 | 2014-03-13 to 2014-03-13 | Forecast of about 160,000 early or postal voters |
| 2014sa / abc-2014sa-election-eve | prepoll_votes_cast_cumulative | bound | 1 | 2014-03-12T18:00:00+10:30 to 2014-03-12T18:00:00+10:30 | More than 50,000 pre-poll votes cast by Wednesday evening |
| 2014sa / abc-2014sa-election-eve | prepoll_votes_cast_cumulative | forecast | 1 | 2014-03-13 to 2014-03-13 | Forecast of more than 70,000 pre-poll votes by Friday evening |
| 2017qld / antony-green-2017qld-election-eve | postal_ballots_issued_cumulative | coarse_or_unspecified_approximation | 1 | 2017-11-25T07:58:00+10:00 to 2017-11-25T07:58:00+10:00 | About 369,000 postal voters; ballots returned were fewer |
| 2017qld / antony-green-2017qld-election-eve | prepoll_votes_cast_cumulative | coarse_or_unspecified_approximation | 1 | 2017-11-25T07:58:00+10:00 to 2017-11-25T07:58:00+10:00 | About 717,000 people had cast pre-poll votes |
| 2017wa / abc-2017wa-election-eve | prepoll_votes_cast_cumulative | forecast | 1 | 2017-03-10T19:14:00+08:00 to 2017-03-10T19:14:00+08:00 | WAEC expected final early voting to exceed 180,000 |
| 2018sa / abc-2018sa-election-morning | postal_ballots_issued_cumulative | coarse_or_unspecified_approximation | 1 | 2018-03-17T09:10:00+10:30 to 2018-03-17T09:10:00+10:30 | About 95,000 people reported as having voted by post |
| 2018sa / abc-2018sa-election-morning | pre_election_votes_cast_cumulative | bound | 1 | 2018-03-17T09:10:00+10:30 to 2018-03-17T09:10:00+10:30 | More than 215,000 people reported as voting before election day |
| 2018sa / abc-2018sa-election-morning | prepoll_votes_cast_cumulative | coarse_or_unspecified_approximation | 1 | 2018-03-17T09:10:00+10:30 to 2018-03-17T09:10:00+10:30 | About 120,000 votes cast at pre-polling centres |
| 2018vic / abc-2018vic-election-eve | early_and_postal_votes_recorded_cumulative | bound | 1 | 2018-11-23 to 2018-11-23 | More than 1.6 million early and postal votes cast |
| 2019nsw / abc-2019nsw-election-eve | pre_election_votes_cast_cumulative | bound | 1 | 2019-03-22 to 2019-03-22 | More than 1.3 million people voted before election day |
| 2019nsw / abc-2019nsw-election-eve | remote_electronic_votes_cast_cumulative | coarse_or_unspecified_approximation | 1 | 2019-03-22 to 2019-03-22 | 220,000 people used iVote |
| 2020qld / antony-green-2020qld-election-eve | postal_votes_ready_for_election_night_count | bound | 1 | 2020-10-31T10:30:00+10:00 to 2020-10-31T10:30:00+10:00 | At least 320,000 postal votes available election night |
| 2020qld / antony-green-2020qld-election-eve | prepoll_votes_ready_for_election_night_count | coarse_or_unspecified_approximation | 1 | 2020-10-31T10:30:00+10:00 to 2020-10-31T10:30:00+10:00 | Around 925,000 pre-poll votes available election night |
| 2022fed / aec-2022-postal-operational | postal_applications_cumulative | after_polling_day | 151 | 2022-06-03 to 2022-06-03 | Valid postal vote applications received |
| 2022fed / aec-2022-postal-operational | postal_votes_returned_cumulative | after_polling_day | 151 | 2022-06-03 to 2022-06-03 | Postal votes returned |
| 2024qld / antony-green-2024qld-election-eve | prepoll_votes_ready_for_election_night_count | coarse_or_unspecified_approximation | 1 | 2024-10-26 to 2024-10-26 | Around three-quarters of final pre-polls countable election night |
| 2024qld / ecq-2024-postal-operational | postal_ballots_issued_cumulative | after_polling_day | 94 | 2024-11-05 to 2024-11-05 | Postal ballots issued - statewide published total |
| 2024qld / ecq-2024-postal-operational | postal_ballots_returned_cumulative | after_polling_day | 94 | 2024-11-05 to 2024-11-05 | Postal ballots returned - statewide published total |
| 2024qld / ecq-2024-postal-operational | postal_votes_accepted_cumulative | after_polling_day | 94 | 2024-11-05 to 2024-11-05 | Postal votes accepted - statewide published total |
| 2025fed / aec-2025-postal-operational | postal_applications_cumulative | after_polling_day | 2700 | 2025-05-04 to 2025-05-21 | Valid postal vote applications received |
| 2025fed / aec-2025-postal-operational | postal_votes_returned_cumulative | after_polling_day | 2700 | 2025-05-04 to 2025-05-21 | Postal votes returned |
| 2025wa / antony-green-2025wa-election-eve | postal_applications_cumulative | coarse_or_unspecified_approximation | 1 | 2025-03-06 to 2025-03-06 | Around 215,000 postal applications received |
| 2025wa / antony-green-2025wa-election-eve | postal_votes_ready_for_election_night_count | coarse_or_unspecified_approximation | 1 | 2025-03-05 to 2025-03-05 | Around 130,000 returned postals ready for election-night count |

## Comparison and availability manifest

Each row groups controls with the same comparison definition. District counts in a row share an election fold. Final formal/ballot targets and their source-row identities are in `audit.json`; no conversion ratios have been fitted.

“Supported” identifies a defensible target for conversion diagnostics. It does not certify historical availability: contemporaneous records have unverified publication times, while final-reconciled records are retrospective. `observed_at` is a quantity date, not a publication date.

| Election / source | Measure | Geography / status | Latest quantity dates | Controls | Target / decision |
| --- | --- | --- | --- | ---: | --- |
| 2005wa / waec-2005-election-report | postal_applications_cumulative | state / final_reconciled | 2005-02-24 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2005wa / waec-2005-election-report | postal_votes_ready_for_election_night_count | state / final_reconciled | 2005-02-26T18:00:00+08:00 | 1 | different_stage: Scrutiny-ready or other operational quantity has no reviewed final-category target. |
| 2005wa / waec-2005-election-report | prepoll_votes_cast_cumulative | state / final_reconciled | 2005-02-25T18:00:00+08:00 | 1 | supported: Final in-person early pool. |
| 2006sa / ecsa-2006-election-report | postal_applications_cumulative | state / final_reconciled | 2006-03-16 | 1 | combined_only: Postal votes are inside an unsplit declaration pool. |
| 2006sa / ecsa-2006-election-report | postal_ballots_issued_cumulative | state / final_reconciled | 2006-03-16 | 1 | combined_only: Postal votes are inside an unsplit declaration pool. |
| 2006sa / ecsa-2006-election-report | postal_votes_accepted_cumulative | state / final_reconciled | 2006-03-18 | 1 | combined_only: Postal votes are inside an unsplit declaration pool. |
| 2006sa / ecsa-2006-election-report | postal_votes_returned_cumulative | state / final_reconciled | 2006-03-18 | 1 | combined_only: Postal votes are inside an unsplit declaration pool. |
| 2006sa / ecsa-2006-election-report | prepoll_votes_cast_cumulative | state / final_reconciled | 2006-03-17T18:00:00+10:30 | 1 | combined_only: Early votes are inside an unsplit declaration pool. |
| 2006vic / vec-2006-annual-report | postal_applications_cumulative | state / final_reconciled | 2006-11-23 | 1 | missing_target: Target category coverage is incomplete across final districts. |
| 2006vic / vec-2006-annual-report | prepoll_votes_cast_cumulative | state / final_reconciled | 2006-11-24 | 1 | missing_target: Target category coverage is incomplete across final districts. |
| 2007fed / aec-2007-jscem-submission | postal_applications_cumulative | national / final_reconciled | 2007-11-22 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2007fed / aec-2007-jscem-submission | postal_ballots_issued_cumulative | national / final_reconciled | 2007-11-22 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2008wa / waec-2008-election-report | postal_ballots_issued_cumulative | state / final_reconciled | 2008-09-04 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2008wa / waec-2008-election-report | postal_votes_ready_for_election_night_count | state / final_reconciled | 2008-09-06T18:00:00+08:00 | 1 | different_stage: Scrutiny-ready or other operational quantity has no reviewed final-category target. |
| 2010fed / aec-2010-postal-operational | postal_applications_cumulative | elector_division / final_reconciled | 2010-08-19 | 150 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2010fed / aec-2010-prepoll-operational | prepoll_votes_issued_cumulative | administering_division / final_reconciled | 2010-08-19 | 150 | aggregate_only: Attendance belongs to the administering district. Aggregate at the election level; elector-seat conversion needs a crosswalk. |
| 2010vic / abc-2014-vic-election-day-retrospective | pre_election_votes_cast_cumulative | state / final_reconciled | 2010-11-26 | 1 | different_stage: Scrutiny-ready or other operational quantity has no reviewed final-category target. |
| 2013fed / aec-2013-postal-operational | postal_applications_cumulative | elector_division / final_reconciled | 2013-09-05 | 150 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2013fed / aec-2013-prepoll-operational | prepoll_votes_issued_cumulative | administering_division / final_reconciled | 2013-09-06 | 150 | aggregate_only: Attendance belongs to the administering district. Aggregate at the election level; elector-seat conversion needs a crosswalk. |
| 2013wa / antony-green-2013wa-election-eve | postal_votes_ready_for_election_night_count | state / contemporaneous | 2013-03-08 | 1 | different_stage: Scrutiny-ready or other operational quantity has no reviewed final-category target. |
| 2013wa / antony-green-2013wa-election-eve | prepoll_votes_ready_for_election_night_count | state / contemporaneous | 2013-03-08 | 1 | different_stage: Scrutiny-ready or other operational quantity has no reviewed final-category target. |
| 2014sa / antony-green-2014sa-retrospective | prepoll_votes_cast_cumulative | state / final_reconciled | 2014-03-14 | 1 | combined_only: Early votes are inside an unsplit declaration pool. |
| 2014vic / antony-green-2014vic-election-eve | early_and_postal_votes_recorded_cumulative | elector_division / contemporaneous | 2014-11-28T18:00:00+11:00 | 1 | missing_target: No matching final category rows for this geography. |
| 2014vic / antony-green-2014vic-election-eve | early_and_postal_votes_recorded_cumulative | elector_division / contemporaneous | 2014-11-28T18:00:00+11:00 | 87 | supported: Combined early/postal conversion only; do not split the control. |
| 2015nsw / nswec-2015nsw-prepoll-transactions | prepoll_votes_cast_cumulative | elector_division / contemporaneous | 2015-03-27 | 93 | supported: Match the named pre-poll ordinary source rows; exclude other early modes. |
| 2015nsw / nswec-2015nsw-prepoll-transactions | prepoll_votes_cast_cumulative | state / contemporaneous | 2015-03-27 | 1 | supported: Match the named pre-poll ordinary source rows; exclude other early modes. |
| 2015qld / ecq-2014-15-annual-report | postal_ballots_issued_cumulative | state / final_reconciled | 2015-01-28 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2016fed / aec-2016-postal-operational | postal_applications_cumulative | elector_division / final_reconciled | 2016-07-01 | 150 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2016fed / aec-2016-prepoll-operational | prepoll_votes_issued_cumulative | administering_division / final_reconciled | 2016-07-01 | 150 | aggregate_only: Attendance belongs to the administering district. Aggregate at the election level; elector-seat conversion needs a crosswalk. |
| 2017wa / abc-2017wa-postal-update | postal_applications_cumulative | state / contemporaneous | 2017-03-07T08:21:00+08:00 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2018sa / antony-green-2018sa-retrospective | postal_applications_cumulative | state / final_reconciled | 2018-03-16 | 1 | combined_only: Postal votes are inside an unsplit declaration pool. |
| 2018sa / antony-green-2018sa-retrospective | postal_ballots_issued_cumulative | state / final_reconciled | 2018-03-16 | 1 | combined_only: Postal votes are inside an unsplit declaration pool. |
| 2018sa / antony-green-2018sa-retrospective | prepoll_votes_cast_cumulative | state / final_reconciled | 2018-03-16 | 1 | combined_only: Early votes are inside an unsplit declaration pool. |
| 2018vic / antony-green-2018vic-election-eve | postal_applications_cumulative | state / final_reconciled | 2018-11-21 | 1 | missing_target: Target category coverage is incomplete across final districts. |
| 2018vic / antony-green-2018vic-election-eve | prepoll_votes_cast_cumulative | state / final_reconciled | 2018-11-23 | 1 | missing_target: Target category coverage is incomplete across final districts. |
| 2019fed / aec-2019-postal-operational | postal_applications_cumulative | elector_division / final_reconciled | 2019-05-17 | 151 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2019fed / aec-2019-prepoll-operational | prepoll_votes_issued_cumulative | administering_division / final_reconciled | 2019-05-17 | 151 | aggregate_only: Attendance belongs to the administering district. Aggregate at the election level; elector-seat conversion needs a crosswalk. |
| 2020qld / antony-green-2020qld-election-eve | postal_ballots_issued_cumulative | elector_division / contemporaneous | 2020-10-31T10:30:00+10:00 | 93 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2020qld / antony-green-2020qld-election-eve | postal_ballots_issued_cumulative | state / contemporaneous | 2020-10-31T10:30:00+10:00 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2020qld / antony-green-2020qld-election-eve | postal_votes_accepted_cumulative | state / contemporaneous | 2020-10-31T10:30:00+10:00 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2020qld / antony-green-2020qld-election-eve | postal_votes_returned_cumulative | elector_division / contemporaneous | 2020-10-31T10:30:00+10:00 | 93 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2020qld / antony-green-2020qld-election-eve | postal_votes_returned_cumulative | state / contemporaneous | 2020-10-30T18:00:00+10:00 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2020qld / antony-green-2020qld-election-eve | prepoll_votes_cast_cumulative | state / contemporaneous | 2020-10-30 | 1 | needs_definition_review: Published counts have unverified coverage of absent early voters and smaller early modes. These categories are not certified as a comparable target. |
| 2021wa / antony-green-2021wa-election-eve | postal_applications_cumulative | elector_division / contemporaneous | 2021-03-12 | 59 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2021wa / antony-green-2021wa-election-eve | postal_applications_cumulative | state / contemporaneous | 2021-03-12 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2021wa / antony-green-2021wa-election-eve | postal_votes_returned_cumulative | elector_division / contemporaneous | 2021-03-12 | 59 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2021wa / antony-green-2021wa-election-eve | postal_votes_returned_cumulative | state / contemporaneous | 2021-03-12 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2021wa / antony-green-2021wa-election-eve | prepoll_votes_cast_cumulative | elector_division / contemporaneous | 2021-03-12 | 59 | supported: Final in-person early pool. |
| 2021wa / antony-green-2021wa-election-eve | prepoll_votes_cast_cumulative | state / contemporaneous | 2021-03-12 | 1 | supported: Final in-person early pool. |
| 2022fed / aec-2022-prepoll-operational | prepoll_votes_issued_cumulative | administering_division / final_reconciled | 2022-05-21 | 151 | aggregate_only: Attendance belongs to the administering district. Aggregate at the election level; elector-seat conversion needs a crosswalk. |
| 2022fed / antony-green-2022fed-election-eve | postal_applications_cumulative | national / contemporaneous | 2022-05-20 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2022fed / antony-green-2022fed-election-eve | postal_votes_returned_cumulative | national / contemporaneous | 2022-05-20 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2022fed / antony-green-2022fed-election-eve | prepoll_votes_issued_cumulative | national / contemporaneous | 2022-05-20 | 1 | needs_split: Own-division early votes are inside OrdinaryVotes. |
| 2022sa / antony-green-2022sa-election-eve | postal_applications_cumulative | elector_division / contemporaneous | 2022-03-18 | 47 | combined_only: Postal votes are inside an unsplit declaration pool. |
| 2022sa / antony-green-2022sa-election-eve | postal_applications_cumulative | state / contemporaneous | 2022-03-18 | 1 | combined_only: Postal votes are inside an unsplit declaration pool. |
| 2022sa / antony-green-2022sa-election-eve | prepoll_votes_cast_cumulative | elector_division / contemporaneous | 2022-03-18 | 47 | combined_only: Early votes are inside an unsplit declaration pool. |
| 2022sa / antony-green-2022sa-election-eve | prepoll_votes_cast_cumulative | state / contemporaneous | 2022-03-18 | 1 | combined_only: Early votes are inside an unsplit declaration pool. |
| 2022vic / antony-green-2022vic-election-eve | postal_applications_cumulative | elector_division / contemporaneous | 2022-11-24 | 87 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2022vic / antony-green-2022vic-election-eve | postal_applications_cumulative | state / contemporaneous | 2022-11-24 | 1 | coverage_mismatch: State operational controls and the final 87-district sample have different coverage; Narracan is excluded from the final sample. Use the matched district controls for conversion diagnostics. |
| 2022vic / antony-green-2022vic-election-eve | postal_votes_returned_cumulative | state / contemporaneous | 2022-11-24 | 1 | coverage_mismatch: State operational controls and the final 87-district sample have different coverage; Narracan is excluded from the final sample. Use the matched district controls for conversion diagnostics. |
| 2022vic / antony-green-2022vic-election-eve | prepoll_votes_cast_cumulative | elector_division / contemporaneous | 2022-11-24 | 87 | supported: Empirical conversion to the published combined early pool; not a pure in-person acceptance/formality rate. |
| 2022vic / antony-green-2022vic-election-eve | prepoll_votes_cast_cumulative | state / contemporaneous | 2022-11-24 | 1 | coverage_mismatch: State operational controls and the final 87-district sample have different coverage; Narracan is excluded from the final sample. Use the matched district controls for conversion diagnostics. |
| 2023nsw / antony-green-2023nsw-election-eve | postal_ballots_issued_cumulative | elector_division / contemporaneous | 2023-03-24 | 93 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2023nsw / antony-green-2023nsw-election-eve | postal_ballots_issued_cumulative | state / contemporaneous | 2023-03-24 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2023nsw / antony-green-2023nsw-election-eve | postal_votes_returned_cumulative | state / contemporaneous | 2023-03-24 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2023nsw / antony-green-2023nsw-election-eve | prepoll_votes_cast_cumulative | elector_division / contemporaneous | 2023-03-24 | 93 | supported: Empirical conversion to the published combined early pool; not a pure in-person acceptance/formality rate. |
| 2023nsw / antony-green-2023nsw-election-eve | prepoll_votes_cast_cumulative | state / contemporaneous | 2023-03-24 | 1 | supported: Empirical conversion to the published combined early pool; not a pure in-person acceptance/formality rate. |
| 2024qld / antony-green-2024qld-election-eve | postal_ballots_issued_cumulative | state / contemporaneous | 2024-10-26 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2024qld / antony-green-2024qld-election-eve | postal_votes_returned_cumulative | state / contemporaneous | 2024-10-26 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2024qld / antony-green-2024qld-election-eve | prepoll_votes_cast_cumulative | state / contemporaneous | 2024-10-26 | 1 | needs_definition_review: Published counts have unverified coverage of absent early voters and smaller early modes. These categories are not certified as a comparable target. |
| 2024qld / ecq-2024-attendance-operational | early_in_person_attendance_cumulative | administering_division / final_reconciled | 2024-10-25 | 93 | aggregate_only: Attendance belongs to the administering district. Aggregate at the election level; elector-seat conversion needs a crosswalk. |
| 2025fed / aec-2025-postal-operational | postal_applications_cumulative | elector_division / contemporaneous | 2025-05-03 | 150 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2025fed / aec-2025-postal-operational | postal_votes_returned_cumulative | elector_division / contemporaneous | 2025-05-03 | 150 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2025fed / aec-2025-prepoll-operational | prepoll_votes_issued_cumulative | administering_division / final_reconciled | 2025-05-03 | 150 | aggregate_only: Attendance belongs to the administering district. Aggregate at the election level; elector-seat conversion needs a crosswalk. |
| 2025wa / antony-green-2025wa-election-eve | prepoll_votes_cast_cumulative | state / contemporaneous | 2025-03-07 | 1 | supported: Final in-person early pool. |
| 2026sa / ecsa-2026sa-daily-tally | postal_applications_cumulative | elector_division / contemporaneous | 2026-03-16 | 47 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2026sa / ecsa-2026sa-daily-tally | postal_applications_cumulative | state / contemporaneous | 2026-03-16 | 1 | supported: Final postal pool; applications/issues/returns/accepted votes remain separate stages. A formal-count ratio is a combined conversion, not a pure acceptance probability. |
| 2026sa / ecsa-2026sa-daily-tally | prepoll_votes_cast_cumulative | elector_division / contemporaneous | 2026-03-20 | 47 | supported: Final early pool includes named EVCs, early absent-ordinary votes and separate early declarations. |
| 2026sa / ecsa-2026sa-daily-tally | prepoll_votes_cast_cumulative | state / contemporaneous | 2026-03-20 | 1 | supported: Final early pool includes named EVCs, early absent-ordinary votes and separate early declarations. |

## Independent conversion evidence

Counts below count elections once per measure/geography, regardless of district count or source count. These are diagnostic folds, including retrospective evidence; they are not a backtest availability guarantee.

| Measure | Geography | Elections | Codes |
| --- | --- | ---: | --- |
| early_and_postal_votes_recorded_cumulative | elector_division | 1 | 2014vic |
| postal_applications_cumulative | elector_division | 8 | 2010fed, 2013fed, 2016fed, 2019fed, 2021wa, 2022vic, 2025fed, 2026sa |
| postal_applications_cumulative | national | 2 | 2007fed, 2022fed |
| postal_applications_cumulative | state | 4 | 2005wa, 2017wa, 2021wa, 2026sa |
| postal_ballots_issued_cumulative | elector_division | 2 | 2020qld, 2023nsw |
| postal_ballots_issued_cumulative | national | 1 | 2007fed |
| postal_ballots_issued_cumulative | state | 5 | 2008wa, 2015qld, 2020qld, 2023nsw, 2024qld |
| postal_votes_accepted_cumulative | state | 1 | 2020qld |
| postal_votes_returned_cumulative | elector_division | 3 | 2020qld, 2021wa, 2025fed |
| postal_votes_returned_cumulative | national | 1 | 2022fed |
| postal_votes_returned_cumulative | state | 4 | 2020qld, 2021wa, 2023nsw, 2024qld |
| prepoll_votes_cast_cumulative | elector_division | 5 | 2015nsw, 2021wa, 2022vic, 2023nsw, 2026sa |
| prepoll_votes_cast_cumulative | state | 6 | 2005wa, 2015nsw, 2021wa, 2023nsw, 2025wa, 2026sa |

## Overlap and aggregate residuals

Published parent totals and district sums are kept as alternatives. These residuals are reported only for the same source, measure, date and reconciliation status; districts with different latest dates are not summed. Small residuals do not warrant invented district allocations.

VIC 2022’s final district sample excludes Narracan; its statewide operational totals and the 87-district sample therefore have different coverage. Do not force their residual into the retained districts.

| Election / source | Measure / date | District sum | Published parent | Parent minus districts |
| --- | --- | ---: | ---: | ---: |
| 2015nsw / nswec-2015nsw-prepoll-transactions | prepoll_votes_cast_cumulative / 2015-03-27 | 642408 | 642408 | 0 |
| 2020qld / antony-green-2020qld-election-eve | postal_ballots_issued_cumulative / 2020-10-31T10:30:00+10:00 | 905806 | 905806 | 0 |
| 2021wa / antony-green-2021wa-election-eve | postal_applications_cumulative / 2021-03-12 | 328854 | 331078 | 2224 |
| 2021wa / antony-green-2021wa-election-eve | postal_votes_returned_cumulative / 2021-03-12 | 169280 | 169301 | 21 |
| 2021wa / antony-green-2021wa-election-eve | prepoll_votes_cast_cumulative / 2021-03-12 | 585778 | 585774 | -4 |
| 2022sa / antony-green-2022sa-election-eve | postal_applications_cumulative / 2022-03-18 | 170072 | 170081 | 9 |
| 2022sa / antony-green-2022sa-election-eve | prepoll_votes_cast_cumulative / 2022-03-18 | 208110 | 208136 | 26 |
| 2022vic / antony-green-2022vic-election-eve | postal_applications_cumulative / 2022-11-24 | 579906 | 586208 | 6302 |
| 2022vic / antony-green-2022vic-election-eve | prepoll_votes_cast_cumulative / 2022-11-24 | 1887521 | 1908400 | 20879 |
| 2023nsw / antony-green-2023nsw-election-eve | postal_ballots_issued_cumulative / 2023-03-24 | 540160 | 540208 | 48 |
| 2023nsw / antony-green-2023nsw-election-eve | prepoll_votes_cast_cumulative / 2023-03-24 | 1566305 | 1566493 | 188 |
| 2026sa / ecsa-2026sa-daily-tally | postal_applications_cumulative / 2026-03-16 | 174121 | 174121 | 0 |
| 2026sa / ecsa-2026sa-daily-tally | prepoll_votes_cast_cumulative / 2026-03-20 | 454860 | 454862 | 2 |

## South Australia 2026 final evidence

The dataset contains reviewed final first-preference counts for all 47 districts from the [ECSA results website](https://result.ecsa.sa.gov.au/). District totals combine polling places, declaration batches and absent-ordinary batches once each. They total 1,317,186 enrolled electors, 1,115,864 formal votes, 50,332 informal votes and 1,166,196 ballots.

Early-vote comparisons include named early-voting centres, early absent-ordinary votes and the separate early declaration category. Postal votes have their own observed final category. Older SA declaration totals remain combined.

ECSA acknowledges residual differences between some count stages in its [results-review statement](https://ecsa.sa.gov.au/se2026news/se2026-results-review-complete). This dataset uses first preferences; TCP and preference-distribution counts are separate stages and do not replace its turnout totals. The source revision and raw-source hashes are recorded in the normalized dataset.

## Coverage limitations

Historical same-name seat comparisons are unadjusted for redistribution. This audit does not establish boundary continuity or an official geography crosswalk. Federal early counts from 2010 lack a comparable final early-only category in the normalized ordinary results. Attendance recorded by the administering district cannot be treated as turnout of electors enrolled in that district.

## Refresh and provenance

The manifest retains source URLs, adapters, category regimes, observation dates/statuses and semantic SHA-256 hashes of each normalized input. Refresh the relevant ingestion adapter when a source changes, then rerun this command. `--check` detects changed counts/metadata, added or removed datasets, and changes to the audit/loader code. Formatting alone does not invalidate the input hash. It does not poll remote sources or reconstruct unavailable publications.

Run from `analysis/`:

```powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_evidence_audit
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_evidence_audit --check
```
