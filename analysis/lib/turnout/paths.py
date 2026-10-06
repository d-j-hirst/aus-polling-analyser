"""Locate privately retained feeds without publishing a machine's folder layout.

Local analysis defaults to the ignored downloads/turnout/feed-archive directory.
An operator can use one archive and incoming-download location across elections
through environment variables, or override them on an individual command.
"""

import os
from pathlib import Path

from lib.paths import REPOSITORY_DIRECTORY


def archive_directory():
    """Find the archive containing an operator's authorised historical feeds."""
    value = os.environ.get('POLLING_ANALYSER_FEED_ARCHIVE')
    return Path(value).expanduser() if value else REPOSITORY_DIRECTORY / 'downloads/turnout/feed-archive'


def download_directory():
    """Use a separate incoming location only when the operator supplies one."""
    value = os.environ.get('POLLING_ANALYSER_FEED_DOWNLOADS')
    return Path(value).expanduser() if value else archive_directory()
