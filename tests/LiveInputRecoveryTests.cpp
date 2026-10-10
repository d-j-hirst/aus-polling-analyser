#include "../LiveInputRecovery.h"
#include "../RetainedLiveState.h"
#ifdef LIVE_INPUT_PARSER_TESTS
#include "../LiveInputAdapter.h"
#endif
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace LiveInputRecovery;
using json = nlohmann::json;

Snapshot example(std::string time = "2026-03-30T08:00:00") {
    Snapshot source;
    source.election = "fictional"; source.sourceTime = time; source.sourceHash = time;
    source.districts = {{"North",{1000,false}},{"South",{1000,false}}};
    for (auto const& district : {"North","South"}) {
        Record booth;
        booth.key = std::string(district)+"/ordinary/School";
        booth.district = district; booth.name = "School"; booth.role = "ordinary";
        booth.identity = std::string(district)+"/School/candidates";
        booth.candidates = {11,12}; booth.expected = 400;
        booth.fp.votes = Counts{{11,180},{12,120}};
        booth.tcp.votes = Counts{{11,190},{12,110}};
        source.records.push_back(booth);
        booth.key = std::string(district)+"/declaration/Postal";
        booth.name = "Postal"; booth.role = "declaration"; booth.identity += "/Postal";
        booth.fp.votes = Counts{{11,50},{12,50}};
        booth.tcp.votes = Counts{{11,30},{12,20}};
        source.records.push_back(booth);
    }
    return source;
}

void checkLocalRecovery() {
    auto old = recover(example(),{}).snapshot;
    auto now = example("2026-03-30T09:00:00");
    now.records[0].fp.votes = Counts{{11,-1},{12,120}};
    now.records[1].tcp.votes = Counts{{11,600},{12,500}};
    now.records[2].fp.votes = Counts{{11,210},{12,120}};
    auto result = recover(now,old);
    assert(result.snapshot.records[0].fp.status == Status::Restored);
    assert(result.snapshot.records[0].fp.observedAt == old.sourceTime);
    assert(!result.snapshot.records[0].fp.fresh());
    assert(total(result.snapshot.records[0].fp.votes) == 300);
    assert(total(result.snapshot.records[1].tcp.votes) == 50);
    assert(total(result.snapshot.records[2].fp.votes) == 330);
    assert(result.snapshot.records[0].tcp.status == Status::Current);
    now.records[0].identity += "changed";
    assert(!recover(now,old).snapshot.records[0].fp.votes);
    assert(!recover(now,{}).snapshot.records[0].fp.votes);
    now.records[0].identity = old.records[0].identity;
    auto changedCandidates = old; changedCandidates.records[0].candidates = {11,13};
    assert(!recover(now,changedCandidates).snapshot.records[0].fp.votes);
    assert(!recover(now,now).snapshot.records[0].fp.votes); // Same source is not earlier.
    auto future = old; future.sourceTime = "2026-03-30T10:00:00";
    assert(!recover(now,future).snapshot.records[0].fp.votes);
    now.records[0].fp.votes = Counts{{11,0},{12,0}};
    now.records[0].tcp.votes = Counts{{11,0},{12,0}};
    assert(recover(now,old).snapshot.records[0].fp.fresh());
    now.records[0].closed = true;
    auto closed = recover(now,old).snapshot;
    assert(closed.records[0].closed && total(closed.records[0].fp.votes) == 0);
    now.records[0].fp.votes = Counts{{11,10},{12,20}};
    assert(!recover(now,{}).snapshot.records[0].fp.votes);
    now.records[0].closed = false;
    now.records[0].fp.votes = Counts{{11,180}};
    assert(!recover(now,{}).snapshot.records[0].fp.votes); // Missing candidate is not a measured zero.
    assert(total(recover(now,old).snapshot.records[0].fp.votes) == 300);
}

