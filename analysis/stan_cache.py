"""Compile and cache Stan models without changing their sampling behaviour.

This is infrastructure rather than statistical processing. Callers provide
the Stan source; this module derives a safe interpreter-specific cache name,
loads a valid compiled model when available, and atomically replaces corrupt
or missing cache entries after compilation.

Main functions:
* ``_cache_filename`` derives a filesystem-safe cache key from source and
  Python/PyStan versions.
* ``_write_cache_atomically`` publishes a newly compiled cache entry safely.
* ``stan_cache`` loads or compiles one model for callers such as fp_model.py.
"""

import gzip
import os
import pickle
import re
import sys
import tempfile
from hashlib import md5


import pystan


CACHE_DIRECTORY = './stan-cache/'


def _cache_filename(model_code):
    """Return the legacy-compatible cache path for one compiled Stan model."""

    code_hash = md5(model_code.encode('ascii')).hexdigest()
    filename = (
        '-' + sys.version + '-' + pystan.__version__ + '-' + code_hash +
        '.pkl.gz'
    )
    # sys.version can contain a source-control path such as "tags/v3.12".
    # It belongs in the cache filename, never in a directory beneath it.
    filename = re.sub('[^a-zA-Z0-9_ ,.\\-]', '', filename)
    return CACHE_DIRECTORY + filename


def _write_cache_atomically(filename, model):
    """Publish only complete compressed pickles when processes share a cache."""

    file_descriptor, temporary_filename = tempfile.mkstemp(
        prefix='.stan-model-', suffix='.tmp', dir=CACHE_DIRECTORY
    )
    os.close(file_descriptor)
    try:
        with gzip.open(temporary_filename, 'wb') as cache_file:
            pickle.dump(model, cache_file)
        os.replace(temporary_filename, filename)
    finally:
        if os.path.exists(temporary_filename):
            os.unlink(temporary_filename)


def stan_cache(model_code):
    """Load a locally cached compiled model or compile and cache it."""

    os.makedirs(CACHE_DIRECTORY, exist_ok=True)
    filename = _cache_filename(model_code)
    try:
        with gzip.open(filename, 'rb') as cache_file:
            sm = pickle.load(cache_file)
    except Exception:
        print("About to compile model")
        sm = pystan.StanModel(model_code=model_code, verbose=False)
        _write_cache_atomically(filename, sm)
    else:
        print("Using cached model")

    return sm
