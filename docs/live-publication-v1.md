# Live forecast publication contract version 1

This contract specifies how the Polling Analyser client hands automated forecasts to Australian Election Forecasts. It separates replay runs from official live results, gives every immutable report an identity, and permits numerical publication when commentary misses its deadline. The client, fixtures and mock server are implemented here; backend persistence, access control, public routing and frontend rendering are implemented separately.

## Transport and registration

The client first requests `GET /forecast-api/live/v1/capabilities`. A supporting server returns:

```json
{"schema_version":1,"replay_isolation":true,"idempotent_submission":true,"monotonic_latest":true}
```

All flags must be explicitly true. An unsupported endpoint, missing flag or different version stops submission to that destination. There is no fallback to `submit-report`.

Authentication continues through `POST /auth-api/v1/auth/login/` using the existing operator credentials and `jwt_token` cookie, sent subsequently as `Authorization: JWT <token>`. Use HTTPS except for local development. Credentials are not payload fields.

`POST /forecast-api/live/v1/runs` registers the `run` object from the package. The response contains `run_id` and may include `public_route`. Registration is idempotent for identical metadata. Election, kind, display name, initial visibility, election date and timezone are fixed under that ID; conflicting registrations must not silently change their meaning. An operator's future website-side visibility controls can be implemented separately.

| Run field | Meaning |
| --- | --- |
| `version` | Run-settings contract version, currently 1 |
| `run_id` | Lowercase letters, digits and hyphens; unique per rerun, stable on restart |
| `election` | Election code, for example `2026sa` |
| `kind` | `live` or `replay` |
| `display_name` | Operator-supplied plain text; escape when rendering |
| `visibility` | `private` or `public`; enforce access control on private histories |
| `election_date` | Local calendar date, `YYYY-MM-DD` |
| `timezone` | IANA name, for example `Australia/Adelaide` |

## Submit an immutable forecast

`POST /forecast-api/live/v1/runs/<run-id>/forecasts/<forecast-id>` receives the publication JSON as its **raw UTF-8 request body**. Headers include:

```text
Content-Type: application/json; charset=utf-8
Idempotency-Key: <run-id>/<forecast-id>
X-Content-SHA256: <sha256 of the exact request bytes>
```

Hash the received bytes before parsing. No cross-language JSON reserialization is necessary. The Python client produces the same bytes on retry. Store both the immutable identity and digest. Accept an identical duplicate without creating a second report; reject a different digest for the same identity with HTTP 409.

`forecast_id` is `origin` for the saved pre-count reference, or `f` followed by eight digits for calculations within the run. Failed calculations may leave gaps. The origin is a reference, **never a candidate for the latest live result**. It may have no observation timestamp. The reference is uploaded before numbered forecasts where possible; references can otherwise be resolved after arrival without altering the referring package.

| Package field | Meaning |
| --- | --- |
| `schema_version` | 1 |
| `run` | Registered run metadata |
| `forecast` | Identity, times and revisions listed below |
| `report` | Existing website report fields, plus explicit probability-band thresholds, seat TPP/FP summaries, expected seats and count-progress accounts |
| `comparisons` | Named mechanical comparisons and reference IDs |
| `commentary` | Independently completed text/facts or explicit unavailable status per window |
| `commentary_requests` | Window to request-key mapping, including shared requests |
| `commentary_evidence` | Compact evidence selected for those requests, keyed by request key |
| `reporting_schedule` | Optional reporting timezone and daily/afternoon hours; present in new packages |
| `retained_reports` | Optional references to saved reports, currently yesterday's daily report |
| `released_at` | UTC time the immutable package was released by the reporting worker |
| `frozen` | Always true; later commentary cannot revise this package |

`forecast` contains `forecast_id`, `simulation`, `origin`, `input_revision`, `input_fingerprint`, `source_fingerprint`, `model_revision`, `iterations`, `source_time`, `source_timezone`, `delivered_at` and `completed_at`. Source time is emitted with an explicit UTC offset; `source_timezone` preserves the election-local interpretation. The origin's source time is null because no commission observation produced it. Delivery/completion are real elapsed wall-clock events even during accelerated replay. The C++ build identifier and fixed-input revision are recorded separately.

Do not infer publication time from completion or release. The destination supplies an acknowledgement after its durable transaction:

