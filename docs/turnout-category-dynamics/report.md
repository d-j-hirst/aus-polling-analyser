# Turnout category allocation analysis

This report assesses how early-voting and postal information can improve estimates of the final vote counts in each voting category. Its purpose is to determine how to allocate a total-vote estimate when voting patterns change.

The analysis compares three simple rules using historical final results and audited operational counts. It tests allocation with the actual final total supplied, repeats the comparison with predicted totals, documents comparable category definitions, and describes errors that move together. It does not change live forecasting.

Generated 2026-10-03T00:24:51.639125+00:00.

## Definitions and the three rules

A formal vote is a valid lower-house ballot in the final first-preference count. Enrolment is the number of registered electors. An operational control is a published early-voting or postal count converted into an estimate of final formal votes. The [operational calibration report](../turnout-operational-calibration/report.md) explains these conversion multipliers and their errors.

A district observation here is one election and district with usable final category counts and at least one independently calibrated control. Every rule is tested on the same observations. Earlier complete same-name category counts provide the baseline. For a new name or missing earlier partition, the previous election’s observed aggregate category rates supply a labelled baseline; no earlier district counts are invented.

| Allocation rule | Calculation | Question |
| --- | --- | --- |
| Previous category shares | Previous category shares × current vote budget | Do new controls improve on carrying the previous distribution forward? |
| Controls + proportional remainder | Use controls, then share the remaining budget in previous uncontrolled proportions | Does a simple proportional adjustment allocate the residual accurately? |
| Controls + mainly ordinary adjustment | Use controls, retain uncontrolled category counts per enrolled elector, allocate the remainder to ordinary | Should ordinary voting absorb most of the adjustment instead of reducing smaller categories? |

When a controlled early total has an observed ordinary/declaration split, the proportional rule keeps their previous proportions. The ordinary-adjustment rule retains the previous declaration-early rate per enrolled elector and assigns the rest to ordinary early. VIC’s published combined early category is not split. Early votes without a current control retain their previous rate under ordinary adjustment; that assumption is tested explicitly through the postal-only comparisons.

## Main findings

With the actual total supplied, category error can be expressed as the percentage of votes that would need to move between categories to correct the allocation. This is half the sum of absolute category errors, divided by formal votes. It is a diagnostic of allocation, not a forecast using an unknown election total.

- Other elections: votes requiring reallocation average 10.59% with previous shares, 4.75% with proportional remainder, and 5.02% with mainly ordinary adjustment, across 13 elections.
- Earlier elections only: votes requiring reallocation average 10.59% with previous shares, 5.10% with proportional remainder, and 5.45% with mainly ordinary adjustment, across 13 elections.

Of the three tested rules, calibrated controls with proportional allocation of the remainder perform best overall under both training methods. The evidence does not support treating unchanged smaller-category rates as a general rule. Federal comparisons are closer and sometimes favour ordinary adjustment; the jurisdiction and individual-election tables retain that distinction.

The control-specific and jurisdiction results below show whether that overall comparison is transferable. Scores are descriptive evidence from a small number of elections; many districts do not create many independent election conditions.

## Which historical categories are comparable?

This table establishes which comparisons describe the same voting groups. A category regime is a set of compatible published definitions, not a fitted statistical effect. Grouping observed counts preserves their totals; it does not recover a hidden old split.

Known service or eligibility changes are listed in a shared category policy. The affected category is combined with ordinary votes where a broader observed comparison is available. Its separate change does not train behavioural uncertainty. QLD 2020 cancelled declared-institution placeholders are suppressed within that election; the separately reported remote-mobile votes remain included.

Current districts counts available final partitions in the current election. Missing current partitions cannot be scored. Missing prior baselines includes renamed/new districts and absent earlier partitions; aggregate-rate baselines are used for those districts. Routine eligibility allows the category comparison, but does not guarantee a suitable operational control or an independent conversion estimate.

