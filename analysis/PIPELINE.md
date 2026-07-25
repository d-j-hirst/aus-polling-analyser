# Python Data Pipeline

This document describes how authored analysis inputs become the generated data
used by the C++ forecast pipeline. The machine-readable version of the same
dependency map is in `pipeline_registry.json`.

The registry describes and validates the known dependency graph and prints a
regeneration order. `analysis_provenance.py` combines that graph with source
and generated-data manifests to detect stale work units and trace their impact
to the C++ model. Generator orchestration is not yet automated.

## Inspecting the Registry

Run these commands from `analysis/`:

```bash
python3 pipeline_registry.py
python3 pipeline_registry.py --check-paths
```

The first command validates category references, producers and the strict
dependency graph. The second also checks that every authored input pattern
currently matches at least one file.

## Data Categories

The registry groups files by purpose rather than recording every individual
file. This is important for calibration, where one logical category can contain
thousands of files.

Categories currently have four kinds:

* `authored`: manually maintained source data or model definitions.
* `generated`: reproducible outputs consumed by later stages.
* `cache`: downloaded source material retained to avoid changing or repeatedly
  fetching the underlying data.
* `diagnostic`: generated analysis that is not currently required by the core
  forecast pipeline.

Scopes identify the useful unit of generation and future freshness reporting.
Examples include a whole election, an election-party pair, or an individual
pollster calibration unit.

## Dependency Meanings

The registry distinguishes three forms of dependency:

* `inputs` are required and determine the strict regeneration order.
* `optional_inputs` improve or extend a calculation but have a defined fallback
  when absent.
* `feedback_inputs` are outputs from an earlier generation that can affect a
  new generation of the same or an upstream category.

Feedback dependencies are deliberately excluded from the topological order.
They expose places where the current pipeline is iterative or depends on the
last successful output, rather than pretending the pipeline is a simple DAG.

## Core Flow

The core path has three main branches that converge in the C++ program.

```text
Historical election sources
  -> cached election results
  -> checked election-result CSVs
  -> seat statistics / Nationals allocations / federal regional statistics

Polling inputs + Stan models
  -> leave-one-pollster-out calibration and bias calibration
  -> compact pollster parameters
  -> pure poll trends
  -> synthetic TPP observations
  -> normal poll trends
  -> point-in-time historical cutoff trends
  -> trend adjustments and fundamentals

Regional polling inputs + Stan models
  -> regional swing deviations

Federal booth results + authored booth-to-seat mappings
  -> federal/state seat deviations
  -> manually maintained fTransposedFederalSwing in forecast seat files

All required generated categories + forecast seat files
  -> C++ model, projection and simulation
```

The live booth-result scrapers are recorded separately because they are not
required for an ordinary forecast. `federal_state.py` is also outside routine
forecast execution, but its results feed authored state seat configuration.

## Strict Regeneration Sequence

Independent stages in this list may run concurrently. The registry validator is
the authoritative source if this order changes.

1. Cache historical election results with `election_data.py`.
2. Export checked election-result CSVs with `election_store.py`.
3. Generate seat, Coalition-allocation and federal regional statistics with
   `election_analysis.py`.
4. Run leave-one-pollster-out calibration with
   `fp_model.py --calibrate`.
5. Run bias calibration with `fp_model.py --bias`.
6. Reduce calibration output with `pollster_analysis.py`.
7. Generate voting-intention-only trends with `fp_model.py --pure`.
8. Generate synthetic TPP observations with `approvals.py`.
9. Generate normal poll trends with `fp_model.py`.
10. Generate historical point-in-time fits with
    `fp_model.py --cutoff`.
11. Generate adjustments and fundamentals with `trend_adjust.py`.
12. Generate election-specific regional swing deviations with
    `region_model.py`.

Steps 1-3, 4-11 and 12 are largely independent branches. A normal C++ forecast
requires the relevant outputs from each branch, but it does not require every
historical election or every calibration unit to have been regenerated at the
same time.

Step 10 is the intended leakage-safe dependency for step 11, but that edge is
not implemented yet. Until it is, regenerated adjustments remain affected by
the look-ahead issue described below.

## Calibration and Partial Freshness

