"""Validate and inspect the Python analysis dependency registry.

This module deliberately uses only the Python standard library. It documents
the current pipeline but does not yet run generators or calculate freshness.
"""

import argparse
import glob
import json
from collections import Counter, defaultdict
from pathlib import Path


REGISTRY_PATH = Path(__file__).with_name("pipeline_registry.json")
CATEGORY_KINDS = {"authored", "code", "generated", "cache", "diagnostic"}
DEPENDENCY_FIELDS = ("inputs", "optional_inputs", "feedback_inputs")


class RegistryError(ValueError):
    """Raised when the dependency registry is structurally invalid."""


def load_registry(path=REGISTRY_PATH):
    with Path(path).open("r", encoding="utf-8") as registry_file:
        return json.load(registry_file)


def _require_keys(item, required_keys, context):
    missing = sorted(set(required_keys) - set(item))
    if missing:
        raise RegistryError(
            "{} is missing required field(s): {}".format(
                context, ", ".join(missing)
            )
        )


def _require_string_list(value, context, allow_empty=False):
    if not isinstance(value, list):
        raise RegistryError("{} must be a list".format(context))
    if not allow_empty and not value:
        raise RegistryError("{} must not be empty".format(context))
    if any(not isinstance(item, str) or not item for item in value):
        raise RegistryError(
            "{} must contain only non-empty strings".format(context)
        )
    duplicates = sorted(
        item for item, count in Counter(value).items() if count > 1
    )
    if duplicates:
        raise RegistryError(
            "{} contains duplicate value(s): {}".format(
                context, ", ".join(duplicates)
            )
        )


def _validate_categories(registry):
    categories = registry.get("categories")
    if not isinstance(categories, dict) or not categories:
        raise RegistryError("categories must be a non-empty object")

    for category_id, category in categories.items():
        context = "category '{}'".format(category_id)
        if not isinstance(category, dict):
            raise RegistryError("{} must be an object".format(context))
        _require_keys(
            category,
            ("kind", "scope", "core", "paths", "description"),
            context,
        )
        if category["kind"] not in CATEGORY_KINDS:
            raise RegistryError(
                "{} has unsupported kind '{}'".format(
                    context, category["kind"]
                )
            )
        if not isinstance(category["scope"], str) or not category["scope"]:
            raise RegistryError("{} scope must be a string".format(context))
        if not isinstance(category["core"], bool):
            raise RegistryError("{} core must be a boolean".format(context))
        if (
            not isinstance(category["description"], str)
            or not category["description"]
        ):
            raise RegistryError(
                "{} description must be a non-empty string".format(context)
            )
        _require_string_list(category["paths"], "{} paths".format(context))
        if "exclude_paths" in category:
            _require_string_list(
                category["exclude_paths"],
                "{} exclude_paths".format(context),
            )


def _validate_stage_dependencies(stage, category_ids):
    stage_id = stage["id"]
    for field in DEPENDENCY_FIELDS:
        values = stage.get(field, [])
        _require_string_list(
            values,
            "stage '{}' {}".format(stage_id, field),
            allow_empty=True,
        )
        unknown = sorted(set(values) - category_ids)
        if unknown:
            raise RegistryError(
                "stage '{}' {} reference unknown categories: {}".format(
                    stage_id, field, ", ".join(unknown)
                )
            )

    outputs = stage["outputs"]
    _require_string_list(outputs, "stage '{}' outputs".format(stage_id))
    unknown_outputs = sorted(set(outputs) - category_ids)
    if unknown_outputs:
        raise RegistryError(
            "stage '{}' outputs reference unknown categories: {}".format(
                stage_id, ", ".join(unknown_outputs)
            )
        )

    overlapping_dependencies = (
        set(stage.get("inputs", []))
        & set(stage.get("optional_inputs", []))
    ) | (
        set(stage.get("inputs", []))
        & set(stage.get("feedback_inputs", []))
    ) | (
        set(stage.get("optional_inputs", []))
        & set(stage.get("feedback_inputs", []))
    )
    if overlapping_dependencies:
        raise RegistryError(
            "stage '{}' assigns multiple dependency types to: {}".format(
                stage_id, ", ".join(sorted(overlapping_dependencies))
            )
        )


