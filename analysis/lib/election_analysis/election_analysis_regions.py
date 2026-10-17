"""Generate federal regional prediction parameters without fitting Stan models.

The regional calibration excludes entire elections when measuring prediction
errors. The public entry point is shared by the full historical analysis and
standalone regional generator; callers need not manage individual fitting stages.
"""

from pathlib import Path


def is_before_target(election_code, target_year):
    """Return whether an election may train the requested target."""
    return election_code.year() < int(target_year)


def analyse_region_swings(target_year, data_directory=Path("Data"),
                          output_directory=Path("Regional")):
    """Fit, validate and export regional parameters for one forecast election."""
    from lib.election_analysis.regional_calibration import (
        load_evidence, calibrate_regions, write_parameters,
    )

    evidence = load_evidence(int(target_year), Path(data_directory))
    calibration = calibrate_regions(evidence)
    write_parameters(calibration, int(target_year), Path(output_directory))
    return calibration["diagnostics"]