void checkDistrictRecoveryAndProcedures() {
    auto old = recover(example(),{}).snapshot;
    old.districts["North"].finalised = true;
    auto now = example("2026-03-30T09:00:00");
    now.records[1].fp.votes = Counts{{11,450},{12,450}};
    now.records[2].fp.votes = Counts{{11,200},{12,110}};
    auto result = recover(now,old);
    assert(total(result.snapshot.records[0].fp.votes) == 300);
    assert(total(result.snapshot.records[1].fp.votes) == 100);
    assert(result.snapshot.districts["North"].finalised);
    assert(total(result.snapshot.records[2].fp.votes) == 310);
    assert(!recover(now,{}).snapshot.records[0].fp.votes);
    assert(!recover(now,{}).snapshot.districts["North"].finalised);
    auto diagnostic = result.issues.summary();
    assert(diagnostic.at(0).at("affected_records") == 2);
    assert(diagnostic.at(0).at("affected_districts") == 1);
    assert(diagnostic.at(0).at("impact_votes") == 200);
    now = example("2026-03-30T09:00:00");
    now.records[0].tcp.votes = Counts{{11,100},{12,100}};
    assert(!recover(now,{}).snapshot.records[0].tcp.votes);
    assert(recover(now,{}).issues.summary().at(0).at("routine") == true);
    now.optionalPreferential = true;
    assert(total(recover(now,{}).snapshot.records[0].tcp.votes) == 200);
    now.optionalPreferential = false;
    now.records[0].tcp.votes = Counts{{11,200},{12,115}}; // exactly 5% excess
    assert(total(recover(now,{}).snapshot.records[0].tcp.votes) == 315);
    now.records[0].tcp.votes = Counts{{11,200},{12,116}};
    assert(!recover(now,{}).snapshot.records[0].tcp.votes);
    auto small = now;
    small.records[0].fp.votes = Counts{{11,4},{12,4}};
    small.records[0].tcp.votes = Counts{{11,10},{12,8}}; // 10 extra votes permitted.
    assert(total(recover(small,{}).snapshot.records[0].tcp.votes) == 18);
    // Partial declaration TCP is a legitimate batch; preferences may lag FP.
    assert(total(recover(now,{}).snapshot.records[1].tcp.votes) == 50);
    now.records[1].fp.votes.reset();
    assert(!recover(now,{}).snapshot.records[1].tcp.votes);
    now.districts["North"].finalised = true;
    now.records[0].fp.error = "Malformed integer";
    assert(!recover(now,old).snapshot.districts["North"].finalised);
    auto nearFinal = example("2026-03-30T10:00:00");
    nearFinal.records[0].fp.votes = Counts{{11,300},{12,300}};
    nearFinal.records[0].tcp.votes = Counts{{11,315},{12,315}};
    nearFinal.records[1].fp.votes = Counts{{11,200},{12,190}};
    nearFinal.records[1].tcp.votes = Counts{{11,210},{12,199}};
    auto pairRecovered = recover(nearFinal,old);
    assert(total(pairRecovered.snapshot.records[0].fp.votes) == 600);
    assert(total(pairRecovered.snapshot.records[1].fp.votes) == 390);
    assert(total(pairRecovered.snapshot.records[0].tcp.votes)+total(pairRecovered.snapshot.records[1].tcp.votes) < 1000);
}

bool hasIssue(Result const& result, std::string const& type) {
    auto rows = result.issues.summary();
    return std::any_of(rows.begin(),rows.end(),[&](auto const& row) { return row.at("type") == type; });
}

