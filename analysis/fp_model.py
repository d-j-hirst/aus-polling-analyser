"""Fit primary-vote and TPP poll trends, calibration evidence and cutoffs.

The command has four modes: ordinary final trends, voting-intention-only
``--pure`` trends, leave-one-pollster-out/bias calibration, and historical
``--cutoff`` fits. Validation and file loading are kept in configuration and
data-preparation helpers; the model calculation itself starts with
``run_party`` and the functions it calls to prepare Stan inputs, sample the
model, and reduce posterior draws.

Implementation is split across ``fp_model_constants``, ``fp_model_data``,
``fp_model_prepare``, ``fp_model_stan``, ``fp_model_outputs`` and
``fp_model_runner``. This module is the command-line entry point.
"""

from fp_model_runner import run_models

__all__ = ["run_models"]

if __name__ == '__main__':
    run_models()
