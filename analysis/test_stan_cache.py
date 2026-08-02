import gzip
import importlib.util
import inspect
import os
import pickle
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock


def load_stan_cache(fake_pystan):
    module_path = Path(__file__).with_name("stan_cache.py")
    spec = importlib.util.spec_from_file_location(
        "stan_cache_under_test", module_path
    )
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"pystan": fake_pystan}):
        spec.loader.exec_module(module)
    return module


class StanCacheTests(unittest.TestCase):
    def setUp(self):
        self.compiled_models = []
        self.pystan = types.ModuleType("pystan")
        self.pystan.__version__ = "test-version"

        def compile_model(**kwargs):
            model = {"model_code": kwargs["model_code"]}
            self.compiled_models.append(model)
            return model

        self.pystan.StanModel = compile_model
        self.cache = load_stan_cache(self.pystan)
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.old_directory = os.getcwd()
        os.chdir(self.temporary_directory.name)
        self.addCleanup(os.chdir, self.old_directory)

    def test_compiles_once_then_loads_the_existing_cache(self):
        first_model = self.cache.stan_cache("model { }")
        second_model = self.cache.stan_cache("model { }")

        self.assertEqual(len(self.compiled_models), 1)
        self.assertEqual(first_model, second_model)
        self.assertTrue((Path("stan-cache")).is_dir())

    def test_public_api_accepts_only_model_code(self):
        self.assertEqual(
            list(inspect.signature(self.cache.stan_cache).parameters),
            ["model_code"],
        )

    def test_replaces_a_corrupt_cache_without_leaving_temporary_files(self):
        filename = Path(self.cache._cache_filename("model { }"))
        filename.parent.mkdir()
        filename.write_bytes(b"not a gzip file")

        self.cache.stan_cache("model { }")

        with gzip.open(filename, "rb") as cache_file:
            self.assertEqual(pickle.load(cache_file), {"model_code": "model { }"})
        self.assertEqual(list(filename.parent.glob(".stan-model-*.tmp")), [])

    def test_interrupts_are_not_treated_as_cache_misses(self):
        filename = Path(self.cache._cache_filename("model { }"))
        filename.parent.mkdir()
        with gzip.open(filename, "wb") as cache_file:
            pickle.dump({"model_code": "model { }"}, cache_file)

        with mock.patch.object(
            self.cache.pickle, "load", side_effect=KeyboardInterrupt
        ):
            with self.assertRaises(KeyboardInterrupt):
                self.cache.stan_cache("model { }")

        self.assertEqual(self.compiled_models, [])


if __name__ == "__main__":
    unittest.main()