```json
{
  "run_id":"2026sa-example",
  "forecast_id":"f00000002",
  "sha256":"<received request digest>",
  "published_at":"2026-11-17T01:02:03+00:00",
  "latest_forecast_id":"f00000003",
  "latest_source_time":"2026-03-21T10:31:00+00:00"
}
```

`latest_forecast_id` and `latest_source_time` may be null if only the origin exists. Preserve the original `published_at` on duplicate acknowledgement. The example deliberately acknowledges an older forecast: accepting it must not move the latest pointer backwards.

## Latest selection and public routes

Compare instants, not the lexical spelling of timezone offsets. Within a run, select the greatest source observation time, breaking a tie with the numbered forecast sequence. Exclude the origin. Apply this comparison in the same durable transaction as insertion; asynchronous workers must not race to overwrite the latest pointer.

A replay is addressed at a dedicated route such as `/2026sa/replays/<run-id>`, with its own history and latest pointer. Published replays may be publicly viewed; private ones require authorization. A replay must never appear in the election's official live timeline or overwrite its latest forecast. A separate official-run selection is needed if multiple live runs exist. A rerun gets a new ID and URL; restarting an interrupted run preserves both.

## Comparisons and frontend meanings

Windows are `previous`, `since_start`, then `15m`/`1h` on election night. After election night they are `today`, plus `3h` only from 1 pm in the reporting timezone. Today starts from the last forecast strictly before 5 am in that timezone; a 5-am capture belongs to the new day. `reporting_schedule` defaults to `{"timezone":"Australia/Sydney","day_start_hour":5,"three_hour_start_hour":13}`. Source clocks continue to use the election's timezone. `today` additionally supplies `report_date`, `reporting_timezone` and the intended `requested_reference_time` including its UTC offset. `requested_seconds` is null for a calendar-day comparison. Before 5 am the previous reporting day is still active; display its date to avoid misleading readers.

Display each exact reference identity, actual interval and whether inputs changed. `available: false` means no eligible reference exists; show its reason. An omitted `3h` window is deliberately not scheduled before 1 pm, not failed commentary. Do not relabel a 67-minute comparison as exactly 60 minutes. The origin comparison has `actual_seconds: null`. Existing version-one packages may retain `24h` and omit the new optional fields; the schema accepts them without rewriting their contents.

`retained_reports.yesterday` is a reference to an immutable report in **the same run**, not a new comparison against the current forecast. When available, it names `source_forecast_id` and `source_window` (`today`, or `since_start` for the first election night). Fetch that publication's comparison, commentary and cited evidence and display them as **Yesterday's results**, with the original endpoint time. Do not reuse the current forecast's facts or request IDs. `report_date`, `day_start`, `day_end` and `timezone` identify the intended day; `source_time`, `reference_source_time` and `actual_seconds` identify the actual coverage. No artificial end-of-day forecast is implied. `commentary_status` can be unavailable even when the mechanical report exists.

The selection is frozen locally at the first next-day release, with `selected_by_forecast_id` recording that decision. No new commentary is requested. An unavailable entry gives a reason and can still identify a source whose publication is pending. If that source has not reached this destination yet, display it as pending until history catches up; out-of-order uploads must not cause the server to substitute another day or move its latest pointer backwards. Missing days are not filled with older reports. These references add no copied diagnostics or generated text to each package.

`win_changes` uses percentages for `before`/`after`, percentage points for `change_pp`, and includes within-window minimum/maximum. It is ordered by absolute change. Party IDs are simulation indexes within the run; candidate name/party metadata accompany them. `identity_changed` prevents silently treating a newly named candidate as an unchanged person. Pairing probabilities are percentages. TCP percentile entries are conditional on their explicit pair, and the first party in the pair supplies the share. Do not compare different TCP pairs as a single continuous margin.

`seat_changes` lists matched numerical quantities with their before, after and difference. A missing quantity remains missing rather than becoming a zero. `statewide` includes vote/seat summaries, parliamentary outcomes and measured FP/TCP counting progress against estimated formal turnout. Percentile thresholds are explicit. The complete `report` provides the current distributions; references provide previous full reports.

### Statewide preference flows

An optional `report.statewidePreferenceFlows` table describes how voters who gave one party their first preference are estimated to divide between two specified finalists. Each row identifies three distinct parties through `sourceParty`, `destinationParty` and `otherFinalist`. Resolve all three IDs through that report's `partyAbbr`. For example, Labor → Liberal vs One Nation covers all projected Labor first-preference ballots in districts where all three parties stood. A district with twice as many projected Labor ballots carries twice the weight. Its chance of a Liberal–One Nation final does not enter the weight. The denominator includes every vote category and both counted and projected outstanding ballots.