Poll calibration is exceptionally expensive and may take more than a month to
regenerate. The intended future orchestration must therefore:

* retain the last successful output for every calibration unit;
* promote a replacement only after that unit completes and validates;
* allow current and older valid units to coexist;
* record exactly which generations were used by downstream summaries;
* report partial or stale data without rejecting it by default;
* resume after interruption without repeating completed work.

Missing, corrupt or schema-incompatible data should eventually be classified as
invalid. Valid but old data is a provenance warning, not automatically an
error.

Detailed calibration trends currently dominate analysis storage. Downstream
pollster analysis mostly consumes compact statistics, but some calculations
still inspect detailed adjusted-poll and house-effect files. Those remaining
uses must be migrated before detailed traces can become optional diagnostics.

## Source Provenance

`source_provenance.schema.json` defines the versioned format for authored
source manifests. `source_provenance.py` validates and updates those manifests
using only the Python standard library.

Source manifests are intended to be committed to Git, with one manifest per
authored folder. They record file hashes and manually assessed change events.
Generated-data manifests will be kept outside Git and included with future
generated-data archives.

The filename is part of this boundary. Committed source manifests use the
plain `provenance.json` name. Local generated-data bundles must end in
`generated-provenance.json` and are ignored repository-wide. A manifest must
not combine authored source categories with generated work-unit records; a
folder containing both uses one source manifest and a separate generated
bundle.

The repository baseline is recorded in:

* `provenance.json` for the initially monitored Python code path;
* `Data/provenance.json`;
* `Regional/provenance.json`;
* `Models/provenance.json`;
* `seats/provenance.json`;
* `Federal-State/provenance.json`.

These initial events fingerprint the authored files as they existed when
provenance tracking was introduced. They do not state that any generated
output was regenerated, validated against those sources, or current. Generated
outputs remain of unknown provenance until separate generation manifests are
introduced.

Create a manifest and record a category baseline:

```bash
python3 source_provenance.py init Data/provenance.json \
  --description "Authored polling and election inputs."

python3 source_provenance.py add-category Data/provenance.json raw_poll_data \
  --description "Raw polling observations." \
  --pattern "poll-data-*.csv" \
  --summary "Initial repository provenance baseline."
```

Check recorded files against the filesystem:

```bash
python3 source_provenance.py check \
  Data/provenance.json \
  Regional/provenance.json \
  Models/provenance.json \
  seats/provenance.json \
  Federal-State/provenance.json
```

A content change, addition or removal returns a non-zero status until it is
assessed and recorded. A modification-time change with identical content is
reported but does not fail the check.

Record an output-affecting correction with explicit scope:

```bash
python3 source_provenance.py record Data/provenance.json raw_poll_data \
  --summary "Corrected one poll in the 2019 federal cycle." \
  --change-type correction \
  --magnitude minor \
  --affects-outputs yes \
  --election 2019fed
```

Semantic revisions increase only for changes declared capable of changing
generated output. Non-output-affecting changes must use `negligible` magnitude
and do not invalidate downstream data.

Each output-affecting event can be limited by election, party and/or generation
stage. Multiple values within one dimension are alternatives; populated
dimensions apply together. Matching is case-insensitive. If a downstream work
unit does not identify a dimension constrained by the event, matching is
conservative: the event is treated as potentially relevant rather than being
silently ignored.

A future generated-data manifest will record the source category revision used
for each work unit. Later events can then be matched against that work unit:

```bash
python3 source_provenance.py impact Data/provenance.json raw_poll_data \
  --after-revision 1 \
  --election 2028fed \
  --party "ONP FP" \
  --stage generate_poll_trends
```

This reports only output-affecting events newer than revision 1 whose scopes
can affect that election-party-stage combination. A category-level or global
output can use `--all-scopes`; an election-level target can omit `--party`, in
which case party-specific changes to that election are conservatively included.

Modification times are retained as a local editing safeguard, but content
hashes are authoritative. Git checkouts, archive extraction and file copying
can change timestamps without changing the underlying source.

### Repository-Level Audit

`analysis_provenance.py` is the normal top-level interface. It audits every
authored-source manifest, the monitored Python scripts and available generated
manifests:

```bash
python3 analysis_provenance.py audit
```