| Previous → current | Category grouping | Current districts | Missing current | Missing prior baseline | Routine eligible | Definitions and cautions |
| --- | --- | --- | --- | --- | --- | --- |
| 2004fed → 2007fed | Federal total early, old declaration regime | 150 | 0 | 1 | Yes | Ordinary remainder includes mobile/hospital votes; early is PPVC ordinary plus declaration pre-poll. All early votes were declarations before 2010; retain total early without a fabricated ordinary split. |
| 2005wa → 2008wa | WA early/ordinary, ordinary includes mobile | 59 | 0 | 21 | Yes | Observed category groups retained. |
| 2006qld → 2009qld | QLD early aggregate | 89 | 0 | 8 | Yes | Observed category groups retained. |
| 2006sa → 2010sa | SA ordinary/combined declarations | 47 | 0 | 0 | Yes | Old declarations remain combined; no historical early/postal or smaller declaration split. |
| 2007fed → 2010fed | Federal total early, old declaration regime | 150 | 0 | 3 | Yes | Ordinary remainder includes mobile/hospital votes; early is PPVC ordinary plus declaration pre-poll. Use total early only across the 2010 ordinary/declaration processing change. |
| 2006vic → 2010vic | VIC observed early aggregate | 87 | 1 | 1 | Yes | Early remains the published aggregate; other combines provisional, marked-as-voted and the small 2006 declaration group. Combine other with ordinary in a broader observed pool across a definition or service change. |
| 2009qld → 2012qld | QLD early aggregate | 89 | 0 | 0 | Yes | Observed category groups retained. |
| 2008wa → 2013wa | WA early/ordinary, ordinary includes mobile | 59 | 0 | 4 | Yes | Observed category groups retained. |
| 2010fed → 2013fed | Federal PPVC/declaration early split | 150 | 0 | 0 | Yes | Ordinary remainder includes mobile/hospital votes; early is PPVC ordinary plus declaration pre-poll. |
| 2010sa → 2014sa | SA ordinary/combined declarations | 47 | 0 | 1 | Yes | Old declarations remain combined; no historical early/postal or smaller declaration split. |
| 2010vic → 2014vic | VIC observed early aggregate | 85 | 3 | 16 | Yes | Early remains the published aggregate; other combines provisional, marked-as-voted and the small 2006 declaration group. |
| 2012qld → 2015qld | QLD early aggregate | 89 | 0 | 0 | No | Reporting/category break: descriptive totals only, excluded from routine dynamics and allocation tests. Combine other with ordinary in a broader observed pool across a definition or service change. |
| 2013fed → 2016fed | Federal PPVC/declaration early split | 150 | 0 | 3 | Yes | Ordinary remainder includes mobile/hospital votes; early is PPVC ordinary plus declaration pre-poll. |
| 2013wa → 2017wa | WA early/ordinary, ordinary includes mobile | 59 | 0 | 5 | Yes | Observed category groups retained. |
| 2015qld → 2017qld | QLD ordinary/declaration early split | 93 | 0 | 16 | Yes | Combine other with ordinary in a broader observed pool across a definition or service change. |
| 2014sa → 2018sa | SA ordinary/combined declarations | 47 | 0 | 7 | Yes | Old declarations remain combined; no historical early/postal or smaller declaration split. |
| 2014vic → 2018vic | VIC observed early aggregate | 86 | 2 | 3 | Yes | Early remains the published aggregate; other combines provisional, marked-as-voted and the small 2006 declaration group. |
| 2015nsw → 2019nsw | NSW observed early aggregate | 93 | 0 | 0 | Yes | Observed category groups retained. |
| 2016fed → 2019fed | Federal PPVC/declaration early split | 151 | 0 | 8 | Yes | Ordinary remainder includes mobile/hospital votes; early is PPVC ordinary plus declaration pre-poll. |
| 2017qld → 2020qld | QLD ordinary/declaration early split | 93 | 0 | 0 | No | Reporting/category break: descriptive totals only, excluded from routine dynamics and allocation tests. COVID-period endpoint; also assess training without these transitions. Combine other with ordinary in a broader observed pool across a definition or service change. |
| 2017wa → 2021wa | WA early/ordinary, ordinary includes mobile | 59 | 0 | 1 | Yes | COVID-period endpoint; also assess training without these transitions. |
| 2018sa → 2022sa | SA ordinary/combined declarations | 47 | 0 | 0 | Yes | Old declarations remain combined; no historical early/postal or smaller declaration split. COVID-period endpoint; also assess training without these transitions. |
| 2019fed → 2022fed | Federal PPVC/declaration early split | 151 | 0 | 1 | Yes | Ordinary remainder includes mobile/hospital votes; early is PPVC ordinary plus declaration pre-poll. COVID-period endpoint; also assess training without these transitions. |
| 2018vic → 2022vic | VIC observed early aggregate | 87 | 0 | 11 | Yes | Early remains the published aggregate; other combines provisional, marked-as-voted and the small 2006 declaration group. COVID-period endpoint; also assess training without these transitions. |
| 2019nsw → 2023nsw | NSW observed early aggregate | 93 | 0 | 5 | Yes | Combine other with ordinary in a broader observed pool across a definition or service change. |
| 2020qld → 2024qld | QLD ordinary/declaration early split | 93 | 0 | 0 | Yes | COVID-period endpoint; also assess training without these transitions. Combine other with ordinary in a broader observed pool across a definition or service change. |
| 2021wa → 2025wa | WA broad non-postal group | 59 | 0 | 6 | Yes | Combine ordinary, early, mobile and absent; unresolved early/absent distinction. COVID-period endpoint; also assess training without these transitions. Combine other with ordinary in a broader observed pool across a definition or service change. |
| 2022fed → 2025fed | Federal PPVC/declaration early split | 150 | 0 | 1 | Yes | Ordinary remainder includes mobile/hospital votes; early is PPVC ordinary plus declaration pre-poll. COVID-period endpoint; also assess training without these transitions. |
| 2022sa → 2026sa | SA ordinary/combined declarations | 38 | 9 | 1 | No | Old declarations remain combined; no historical early/postal or smaller declaration split. 2026 detailed categories have no matched earlier detailed baseline; separate controlled-budget diagnostic. COVID-period endpoint; also assess training without these transitions. |

Federal ordinary remainder means ordinary votes outside the identified PPVC component, including mobile/hospital votes. Federal early counts are used nationally and distributed across all current districts according to previous resident early rates and current enrolment. Issuing-centre district counts are not used as resident controls. Aggregate allocation includes districts lacking a named prior baseline before scoring the districts with current category data.

SA 2026 has no earlier detailed declaration baseline. Its early/postal budget check appears separately below. WA 2025 uses all non-postal votes as one group; that view cannot identify ordinary-to-early substitution. NSW is displayed as category evidence but excluded from training and prediction tests.

## Recent observed category changes

This check asks whether changes in voting methods principally coincide with ordinary declines or with changes in postal and smaller categories. It describes the same named districts at both elections; it does not establish who switched voting methods.

Share change is the change in percentage points of formal votes. Count change per 1,000 electors removes enrolment growth. Allocation change per 1,000 compares the current count with previous shares applied to the actual current formal total, removing the change in total voting participation/formality. These allocation changes sum to zero, so negative relationships partly follow from accounting. Reporting breaks stay visible and are not routine training evidence.