Each row's `percentiles` array describes uncertainty in the current **shared statewide estimate**. Use the report's existing `voteTotalThresholds` to display the median and the 25–75%, 5–95%, 1–99% and 0.1–99.9% intervals. Individual districts may differ. To show the other finalist's share in a compulsory-preferential contest, reverse the endpoints when taking the complement. A 54–65% interval for Liberal gives a 35–46% interval for One Nation.

Counted and outstanding ballots contribute differently. At Moorook in Chaffey on 21 March 2026, the 18:57 SA feed reported 85 Liberal first preferences and 126 Liberal TCP votes. That identifies exactly 41 Liberal transfers from a pool of 60. Labor supplied 29 ballots to the pool, Greens 11 and other candidates 20. To estimate how many of each supplied the 41, each simulation draw starts with the forecast's relative preference rates and adjusts the groups together until their contributions sum to 41. The split between original parties remains uncertain. A single-source count or a published source-by-source transfer table identifies those votes directly.

Where TCP covers only part of a booth, an accepted earlier FP batch can identify the ballots included. Otherwise the calculation uses the available FP mix for that portion, followed by the projected mix if more ballots are needed. A ballot whose source is still unknown can contribute to the measured finalist totals. It receives a named source weight only when there is a basis for assigning one. A retained count can continue to constrain its original pairing after realignment.

The calculation holds the snapshot's projected FP populations fixed and varies the common preference assumptions across all eligible districts together. For non-classic contests it uses the ranking model, district ticket and geography, and the live adjustments learned for the district and counting category. For classic contests it uses the forecast's sampled flows to Labor, the booth's estimate for pending and future preferences, and the shared Greens and independent conversion errors from matched FP/TPP counts. The table estimates the route of original voters. The forecast can also infer a seat's overall TPP from evidence that does not identify any particular party's preferences. Up to 4,096 accepted election draws supply the displayed percentiles, so the outermost endpoints are approximate.

Show the section for a state live forecast or replay with populated rows. The exporter omits the field from regular forecasts, nowcasts, the separate pre-count reference and saved reports that predate it. The three initial SA rows cover Labor → Liberal over One Nation, Liberal → One Nation over Labor and One Nation → Labor over Liberal. Victorian reports additionally cover selected Greens and Nationals contests. Publication requires compulsory preferences, an active whole-election ranking model and identifiable eligible voters. [Example report with fictional figures](../tests/fixtures/live-publication-v1/statewide-preference-flows.json)

Window comparisons can include `statewide_preference_changes`, with the actual reference forecast ID and the movement in each matched median. For a comparison against the pre-count reference, the first real live capture can establish the starting flow only if it reported zero FP and zero TCP and used the same inputs as the reference. Its ID and source time are recorded separately. If counting was already under way when the first capture arrived, the starting preference estimate is unavailable. The commentary input includes at most three median movements of at least two percentage points and omits comparisons spanning input revisions. This editorial selection leaves every interval available in the numerical report.

### Commentary and counting changes

Completed commentary from `live-changes-4` through `live-changes-8` has `format: "inline-figures-v1"`, a `text` object and an `entities` lookup, plus the model/prompt/request metadata. The text object contains `summary`, `developments` and `insufficient_evidence`. Each development contains `seats` (a list of actual district names, or an empty list for statewide commentary) and `text`. A paragraph may cover several districts. A quiet update can contain only a short summary with an empty `developments` list.

Prompt revisions 9 through 12 give readers headings and short lists. Substantive reports have `format: "seat-bullets-v1"` and use the same text and entity fields. Split `text.summary` and each development's `text` on the literal newline character. Render the first line as a heading and every subsequent line as an item in one unordered list, removing its leading `- `. Apply the existing inline name, badge and bold-figure renderer separately to each heading and item. A standalone party token such as `{party:4}` should display the supplied party as a badge. The exporter supplies every heading and list marker. Each development names exactly one district in `seats` and appears once, with district-wide changes before supporting booth or declaration counts. A quiet notice or an older paragraph report still uses the existing paragraph renderer.

```text
Key changes:
- {seat:Flinders}: {candidate:Flinders:1} gained ground.

In {seat:Flinders}:
- The win chance for {candidate:Flinders:1} rose to **56.1% (+9.1 pp)**.
- At Ungarra, the primary share for {candidate:Flinders:1} was **24.2 pp** above expectations.
```