Audit output is collapsed by root cause rather than listing every stale
generated work unit. It reports only the changed source or script and the
affected categories consumed directly by the C++ model. Impacts that pass
through pollster or bias calibration are listed separately because those paths
can take days or weeks to refresh and may remain knowingly stale; other C++
input impacts should normally be addressed promptly.

Use one or more `--election` options to audit only a current forecast group:

```bash
python3 analysis_provenance.py audit \
  --election 2026vic --election 2027nsw --election 2028fed
```

The selected records are expanded through their recorded generated-file and
generated-manifest dependencies, including dependencies belonging to earlier
or different elections. This makes the scope a dependency closure rather than
a simple filename filter. Unregistered authored-source changes remain visible
in every scope because their impact has not yet been assessed.

The currently monitored code path covers `election_store.py`,
`election_check.py`, `election_data.py`, `election_code.py`,
`election_analysis.py`, `poll_transform.py`, `sample_kurtosis.py`,
`fp_model.py`, `stan_cache.py`, `calibration_provenance.py`,
`fp_model_provenance.py`, `pollster_analysis.py` and
`pollster_analysis_provenance.py`, `trend_adjust.py` and
`trend_adjust_provenance.py`. Each script has its own category so that
assessing one edit cannot silently accept changes to another.

Register an assessed script or source-file change with:

```bash
python3 analysis_provenance.py register-change election_store.py \
  --summary "Added comments without changing export behaviour." \
  --impact negligible
```

`negligible` changes retain the existing semantic revision and permit generated
data to remain current. `minor`, `material` and `major` changes increment the
revision and invalidate matching generated work units until regeneration.
Scopes can be supplied with `--election`, `--party` and `--stage`; without a
scope the change is conservatively recorded as affecting all work.

Run `python3 analysis_provenance.py` without arguments for an interactive audit
and registration menu. It uses InquirerPy when available and falls back to
plain terminal prompts otherwise, so InquirerPy remains optional.

## Generated Provenance

`generated_provenance.schema.json` and `generated_provenance.py` define bundled
metadata for generated files. A bundle contains:

* shared run records containing UTC time, command, Git revision, working-tree
  status and Python/platform information;
* scoped work-unit records containing category, stage, election/party scope,
  random seed, direct dependency digests and output fingerprints.

Output fingerprints retain SHA-256 as the authority and also store size and
nanosecond modification time. Routine audits batch filesystem checks for the
selected work units and avoid rehashing unchanged large files; a metadata
change falls back to the content hash, so timestamp-only copies do not create
false staleness.

Separating shared runs from work units avoids repeating environment metadata
across potentially thousands of calibration records. Updating one work unit
preserves unrelated records and removes shared runs only after no current
record refers to them.

Generated manifests are ignored by Git. They describe the generated archive
present on a particular machine and will be included when that archive is
rebuilt. Missing manifests remain supported as legacy data with unknown
provenance rather than being treated as proof that outputs are current.

Generated work units can depend on selected records in another generated
manifest. The dependency digest covers the upstream records' scopes,
dependencies and output fingerprints, while excluding run time and environment
metadata. This propagates substantive staleness without invalidating downstream
data merely because equivalent upstream outputs were regenerated.

The first two integrated stages are the checked election-result export and
historical election analysis:

```bash
python3 election_store.py
python3 election_analysis.py
python3 generated_provenance.py check \
  elections/generated-provenance.json
python3 generated_provenance.py check \
  "Seat Statistics/generated-provenance.json"
```

`election_store.py` now requires every configured
`elections/<term>_results.pkl` cache to exist. It never downloads source data;
`election_data.py` remains the separate acquisition stage. The configured
elections and source-specific corrections are intentionally maintained in
`election_data.py`, since new elections commonly require manual source
overrides.

Only after all election CSVs are written successfully does the exporter update
the bundle. It records one `election_result_exports` work unit per election.
The internal pickle and public CSV are certified together as outputs of that
work unit. Its dependencies are the party-simplification rules and the
semantic revisions of the four monitored election-result scripts. Thus a
pickle update appears as an altered internal output, while an assessed
output-affecting code change invalidates the work unit. A failed or interrupted
export leaves the previous manifest in place, allowing changed output hashes
to be detected rather than incorrectly certifying a partial run.