| Previous → current | Category | Districts | Share change (points) | Count change / 1,000 electors | Allocation change / 1,000 electors |
| --- | --- | --- | --- | --- | --- |
| 2017wa → 2021wa | Absent | 58 | -4.266 | -35.891 | -35.072 |
| 2017wa → 2021wa | Combined early | 58 | 23.750 | 194.032 | 195.233 |
| 2017wa → 2021wa | Ordinary remainder | 58 | -26.025 | -218.639 | -213.937 |
| 2017wa → 2021wa | Other smaller categories | 58 | -0.061 | -0.504 | -0.499 |
| 2017wa → 2021wa | Postal | 58 | 6.603 | 53.652 | 54.275 |
| 2018vic → 2022vic | Absent | 76 | -2.180 | -19.124 | -18.198 |
| 2018vic → 2022vic | Combined early | 76 | 12.223 | 96.905 | 102.058 |
| 2018vic → 2022vic | Ordinary remainder | 76 | -13.010 | -115.268 | -108.627 |
| 2018vic → 2022vic | Other smaller categories | 76 | -0.226 | -1.988 | -1.890 |
| 2018vic → 2022vic | Postal | 76 | 3.193 | 25.626 | 26.657 |
| 2020qld → 2024qld | Absent | 93 | 0.579 | 4.639 | 4.881 |
| 2020qld → 2024qld | Declaration early | 93 | 1.646 | 13.310 | 13.877 |
| 2020qld → 2024qld | Ordinary early | 93 | 5.541 | 44.703 | 46.726 |
| 2020qld → 2024qld | Ordinary + other | 93 | 0.184 | -0.169 | 1.555 |
| 2020qld → 2024qld | Postal | 93 | -7.950 | -68.498 | -67.039 |
| 2021wa → 2025wa | All non-postal categories | 53 | 4.149 | 24.723 | 33.760 |
| 2021wa → 2025wa | Postal | 53 | -4.149 | -35.361 | -33.760 |
| 2022fed → 2025fed | Absent | 149 | 0.073 | 0.783 | 0.628 |
| 2022fed → 2025fed | Declaration early | 149 | -0.191 | -1.456 | -1.634 |
| 2022fed → 2025fed | Ordinary early | 149 | 5.113 | 45.417 | 43.775 |
| 2022fed → 2025fed | Ordinary remainder | 149 | -3.865 | -30.896 | -33.095 |
| 2022fed → 2025fed | Other smaller categories | 149 | -0.149 | -1.262 | -1.277 |
| 2022fed → 2025fed | Postal | 149 | -0.981 | -7.683 | -8.397 |
| 2022sa → 2026sa | Combined declarations | 38 | 17.108 | 139.730 | 145.268 |
| 2022sa → 2026sa | Ordinary remainder | 38 | -17.108 | -153.599 | -145.268 |

VIC 2022 and WA 2021 show that growth in early/postal voting can accompany substantial reductions in absent voting as well as ordinary voting. FED 2025 shows a different pattern: most growth is in ordinary early voting, while absent and declaration early rates change much less. These are reasons to test allocation assumptions rather than require all smaller categories to stay unchanged. They are observations across different conditions, including pandemic-period elections, rather than estimates of individual voters’ switching behaviour.

## How the prediction tests work

The purpose is to test transfer to another election rather than reproduce each election with its own conversion. “Other elections” excludes the test election. For turnout changes it also excludes a successor transition using that election as its starting point. “Earlier elections only” uses historical data completed before the test election. Neither is treated as the definitive verdict.

Early and postal conversion uses the independent operational-calibration estimates. Postal stages are chosen in this fixed order: applications, issued ballots, returned ballots, then accepted-so-far counts. A stage without an independent conversion is skipped. Contemporary evidence is preferred by the audit; revised historical series remain labelled in the local output. Dates are count dates, with historical publication availability not fully verified. This is not a complete historical live replay.

The predicted vote budget is current enrolment × previous turnout, adjusted by half the mean training turnout change in log odds, × previous formality. The sensitivity table also uses zero and full turnout change. No formality point drift or per-district coefficients are fitted. The actual-total pass supplies final district totals solely to isolate allocation error.

Category error % is the sum of absolute district/category errors divided by all formal votes, times 100. Combined-category error % adds districts within each category before taking absolute errors: offsetting district errors can cancel. Total error % measures the absolute error in the combined formal total. Each percentage is calculated per election and elections are averaged equally. A vote shifted between two categories contributes twice to category error. Predicted-total errors also include missing or extra total votes, so they cannot simply be halved into votes requiring reallocation.

### Controls and independent training support

This table shows what information each prediction actually uses and how many independent elections supply its conversion multiplier. A large district sample cannot compensate for a multiplier learned from one election. A multiplier of 0.8 means 100 recorded votes or applications become an estimate of 80 final formal votes. Aggregate controls are distributed using previous category rates and current enrolment; district controls are converted directly.

Input status distinguishes counts retained during an election from later reconciled historical series. The count dates and source identities are retained in the local analytical output and the operational-calibration report. A one-election estimate is kept visible as weak evidence, rather than silently dropping those prediction tests.