def _validate_stages(registry):
    stages = registry.get("stages")
    if not isinstance(stages, list) or not stages:
        raise RegistryError("stages must be a non-empty list")

    category_ids = set(registry["categories"])
    stage_ids = []
    producers = {}

    for index, stage in enumerate(stages):
        context = "stage at index {}".format(index)
        if not isinstance(stage, dict):
            raise RegistryError("{} must be an object".format(context))
        _require_keys(
            stage,
            (
                "id",
                "description",
                "command",
                "inputs",
                "outputs",
                "core",
                "network",
                "cost",
                "supports_partial",
            ),
            context,
        )
        stage_id = stage["id"]
        if not isinstance(stage_id, str) or not stage_id:
            raise RegistryError("{} id must be a string".format(context))
        stage_ids.append(stage_id)
        for field in (
            "description",
            "command",
            "cost",
        ):
            if not isinstance(stage[field], str) or not stage[field]:
                raise RegistryError(
                    "stage '{}' {} must be a non-empty string".format(
                        stage_id, field
                    )
                )
        for field in ("core", "network", "supports_partial"):
            if not isinstance(stage[field], bool):
                raise RegistryError(
                    "stage '{}' {} must be a boolean".format(stage_id, field)
                )

        _validate_stage_dependencies(stage, category_ids)

        for category_id in stage["outputs"]:
            if registry["categories"][category_id]["kind"] == "authored":
                raise RegistryError(
                    "stage '{}' cannot generate authored category '{}'".format(
                        stage_id, category_id
                    )
                )
            if category_id in producers:
                raise RegistryError(
                    "category '{}' has multiple producers: '{}' and '{}'".format(
                        category_id, producers[category_id], stage_id
                    )
                )
            producers[category_id] = stage_id

    duplicate_stage_ids = sorted(
        stage_id
        for stage_id, count in Counter(stage_ids).items()
        if count > 1
    )
    if duplicate_stage_ids:
        raise RegistryError(
            "duplicate stage id(s): {}".format(
                ", ".join(duplicate_stage_ids)
            )
        )

    return producers


def _validate_consumers(registry):
    consumers = registry.get("consumers")
    if not isinstance(consumers, list) or not consumers:
        raise RegistryError("consumers must be a non-empty list")

    category_ids = set(registry["categories"])
    consumer_ids = []
    for index, consumer in enumerate(consumers):
        context = "consumer at index {}".format(index)
        if not isinstance(consumer, dict):
            raise RegistryError("{} must be an object".format(context))
        _require_keys(consumer, ("id", "description", "inputs"), context)
        consumer_ids.append(consumer["id"])
        _require_string_list(
            consumer["inputs"],
            "consumer '{}' inputs".format(consumer["id"]),
        )
        unknown = sorted(set(consumer["inputs"]) - category_ids)
        if unknown:
            raise RegistryError(
                "consumer '{}' references unknown categories: {}".format(
                    consumer["id"], ", ".join(unknown)
                )
            )

    duplicate_consumer_ids = sorted(
        consumer_id
        for consumer_id, count in Counter(consumer_ids).items()
        if count > 1
    )
    if duplicate_consumer_ids:
        raise RegistryError(
            "duplicate consumer id(s): {}".format(
                ", ".join(duplicate_consumer_ids)
            )
        )


