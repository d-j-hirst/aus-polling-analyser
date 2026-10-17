"""Regenerate only federal regional prediction parameters, without Stan fitting.

Use --output-directory for a scratch comparison. The default writes the four
regional CSVs for each selected election and records their exact dependencies
without certifying or regenerating any other historical-analysis outputs.
Optional JSON reports contain excluded-election errors and reconciliation SDs.
"""

import argparse
import json
from pathlib import Path

from lib.paths import ANALYSIS_DIRECTORY
from lib.election_analysis.election_analysis_regions import analyse_region_swings
from lib.provenance import federal_regional_provenance, generated_provenance


def record_outputs(elections):
    """Certify just the regional outputs this standalone invocation generated."""
    base = ANALYSIS_DIRECTORY
    dependencies = {
        "regional_calibration_code": generated_provenance.file_dependency(
            "regional_calibration_code", [base / path for path in [
                "scripts/pipeline/regional_analysis.py",
                "lib/election_analysis/election_analysis_regions.py",
                "lib/election_analysis/regional_calibration.py",
                "lib/shared/sample_kurtosis.py",
                "lib/provenance/federal_regional_provenance.py"]], base),
        "regional_history": generated_provenance.file_dependency(
            "regional_history", [base / "Data" / name for name in [
                "tpp-fed-regions.csv", "region-polls-fed.csv"]], base),
    }
    run_id, run = generated_provenance.generation_run(
        generated_provenance.current_command(),
        generated_provenance.current_source_revision(base),
        generated_provenance.current_environment())
    records = {federal_regional_provenance.record_key(election):
        generated_provenance.generation_record(
            category="federal_regional_statistics", stage="analyse_elections",
            scope=generated_provenance.generation_scope(elections=[election]), run=run_id,
            dependencies=dependencies,
            outputs=generated_provenance.output_fingerprints(
                federal_regional_provenance.output_paths(election), base))
        for election in elections}
    generated_provenance.update_manifest(federal_regional_provenance.MANIFEST_PATH,
                                         records, {run_id: run}, path_base="..")


def generate(elections, output_directory, report_directory):
    """Calculate requested elections, then publish provenance for maintained files."""
    for election in elections:
        diagnostics = analyse_region_swings(int(election[:4]),
                                            ANALYSIS_DIRECTORY / "Data", output_directory)
        if report_directory is not None:
            report_directory.mkdir(parents=True, exist_ok=True)
            (report_directory / f"{election}-regional-calibration.json").write_text(
                json.dumps(diagnostics, indent=2) + "\n", encoding="utf-8")
        print(f"Generated {election}: {len(diagnostics['training_years'])} independent polling elections.")
    if output_directory.resolve() == (ANALYSIS_DIRECTORY / "Regional").resolve():
        record_outputs(elections)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--election", action="append", choices=federal_regional_provenance.SUPPORTED_ELECTIONS)
    parser.add_argument("--output-directory", type=Path, default=ANALYSIS_DIRECTORY / "Regional")
    parser.add_argument("--report-directory", type=Path)
    args = parser.parse_args()
    elections = list(dict.fromkeys(args.election or federal_regional_provenance.SUPPORTED_ELECTIONS))
    generate(elections, args.output_directory, args.report_directory)


if __name__ == "__main__":
    main()