| Election | Test method | Control | Geography | Multiplier | Training elections | Input status |
| --- | --- | --- | --- | --- | --- | --- |
| 2007fed | Other elections | Postal applications | Aggregate distributed to districts | 0.806 | 6 | Later reconciled series |
| 2007fed | Earlier elections only | Postal applications | Aggregate distributed to districts | 0.747 | 1 | Later reconciled series |
| 2008wa | Other elections | Postal ballots issued | Aggregate distributed to districts | 0.786 | 4 | Later reconciled series |
| 2008wa | Earlier elections only | Postal ballots issued | Aggregate distributed to districts | 0.852 | 1 | Later reconciled series |
| 2010fed | Other elections | Postal applications | District count | 0.807 | 6 | Later reconciled series |
| 2010fed | Earlier elections only | Postal applications | District count | 0.789 | 2 | Later reconciled series |
| 2013fed | Other elections | Federal national early count | Aggregate distributed to districts | 0.970 | 4 | Later reconciled series |
| 2013fed | Other elections | Postal applications | District count | 0.807 | 6 | Later reconciled series |
| 2013fed | Earlier elections only | Postal applications | District count | 0.826 | 2 | Later reconciled series |
| 2016fed | Other elections | Federal national early count | Aggregate distributed to districts | 0.978 | 4 | Later reconciled series |
| 2016fed | Earlier elections only | Federal national early count | Aggregate distributed to districts | 1.021 | 1 | Later reconciled series |
| 2016fed | Other elections | Postal applications | District count | 0.812 | 6 | Later reconciled series |
| 2016fed | Earlier elections only | Postal applications | District count | 0.826 | 3 | Later reconciled series |
| 2017wa | Other elections | Postal applications | Aggregate distributed to districts | 0.684 | 4 | Recorded during the election |
| 2017wa | Earlier elections only | Postal applications | Aggregate distributed to districts | 0.803 | 5 | Recorded during the election |
| 2018vic | Other elections | Postal applications | Aggregate distributed to districts | 0.686 | 5 | Later reconciled series |
| 2018vic | Earlier elections only | Postal applications | Aggregate distributed to districts | 0.722 | 2 | Later reconciled series |
| 2018vic | Other elections | Early votes recorded | Aggregate distributed to districts | 0.950 | 4 | Later reconciled series |
| 2018vic | Earlier elections only | Early votes recorded | Aggregate distributed to districts | 0.940 | 1 | Later reconciled series |
| 2019fed | Other elections | Federal national early count | Aggregate distributed to districts | 0.982 | 4 | Later reconciled series |
| 2019fed | Earlier elections only | Federal national early count | Aggregate distributed to districts | 1.006 | 2 | Later reconciled series |
| 2019fed | Other elections | Postal applications | District count | 0.813 | 6 | Later reconciled series |
| 2019fed | Earlier elections only | Postal applications | District count | 0.818 | 4 | Later reconciled series |
| 2021wa | Other elections | Postal applications | District count | 0.696 | 4 | Recorded during the election |
| 2021wa | Earlier elections only | Postal applications | District count | 0.722 | 2 | Recorded during the election |
| 2021wa | Other elections | Early votes recorded | District count | 0.946 | 3 | Recorded during the election |
| 2021wa | Earlier elections only | Early votes recorded | District count | 0.940 | 1 | Recorded during the election |
| 2022fed | Other elections | Federal national early count | Aggregate distributed to districts | 0.985 | 4 | Later reconciled series |
| 2022fed | Earlier elections only | Federal national early count | Aggregate distributed to districts | 0.996 | 3 | Later reconciled series |
| 2022fed | Other elections | Postal applications | Aggregate distributed to districts | 0.814 | 6 | Recorded during the election |
| 2022fed | Earlier elections only | Postal applications | Aggregate distributed to districts | 0.812 | 5 | Recorded during the election |
| 2022vic | Other elections | Postal applications | District count | 0.690 | 4 | Recorded during the election |
| 2022vic | Earlier elections only | Postal applications | District count | 0.697 | 3 | Recorded during the election |
| 2022vic | Other elections | Early votes recorded | District count | 0.951 | 3 | Recorded during the election |
| 2022vic | Earlier elections only | Early votes recorded | District count | 0.952 | 2 | Recorded during the election |
| 2024qld | Other elections | Postal ballots issued | Aggregate distributed to districts | 0.771 | 4 | Recorded during the election |
| 2024qld | Earlier elections only | Postal ballots issued | Aggregate distributed to districts | 0.771 | 4 | Recorded during the election |
| 2025fed | Other elections | Federal national early count | Aggregate distributed to districts | 0.988 | 4 | Later reconciled series |
| 2025fed | Earlier elections only | Federal national early count | Aggregate distributed to districts | 0.988 | 4 | Later reconciled series |
| 2025fed | Other elections | Postal applications | District count | 0.807 | 6 | Recorded during the election |
| 2025fed | Earlier elections only | Postal applications | District count | 0.807 | 6 | Recorded during the election |

### Overall comparison

| Test method | Vote budget | Allocation rule | Elections | Districts | Category error % | Combined-category error % | Total error % |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Other elections | Actual final total (diagnostic) | Previous category shares | 13 | 1495 | 21.173 | 19.859 | 0.000 |
| Other elections | Actual final total (diagnostic) | Controls + proportional remainder | 13 | 1495 | 9.490 | 5.473 | 0.000 |
| Other elections | Actual final total (diagnostic) | Controls + mainly ordinary adjustment | 13 | 1495 | 10.050 | 6.090 | 0.000 |
| Other elections | Half historical turnout change | Previous category shares | 13 | 1495 | 21.787 | 20.317 | 1.525 |
| Other elections | Half historical turnout change | Controls + proportional remainder | 13 | 1495 | 10.138 | 6.224 | 1.525 |
| Other elections | Half historical turnout change | Controls + mainly ordinary adjustment | 13 | 1495 | 10.545 | 6.637 | 1.525 |
| Earlier elections only | Actual final total (diagnostic) | Previous category shares | 13 | 1495 | 21.173 | 19.859 | 0.000 |
| Earlier elections only | Actual final total (diagnostic) | Controls + proportional remainder | 13 | 1495 | 10.204 | 6.334 | 0.000 |
| Earlier elections only | Actual final total (diagnostic) | Controls + mainly ordinary adjustment | 13 | 1495 | 10.906 | 7.493 | 0.000 |
| Earlier elections only | Half historical turnout change | Previous category shares | 13 | 1495 | 21.872 | 20.426 | 1.623 |
| Earlier elections only | Half historical turnout change | Controls + proportional remainder | 13 | 1495 | 10.873 | 7.025 | 1.623 |
| Earlier elections only | Half historical turnout change | Controls + mainly ordinary adjustment | 13 | 1495 | 11.430 | 7.941 | 1.623 |

### What controls are available?

This separates elections with both early and postal information from those with only one type. It tests whether ordinary adjustment works only when early growth is actually observed. Group samples can differ; compare rules within a row group.