void checkReconciliationHold() {
    // Fictional declaration counts reproduce a reset followed by partial
    // rechecking. The old pair remains evidence, while unaffected booths and
    // districts continue using their new counts.
    auto first = example();
    first.records[1].tcp.votes = Counts{{11,60},{12,40}};
    auto old = recover(first,{}).snapshot;
    auto current = example("2026-03-30T09:00:00");
    current.records[1].fp.votes = Counts{{11,0},{12,0}};
    current.records[1].tcp.votes = Counts{{11,62},{12,40}};
    current.records[2].fp.votes = Counts{{11,200},{12,130}};
    current.records[2].tcp.votes = Counts{{11,210},{12,120}};
    current.districts["North"].finalised = true;
    auto held = recover(current,old);
    auto const& record = held.snapshot.records[1];
    assert(record.fp.votes == old.records[1].fp.votes && record.tcp.votes == old.records[1].tcp.votes);
    assert(record.fp.status == Status::Restored && record.tcp.status == Status::Restored);
    assert(record.fp.observedAt == first.sourceTime && record.tcp.observedAt == first.sourceTime);
    assert(!record.fp.fresh() && !record.tcp.fresh() && !record.fp.complete && !record.tcp.complete);
    assert(!held.snapshot.districts.at("North").finalised);
    assert(held.snapshot.records[0].fp.fresh());
    assert(total(held.snapshot.records[2].fp.votes) == 330 && held.snapshot.records[2].tcp.fresh());
    auto rows = held.issues.summary();
    auto warning = std::find_if(rows.begin(),rows.end(),[](auto const& row) { return row.at("type") == "reconciliation_hold"; });
    assert(warning != rows.end() && warning->at("affected_records") == 1 && warning->at("affected_districts") == 1);
    assert(warning->at("label") == "FP/TCP reconciliation hold" && warning->at("routine") == false);
    assert(warning->at("rejected_total") == 0 && warning->at("effective_total") == 100);

    // Restored FP must not manufacture local activity or pooled slowing
    // evidence. The unaffected South account remains a fresh measurement.
    TurnoutModel::Prior prior;
    prior.seats = {"North","South"}; prior.groups = {"other"};
    std::vector<TurnoutModel::Unit> units;
    RecordStatuses statuses;
    for (auto const& effective : held.snapshot.records) {
        TurnoutModel::Unit unit;
        unit.seat = effective.district == "North" ? 0 : 1;
        unit.kind = effective.role; unit.name = unit.category = effective.name;
        unit.counted = total(effective.fp.votes);
        units.push_back(unit);
        statuses[effective.key] = {effective.fp.metadata(),effective.tcp.metadata()};
    }
    auto observation = freshObservation(prior,units,statuses,current.sourceTime);
    assert(!observation.counts.count({"North","Postal"}));
    assert(!observation.counts.count({"North","allocation:other"}));
    assert(observation.counts.at({"South","allocation:other"}) == 430);

    current.sourceTime = "2026-03-30T10:00:00";
    current.records[1].fp.votes = Counts{{11,8},{12,5}};
    auto repeated = recover(current,held.snapshot);
    assert(total(repeated.snapshot.records[1].fp.votes) == 100);
    assert(repeated.snapshot.records[1].fp.observedAt == first.sourceTime);
    assert(repeated.snapshot.records[1].tcp.observedAt == first.sourceTime);

    // Reconciled lower counts are accepted on this first pass. Accepting the
    // coherent new account removes the hold instead of enforcing growth.
    current.sourceTime = "2026-03-30T11:00:00";
    current.districts["North"].finalised = false;
    current.records[1].fp.votes = Counts{{11,55},{12,38}};
    current.records[1].tcp.votes = Counts{{11,57},{12,37}};
    auto released = recover(current,repeated.snapshot);
    assert(total(released.snapshot.records[1].fp.votes) == 93 && total(released.snapshot.records[1].tcp.votes) == 94);
    assert(released.snapshot.records[1].fp.fresh() && released.snapshot.records[1].tcp.fresh());
    assert(!hasIssue(released,"reconciliation_hold"));

    // The same account-level recovery covers ordinary booths without changing
    // their existing completion rules. A rejected declaration of completion
    // cannot complete an unfinished held service.
    current = example("2026-03-30T09:00:00");
    current.records[0].fp.votes = Counts{{11,100},{12,80}};
    auto booth = recover(current,old);
    assert(total(booth.snapshot.records[0].fp.votes) == 300);
    assert(booth.snapshot.records[0].fp.complete && booth.snapshot.records[0].tcp.complete);
    assert(hasIssue(booth,"reconciliation_hold"));

    // Normal additions ahead of preferences, permitted differences, genuine
    // zeros and coherent downward corrections still pass through normally.
    current = example("2026-03-30T09:00:00");
    current.records[1].fp.votes = Counts{{11,70},{12,50}};
    current.records[1].tcp.votes = first.records[1].tcp.votes;
    assert(recover(current,old).snapshot.records[1].fp.fresh());
    current.records[1].fp.votes = Counts{{11,50},{12,40}};
    assert(!hasIssue(recover(current,old),"reconciliation_hold")); // Exactly 10 excess votes.
    current.records[1].fp.votes = Counts{{11,50},{12,39}};
    assert(hasIssue(recover(current,old),"reconciliation_hold"));
    current.records[1].fp.votes = Counts{{11,50},{12,30}};
    current.records[1].tcp.votes = Counts{{11,30},{12,20}};
    assert(recover(current,old).snapshot.records[1].fp.fresh()); // Valid partial declarations.
    current.records[1].fp.votes = current.records[1].tcp.votes = Counts{{11,0},{12,0}};
    assert(recover(current,old).snapshot.records[1].fp.fresh());

    // TCP overcounts without an FP reduction remain ordinary error recovery;
    // malformed records, incompatible identities and absent history likewise
    // cannot establish a recount hold. OPV keeps its existing treatment.
    current.records[1].fp.votes = first.records[1].fp.votes;
    current.records[1].tcp.votes = Counts{{11,600},{12,400}};
    auto overcount = recover(current,old);
    assert(overcount.snapshot.records[1].fp.fresh() && !hasIssue(overcount,"reconciliation_hold"));
    current.records[1].fp.votes = Counts{{11,0},{12,0}};
    current.records[1].tcp.votes = first.records[1].tcp.votes;
    assert(recover(current,{}).snapshot.records[1].fp.fresh());
    auto incompatible = old; incompatible.records[1].identity += "/changed";
    assert(recover(current,incompatible).snapshot.records[1].fp.fresh());
    incompatible = old; incompatible.records[1].candidates = {11,13};
    assert(recover(current,incompatible).snapshot.records[1].fp.fresh());
    incompatible = old; incompatible.records[1].tcp.votes.reset();
    assert(recover(current,incompatible).snapshot.records[1].fp.fresh());
    current.sourceTime = old.sourceTime;
    assert(recover(current,old).snapshot.records[1].fp.fresh()); // Strictly earlier observations only.
    current.sourceTime = "2026-03-30T07:00:00";
    assert(recover(current,old).snapshot.records[1].fp.fresh());
    current.sourceTime = "2026-03-30T09:00:00";
    current.records[1].tcp.error = "Malformed preference count";
    assert(recover(current,old).snapshot.records[1].fp.fresh());
    current.records[1].tcp.error.clear();
    current.records[1].fp.error = "Malformed first-preference count";
    assert(!hasIssue(recover(current,old),"reconciliation_hold"));
    current.records[1].fp.error.clear(); current.optionalPreferential = true;
    assert(!hasIssue(recover(current,old),"reconciliation_hold"));
}

