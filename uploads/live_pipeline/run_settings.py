"""Locate a saved run for the uploader and replay feeder on either host."""
import os
from pathlib import Path, PureWindowsPath
import re
import subprocess

from .storage import read


def check_trial_options(config, destinations=(), commentary=None):
    """A commission's local trial may exercise publication only on the local replay server."""
    if 'local_trial' not in config:
        return
    if not isinstance(config['local_trial'], bool):
        raise ValueError('local_trial must be true or false.')
    if not config['local_trial']:
        return
    if config.get('kind') != 'replay' or config.get('visibility') != 'private':
        raise ValueError('Local trial data requires a private replay.')
    if any(name != 'local' for name in destinations):
        raise ValueError('This trial may only be uploaded to --local.')
    for settings in (config.get('commentary'), commentary):
        if settings and (settings.get('enabled') or settings.get('compare_writers') or settings.get('rules_only')):
            raise ValueError('Commentary must stay off for this local trial.')


def load_run(workspace, name, *, allow_path=False):
    is_id = re.fullmatch('[a-z0-9][a-z0-9-]*', name) is not None
    if is_id:
        folder = Path(workspace)/'live_runs'/name
        # Older uploader commands could name a directory in the current
        # working directory. A saved ID at the workspace root takes precedence.
        if allow_path and not folder.exists() and (Path(name)/'run.json').is_file():
            folder = Path(name)
            is_id = False
    elif allow_path:
        folder = Path(name)
    else:
        raise ValueError('Use the run ID from Configure watching (lowercase letters, digits and hyphens).')
    try:
        settings = read(folder/'run.json')
    except FileNotFoundError:
        raise ValueError(f'No saved run at {folder}. Save the run in Configure watching first.') from None
    if not isinstance(settings, dict) or not isinstance(settings.get('run_id'), str):
        raise ValueError(f'{folder}/run.json must contain a saved run ID.')
    if is_id and settings.get('run_id') != name:
        raise ValueError(f'The saved run ID does not match {name}.')
    return folder, settings


def feed_inbox(workspace, settings):
    """The GUI records relative inboxes from the workspace, not the shell."""
    source = settings['source_directory']
    if not source:
        raise ValueError('The saved run has no feed inbox.')
    home_marker = source == '<HOME>' or source.startswith(('<HOME>/', '<HOME>\\'))
    if home_marker:
        relative = source[6:].lstrip('/\\')
        if os.name != 'nt' and 'WSL_DISTRO_NAME' in os.environ:
            # Home-relative paths saved by the Windows GUI refer to the
            # Windows user's profile, which may differ from the WSL home.
            profile = subprocess.check_output(['cmd.exe', '/d', '/c', 'echo %USERPROFILE%'],
                                              cwd='/mnt/c', text=True, timeout=10).strip()
            source = str(PureWindowsPath(profile)/relative)
        else:
            return Path.home()/relative
    path = Path(source)
    if os.name != 'nt' and PureWindowsPath(source).is_absolute():
        return Path(subprocess.check_output(['wslpath', '-u', source], text=True, timeout=10).strip())
    return Path(workspace)/path
