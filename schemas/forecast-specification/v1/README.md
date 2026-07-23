# Forecast Specification Schema Version 1

This schema contains the portable configuration required by the core forecast
pipeline. It does not replace existing analysis datasets or preserve the
complete contents of a `.pol2` project.

## Scope

Version 1 owns:

- election identity and display name;
- core party configuration;
- core region definitions not provided by existing analysis files;
- Stan model configuration;
- projection configuration; and
- election-wide historical and live source settings; and
- per-simulation configuration.

Version 1 references the canonical seat file under `analysis/seats` and does not
copy its contents. It excludes Poll and Pollster collections, generated model
and projection output, reports, saved results, GUI state, per-seat live
overrides, and temporary forced-TPP values.

`election_name` is the canonical human-readable name used in reports and
uploads. The legacy `.pol2` project `name` field is not used by the core
pipeline and is excluded. Import updates the legacy `electionName` field but
leaves the separate project name unchanged.

The old `StanModel::preferenceFlow` text setting is excluded because current
models load preference flow and exhaust estimates from
`analysis/Data/preference-estimates.csv`. Preference deviations and historical
sample counts remain model configuration and are represented explicitly in the
manifest.

The collection-wide Others preference flow and exhaust rate are also excluded.
They support legacy poll recalculation and fallback code, while the core model
and simulations use the election-specific values in
`analysis/Data/preference-estimates.csv`. That canonical file remains required
and takes precedence in the active forecast path.

## Package

A version 1 package consists of a manifest and four CSV tables:

```text
forecast.json
parties.csv
party-official-codes.csv
nonclassic-preferences.csv
regions.csv
```

The manifest names the tables, so these filenames are conventional rather than
mandatory. Table paths are resolved relative to `forecast.json`. External data
source paths, such as the seat file, are resolved relative to the Polling
Analyser workspace root and continue to point to their existing locations.

`forecast.schema.json` is the Draft 7 machine-readable schema for the manifest. The
rules below are normative for the CSV tables and for constraints which require
cross-file validation.

## Common CSV Rules

- Files are UTF-8 CSV with one header row.
- RFC 4180 quoting is supported. A quoted field uses doubled quotes to represent
  a literal quote.
- Readers accept CRLF or LF line endings. Writers use the repository's existing
  line-ending policy.
- Headers and column order must exactly match this document for schema version
  1. Unknown or missing columns are errors.
- Empty fields are allowed only where explicitly stated.
- Identifiers match `^[a-z][a-z0-9_-]*$` and are case-sensitive.
- Numeric fields must be finite. `nan`, infinity, and partially parsed values
  are errors.
- `index` values start at zero, are unique and contiguous, and determine the
  collection order independently of row order.
- References use stable identifiers rather than transient `.pol2` collection
  IDs or indices.

## `parties.csv`

```csv
index,id,name,abbreviation,home_region_id,seat_target,relation_type,relation_target_party_id,ideology,preference_consistency
```

- `name` and `abbreviation` must be non-empty.
- `home_region_id` may be empty; otherwise it references `regions.csv`.
- `seat_target` is non-negative.
- `relation_type` is empty, `supports`, `coalition`, or `is_part_of`.
- `relation_target_party_id` is empty when `relation_type` is empty and otherwise
  references another party.
- `ideology` is `strong_left`, `moderate_left`, `centrist`, `moderate_right`, or
  `strong_right`.
- `preference_consistency` is `low`, `moderate`, or `high`.

The manifest identifies the two major parties explicitly. They must occupy
indices zero and one respectively when loaded into the current simulation
engine.

Poll-side preference and exhaust estimates, colours, `boothColourMult`, and
`includeInOthers` are not part of the core specification. Core preference and
exhaust estimates come from `analysis/Data/preference-estimates.csv`.

## `party-official-codes.csv`

```csv
party_id,official_code
```

Each row assigns one electoral or seat-data code to a configured party. These
codes are distinct from the trend groupings in `party-groups.csv` and the
historical classifications in `party-simplification.csv`; the current
simulation uses them to resolve party references from seat and results data.

- `party_id` references `parties.csv`.
- `official_code` must be non-empty.
- A code may not occur more than once for one party. Cross-party duplicates are
  rejected because they would make party lookup ambiguous.

## `nonclassic-preferences.csv`

```csv
source_party_id,first_target_code,second_target_code,preference_to_first
```

Each row supplies a source party's preference flow for a particular non-classic
pair. Target values use official codes because that is how current non-classic
contests are identified.