void checkRegistry() {
    Registry registry;
    Issue issue{"bad","b","North","Second","Contradiction","Inspect source","",50,false,{}, {}};
    registry.add(issue); registry.add(issue);
    issue.key = "a"; issue.name = "First"; registry.add(issue);
    issue.impact = 100; registry.add(issue);
    auto summary = registry.summary().at(0);
    assert(summary.at("affected_records") == 2 && summary.at("affected_districts") == 1);
    assert(summary.at("record") == "First" && summary.at("impact_votes") == 100);
    assert(registry.details().size() == 2);
}

void checkFailedUpdateTransaction(std::filesystem::path const& folder) {
    // The same rollback helper guards the application's report and outcomes.
    // Inject a failure after counts have been prepared but before any commit:
    // recovery/history/export side effects must not escape that attempt.
    auto old = recover(example(),{}).snapshot;
    json report = {{"forecast","previous"}}, outcomes = {{"North",400}};
    auto originalReport = report, originalOutcomes = outcomes;
    auto countFile = folder/"transaction-history";
    auto exportFile = folder/"transaction-export.json";
    bool failed = false;
    try {
        RetainedLiveState reportTransaction(report);
        RetainedLiveState outcomeTransaction(outcomes);
        auto current = example("2026-03-30T12:00:00");
        auto prepared = recover(current,old);
        outcomes["North"] = 500; report["forecast"] = "workspace";
        current.sourceTime.clear(); recover(current,old); // Unusable input.
        commit(countFile,prepared.snapshot);
        std::ofstream stream(exportFile); stream << report;
        reportTransaction.commit(); outcomeTransaction.commit();
    } catch (std::runtime_error const&) { failed = true; }
    assert(failed && report == originalReport && outcomes == originalOutcomes);
    assert(!std::filesystem::exists(countFile) && !std::filesystem::exists(exportFile));
}

void checkFreshProgressEvidence() {
    auto old = recover(example(),{}).snapshot;
    TurnoutModel::Prior prior;
    prior.election = "fictional"; prior.seats = {"North","South"}; prior.groups = {"other"};
    prior.subdivisions = {"fictional","fictional"};
    prior.enrolment = {1000,1000}; prior.totals = {{500,500},{550,550}}; prior.counts = prior.totals;
    auto observe = [&](Snapshot const& snapshot) {
        std::vector<TurnoutModel::Unit> units;
        RecordStatuses statuses;
        for (auto const& record : snapshot.records) {
            TurnoutModel::Unit unit;
            unit.seat = record.district == "North" ? 0 : 1;
            unit.name = record.name; unit.category = record.name; unit.kind = record.role;
            unit.counted = total(record.fp.votes); unit.closed = record.closed;
            unit.acceptedComplete = record.fp.complete;
            unit.weight = record.role == "ordinary" ? .75 : .25;
            units.push_back(unit); statuses[record.key] = {record.fp.metadata(),record.tcp.metadata()};
        }
        auto observation = freshObservation(prior,units,statuses,snapshot.sourceTime);
        return std::make_pair(units,observation);
    };
    auto before = observe(old);
    auto current = example("2026-03-30T09:00:00");
    current.records[0].fp.error = current.records[1].fp.error = "Malformed count";
    auto after = observe(recover(current,old).snapshot);
    assert(!after.second.counts.count({"North","Postal"}));
    assert(!after.second.counts.count({"North","allocation:other"}));
    assert(after.second.counts.at({"South","allocation:other"}) == 400);
    auto evidence = TurnoutModel::progress(prior,after.first,{before.second,after.second});
    assert(evidence[0].strength == 0 && evidence[1].strength == 0);
    current = example("2026-03-30T09:00:00");
    current.records[1].closed = true;
    current.records[1].fp.votes = current.records[1].tcp.votes = Counts{{11,0},{12,0}};
    auto closed = observe(recover(current,old).snapshot);
    assert(!closed.second.counts.count({"North","Postal"}));
    assert(closed.second.counts.at({"North","allocation:other"}) == 300);
    // A previously validated completed declaration account stays completed
    // when its malformed replacement is restored. Merely unavailable counts
    // still leave an open service and cannot create false completion.
    auto final = example(); final.districts["North"].finalised = true;
    auto accepted = recover(final,{}).snapshot;
    current = example("2026-03-30T09:00:00");
    current.records[1].fp.error = "Malformed count";
    auto restored = observe(recover(current,accepted).snapshot);
    // Progress above deliberately uses a combined group. The count model's
    // subset conditioning needs separate groups, each strictly within total FP.
    auto countPrior = prior;
    countPrior.groups = {"booths","other"};
    countPrior.counts = {{375,125,375,125},{412.5,137.5,412.5,137.5}};
    auto countUnits = [](std::vector<TurnoutModel::Unit> units) {
        for (auto& unit : units) { unit.group = unit.kind == "ordinary" ? 0 : 1; unit.weight = 1; }
        return units;
    };
    auto completed = TurnoutModel::update(countPrior,countUnits(restored.first),{false,false});
    assert(completed.complete[1]);
    assert(completed.remaining[0][1] == 0);
    auto absent = observe(recover(current,{}).snapshot);
    auto unfinished = TurnoutModel::update(countPrior,countUnits(absent.first),{false,false});
    assert(!unfinished.complete[1]);
}

