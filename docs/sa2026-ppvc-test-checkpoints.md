# SA 2026 PPVC Test Checkpoints

Use these checkpoints when comparing the `2026sa-test1` and `2026sa-test2`
live forecast runs after introducing adaptive PPVC size estimates.

## Key checkpoints

| Snapshot transition | Reason to inspect |
| --- | --- |
| `21-03-26 20:36:49` to `20:39:32` | Salisbury East is the first PPVC result. The multiplier changes from `2.19` to `1.921`, so this is the first point where forecasts may differ. |
| `21-03-26 21:09:27` to `21:12:09` | Birdwood produces the lowest early multiplier, `1.369`. This is the strongest test for overreaction to sparse evidence; estimated remaining PPVC votes are about 175,000 below the old estimate. |
| `21-03-26 21:31:22` to `21:34:04` | Aldgate and especially Tanunda make the multiplier rebound to `1.584`. Check for an artificial counter-jump, particularly in Schubert and unrelated seats. |
| `22-03-26 00:21:23` to `00:32:11` | By this point 31 PPVCs have reported and the multiplier has stabilised near `1.555`. This is a useful general election-night comparison. |
| `22-03-26 13:03:32` to `14:02:40` | Kadina and Kingscote report. Kadina is unusually large relative to its historical baseline, making Narungga an important independent check. |
| `22-03-26 16:06:57` to `17:10:36` | Hallett Cove, Seaton and Mount Gambier report, moving the multiplier from `1.565` to `1.590`. Check Black, Lee and Mount Gambier as well as unrelated seats. |
| `23-03-26 09:30:48` to `11:11:33` | Port Pirie and Davoren Park are the final multi-centre first-report batch. Only about 5,700 historical PPVC votes remain unreported afterward. |

Before `21-03-26 20:39:32`, the adaptive method should behave exactly like the
old method because no PPVC evidence is available.

## First and last reports

The first PPVC result is **Salisbury East Early Voting Centre in Wright**, at
`21-03-26 20:39:32`.

The last new PPVC result is **Ridgehaven Early Voting Centre in Newland**, at
`25-03-26 12:34:47`. Its aggregate influence should be negligible by then.
One mapped booth, **Port Augusta Early Voting Centre in Stuart**, never reports
under that identity; it retains a 993-vote historical baseline and may warrant
a separate mapping check. Later PPVC changes are revisions to already reported
totals rather than first reports.