`election_analysis.py` records one global seat-statistics work unit, one
Nationals-allocation work unit per configured election and one current federal
regional-statistics work unit. These records depend on the checked election
exports as generated inputs, as well as the relevant source-data and script
categories. If an election cache or its provenance becomes stale, that state
therefore propagates through the analysis bundle and into the top-level audit.

### Calibration Provenance

The existing calibration archive predates generated-data provenance and is too
expensive to reproduce merely to establish a baseline. It is therefore
fingerprinted explicitly as `legacy`: hashes verify that the files have not
changed, but the records do not claim that their original inputs, environment
or random seeds are known.

Create or refresh that local baseline without running Stan:

```bash
python3 calibration_provenance.py baseline
```

The resulting ignored bundle is
`Outputs/Calibration/generated-provenance.json`. It groups the thousands of
CSVs into work units rather than creating one sidecar per file:

* one leave-one-pollster-out trace record per election, party and excluded
  pollster, containing its trend, adjusted-poll and house-effect outputs;
* one bias-calibration record per election and party, containing the
  corresponding three `_biascal` files;
* one compact-summary record per election, containing its `calib_*.csv`
  files.

Future `fp_model.py --calibrate` and `--bias` runs replace successfully
completed legacy work units with certified `generated` records. Each record
contains the semantic revisions of `fp_model.py`, `stan_cache.py`,
`election_code.py`, the provenance helper, the configured data inputs and the
specific `Models/fp_model.stan` source. Regional and dormant Stan models are
tracked separately and therefore do not unnecessarily invalidate poll
calibration. Python, NumPy, pandas and pystan versions are retained in the run
environment.

Stan receives a separate deterministic seed for each election, party and
excluded pollster. Supply `--seed N` to reproduce a complete calibration run;
if omitted, a random base seed is generated and each exact derived work-unit
seed is still recorded. Provenance is flushed after every excluded-pollster
block, so an interruption loses metadata only for the currently running block.

State calibration still reads existing normal federal minor-party trends as
priors. The precise files found at run time are recorded as
`poll_trend_outputs` dependencies. This preserves visibility of the known
feedback path until calibration-specific federal trends replace it.

The repository audit reports legacy or stale calibration records separately:

```text
Calibration-path-only provenance issues:
  Slow calibration regeneration may be tolerated temporarily.
C++ direct inputs stale only through calibration paths:
  Slow calibration updates may be tolerated temporarily.
```

This classification is intentionally distinct from immediate staleness in
ordinary generated inputs. Calibration can take weeks and may remain knowingly
old, while non-calibration changes should normally be addressed promptly. The
`-only` label is used only when every currently stale route for that issue or
direct input has entered the calibration branch. A shared script change is
therefore an ordinary issue until its non-calibration outputs are regenerated;
after that, an old calibration record can carry the same change exclusively
through the calibration path.
Legacy calibration records remain uncertified because their original input
lineage is unknown. Their historical random seeds may also be unavailable, but
seed availability is informational and does not by itself make an output stale.

### Pollster-Parameter Provenance

`pollster_analysis.py` reduces the calibration archive into three compact
files per target election:

* `variability-<term>.csv`;
* `he_weighting-<term>.csv`;
* `biases-<term>.csv`.

The generated inputs actually read are the poll, trend and house-effect files
from bias calibration and, when available, compact `calib_*.csv` summaries.
The earliest elections may have no leave-one-pollster-out summaries; in that
case variability analysis intentionally uses its configured prior alone.
Ordinary
leave-one-pollster-out trace files are not read directly and are therefore not
declared as an input to this stage. Authored inputs include election dates,
significant parties, linked-pollster relationships and eventual results.

Existing outputs can be fingerprinted without rerunning the analysis:

```bash
python3 pollster_analysis_provenance.py baseline
```

The ignored `Outputs/Calibration/pollster-generated-provenance.json` bundle
contains one election-level work unit holding all three files. Only canonical
filenames are included, so manually retained files such as `BASELINE CHECK`
copies are not treated as generated outputs.