def topological_stage_order(registry, core_only=True):
    """Return the strict stage order, excluding optional and feedback edges."""

    stages = [
        stage
        for stage in registry["stages"]
        if not core_only or stage["core"]
    ]
    stage_by_id = {stage["id"]: stage for stage in stages}
    stage_position = {
        stage["id"]: index for index, stage in enumerate(stages)
    }
    producers = {
        output: stage["id"]
        for stage in stages
        for output in stage["outputs"]
    }

    dependencies = {stage["id"]: set() for stage in stages}
    dependants = defaultdict(set)
    for stage in stages:
        for category_id in stage["inputs"]:
            producer_id = producers.get(category_id)
            if producer_id is None or producer_id == stage["id"]:
                continue
            dependencies[stage["id"]].add(producer_id)
            dependants[producer_id].add(stage["id"])

    ready = sorted(
        (
            stage_id
            for stage_id, required in dependencies.items()
            if not required
        ),
        key=stage_position.get,
    )
    ordered = []
    while ready:
        stage_id = ready.pop(0)
        ordered.append(stage_id)
        for dependant_id in sorted(
            dependants[stage_id], key=stage_position.get
        ):
            dependencies[dependant_id].discard(stage_id)
            if not dependencies[dependant_id] and dependant_id not in ready:
                ready.append(dependant_id)
                ready.sort(key=stage_position.get)

    if len(ordered) != len(stage_by_id):
        cyclic = sorted(
            stage_id
            for stage_id, required in dependencies.items()
            if required
        )
        raise RegistryError(
            "required dependency graph contains a cycle involving: {}".format(
                ", ".join(cyclic)
            )
        )
    return ordered


def validate_registry(registry):
    if not isinstance(registry, dict):
        raise RegistryError("registry root must be an object")
    _require_keys(
        registry,
        (
            "schema_version",
            "path_base",
            "description",
            "dependency_types",
            "categories",
            "stages",
            "consumers",
        ),
        "registry",
    )
    if registry["schema_version"] != 1:
        raise RegistryError(
            "unsupported schema_version {}".format(
                registry["schema_version"]
            )
        )
    _validate_categories(registry)
    _validate_stages(registry)
    _validate_consumers(registry)
    topological_stage_order(registry, core_only=False)


def validate_authored_paths(registry, analysis_directory):
    """Check that each manually maintained path currently matches a file."""

    missing_patterns = []
    for category_id, category in registry["categories"].items():
        if category["kind"] not in {"authored", "code"}:
            continue
        for pattern in category["paths"]:
            absolute_pattern = str(analysis_directory / pattern)
            if not glob.glob(absolute_pattern):
                missing_patterns.append(
                    "{}: {}".format(category_id, pattern)
                )
    if missing_patterns:
        raise RegistryError(
            "authored path patterns with no matches:\n  {}".format(
                "\n  ".join(missing_patterns)
            )
        )


def print_summary(registry):
    category_counts = Counter(
        category["kind"] for category in registry["categories"].values()
    )
    print(
        "Registry valid: {} categories, {} stages, {} consumers".format(
            len(registry["categories"]),
            len(registry["stages"]),
            len(registry["consumers"]),
        )
    )
    print(
        "Categories: {}".format(
            ", ".join(
                "{} {}".format(kind, category_counts[kind])
                for kind in sorted(category_counts)
            )
        )
    )
    print("Strict core regeneration order:")
    stage_by_id = {stage["id"]: stage for stage in registry["stages"]}
    for index, stage_id in enumerate(
        topological_stage_order(registry, core_only=True), start=1
    ):
        stage = stage_by_id[stage_id]
        print(
            "  {}. {} [{}]".format(index, stage_id, stage["cost"])
        )

    feedback_stages = [
        stage
        for stage in registry["stages"]
        if stage.get("feedback_inputs")
    ]
    if feedback_stages:
        print("Feedback dependencies excluded from that order:")
        for stage in feedback_stages:
            print(
                "  {} <- {}".format(
                    stage["id"], ", ".join(stage["feedback_inputs"])
                )
            )


def parse_args():
    parser = argparse.ArgumentParser(
        description="Validate and inspect analysis/pipeline_registry.json"
    )
    parser.add_argument(
        "--registry",
        type=Path,
        default=REGISTRY_PATH,
        help="Path to the registry JSON file.",
    )
    parser.add_argument(
        "--check-paths",
        action="store_true",
        help="Also require every authored path pattern to match.",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    registry = load_registry(args.registry)
    validate_registry(registry)
    if args.check_paths:
        validate_authored_paths(registry, args.registry.resolve().parent)
    print_summary(registry)


if __name__ == "__main__":
    main()