| Controls | Test method | Vote budget | Allocation rule | Elections | Districts | Category error % | Combined-category error % | Total error % |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Early and postal | Other elections | Actual final total (diagnostic) | Previous category shares | 8 | 984 | 25.557 | 24.452 | 0.000 |
| Early and postal | Other elections | Actual final total (diagnostic) | Controls + proportional remainder | 8 | 984 | 8.182 | 3.203 | 0.000 |
| Early and postal | Other elections | Actual final total (diagnostic) | Controls + mainly ordinary adjustment | 8 | 984 | 9.165 | 4.268 | 0.000 |
| Early and postal | Earlier elections only | Actual final total (diagnostic) | Previous category shares | 7 | 834 | 26.516 | 25.353 | 0.000 |
| Early and postal | Earlier elections only | Actual final total (diagnostic) | Controls + proportional remainder | 7 | 834 | 8.435 | 3.029 | 0.000 |
| Early and postal | Earlier elections only | Actual final total (diagnostic) | Controls + mainly ordinary adjustment | 7 | 834 | 9.922 | 5.281 | 0.000 |
| Postal only | Other elections | Actual final total (diagnostic) | Previous category shares | 5 | 511 | 14.157 | 12.511 | 0.000 |
| Postal only | Other elections | Actual final total (diagnostic) | Controls + proportional remainder | 5 | 511 | 11.584 | 9.104 | 0.000 |
| Postal only | Other elections | Actual final total (diagnostic) | Controls + mainly ordinary adjustment | 5 | 511 | 11.464 | 9.005 | 0.000 |
| Postal only | Earlier elections only | Actual final total (diagnostic) | Previous category shares | 6 | 661 | 14.938 | 13.451 | 0.000 |
| Postal only | Earlier elections only | Actual final total (diagnostic) | Controls + proportional remainder | 6 | 661 | 12.269 | 10.190 | 0.000 |
| Postal only | Earlier elections only | Actual final total (diagnostic) | Controls + mainly ordinary adjustment | 6 | 661 | 12.055 | 10.073 | 0.000 |

### Results by jurisdiction

This checks whether a pooled result conceals a failure in the target jurisdiction. The supplied final total isolates allocation; the full pooled training exclusions remain unchanged.

| Jurisdiction | Test method | Rule | Elections | Districts | Category error % |
| --- | --- | --- | --- | --- | --- |
| FED | Other elections | Previous category shares | 7 | 1052 | 14.526 |
| FED | Other elections | Controls + proportional remainder | 7 | 1052 | 8.442 |
| FED | Other elections | Controls + mainly ordinary adjustment | 7 | 1052 | 8.029 |
| FED | Earlier elections only | Previous category shares | 7 | 1052 | 14.526 |
| FED | Earlier elections only | Controls + proportional remainder | 7 | 1052 | 9.653 |
| FED | Earlier elections only | Controls + mainly ordinary adjustment | 7 | 1052 | 9.489 |
| QLD | Other elections | Previous category shares | 1 | 93 | 21.333 |
| QLD | Other elections | Controls + proportional remainder | 1 | 93 | 14.220 |
| QLD | Other elections | Controls + mainly ordinary adjustment | 1 | 93 | 14.948 |
| QLD | Earlier elections only | Previous category shares | 1 | 93 | 21.333 |
| QLD | Earlier elections only | Controls + proportional remainder | 1 | 93 | 14.220 |
| QLD | Earlier elections only | Controls + mainly ordinary adjustment | 1 | 93 | 14.948 |
| VIC | Other elections | Previous category shares | 2 | 173 | 28.397 |
| VIC | Other elections | Controls + proportional remainder | 2 | 173 | 7.885 |
| VIC | Other elections | Controls + mainly ordinary adjustment | 2 | 173 | 9.218 |
| VIC | Earlier elections only | Previous category shares | 2 | 173 | 28.397 |
| VIC | Earlier elections only | Controls + proportional remainder | 2 | 173 | 7.911 |
| VIC | Earlier elections only | Controls + mainly ordinary adjustment | 2 | 173 | 9.302 |
| WA | Other elections | Previous category shares | 3 | 177 | 31.811 |
| WA | Other elections | Controls + proportional remainder | 3 | 177 | 11.431 |
| WA | Other elections | Controls + mainly ordinary adjustment | 3 | 177 | 13.686 |
| WA | Earlier elections only | Previous category shares | 3 | 177 | 31.811 |
| WA | Earlier elections only | Controls + proportional remainder | 3 | 177 | 11.680 |
| WA | Earlier elections only | Controls + mainly ordinary adjustment | 3 | 177 | 13.936 |

### Individual election comparisons

These results identify concrete successes and failures behind the averages. The percentage is votes requiring reallocation, with the actual final total supplied: it is half the category error percentage above. The columns compare the same districts within each election and training method.

| Election | Test method | Districts | Previous shares % | Proportional remainder % | Mainly ordinary adjustment % |
| --- | --- | --- | --- | --- | --- |
| 2007fed | Other elections | 150 | 3.188 | 3.039 | 3.134 |
| 2007fed | Earlier elections only | 150 | 3.188 | 3.255 | 3.388 |
| 2008wa | Other elections | 59 | 4.035 | 3.204 | 3.222 |
| 2008wa | Earlier elections only | 59 | 4.035 | 3.288 | 3.390 |
| 2010fed | Other elections | 150 | 4.177 | 3.569 | 3.191 |
| 2010fed | Earlier elections only | 150 | 4.177 | 3.664 | 3.300 |
| 2013fed | Other elections | 150 | 9.422 | 4.307 | 3.376 |
| 2013fed | Earlier elections only | 150 | 9.422 | 7.592 | 7.327 |
| 2016fed | Other elections | 150 | 6.089 | 3.819 | 3.524 |
| 2016fed | Earlier elections only | 150 | 6.089 | 4.118 | 3.829 |
| 2017wa | Other elections | 59 | 13.326 | 12.038 | 11.639 |
| 2017wa | Earlier elections only | 59 | 13.326 | 11.899 | 11.287 |
| 2018vic | Other elections | 86 | 12.567 | 6.084 | 5.880 |
| 2018vic | Earlier elections only | 86 | 12.567 | 6.076 | 5.862 |
| 2019fed | Other elections | 151 | 10.517 | 4.497 | 4.440 |
| 2019fed | Earlier elections only | 151 | 10.517 | 4.731 | 4.800 |
| 2021wa | Other elections | 59 | 30.356 | 1.905 | 5.668 |
| 2021wa | Earlier elections only | 59 | 30.356 | 2.334 | 6.226 |
| 2022fed | Other elections | 151 | 11.464 | 5.954 | 6.114 |
| 2022fed | Earlier elections only | 151 | 11.464 | 6.066 | 6.247 |
| 2022vic | Other elections | 87 | 15.829 | 1.801 | 3.338 |
| 2022vic | Earlier elections only | 87 | 15.829 | 1.835 | 3.440 |
| 2024qld | Other elections | 93 | 10.667 | 7.110 | 7.474 |
| 2024qld | Earlier elections only | 93 | 10.667 | 7.110 | 7.474 |
| 2025fed | Other elections | 150 | 5.984 | 4.362 | 4.321 |
| 2025fed | Earlier elections only | 150 | 5.984 | 4.362 | 4.321 |

