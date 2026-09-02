"""Select generated work by reachability from durable C++ inputs.

The audit records every retained generated unit. This module supplies the
smaller operational view used by repository-wide planning and archive
construction: start at generated inputs consumed by C++, then follow the
recorded dependency edges to their required ancestors.

Main functions:
* ``cpp_input_categories`` identifies durable generated C++ input roots.
* ``dependency_closure`` follows audited work-unit dependency identifiers.
* ``classify_required_work`` separates historical/active work from units used
  only by inactive future elections and from unreferenced retained outputs.
"""

from dataclasses import dataclass

import election_catalogue


CPP_CONSUMERS = {"cpp_stan_model", "cpp_seat_simulation", "cpp_live_analysis"}
GENERATED_KINDS = {"generated", "cache"}


class RequiredWorkError(ValueError):
    """Raised when required-work selection cannot be performed safely."""


@dataclass(frozen=True)
class RequiredWorkSelection:
    required_ids: frozenset
    historical_ids: frozenset
    active_ids: frozenset
    inactive_only_ids: frozenset
    unreferenced_ids: frozenset
    selected_elections: tuple
    inactive_elections: tuple

    def as_dict(self):
        return {
            "required_work_unit_ids": sorted(self.required_ids),
            "historical_work_unit_ids": sorted(self.historical_ids),
            "active_work_unit_ids": sorted(self.active_ids),
            "inactive_only_work_unit_ids": sorted(self.inactive_only_ids),
            "unreferenced_work_unit_ids": sorted(self.unreferenced_ids),
            "selected_elections": list(self.selected_elections),
            "inactive_elections": list(self.inactive_elections),
            "suppressed": {
                "inactive_only_count": len(self.inactive_only_ids),
                "unreferenced_count": len(self.unreferenced_ids),
            },
        }


def cpp_input_categories(registry):
    """Return generated/cache categories directly consumed by C++."""

    categories = registry.get("categories", {})
    produced_categories = {
        category_id
        for stage in registry.get("stages", [])
        for category_id in stage.get("outputs", [])
    }
    return {
        category_id
        for consumer in registry.get("consumers", [])
        if consumer.get("id") in CPP_CONSUMERS
        for category_id in consumer.get("inputs", [])
        if (
            categories.get(category_id, {}).get("kind") in GENERATED_KINDS
            or (category_id not in categories and category_id in produced_categories)
        )
    }


def dependency_closure(root_ids, work_units):
    """Return roots and all audited generated ancestors they reference."""

    units_by_id = {unit["id"]: unit for unit in work_units}
    selected = set()
    pending = list(root_ids)
    while pending:
        work_unit_id = pending.pop()
        if work_unit_id in selected or work_unit_id not in units_by_id:
            continue
        selected.add(work_unit_id)
        pending.extend(units_by_id[work_unit_id].get("dependencies", []))
    return frozenset(selected)


def _scope_elections(work_unit):
    return {
        str(election).casefold()
        for election in work_unit.get("scope", {}).get("elections", [])
    }


def _is_global_root(work_unit):
    scope = work_unit.get("scope", {})
    elections = _scope_elections(work_unit)
    # The 0none trend-adjustment bundle is a C++ file-existence fallback, not
    # an input consumed when a complete election-specific bundle is present.
    # Keep it scoped so normal required-work selection prefers regenerating
    # the named target rather than treating the fallback as universally used.
    return bool(scope.get("all")) or not elections


def _roots_for_elections(work_units, root_categories, elections):
    elections = set(elections)
    return {
        work_unit["id"]
        for work_unit in work_units
        if work_unit.get("category") in root_categories
        and (
            _is_global_root(work_unit)
            or bool(_scope_elections(work_unit) & elections)
        )
    }


def classify_required_work(
    work_units,
    registry,
    *,
    explicit_elections=None,
    historical_elections=None,
    active_elections=None,
    inactive_elections=None,
):
    """Classify audited work according to operational graph reachability."""

    try:
        historical = set(
            election_catalogue.historical_elections()
            if historical_elections is None
            else historical_elections
        )
        active = set(
            election_catalogue.active_future_elections()
            if active_elections is None
            else active_elections
        )
        inactive = set(
            election_catalogue.inactive_future_elections()
            if inactive_elections is None
            else inactive_elections
        )
    except election_catalogue.ElectionCatalogueError as error:
        raise RequiredWorkError(str(error)) from error

    root_categories = cpp_input_categories(registry)
    historical_ids = dependency_closure(
        _roots_for_elections(work_units, root_categories, historical),
        work_units,
    )
    active_ids = dependency_closure(
        _roots_for_elections(work_units, root_categories, active),
        work_units,
    )
    inactive_ids = dependency_closure(
        _roots_for_elections(work_units, root_categories, inactive),
        work_units,
    )
    normal_required = historical_ids | active_ids
    inactive_only = inactive_ids - normal_required

    if explicit_elections:
        selected_elections = {
            str(election).strip().casefold()
            for election in explicit_elections
        }
        required = dependency_closure(
            _roots_for_elections(
                work_units, root_categories, selected_elections
            ),
            work_units,
        )
    else:
        selected_elections = historical | active
        required = normal_required

    all_ids = {work_unit["id"] for work_unit in work_units}
    unreferenced = all_ids - normal_required - inactive_only
    inactive_with_roots = {
        election
        for election in inactive
        if _roots_for_elections(work_units, root_categories, {election})
    }
    return RequiredWorkSelection(
        required_ids=frozenset(required),
        historical_ids=historical_ids,
        active_ids=active_ids,
        inactive_only_ids=frozenset(inactive_only),
        unreferenced_ids=frozenset(unreferenced),
        selected_elections=tuple(sorted(selected_elections)),
        inactive_elections=tuple(sorted(inactive_with_roots)),
    )
