"""Read installed live-count tuning independently of private fitting outputs.

The checked-in JSON contains model parameters and aggregate sizing assumptions,
never individual feed records. Experiments may propose replacements, but cannot
silently overwrite the values used to export an operational turnout prior.
"""

import copy
import json
from pathlib import Path

from lib.turnout import prior

PARAMETER_PATH = Path(__file__).with_name('live_parameters.json')


def load_parameters():
    """Reject tuning for a different sampler before preparing any vote counts."""
    values = json.loads(PARAMETER_PATH.read_text(encoding='utf-8'))
    if values['schema_version'] != 1 or values['prior_model'] != prior.MODEL_VERSION:
        raise ValueError('Maintained live parameters do not match the turnout sampler.')
    return values


def prior_parameters(identity):
    """Return a named calibration without requiring its training feed archive.

    Presets describe different category definitions and available early/postal
    information. Their original election names identify the installed setup;
    they do not imply that its category mapping suits every later election.
    """
    return copy.deepcopy(load_parameters()['prior_presets'][identity])


def ppvc_size_parameters():
    """Return shared new-centre assumptions, adding current seat types elsewhere."""
    return copy.deepcopy(load_parameters()['ppvc_starting_sizes'])