Only VIC 2022 has both a compatible prior partition and usable controls with independent conversion estimates in the default prediction sample. Its absent category explains much of the difference between the two controlled rules: the ordinary-adjustment rule retains the higher prior absent rate and compensates by estimating fewer ordinary votes. One Victorian election cannot establish a universal allocation relationship. QLD 2024 has only a postal control here; retaining the old early rate misses the observed rise in early voting.

## Sensitivity to totals and COVID-period training

These checks ask whether the allocation comparison changes under another reasonable formal-total assumption or when unusual pandemic-period evidence is removed from training. They are fixed comparisons, not a search for an optimal drift coefficient.

The COVID sensitivity removes rate transitions touching QLD 2020, WA 2021 and Federal/VIC/SA 2022, and removes those elections from conversion training. The same test elections, their previous baselines and their observed controls remain eligible. If conversion evidence disappears, no prediction is produced. The comparison below uses only election/district observations available under both training choices, keeping the evaluation sample identical.

| Test method | Vote budget | Allocation rule | Elections | Districts | Category error % | Combined-category error % | Total error % |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Other elections | Previous turnout | Previous category shares | 13 | 1495 | 21.931 | 20.460 | 1.728 |
| Other elections | Previous turnout | Controls + proportional remainder | 13 | 1495 | 10.229 | 6.375 | 1.728 |
| Other elections | Previous turnout | Controls + mainly ordinary adjustment | 13 | 1495 | 10.565 | 6.632 | 1.728 |
| Other elections | Full historical turnout change | Previous category shares | 13 | 1495 | 21.640 | 20.171 | 1.444 |
| Other elections | Full historical turnout change | Controls + proportional remainder | 13 | 1495 | 10.068 | 6.184 | 1.444 |
| Other elections | Full historical turnout change | Controls + mainly ordinary adjustment | 13 | 1495 | 10.545 | 6.676 | 1.444 |
| Earlier elections only | Previous turnout | Previous category shares | 13 | 1495 | 21.931 | 20.460 | 1.728 |
| Earlier elections only | Previous turnout | Controls + proportional remainder | 13 | 1495 | 10.886 | 6.989 | 1.728 |
| Earlier elections only | Previous turnout | Controls + mainly ordinary adjustment | 13 | 1495 | 11.368 | 7.796 | 1.728 |
| Earlier elections only | Full historical turnout change | Previous category shares | 13 | 1495 | 21.812 | 20.389 | 1.621 |
| Earlier elections only | Full historical turnout change | Controls + proportional remainder | 13 | 1495 | 10.880 | 7.119 | 1.621 |
| Earlier elections only | Full historical turnout change | Controls + mainly ordinary adjustment | 13 | 1495 | 11.507 | 8.090 | 1.621 |

### Paired comparison of training choices

Category error has the same definition as above. “All training” retains eligible COVID-period evidence; “Without COVID” removes the specified endpoints from training. The tested elections, including pandemic-period elections, stay the same within each row.

| Test method | Vote budget | Rule | Shared elections | Shared districts | All training category error % | Without COVID category error % |
| --- | --- | --- | --- | --- | --- | --- |
| Other elections | Actual final total (diagnostic) | Previous category shares | 13 | 1495 | 21.173 | 21.173 |
| Other elections | Actual final total (diagnostic) | Controls + proportional remainder | 13 | 1495 | 9.490 | 9.536 |
| Other elections | Actual final total (diagnostic) | Controls + mainly ordinary adjustment | 13 | 1495 | 10.050 | 10.101 |
| Other elections | Half historical turnout change | Previous category shares | 13 | 1495 | 21.787 | 21.788 |
| Other elections | Half historical turnout change | Controls + proportional remainder | 13 | 1495 | 10.138 | 10.194 |
| Other elections | Half historical turnout change | Controls + mainly ordinary adjustment | 13 | 1495 | 10.545 | 10.606 |
| Earlier elections only | Actual final total (diagnostic) | Previous category shares | 13 | 1495 | 21.173 | 21.173 |
| Earlier elections only | Actual final total (diagnostic) | Controls + proportional remainder | 13 | 1495 | 10.204 | 10.258 |
| Earlier elections only | Actual final total (diagnostic) | Controls + mainly ordinary adjustment | 13 | 1495 | 10.906 | 10.952 |
| Earlier elections only | Half historical turnout change | Previous category shares | 13 | 1495 | 21.872 | 21.872 |
| Earlier elections only | Half historical turnout change | Controls + proportional remainder | 13 | 1495 | 10.873 | 10.944 |
| Earlier elections only | Half historical turnout change | Controls + mainly ordinary adjustment | 13 | 1495 | 11.430 | 11.489 |

## Do category errors move together?

This checks the uncertainty needed by a conserved allocation. When the total is fixed, increasing early or postal estimates requires a decrease somewhere else. Treating all categories as independent would lose that relationship.

These diagnostics use the actual-total pass. Ordinary, all early, postal and all other categories form four exhaustive groups whose errors sum to zero. Election errors are the combined category error per 1,000 enrolled electors. District errors remove that election’s error first; districts are weighted by enrolment within an election and elections get equal weight. These observed error patterns are not independent probability-coverage guarantees.