A normal `pollster_analysis.py` run validates its authored dependencies before
writing any CSV. It is intentionally allowed to use legacy or stale
calibration records because recalibrating the full archive can take weeks.
Those exact record dependencies remain attached to the new work unit, so the
parameters and their downstream poll trends remain reported as stale only
through calibration paths until the selected calibration units are refreshed.
The three outputs are certified together only after all analyses for the
target election complete.

### Pure Poll-Trend Provenance

`fp_model.py --pure` writes three files for each election and modelled party:
the voting-intention-only trend, adjusted polls and house effects. A completed
party fit is recorded immediately as one work unit in the ignored
`Outputs/pure-generated-provenance.json` bundle, including its Stan seed.
This preserves completed work if a later party or election fit is interrupted.

The work unit records the relevant poll and model source categories, the exact
election-level pollster-parameter record, and any existing federal trend files
actually loaded as minor-party priors. Pure state trends load federal `_pure`
trends, not the later final federal trends. They load each federal cycle whose
dates overlap the state election cycle, but only for parties configured as
significant in both elections. Federal runs do not load federal trend outputs
as priors. Pollster parameters are permitted to retain stale calibration
ancestry so routine generation is not blocked by the multi-week calibration
cycle. Because the pollster manifest is audited separately, inherited
calibration issues are reported at their original calibration roots rather
than duplicated as ordinary pure-trend issues.

Existing canonical `_pure.csv` triplets can be fingerprinted without claiming
that their inputs or random seeds are known:

```bash
python3 fp_model_provenance.py baseline
```

Backup filenames such as `fp_polls#_...` are excluded. An incomplete canonical
triplet is rejected rather than being certified as a valid work unit.

`approvals.py` currently loads the pure TPP trend and adjusted polls from every
configured election term containing valid leader approval observations and
for which those files exist. Terms without approval polls are excluded before
their pure outputs are opened. A formally complete synthetic TPP refresh
therefore depends on the historical pure TPP work units with approval evidence,
not just the target election.

The ignored `Synthetic TPPs/generated-provenance.json` bundle contains one
legacy or generated work unit for each jurisdiction CSV. Each record is scoped
to approval-bearing terms in that jurisdiction, while its dependency set
contains every available approval-bearing pure TPP work unit because each
regression can draw historical evidence from every jurisdiction.

Synthetic-TPP-path staleness is non-blocking during routine updates because
regenerating every historical pure trend is moderately expensive. It is shown
separately in audits, above calibration-only staleness: refreshing it is a
higher priority than the multi-week calibration path, but lower priority than
ordinary direct-input issues.

Existing jurisdiction CSVs can be fingerprinted without claiming their
original inputs are known:

```bash
python3 approvals_provenance.py baseline
```

### Final Poll-Trend Provenance

A normal `fp_model.py` run records each completed election-party triplet in
the ignored `Outputs/poll-trend-generated-provenance.json` bundle. These are
the final trend, adjusted-poll and house-effect files consumed by trend
adjustment and the C++ model. Each record includes the Stan seed and is written
as soon as that party fit completes. Point-in-time cutoff runs are deliberately
excluded and remain a separate pipeline stage.

Every final work unit records the relevant source categories and the target
election's pollster-parameter record. TPP, Labor and Coalition work units also
record the synthetic-TPP output for their jurisdiction because only those
models add approval-derived observations. Minor-party records do not claim
that dependency. A state minor-party record additionally fingerprints only
the same party's final federal trend files actually opened from overlapping
federal cycles. Federal records do not depend on other federal trends.

Existing canonical final-output triplets can be fingerprinted without
claiming their original source versions or Stan seeds:

```bash
python3 fp_model_provenance.py baseline-final
```

Discovery is limited to election-party combinations currently listed in
`Data/significant-parties.csv`. This excludes old diagnostics and obsolete
filename fragments that happen to resemble model outputs.

## Existing Feedback Dependency

For state elections, `fp_model.py` loads existing federal trend files from
overlapping election cycles when constructing prior series for selected minor
parties. Pure runs load federal pure trends. Calibration and normal runs still
load normal federal trends, and missing files are silently replaced by
historical priors. Federal elections do not use this path.

Consequently:

* a clean bootstrap without existing output can differ from an incremental
  rebuild;