void checkHistory(std::filesystem::path const& folder) {
    std::filesystem::create_directories(folder);
    auto first = recover(example(),{}).snapshot;
    commit(folder,first);
    auto current = example("2026-03-30T09:00:00");
    auto earlier = loadCompatiblePrevious(folder,current);
    assert(earlier && earlier->sourceTime == first.sourceTime);
    auto revision = first; revision.sourceHash = "revised"; revision.records[0].fp.votes = Counts{{11,190},{12,120}};
    commit(folder,revision);
    earlier = loadPrevious(folder,current.election,current.sourceTime);
    assert(earlier->sourceHash == "revised" && total(earlier->records[0].fp.votes) == 310);
    assert(!loadPrevious(folder,current.election,first.sourceTime));
    auto changed = current; changed.records[0].identity += "wrong identity";
    changed.records[1].fp.votes = Counts{{11,100},{12,100}};
    changed = recover(changed,{}).snapshot; commit(folder,changed);
    auto later = example("2026-03-30T10:00:00");
    earlier = loadCompatiblePrevious(folder,later);
    auto north = std::find_if(earlier->records.begin(),earlier->records.end(),[](auto const& record) { return record.key == "North/ordinary/School"; });
    assert(north != earlier->records.end() && total(north->fp.votes) == 310);
    auto local = later; local.records[1].fp.error = "Malformed postal record";
    auto localRecovered = recover(local,earlier);
    assert(total(localRecovered.snapshot.records[1].fp.votes) == 200);
    // Both nearest local records are compatible, but come from different
    // source accounts. Ambiguous district rollback must use the whole older
    // consistent 410-vote account rather than their mixed 510-vote sum.
    auto districtConflict = later;
    districtConflict.records[0].fp.votes = Counts{{11,300},{12,300}};
    districtConflict.records[1].fp.votes = Counts{{11,250},{12,250}};
    auto districtRecovered = recover(districtConflict,earlier);
    assert(total(districtRecovered.snapshot.records[0].fp.votes) == 310);
    assert(total(districtRecovered.snapshot.records[1].fp.votes) == 100);
    // Rejected attempts never invoke commit. Simulate a long series without
    // retaining parsed history documents, checking one effective state each time.
    std::size_t before = std::distance(std::filesystem::directory_iterator(folder),std::filesystem::directory_iterator{});
    auto invalid = later; invalid.sourceTime.clear();
    bool failed = false;
    try { recover(invalid,earlier); } catch (std::runtime_error const&) { failed = true; }
    assert(failed);
    assert(std::size_t(std::distance(std::filesystem::directory_iterator(folder),std::filesystem::directory_iterator{})) == before);
    for (int i = 0; i < 150; ++i) {
        auto state = example("2026-03-30T10:" + std::string(i/60 < 10 ? "0" : "") + std::to_string(i/60) + ":" + std::string(i%60 < 10 ? "0" : "") + std::to_string(i%60));
        auto result = recover(state,loadCompatiblePrevious(folder,state));
        commit(folder,result.snapshot);
        assert(result.snapshot.records.size() == 4 && result.issues.summary().size() == 1);
        assert(result.issues.summary().at(0).at("routine") == true);
    }
}