Comparisons that publish only a broader ordinary/other or non-postal pool are omitted from this four-group diagnostic. They remain in allocation scores; their hidden components cannot be recovered without inventing counts. The errors here are signed vote-count differences, so they are reported directly. Modelled category proportions use logarithmic transformations.

RMS is the square root of the mean squared error, so it includes systematic bias and gives larger errors more weight. All RMS columns use votes per 1,000 enrolled electors. The correlation columns measure linear co-movement, from −1 to +1. Negative values indicate errors tending in opposite directions, but conservation itself causes some negative relationships. The local JSON includes complete covariance matrices: covariance describes how the sizes and directions of two errors vary together.

| Controls | Test method | Rule | Error level | Elections | Ordinary RMS | Early RMS | Postal RMS | Other RMS | Ordinary/early correlation | Ordinary/postal correlation |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Early and postal | Other elections | Previous category shares | District after election error | 8 | 33.739 | 36.320 | 14.018 | 12.448 | -0.871 | -0.233 |
| Early and postal | Other elections | Controls + proportional remainder | District after election error | 8 | 31.938 | 32.941 | 9.467 | 10.435 | -0.920 | -0.322 |
| Early and postal | Other elections | Controls + mainly ordinary adjustment | District after election error | 8 | 33.679 | 32.941 | 9.467 | 12.496 | -0.895 | -0.331 |
| Early and postal | Other elections | Previous category shares | Election total | 8 | 104.099 | 95.710 | 29.336 | 15.023 | -0.948 | -0.700 |
| Early and postal | Other elections | Controls + proportional remainder | Election total | 8 | 6.667 | 6.901 | 4.048 | 4.238 | -0.658 | -0.254 |
| Early and postal | Other elections | Controls + mainly ordinary adjustment | Election total | 8 | 18.841 | 6.901 | 4.048 | 15.480 | 0.014 | -0.819 |
| Early and postal | Earlier elections only | Previous category shares | District after election error | 7 | 34.322 | 37.141 | 14.423 | 13.213 | -0.866 | -0.238 |
| Early and postal | Earlier elections only | Controls + proportional remainder | District after election error | 7 | 32.976 | 34.073 | 10.206 | 11.042 | -0.915 | -0.319 |
| Early and postal | Earlier elections only | Controls + mainly ordinary adjustment | District after election error | 7 | 34.842 | 34.073 | 10.206 | 13.263 | -0.888 | -0.331 |
| Early and postal | Earlier elections only | Previous category shares | Election total | 7 | 107.847 | 99.769 | 30.532 | 15.878 | -0.948 | -0.709 |
| Early and postal | Earlier elections only | Controls + proportional remainder | Election total | 7 | 8.673 | 8.372 | 6.147 | 4.575 | -0.570 | 0.190 |
| Early and postal | Earlier elections only | Controls + mainly ordinary adjustment | Election total | 7 | 22.589 | 8.372 | 6.147 | 16.362 | 0.646 | -0.826 |
| Postal only | Other elections | Previous category shares | District after election error | 4 | 25.100 | 26.460 | 8.961 | 12.166 | -0.808 | -0.158 |
| Postal only | Other elections | Controls + proportional remainder | District after election error | 4 | 24.989 | 26.533 | 9.066 | 12.179 | -0.818 | -0.144 |
| Postal only | Other elections | Controls + mainly ordinary adjustment | District after election error | 4 | 24.838 | 26.203 | 9.066 | 12.304 | -0.808 | -0.168 |
| Postal only | Other elections | Previous category shares | Election total | 4 | 57.594 | 43.990 | 11.660 | 6.470 | -0.994 | -0.857 |
| Postal only | Other elections | Controls + proportional remainder | Election total | 4 | 48.964 | 44.681 | 3.599 | 6.741 | -0.997 | -0.480 |
| Postal only | Other elections | Controls + mainly ordinary adjustment | Election total | 4 | 46.123 | 43.343 | 3.599 | 7.101 | -0.992 | -0.550 |
| Postal only | Earlier elections only | Previous category shares | District after election error | 5 | 26.002 | 27.192 | 9.351 | 11.042 | -0.838 | -0.162 |
| Postal only | Earlier elections only | Controls + proportional remainder | District after election error | 5 | 26.094 | 27.293 | 8.824 | 11.056 | -0.857 | -0.161 |
| Postal only | Earlier elections only | Controls + mainly ordinary adjustment | District after election error | 5 | 26.192 | 27.061 | 8.824 | 11.166 | -0.849 | -0.186 |
| Postal only | Earlier elections only | Previous category shares | Election total | 5 | 60.900 | 47.641 | 13.439 | 6.452 | -0.992 | -0.849 |
| Postal only | Earlier elections only | Controls + proportional remainder | Election total | 5 | 47.465 | 48.990 | 7.299 | 6.921 | -0.984 | 0.242 |
| Postal only | Earlier elections only | Controls + mainly ordinary adjustment | Election total | 5 | 43.750 | 47.056 | 7.299 | 6.997 | -0.969 | 0.138 |

A minimal joint structure is ordinary = total − early − postal − other. Total uncertainty moves the overall budget; allocation uncertainty redistributes that budget. A common conversion error must be shared across its districts, while district variation remains additional. These matrices describe the historical evidence; they do not justify fitting a separate relationship for every district.

## SA 2026 controlled vote budget

This diagnostic asks what the early/postal estimates leave for all remaining categories combined. It uses SA 2026 controls with SA 2026 excluded from conversion and turnout training. It does not infer the previous absent/provisional/other declaration split or establish that a live count is complete.

Totals over districts with reliable category splits separate errors in the vote budget from errors in the two controls. All errors are estimated minus actual final votes. Remaining-category error equals total error minus early error minus postal error. Supplying the actual total removes the first contribution and isolates conversion error; using the predicted total shows the complete initial estimate.