* regenerating federal outputs can affect later state runs;
* state runs can depend on federal records in their own broad output category;
* deleting all generated files does not necessarily reproduce an incremental
  generation exactly.

The registry marks this as a feedback dependency. It should be examined during
the review of `fp_model.py`; until then, existing federal trends should be kept
available when reproducing the established incremental workflow.

Pure state generation now consumes federal pure output. The remaining intended
resolution is to make each state calibration mode consume matching federal
output:

* leave-one-pollster-out state calibration should prefer the federal
  calibration trend excluding the same pollster, with the full federal
  calibration trend as a fallback;
* state bias calibration should use the federal `_biascal` trend;
* state normal generation should use the federal normal trend;
* federal runs should not load federal trend outputs as priors.

For a fully converged rebuild, this produces an acyclic mode-aware sequence:

```text
federal calibration
  -> state calibration
  -> pollster parameter summaries
  -> federal pure trends
  -> state pure trends
  -> synthetic TPP observations
  -> federal normal trends
  -> state normal trends
  -> trend adjustments
```

Implementing this requires moving federal-prior selection out of the
election-level `ElectionData` constructor. The constructor runs before the
leave-one-pollster-out loop, so it does not yet know which excluded pollster's
federal calibration file should be selected.

This full sequence is not intended to be mandatory after every new poll. In
particular, rerunning every state pure trend before releasing an updated
federal trend would add hours of work for what is expected to be a small
second-order effect. Routine incremental generation may therefore use the most
recent valid state pure trends and record them as a lagged dependency.

A routine federal-poll update should normally regenerate the affected federal
normal trends and their downstream adjustments without blocking on state pure
or state normal trends. The future freshness system should show the age and
generation of those retained dependencies rather than classifying the federal
output as invalid.

During the `fp_model.py` review, the state-pure dependency should be measured
and either:

* removed by constructing pure TPP without federal-prior-sensitive minor-party
  series;
* retained explicitly as a weak, lagged dependency;
* or included only in occasional full convergence runs if its measured impact
  justifies them.

## Generated-Data Consumers

`StanModel.cpp` principally consumes:

* normal poll trends and adjusted polls;
* trend adjustments and fundamentals;
* selected seat statistics;
* preference estimates.

`SimulationPreparation.cpp` principally consumes:

* checked historical election-result CSVs;
* seat statistics;
* Nationals allocation files;
* federal regional statistics and regional swing deviations;
* authored forecast seat files.

Live-election preparation additionally consumes archived booth-result JSON.

The consumer lists in `pipeline_registry.json` are category-level boundaries,
not yet exhaustive file-access enforcement. Direct file access will be checked
more precisely as each generator and consumer is reviewed.

## Point-In-Time Trend Calibration

`fp_model.py --cutoff` excludes later polls in turn and generates every
historical information cutoff in a single invocation. It uses the same 46
triangular day points as `trend_adjust.py`: 0, 1, 3, 6, 10, through 1035 days
before election day. A scheduled point is fitted only when it contains a new
poll information set. Both the scheduled day and the actual latest poll date
are retained: a fit requested at 276 days whose latest poll was 300 days out
must later be treated as a 300-day trend estimate.

New output is consolidated into one `Outputs/Cutoffs/cutoffs_<election>.csv`
file per election. Each row contains `ScheduledCutoffDays`,
`PollTrendEndDays`, a party, all 101 posterior percentiles and its exact Stan
seed. `PollTrendEndDays` is the value that trend adjustment must eventually
use. The legacy per-cutoff trend, poll and house-effect files remain available
but are not tracked as generated outputs. Adjusted polls and house effects are
no longer written in cutoff mode because downstream trend calibration only
needs the endpoint distribution.

The ignored `Outputs/cutoff-generated-provenance.json` manifest records each
consolidated election file and its current dependencies. Each cutoff invocation
starts a fresh file for every requested election, preventing old, imported or
partially generated rows from being mixed with a new batch. Completed
endpoints are written incrementally during that invocation.

Cutoff generation takes a similar amount of time to poll calibration, so stale
cutoff records are reported as calibration-path-only issues. A scoped
`raw_poll_data` change invalidates only cutoff records for the election or
elections named when that change is registered.