void checkOlderCompatibleCounts(std::filesystem::path const& folder) {
    auto first = recover(example(),{}).snapshot;
    commit(folder,first);
    auto large = example("2026-03-30T09:00:00");
    large.records[0].fp.votes = Counts{{11,360},{12,240}};
    large.records[0].tcp.votes = Counts{{11,390},{12,210}};
    commit(folder,recover(large,{}).snapshot);
    auto corrected = example("2026-03-30T10:00:00");
    corrected.records[0].tcp.error = "Malformed preference record";
    auto lookup = [&](Snapshot const& effective, Snapshot const* previous, HistoryAccount account) {
        return loadEarlierCounts(folder,effective,previous,account);
    };
    // The nearest TCP is 600, which cannot fit corrected FP of 300. Restore
    // the older 300-vote pair, rather than losing previously accepted evidence.
    auto result = recover(corrected,loadCompatiblePrevious(folder,corrected),lookup);
    assert(total(result.snapshot.records[0].tcp.votes) == 300);
    assert(result.snapshot.records[0].tcp.observedAt == first.sourceTime);
    large.records[0].tcp.votes.reset();
    commit(folder,recover(large,{}).snapshot);
    result = recover(corrected,loadCompatiblePrevious(folder,corrected),lookup);
    assert(total(result.snapshot.records[0].tcp.votes) == 300);
    // Unavailable FP in a later consistent partial district account also must
    // not erase an older compatible count when the next record is malformed.
    large.records[0].fp.votes.reset();
    commit(folder,recover(large,{}).snapshot);
    corrected.records[0].fp.error = "Malformed first preferences";
    result = recover(corrected,loadCompatiblePrevious(folder,corrected),lookup);
    assert(total(result.snapshot.records[0].fp.votes) == 300);
    assert(result.snapshot.records[0].fp.observedAt == first.sourceTime);

    auto fractional = example("2026-03-30T11:00:00.10");
    commit(folder,recover(fractional,{}).snapshot);
    assert(!loadPrevious(folder,"fictional","2026-03-30T08:00:00.0"));
    auto earlier = loadPrevious(folder,"fictional","2026-03-30T11:00:00.100");
    assert(earlier && earlier->sourceTime == "2026-03-30T09:00:00");
    earlier = loadPrevious(folder,"fictional","2026-03-30T11:00:00.2");
    assert(earlier && earlier->sourceTime == "2026-03-30T11:00:00.1");
    assert(!recover(fractional,earlier).snapshot.records[0].fp.observedAt.empty());
}

void checkReconciliationHistory(std::filesystem::path const& folder) {
    auto first = example(); first.records[1].tcp.votes = Counts{{11,60},{12,40}};
    auto accepted = recover(first,{}).snapshot;
    commit(folder,accepted);
    // A compatible nearest cache can lack usable preferences. Select an older
    // joint record, rather than combining independent FP and TCP fallbacks.
    auto missing = example("2026-03-30T09:00:00");
    missing.records[1].tcp.votes.reset();
    commit(folder,recover(missing,{}).snapshot);
    auto current = example("2026-03-30T10:00:00");
    current.records[1].fp.votes = Counts{{11,0},{12,0}};
    current.records[1].tcp.votes = first.records[1].tcp.votes;
    auto lookup = [&](Snapshot const& effective, Snapshot const* previous, HistoryAccount account) {
        return loadEarlierCounts(folder,effective,previous,account);
    };
    auto held = recover(current,loadCompatiblePrevious(folder,current),lookup);
    assert(total(held.snapshot.records[1].fp.votes) == 100 && total(held.snapshot.records[1].tcp.votes) == 100);
    assert(held.snapshot.records[1].fp.observedAt == first.sourceTime);
    assert(held.snapshot.records[1].tcp.observedAt == first.sourceTime);
    commit(folder,held.snapshot);

    auto future = first; future.sourceTime = "2026-03-30T12:00:00";
    future.records[1].fp.votes = future.records[1].tcp.votes = Counts{{11,150},{12,100}};
    commit(folder,recover(future,{}).snapshot);
    current.sourceTime = "2026-03-30T11:00:00";
    current.records[1].fp.votes = Counts{{11,8},{12,5}};
    auto repeated = recover(current,loadCompatiblePrevious(folder,current),lookup);
    assert(total(repeated.snapshot.records[1].fp.votes) == 100); // Future source cannot supply 250.
    assert(repeated.snapshot.records[1].fp.observedAt == first.sourceTime);

    // Revising a previously held source with a coherent lower pair replaces
    // that cache atomically. Subsequent holds use the revised accepted account.
    auto revision = example("2026-03-30T10:00:00"); revision.sourceHash = "recount-reconciled";
    revision.records[1].fp.votes = revision.records[1].tcp.votes = Counts{{11,45},{12,35}};
    auto reconciled = recover(revision,loadCompatiblePrevious(folder,revision),lookup);
    assert(reconciled.snapshot.records[1].fp.fresh() && !hasIssue(reconciled,"reconciliation_hold"));
    commit(folder,reconciled.snapshot);
    repeated = recover(current,loadCompatiblePrevious(folder,current),lookup);
    assert(total(repeated.snapshot.records[1].fp.votes) == 80 && total(repeated.snapshot.records[1].tcp.votes) == 80);
    assert(repeated.snapshot.records[1].fp.observedAt == revision.sourceTime);

    // Once lower FP has been accepted, an unrelated TCP error cannot bring
    // back the superseded larger account through a search of older caches.
    current.records[1].fp.votes = revision.records[1].fp.votes;
    auto stableFp = recover(current,loadCompatiblePrevious(folder,current),lookup);
    assert(total(stableFp.snapshot.records[1].fp.votes) == 80 && stableFp.snapshot.records[1].fp.fresh());
    assert(!hasIssue(stableFp,"reconciliation_hold"));
}