| Test method | Vote budget | Districts | Total error (votes) | Early error (votes) | Postal error (votes) | Remaining-category error (votes) |
| --- | --- | --- | --- | --- | --- | --- |
| Earlier elections only | Actual final total (diagnostic) | 38 | 0 | 611.476 | 3043.045 | -3654.521 |
| Earlier elections only | Half historical turnout change | 38 | 10641.273 | 611.476 | 3043.045 | 6986.752 |
| Other elections | Actual final total (diagnostic) | 38 | 0 | 611.476 | 3043.045 | -3654.521 |
| Other elections | Half historical turnout change | 38 | 10641.273 | 611.476 | 3043.045 | 6986.752 |

Six districts were selected for low estimated completion near the final SA 2026 count. The table includes those with reliable category targets; reviewed missing splits are omitted. They are shown because errors in their expected category sizes can inflate the projected uncounted vote. The table uses the predicted total. Remaining-category error is predicted total minus estimated early and postal votes, less the actual sum of all other final formal votes. Positive means too large a combined remainder. This is the initial size estimate, not a count of votes still unreported at the final snapshot.

| District | Test method | Predicted total | Actual total | Estimated early | Estimated postal | Estimated other | Actual other | Other error (votes) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Flinders | Other elections | 22142.035 | 21418 | 9413.939 | 2054.201 | 10673.894 | 10903 | -229.106 |
| Flinders | Earlier elections only | 22142.035 | 21418 | 9413.939 | 2054.201 | 10673.894 | 10903 | -229.106 |
| Giles | Other elections | 21036.880 | 21460 | 12476.179 | 1354.502 | 7206.199 | 7630 | -423.801 |
| Giles | Earlier elections only | 21036.880 | 21460 | 12476.179 | 1354.502 | 7206.199 | 7630 | -423.801 |
| Mount Gambier | Other elections | 23060.254 | 22102 | 12469.524 | 1118.276 | 9472.454 | 8906 | 566.454 |
| Mount Gambier | Earlier elections only | 23060.254 | 22102 | 12469.524 | 1118.276 | 9472.454 | 8906 | 566.454 |
| Stuart | Other elections | 23991.143 | 22411 | 7252.973 | 2240.005 | 14498.164 | 12959 | 1539.164 |
| Stuart | Earlier elections only | 23991.143 | 22411 | 7252.973 | 2240.005 | 14498.164 | 12959 | 1539.164 |

Both training methods give identical SA 2026 estimates because it is the latest election in the retained evidence. The budget over comparison districts is reasonably close while individual districts still differ. In particular, an overall total estimate does not identify closed or mismatched booths, or establish that an apparently open declaration batch has finished counting.

## Data limitations and reproduction

Constraints preserve nonnegative category estimates and their total. If controls exceed the budget they are reduced proportionally. If protected categories exceed the remainder they are reduced proportionally and ordinary is zero. Those are recorded conflicts between uncertain estimates, not evidence that a category is known complete. The analytical output records every activation.

Renamed districts and redistributions are not boundary-adjusted. Missing category partitions are omitted from scores. Whole-election sample sizes, source histories and control training identities remain visible. Elections without a usable independent conversion are listed below.


Reviewed implausible zeros represent missing counts, and the entire affected district split is omitted from allocation comparisons because votes may have been recorded elsewhere. District totals and pre-election controls remain usable. Unknown final targets cannot train conversion factors. The local output records these exclusions separately from empty groups.

Possible observed category zeros receive a half-vote equivalent in starting weights; the published counts and scoring targets stay unchanged. The share input floor is the smaller of 0.1% and half a vote divided by the observed group total, preserving genuine smaller shares in large groups. Entirely empty aggregated groups remain listed for review in the local numerical output; unknown components are omitted rather than filled with zeros. Only the selected final pre-election control is used.

Relationships between turnout (ballots/enrolment), formality (formal/ballots) and category shares (category/formal) have different parent groups. The local output explicitly flags their joint interpretation for human review. Transforming the individual rates does not make these denominators interchangeable. The historical category-share/turnout correlations are exploratory report diagnostics; they do not supply prediction coefficients to this allocation or to the initial count-distribution prototype. Federal issuing-centre district comparisons are also diagnostic only. Federal early allocation uses the national published count and previous resident-electorate results, rather than treating centre attendance as a count of that district’s residents.

| Election | Test method | Reason |
| --- | --- | --- |
| 2009qld | Other elections | No supported control with an independently estimated conversion. |
| 2009qld | Earlier elections only | No supported control with an independently estimated conversion. |
| 2010vic | Other elections | No supported control with an independently estimated conversion. |
| 2010vic | Earlier elections only | No supported control with an independently estimated conversion. |
| 2012qld | Other elections | No supported control with an independently estimated conversion. |
| 2012qld | Earlier elections only | No supported control with an independently estimated conversion. |
| 2013wa | Other elections | No supported control with an independently estimated conversion. |
| 2013wa | Earlier elections only | No supported control with an independently estimated conversion. |
| 2014vic | Other elections | No supported control with an independently estimated conversion. |
| 2014vic | Earlier elections only | No supported control with an independently estimated conversion. |
| 2017qld | Other elections | No supported control with an independently estimated conversion. |
| 2017qld | Earlier elections only | No supported control with an independently estimated conversion. |
| 2025wa | Other elections | No supported control with an independently estimated conversion. |
| 2025wa | Earlier elections only | No supported control with an independently estimated conversion. |

Run from the analysis directory on native Windows:

~~~powershell
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_category_dynamics --dry-run
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_category_dynamics
.\.venv-win\Scripts\python.exe -B -m scripts.turnout.turnout_category_dynamics --check
~~~

The public report contains implemented methods and consolidated findings. The ignored local analysis.json contains comparisons, predictions, training identities, scores and joint error matrices. Refresh normalized sources and the federal supplement after a source correction, then regenerate this analysis. Input and code content hashes let --check detect local changes; it does not poll remote sources. Add --include-nsw to include NSW in training and prediction. An em dash indicates that a quantity cannot be estimated from the available evidence.