`trend_adjust.py` loads these files instead of complete-cycle historical
trends. Exact actual endpoints are used directly. Missing interior days are
interpolated percentile-by-percentile after mapping days through the inverse
triangular-number function. This gives equal weight to endpoints 15 and 28
when estimating day 21, for example. A requested date closer to election day
than the latest poll retains that latest estimate; a date earlier than the
first poll supplies no observation. There is no fallback to a complete-cycle
trend.

## Trend Adjustments And Fundamentals

`trend_adjust.py` writes seven party-group adjustment files and one
fundamentals file for each requested target. Provenance is recorded only after
the complete target has been written. A failed run can therefore leave
partially updated CSVs for diagnosis, but cannot certify them as a successful
generation.

The ignored `Adjustments/generated-provenance.json` bundle stores one record
per target and party group plus one fundamentals record per target. The
adjustment records depend on the matching consolidated cutoff work unit for
each historical election. The fundamentals regression does not use poll
trends, so its record retains only the authored election, result, party and
political-context dependencies shared by the calculation.

Existing files can be fingerprinted without claiming that their original
inputs are known:

```bash
python3 trend_adjust_provenance.py baseline
```

Files containing ` BASELINE CHECK` are comparison artifacts and are excluded
from both the generated category and the legacy baseline.

## Regional Swing Models

`region_model.py --election <election>` fits election-specific regional TPP
deviations. Adding `--party ON` runs the corresponding One Nation model where
that input is available. The stage reads
`Regional/<election>-polls[-ON].csv`, an election-specific Stan model under
`Models/`, the baseline row in that poll file and election-cycle dates, then
writes `Regional/<election>-swing-deviations[-on].csv`.

Regional poll files are authored inputs and remain in Git. Swing-deviation
files and the federal `regions-base`, `regions-polled`, `mix-regions` and
`mix-parameters` files are generated outputs: they are ignored by Git and
should be distributed with generated-data archives instead.

A regional work unit exists only when its poll file contains at least one
non-`Election` row with valid poll dates. An absent file or a file containing
only the previous-election baseline does not require generation. This matters
for future elections, where the general pipeline can be configured well before
regional polling is published.

`Regional/generated-provenance.json` stores one TPP or ONP record per election.
New runs record the exact Stan seed; existing outputs are initially marked as
legacy because their original seeds and environments are unknown. The
repository audit also compares the real-poll work units with manifest records,
so a newly populated regional polling file cannot silently lack an output.

## Federal/State Seat Inputs

`Federal-State/booths-*.txt` contains authored mappings from federal booths to
state seats. `federal_state.py` combines those mappings with downloaded federal
booth results cached in `Federal-State/*.pkl`, then prints seat-level federal
TPP and Greens deviations.

The TPP deviations are manually transferred into
`fTransposedFederalSwing` fields in `seats/*.txt`. The script therefore supports
a real core seat input even though its output is not automatically written to a
generated CSV. Changes to the mappings, cache or calculation should trigger a
review of the affected seat file and a source-provenance update.

## Other Diagnostic Outputs

Jurisdiction-specific booth-result fetchers have inconsistent interfaces and
network dependencies. They belong to the live-analysis branch rather than the
ordinary regeneration sequence.

## Planned Next Steps

The next infrastructure stages are:

1. Add category-level generation manifests, content fingerprints and explicit
   `current`, `stale`, `partial`, `unknown` and `invalid` states.
2. Add planning and status commands without changing generator behaviour.
3. Add atomic promotion, resumable work units and per-stage logs.
4. Review generators in dependency order and tighten their schemas,
   validation, documentation and output boundaries.
5. Replace detailed calibration output with compact summaries where all
   consumers permit it.

Future regeneration planning must support two additional explicit flows:

* A changed-file run should regenerate only categories affected by the selected
  files and their downstream dependencies.
* A completed-election run should require every expected source category for
  that election to be either updated or explicitly marked unavailable or not
  applicable with a reason. An unaddressed expected category should block
  generation.

Routine poll updates remain jurisdiction-specific:

```text
jurisdiction pure trends -> synthetic TPP -> jurisdiction normal trends
```

Trend adjustments are a separate periodic operation rather than an automatic
part of every poll update.