#ifdef LIVE_INPUT_PARSER_TESTS
void checkParserAndAdapter() {
    Results2::Election election("fictional");
    election.sourceTime = "2026-03-30T08:00:00";
    election.candidates.emplace(11,Results2::Candidate{11,"One",1});
    election.candidates.emplace(12,Results2::Candidate{12,"Two",2});
    Results2::Seat seat; seat.id = 1; seat.name = "North"; seat.enrolment = 1000;
    seat.fpVotes[11][Results2::VoteType::Postal] = 50;
    seat.fpVotes[12][Results2::VoteType::Postal] = 50;
    seat.tcpVotesCandidate[11][Results2::VoteType::Postal] = 30;
    seat.tcpVotesCandidate[12][Results2::VoteType::Postal] = 20;
    Results2::Booth booth; booth.id = 7; booth.parentSeat = 1; booth.name = "School";
    booth.fpVotes = {{11,180},{12,120}}; booth.tcpVotesCandidate = {{11,190},{12,110}};
    election.booths.emplace(7,booth); seat.booths.push_back(7); election.seats.emplace(1,seat);
    TurnoutModelIO::Artifact artifact;
    artifact.prior.election = "fictional"; artifact.prior.seats = {"North"}; artifact.prior.groups = {"other"};
    artifact.prior.enrolment = {1000}; artifact.prior.counts = {{600}};
    TurnoutModel::Unit unit; unit.name = "School"; unit.kind = "ordinary"; unit.weight = .5;
    artifact.prior.units.push_back(unit); unit.name = "Postal"; unit.kind = "declaration"; artifact.prior.units.push_back(unit);
    auto source = LiveInputAdapter::identify(election,artifact,"source");
    auto old = recover(source,{}).snapshot;
    source.sourceTime = "2026-03-30T09:00:00"; source.records[0].fp.error = "Malformed";
    auto recovered = recover(source,old);
    LiveInputAdapter::apply(election,recovered.snapshot);
    assert(election.seats.at(1).totalVotesFp() == 400);
    assert(election.booths.at(7).totalVotesFp() == 300);
    assert(!election.liveCountStatus.at("North/ordinary/School").first.fresh());
    assert(election.liveCountStatus.at("North/ordinary/School").first.status == Status::Restored);
    assert(election.liveCountStatus.at("North/ordinary/School").first.observedAt == old.sourceTime);
    assert(election.liveCountStatus.at("North/declaration/Postal").first.fresh());
    // Identifiable count errors are ordinary recovery status, not exceptions.
    tinyxml2::XMLDocument malformed;
    assert(malformed.Parse("<x><v>-20</v></x>") == tinyxml2::XML_SUCCESS);
    // A live parser fixture uses a minimal ECSA source to exercise the public
    // update entry point, including a malformed count and missing structure.
    Results2::Election parser("fictional"); parser.candidates = election.candidates;
    parser.seats = election.seats; parser.booths = election.booths;
    char const* xml = "<HouseOfAssemblyDetail><last_updated>2026-03-30 09:00:00</last_updated><districts><district><district_id>1</district_id><first_preferences><candidate><candidate_id>11</candidate_id><polling_places><polling_place><polling_place_id>7</polling_place_id><polling_place_name>School</polling_place_name><ballot_papers>broken</ballot_papers></polling_place></polling_places></candidate></first_preferences></district></districts></HouseOfAssemblyDetail>";
    // The exact commission structure is exercised by private replay below;
    // required structure failures always remain failures, never synthetic zeros.
    tinyxml2::XMLDocument broken; broken.Parse(xml);
    parser.updateLive(broken,Results2::Election::Format::ECSA);
    assert(!parser.invalidLiveCounts.empty());
    auto parsed = LiveInputAdapter::identify(parser,artifact,"broken-source");
    assert(!parsed.records[0].fp.error.empty() && !parsed.records[0].fp.votes);
    auto repaired = recover(parsed,old);
    assert(total(repaired.snapshot.records[0].fp.votes) == 300);
    assert(repaired.snapshot.records[0].fp.status == Status::Restored);
    bool rejected = false;
    try { parser.updateLive(broken,Results2::Election::Format::AEC); }
    catch (std::runtime_error const&) { rejected = true; }
    assert(rejected);

    // QEC reports ordinary booth candidate counts under named count rounds.
    // Candidate-level TCP must survive this adapter, and a repeat read must
    // replace the measured account rather than adding the same votes twice.
    Results2::Election qec("fictional");
    qec.candidates = election.candidates;
    auto qecSeat = seat; qecSeat.booths = {100007};
    auto qecBooth = booth; qecBooth.id = 100007;
    qec.seats.emplace(1,qecSeat); qec.booths.emplace(100007,qecBooth);
    qec.candidateNameToId[std::string("1\x1f")+"One"] = 11;
    qec.candidateNameToId[std::string("1\x1f")+"Two"] = 12;
    tinyxml2::XMLDocument qecFeed;
    qecFeed.Parse("<ecq><generationDateTime>2026-03-30T09:00:00</generationDateTime><election id='99'><districts><district number='1' enrolment='1000'><countRound countName='Unofficial Preliminary Count'><booths><booth id='7'><primaryVoteResults><candidate ballotName='One'><count>180</count></candidate><candidate ballotName='Two'><count>120</count></candidate></primaryVoteResults></booth></booths></countRound><countRound countName='Unofficial Indicative Count'><booths><booth id='7'><twoCandidateVotes><candidate ballotName='One'><count>190</count></candidate><candidate ballotName='Two'><count>110</count></candidate></twoCandidateVotes></booth></booths></countRound></district></districts></election></ecq>");
    auto qecArtifact = artifact; qecArtifact.prior.units.resize(1);
    for (int repeat = 0; repeat < 2; ++repeat) {
        qec.updateLive(qecFeed,Results2::Election::Format::QEC);
        auto measured = LiveInputAdapter::identify(qec,qecArtifact,"qec-fixture");
        assert(total(measured.records[0].fp.votes) == 300);
        assert(total(measured.records[0].tcp.votes) == 300);
        LiveInputAdapter::apply(qec,recover(measured,{}).snapshot);
        assert(qec.seats.at(1).totalVotesFp() == 300);
    }
    tinyxml2::XMLDocument emptySource;
    rejected = false;
    try { qec.updateLive(emptySource,Results2::Election::Format::WAEC); }
    catch (std::runtime_error const&) { rejected = true; }
    assert(rejected);

    // A VEC candidate-only preload knows the contest before any count exists.
    // Those identities must not depend on inventing zero measurements, or the
    // first unreported districts would fail preparation instead of using priors.
    tinyxml2::XMLDocument vecCandidates, vecBooths;
    vecCandidates.Parse("<EML><CandidateList><EventIdentifier Id='99'/><Election><Contest><PollingDistrictIdentifier Id='1'/><ContestIdentifier><ContestName>North District</ContestName></ContestIdentifier><Enrolment>1000</Enrolment><Candidate><CandidateIdentifier Id='11'><CandidateName>One</CandidateName></CandidateIdentifier></Candidate><Candidate><CandidateIdentifier Id='12'><CandidateName>Two</CandidateName></CandidateIdentifier></Candidate></Contest></Election></CandidateList></EML>");
    vecBooths.Parse("<PollingDistrictList><PollingDistrict><PollingDistrictIdentifier Id='1'/><PollingPlaces><PollingPlace><PollingPlaceIdentifier Id='7' Name='School'/></PollingPlace></PollingPlaces></PollingDistrict></PollingDistrictList>");
    auto vec = Results2::Election::createVec(vecCandidates,vecBooths,"fictional");
    vec.sourceTime = "2026-03-30T09:00:00";
    assert(vec.liveCandidateRosters.at(1).size() == 2);
    auto unreported = LiveInputAdapter::identify(vec,artifact,"vec-preload");
    assert(unreported.records[0].candidates == std::vector<int>({11,12}));
    assert(!unreported.records[0].fp.votes);
}
#endif
}

int main(int argc, char** argv) {
    auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
    auto folder = root/"cli-build"/"input-recovery-tests";
    std::filesystem::remove_all(folder);
    checkLocalRecovery(); checkDistrictRecoveryAndProcedures(); checkRegistry();
    checkReconciliationHold();
    checkFreshProgressEvidence();
    checkHistory(folder); checkFailedUpdateTransaction(folder);
    checkOlderCompatibleCounts(folder/"older-compatible");
    checkReconciliationHistory(folder/"reconciliation");
#ifdef LIVE_INPUT_PARSER_TESTS
    checkParserAndAdapter();
#endif
    std::filesystem::remove_all(folder);
    std::cout << "Live input recovery checks passed.\n";
}
