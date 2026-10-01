"""Locate authored inputs and outputs independently of internal module layout."""

from pathlib import Path


# Internal modules live beneath lib/, while operational commands retain analysis/
# as their working directory. Anchor durable data to the package's parent so
# relocating a helper cannot redirect it into a library subdirectory.
ANALYSIS_DIRECTORY = Path(__file__).resolve().parent.parent
REPOSITORY_DIRECTORY = ANALYSIS_DIRECTORY.parent