- `source_party_id` references `parties.csv`.
- Both target codes must resolve to configured parties and must be different.
- `preference_to_first` is strictly between zero and 100.
- The source party and unordered target pair must be unique.

The reverse ordering is derived as `100 - preference_to_first` and is not stored
as another row.

## `regions.csv`

```csv
index,id,name,population,analysis_code,previous_election_tpp
```

- `name` must be non-empty and unique because the existing seat files refer to
  regions by name.
- `population` must be positive. Simulation preparation still requires this
  `.pol2`-owned value even though current forecast output does not use the
  resulting election-wide total directly.
- `analysis_code` is the code used by files under `analysis/Regional`. It may be
  empty only where no regional analysis rows are expected.
- `previous_election_tpp` is strictly between zero and 100.

Region order is retained explicitly because current regional deviation imports
contain election-specific conversions based partly on numeric region position.

The old `sample2pp`, `swingDeviation`, and `homeRegionMod` fields are excluded.
They do not currently affect core simulation output.

## Manifest Details

Model `parties` are ordered. The first two codes define the TPP pair, and each
entry carries the corresponding preference deviation and historical sample
count. Model term codes must match the top-level election code in version 1.

Projection and simulation references use manifest IDs. Possible projection
dates use ISO `YYYY-MM-DD` dates and non-negative weights; if any are supplied,
at least one weight must be positive.

Election-level `previous_term_codes` contains at least one entry and is ordered
most-recent first. `previous_election_tpp`, `federal_election_date`, and live
source URLs are also election-wide because every simulation of the same
election must use the same historical baseline and source data. The date and
`live_sources` are optional, including when an automatic live simulation is
configured but has not yet been prepared. Required files and URLs depend on the
election authority and are checked when that live simulation is run.

Simulation names, base projections, iteration counts, live modes, and report
modes remain per-simulation. Iteration counts are committed because they affect
the statistical resolution and reproducibility of forecast output. A future
command-line override may provide temporary machine-specific scaling without
changing this canonical value.

## Cross-File Validation

The JSON Schema cannot express all package-level rules. The C++ validator must
also check:

- unique IDs for parties, regions, models, projections, and simulations;
- contiguous party and region indices;
- valid major-party, relation, home-region, model, and projection references;
- agreement between the top-level election code and model term codes;
- required model codes, including `OTH`, `xOTH`, and `eOTH`;
- uniqueness and resolvability of official party codes;
- valid non-classic target pairs;
- positive iteration counts and coherent projection dates;
- existence of each configured file and external data source.

Detailed seat-format and region compatibility checks remain the responsibility
of the canonical seat importer. Keeping that parsing in one place avoids a
second interpretation of the existing `analysis/seats` format.

Validation should report all independently discoverable errors in deterministic
file and row order rather than stopping at the first semantic error.

## Exporting Existing Projects

With a `.pol2` project open, use **File > Export Forecast Configuration...**
and select or create the package directory. Existing package files require
confirmation before they are replaced. The exporter writes all five files and
then reads them through the public loader in a temporary staging directory. It
publishes them only after validation succeeds, so a success message confirms
both serialization and version 1 validation.

The current `.pol2` format stores election-wide settings in every simulation.
Export ignores empty copies and uses the sole non-empty value for previous term
codes, previous-election TPP, federal election date, and each live source URL.
Conflicting non-empty values are reported rather than resolved arbitrarily.

This operation does not modify the source project.

Normal **Save** and **Save As** operations automatically export to
`forecasts/<model-term-code>` before writing the `.pol2` file when that forecast
directory already exists. Validation failure aborts the whole save and leaves
the `.pol2` file untouched. If no matching forecast directory exists, legacy
projects continue to save normally and the application displays a warning that
portable configuration was not exported.

## Loading Existing Projects

After loading a `.pol2` file, the application looks for
`forecasts/<model-term-code>/forecast.json`. If no matching package exists, the
legacy configuration remains in use. If one exists, it is strictly validated
and its core settings override the corresponding `.pol2` values.

Portable objects are matched to legacy collections by exported order because
the current `.pol2` format does not persist portable IDs. Collection counts
must therefore match. Generated model, projection, and simulation state is
retained when source settings are unchanged and invalidated when an imported
change makes it stale. Reports and all data outside the version 1 scope remain
in the `.pol2` file.

## Versioning

`schema_version` selects the complete manifest and CSV contract. Readers must
reject unsupported versions rather than attempting a best-effort parse. Future
changes to field meaning, required columns, or normalisation rules require a new
schema directory and an explicit migration path.