The blank line above separates two JSON text fields for illustration. Each field contains only its heading and prefixed items, with no blank or trailing lines.

Numbers are literal text in **bold**, for example `**47.9% (+7.5 pp)**`. The level is a percentage; `pp` means percentage points. Values are rounded to one decimal for commentary only, with vote counts shown as whole votes. The client validates numerical phrases against prepared forecast figures and their district context. It also supplies aggregated finalist probabilities, distinct from the probability of one particular pair. These checks do not independently prove causal attribution or catch every possible misdescription of a valid figure.

Names use `{candidate:Flinders:1}`, `{seat:Flinders}` and `{party:1}` placeholders. Resolve the **complete token** through that commentary's `entities` object. A candidate entry identifies its seat, party ID, display label and party badge; this keeps multiple independents distinct. A seat entry provides its label, pre-election holder party and margin where available. Bold the seat name and display the holder badge/margin; do not interpret these as the projected winner or projected margin. Missing holder metadata stays absent. Party-only references are useful for statewide prose. Escape all text and labels, and allow only the documented placeholders and bold spans: do not render arbitrary model-supplied HTML. The local benchmark demonstrates this rendering.

For example, a development can be `{"seats":["Flinders"],"text":"In {seat:Flinders}, the win chance for {candidate:Flinders:1} rose to **47.9% (+7.5 pp)**."}`. No `facts` or `evidence_ids` are requested in the model's output. Numerical comparisons and the original evidence remain in the package for inspection. Existing immutable packages can still contain the earlier `seat`/`explanation`/`facts` format with `metrics` and `metric_contexts`; consumers must retain that display support rather than revalidating old reports against the new schema.

Mechanical comparisons now include `booth_changes`. When available, its `new` and `updated` groups, also repeated under `by_seat`, contain `total`, `shown`, `other_count` and a short `text` list. Show up to three newly reporting units and two significantly updated units, then “and N others”. One unit can be a booth or a declaration category. Ordering uses affected votes, including candidate corrections with no net total change; FP and TCP are not added twice. A first TCP count at a booth with existing FP is an update. The default significant revision is at least ten affected votes. Lists compare the endpoints; the separate count-activity and reversal information can additionally record intervening changes whose endpoints cancel. Each shown record retains its seat, booth/category identity, affected votes and separate FP/TCP net changes. `available: false` means observed count data are missing, not that no booths changed.

Newly generated mechanical comparisons include an optional `booth_changes.retained_events` list when observed counts are available. It identifies every newly reporting unit and every revision affecting more than 20 votes, including candidate reallocations. Each record carries its kind, record ID, seat, name, affected votes and separate signed FP and TCP changes. The existing display shortlist and its omitted counts remain available in older packages.

Unavailable commentary includes a reason. It is permanent for that publication even if the client later obtains text for local inspection. Partial commentary is valid: display available windows and explicit unavailable notices for the rest. Comparisons always refer to the previous **calculated** forecast, not necessarily the previous report successfully delivered to this server.

Unchanged or insignificant counts instead receive completed local text: `status: "complete"`, `source: "local"`, and `kind: "no_new_counting"` or `"minor_changes"`. These use the inline text structure with an empty entities map and no model/request metadata. They have no entry in `commentary_requests`; the frontend must not require an API request for every completed window. Mechanical comparisons remain available even when the local notice replaces paid commentary. `count_activity` and `commentary_decision` explain the filter. The activity measure is a conservative count of changed allocations, including manual preference samples, not a change in formal turnout.

## Fixtures and checks

`tests/fixtures/live-publication-v1/` contains fictional election-night and morning packages plus capability/acknowledgement examples. The morning package shows the new schedule and a yesterday reference to the election-night package. `uploads/live_pipeline/mock_server.py` implements only the test boundary: it demonstrates duplicate acceptance, conflict rejection, origin handling, run isolation and monotonic latest selection, including a retained source arriving after the newer forecast. It is not a production backend or authentication implementation.

The fictional `tests/fixtures/live-publication-v1/inline-commentary.json` demonstrates the inline display contract. The older full-package fixtures deliberately retain their original fact-based format for compatibility checks.

Run `analysis/tests/run_tests.py test_live_automation` with the project's non-Stan Python environment. These checks contact a temporary loopback server and mocked commentary provider. They do not publish real reports or use paid APIs.
