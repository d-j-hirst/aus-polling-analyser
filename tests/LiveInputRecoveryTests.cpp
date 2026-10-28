#include "../LiveInputRecovery.h"
#include "../RetainedLiveState.h"
#include "../LivePartialCount.h"
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
    // A mixed early/day venue may first report a small day batch. Its positive
    // FP total must not turn into completion until the district is finalised.
    auto mixed = example(); mixed.records[0].incremental = true;
    assert(!recover(mixed,{}).snapshot.records[0].fp.complete);
    mixed.districts.at("North").finalised = true;
    assert(recover(mixed,{}).snapshot.records[0].fp.complete);
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

void checkExpectedTcpWithdrawal() {
    // Ordinary feeds still warn when a previously reported vector disappears.
    // Only a reader's current, explicit ECQ-recheck tag silences that notice;
    // the recovered values, original clock and compatibility checks stay intact.
    auto old = recover(example(),{}).snapshot;
    auto now = example("2026-03-30T09:00:00");
    auto& record = now.records.front();
    record.tcp.votes.reset();
    auto hasWarning = [&](Result const& result) {
        for (auto const& issue : result.issues.details())
            if (issue.at("key") == record.key && issue.at("type") == "invalid_tcp") return true;
        return false;
    };
    assert(hasWarning(recover(now,old)));
    record.expectedTcpWithdrawal = true;
    auto result = recover(now,old);
    assert(!hasWarning(result));
    assert(result.snapshot.records.front().tcp.votes == old.records.front().tcp.votes);
    assert(result.snapshot.records.front().tcp.status == Status::Restored);
    assert(result.snapshot.records.front().tcp.observedAt == old.sourceTime);
    record.tcp.error = "Malformed TCP";
    assert(hasWarning(recover(now,old))); // Present/malformed data is still exceptional.
    record.tcp.error.clear();
    record.fp.votes = Counts{{11,280},{12,20}};
    result = recover(now,old);
    assert(!result.snapshot.records.front().tcp.votes); // Cannot silence a count into compatibility.
}

void checkPreferenceHistory(std::filesystem::path const& folder) {
    // Received count history starts with a batch's FP. A later TCP describes
    // that batch after additional primaries have already appeared. Same-time
    // revisions and future feeds cannot help identify it in a live forecast.
    auto first = example("2026-03-30T08:00:00");
    for (auto& service : first.records)
        for (auto count : {&service.fp,&service.tcp}) count->observedAt = first.sourceTime;
    auto& record = first.records.front();
    record.candidates = {11,12,13};
    record.fp = {Counts{{11,100},{12,80},{13,20}},Status::Current,first.sourceTime};
    record.tcp = {};
    commit(folder,first);
    auto current = first; current.sourceTime = "2026-03-30T09:00:00";
    current.records.front().fp = {Counts{{11,150},{12,160},{13,40}},Status::Current,current.sourceTime};
    current.records.front().tcp = {Counts{{11,116},{12,84}},Status::Current,current.sourceTime};
    assert(!loadPreferenceHistory(folder/"empty",current).at(record.key).matchingCurrentTcp);
    auto selected = loadPreferenceHistory(folder,current).at(record.key);
    assert(selected.matchingCurrentTcp && !selected.previousAligned);
    assert(selected.matchingCurrentTcp->fp.at(11) == 100);
    assert(selected.matchingCurrentTcp->fpTime == first.sourceTime);
    auto changed = current; changed.records.front().identity += "/replacement";
    assert(!loadPreferenceHistory(folder,changed).at(record.key).matchingCurrentTcp);
    changed = current; changed.records.front().fp.votes->at(11) = 99;
    assert(!loadPreferenceHistory(folder,changed).at(record.key).matchingCurrentTcp);
    changed = current; changed.optionalPreferential = true;
    assert(loadPreferenceHistory(folder,changed).empty());
    auto aligned = first; aligned.sourceTime = "2026-03-30T08:30:00";
    aligned.records.front().tcp = current.records.front().tcp;
    aligned.records.front().tcp.observedAt = aligned.sourceTime;
    commit(folder,aligned);
    selected = loadPreferenceHistory(folder,current).at(record.key);
    assert(selected.previousAligned && selected.previousAligned->tcpTime == aligned.sourceTime);
    auto future = current; future.sourceTime = "2026-03-30T10:00:00";
    future.records.front().fp.votes = Counts{{11,90},{12,90},{13,20}};
    commit(folder,future);
    retainPreferenceHistory(current,loadPreferenceHistory(folder,current));
    commit(folder,current);
    selected = loadPreferenceHistory(folder,current).at(record.key);
    assert(selected.matchingCurrentTcp->fp.at(11) == 100);
    // The 09:00 TCP matched an older FP batch even though that source's FP
    // was already ahead. A later unmatched TCP must retain the 09:00 pair,
    // rather than falling all the way back to the 08:30 aligned account.
    auto later = current; later.sourceTime = "2026-03-30T09:15:00";
    later.records.front().tcp = {Counts{{11,125},{12,95}},Status::Current,later.sourceTime};
    selected = loadPreferenceHistory(folder,later).at(record.key);
    assert(!selected.matchingCurrentTcp && selected.previousAligned);
    assert(selected.previousAligned->tcpTime == current.sourceTime);
    assert(selected.previousAligned->fpTime == first.sourceTime);
    assert(selected.previousAligned->fp.at(11) == 100);
    assert(selected.previousAligned->tcp.at(11) == 116);
    // These retained observations belong to the count cache, not a fresh
    // counting event. They keep both original clocks through serialization.
    auto retained = loadPrevious(folder,"fictional",later.sourceTime);
    assert(retained && retained->records.front().matchedPreferences);
    assert(retained->records.front().matchedPreferences->fpTime == first.sourceTime);
    assert(retained->records.front().matchedPreferences->tcpTime == current.sourceTime);
    auto missing = later;
    missing.records.front().fp.votes.reset();
    retainPreferenceHistory(missing,loadPreferenceHistory(folder,later));
    assert(!missing.records.front().matchedPreferences);
    // A pair change cannot borrow the older pair's preference calibration.
    // Its FP cohort can still be useful if the new pair is itself feasible.
    changed = current; changed.records.front().tcp.votes = Counts{{11,110},{13,90}};
    assert(!loadPreferenceHistory(folder,changed).at(record.key).previousAligned);
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

void checkRecheckedPrimaryRecovery(std::filesystem::path const& folder) {
    // A fictional completed booth changes its primary composition on recheck,
    // while the preliminary TCP still describes the old primaries. Totals are
    // unchanged, so aggregate FP/TCP checks alone cannot establish the conflict.
    auto source = example();
    auto& booth = source.records.front();
    booth.candidates = {11,12,13};
    booth.fp.votes = Counts{{11,30},{12,150},{13,20}};
    booth.tcp.votes = Counts{{11,45},{12,155}};
    booth.fp.stage = booth.tcp.stage = CountStage::Preliminary;
    auto old = recover(source,{}).snapshot;
    commit(folder,old);
    auto current = source; current.sourceTime = "2026-03-30T09:00:00";
    current.records.front().fp.votes = Counts{{11,110},{12,70},{13,20}};
    current.records.front().fp.stage = CountStage::Rechecked;
    auto recovered = recover(current,old);
    auto const& effective = recovered.snapshot.records.front();
    assert(effective.fp.fresh() && effective.fp.votes == current.records.front().fp.votes);
    assert(effective.fp.stage == CountStage::Rechecked);
    assert(effective.tcp.votes && hasCurrentTcpBasis(effective));
    assert(hasIssue(recovered,"tcp_fp_revision"));
    assert(!hasIssue(recovered,"reconciliation_hold"));

    // With no authority advantage, keep an earlier coherent pair. Unknown
    // stages (including older caches) do not acquire invented priority.
    auto ambiguous = current;
    ambiguous.records.front().fp.stage = CountStage::Preliminary;
    auto held = recover(ambiguous,old);
    assert(held.snapshot.records.front().fp.votes == old.records.front().fp.votes);
    assert(held.snapshot.records.front().tcp.votes == old.records.front().tcp.votes);
    assert(hasIssue(held,"reconciliation_hold"));
    ambiguous.records.front().fp.stage = ambiguous.records.front().tcp.stage = CountStage::Unspecified;
    assert(hasIssue(recover(ambiguous,old),"reconciliation_hold"));
    assert(!recover(ambiguous,{}).snapshot.records.front().tcp.votes);

    // All pending ballots could be a finalist's primaries. Such partial TCP
    // remains valid even when it is far below that finalist's full FP count.
    auto partial = current;
    partial.records.front().tcp.votes = Counts{{11,20},{12,30}};
    partial.records.front().role = "declaration";
    partial.records.front().key = "North/declaration/School";
    assert(recover(partial,{}).snapshot.records.front().tcp.fresh());
    auto exhausted = current; exhausted.optionalPreferential = true;
    assert(recover(exhausted,{}).snapshot.records.front().tcp.fresh());
    auto smallDifference = current;
    smallDifference.records.front().tcp.votes = Counts{{11,100},{12,100}};
    assert(recover(smallDifference,{}).snapshot.records.front().tcp.fresh()); // Exactly 10 votes.
    smallDifference.records.front().tcp.votes = Counts{{11,99},{12,101}};
    assert(!recover(smallDifference,{}).snapshot.records.front().tcp.votes);

    // Even an official-stage label cannot make a tiny unfinished recheck
    // supersede a fuller earlier account. The established reset hold remains.
    auto reset = current;
    reset.records.front().fp.votes = Counts{{11,1},{12,1},{13,0}};
    assert(hasIssue(recover(reset,old),"reconciliation_hold"));

    // The old pair remains the basis of the TCP, rather than being falsely
    // paired with revised FP. Only the changed votes need a new estimate.
    auto history = loadPreferenceHistory(folder,recovered.snapshot);
    assert(!loadPreferenceHistory(folder/"empty",recovered.snapshot).at(booth.key).previousAligned);
    auto const& selected = history.at(booth.key).matchingCurrentTcp;
    assert(selected && history.at(booth.key).tcpBasis);
    assert(selected->fp == *old.records.front().fp.votes && selected->tcp == *old.records.front().tcp.votes);
    auto flow = LivePartialCount::measure(selected->fp,selected->tcp,{{11,1},{12,0},{13,.5}},11,12);
    assert(flow && !LivePartialCount::remainingCohort(*effective.fp.votes,*flow));
    auto predicted = LivePartialCount::expectedFirst(*effective.fp.votes,{{11,1},{12,0},{13,.5}},
        LivePartialCount::evidenceOffset(0,flow,11,12));
    assert(predicted > 110 && predicted < 130);
    retainPreferenceHistory(recovered.snapshot,history);
    commit(folder,recovered.snapshot);
    auto later = recovered.snapshot; later.sourceTime = "2026-03-30T10:00:00";
    auto saved = loadPrevious(folder,later.election,later.sourceTime);
    assert(saved && saved->records.front().fp.stage == CountStage::Rechecked);
    assert(saved->records.front().tcp.votes && saved->records.front().tcpBasis);
    assert(loadPreferenceHistory(folder,later).at(booth.key).matchingCurrentTcp->tcpTime == old.sourceTime);
    auto changedPair = later;
    changedPair.records.front().tcp.votes = Counts{{11,120},{13,80}};
    assert(!loadPreferenceHistory(folder,changedPair).at(booth.key).previousAligned);

    auto corrected = current; corrected.sourceTime = "2026-03-30T11:00:00";
    corrected.records.front().tcp.votes = Counts{{11,120},{12,80}};
    corrected.records.front().tcp.stage = CountStage::Rechecked;
    auto accepted = recover(corrected,recovered.snapshot);
    assert(accepted.snapshot.records.front().tcp.fresh() && !hasIssue(accepted,"invalid_tcp"));

    auto malformed = current; malformed.records.front().fp.error = "Unreadable recheck";
    assert(recover(malformed,old).snapshot.records.front().fp.stage == CountStage::Preliminary);
    // If an older TCP really fits the corrected FP, ordinary historical
    // selection may restore it. An incompatible nearest entry must not block
    // that search, and the restored count retains its original clock.
    auto earlier = old; earlier.sourceTime = "2026-03-30T07:00:00";
    earlier.records.front().fp.votes = current.records.front().fp.votes;
    earlier.records.front().tcp.votes = Counts{{11,120},{12,80}};
    for (auto& r : earlier.records) r.fp.observedAt = r.tcp.observedAt = earlier.sourceTime;
    commit(folder,earlier);
    auto invalidNewTcp = current;
    invalidNewTcp.records.front().tcp.votes = Counts{{11,10},{12,190}};
    auto fallback = recover(invalidNewTcp,old,[&](auto const& effective,auto previous,auto account) {
        return loadEarlierCounts(folder,effective,previous,account);
    });
    assert(fallback.snapshot.records.front().fp.fresh());
    assert(fallback.snapshot.records.front().tcp.votes == earlier.records.front().tcp.votes);
    assert(fallback.snapshot.records.front().tcp.status == Status::Restored);
    assert(fallback.snapshot.records.front().tcp.observedAt == earlier.sourceTime);

    // Existing private count caches predate stage metadata. Their counts and
    // clocks remain readable, but reload must leave the stage unspecified.
    auto legacyFile = folder/"2026-03-30T08-00-00.json";
    json legacy;
    { std::ifstream input(legacyFile); input >> legacy; }
    for (auto& r : legacy["records"]) for (auto field : {"fp","tcp"}) r[field].erase("stage");
    { std::ofstream output(legacyFile); output << legacy; }
    auto legacySource = loadPrevious(folder,source.election,"2026-03-30T08:30:00");
    assert(legacySource && legacySource->records.front().fp.votes == old.records.front().fp.votes);
    assert(legacySource->records.front().fp.stage == CountStage::Unspecified);
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

    // Historical evidence notices use the same persistent grouping, without
    // labelling a genuine extreme flow as a malformed current count.
    Registry historical;
    Issue limited{"historical_preference_limited","North/ordinary/A","North","A",
        "Previous FP: ALP 600, LNP 300, GRN 100. Previous TCP: ALP 697, LNP 300.",
        "Check historical counts; counted TCP is retained.","2026-03-30T10:00:00",30,false,{},{}};
    historical.add(limited); historical.add(limited);
    limited.key="South/ordinary/B"; limited.district="South"; limited.name="B";
    limited.impact=40; historical.add(limited);
    auto row=historical.summary().at(0);
    assert(row.at("label")=="Historical preference evidence limited");
    assert(row.at("affected_records")==2 && row.at("affected_districts")==2);
    assert(row.at("record")=="B" && !row.at("routine").get<bool>());
    assert(row.at("rejected_total").is_null() && row.at("effective_total").is_null());
    assert(historical.details().at(0).at("explanation").get<std::string>().find("Previous FP:")!=std::string::npos);
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

void checkIncrementalHistory(std::filesystem::path const& folder) {
    auto source = example();
    source.records.front().incremental = true;
    auto first = recover(source,{}).snapshot;
    commit(folder,first);
    auto later = source; later.sourceTime = "2026-03-30T09:00:00";
    auto saved = loadCompatiblePrevious(folder,later);
    assert(saved && saved->records.front().incremental);
    assert(!saved->records.front().fp.complete);
    auto path = folder/"2026-03-30T08-00-00.json";
    json legacy;
    { std::ifstream input(path); input >> legacy; }
    legacy["records"][0].erase("incremental");
    { std::ofstream output(path); output << legacy; }
    auto old = loadPrevious(folder,source.election,later.sourceTime);
    assert(old && !old->records.front().incremental);
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
void checkSnapshotElectionIdentity() {
    // The source must identify the very account the count reader will consume,
    // before any counts or history can be updated. ECQ bundles may also contain
    // a council election; its identifier cannot stand in for the state one.
    Results2::Election election("fictional"); election.id = 99;
    using Format = Results2::Election::Format;
    auto matches = [&](char const* text, Format format) {
        tinyxml2::XMLDocument document;
        assert(document.Parse(text) == tinyxml2::XML_SUCCESS);
        return election.matchesSnapshotElection(document, format);
    };
    assert(matches("<ecq><election id='99'/><election id='100'/></ecq>", Format::QEC));
    assert(!matches("<ecq><election id='100'/><election id='99'/></ecq>", Format::QEC));
    assert(!matches("<ecq><election/></ecq>", Format::QEC));
    assert(!matches("<ecq><election id='broken'/></ecq>", Format::QEC));
    assert(!matches("<ecq/>", Format::QEC));
    assert(matches("<MediaFeed><Results><eml:EventIdentifier Id='99'/></Results></MediaFeed>", Format::AEC));
    assert(matches("<MediaFeed><eml:EventIdentifier Id='99'/></MediaFeed>", Format::VEC));
    assert(!matches("<MediaFeed><eml:EventIdentifier Id='100'/></MediaFeed>", Format::VEC));
    assert(!matches("<MediaFeed><Results><eml:EventIdentifier Id='100'/></Results></MediaFeed>", Format::AEC));
    assert(!matches("<MediaFeed><eml:EventIdentifier Id='99'/></MediaFeed>", Format::QEC));
}

std::string qecFixtureRound(char const* name, char const* field,
    std::vector<std::string> const& values, std::vector<std::string> const& names) {
    std::string xml = "<countRound countName='" + std::string(name) + "'><booths><booth id='7' name='School'><" + field + ">";
    for (size_t i = 0; i < values.size(); ++i)
        xml += "<candidate ballotName='" + names.at(i) + "'><count>" + values[i] + "</count></candidate>";
    return xml + "</" + field + "></booth></booths></countRound>";
}

Results2::Election qecRecheckFixture(std::vector<std::string> const& officialFp,
    std::vector<std::string> const& officialTcp = {},
    std::vector<std::string> const& tcpNames = {"One", "Two"}, bool preliminary = true,
    std::string const& details = "") {
    // Every call is a fresh read, with no accepted-count history. The retained
    // preliminary account must come from this source rather than a past run.
    std::vector<std::string> const names = {"One", "Two", "Three"};
    std::string xml = "<ecq><generationDateTime>2026-03-30T09:00:00</generationDateTime><election id='99'><districts><district number='1' districtName='North' enrolment='10000'><candidates>";
    for (auto const& name : names) xml += "<candidate ballotName='" + name + "' party='Independent'/>";
    xml += "</candidates>";
    if (preliminary) {
        xml += qecFixtureRound("Unofficial Preliminary Count", "primaryVoteResults", {"600", "300", "100"}, names);
        auto tcpRound = qecFixtureRound("Unofficial Indicative Count", "twoCandidateVotes", {"650", "350"}, names);
        tcpRound.insert(tcpRound.find("</booth>"),details);
        tcpRound.insert(tcpRound.find(" name='School'")," lastUpdated='2026-03-30T08:00:00'");
        xml += tcpRound;
    }
    xml += qecFixtureRound("Official First Preference Count", "primaryVoteResults", officialFp, names);
    xml += qecFixtureRound("Official Distribution of Preferences Count", "twoCandidateVotes", officialTcp, tcpNames);
    xml += "</district></districts></election></ecq>";
    tinyxml2::XMLDocument feed;
    assert(feed.Parse(xml.c_str()) == tinyxml2::XML_SUCCESS);
    auto election = Results2::Election::createQec(json(),feed,"fictional");
    election.updateLive(feed,Results2::Election::Format::QEC);
    return election;
}

void checkQecPartialRechecks() {
    std::string details = "<indicativeCountDetails>"
        "<selectedCandidate ballotName='One'><primaryVotes>600</primaryVotes><afterPreferences><count>650</count></afterPreferences></selectedCandidate>"
        "<selectedCandidate ballotName='Two'><primaryVotes>300</primaryVotes><afterPreferences><count>350</count></afterPreferences></selectedCandidate>"
        "<otherCandidate ballotName='Three'><preferenceCount ballotName='One'>50</preferenceCount><preferenceCount ballotName='Two'>50</preferenceCount><exhausted>0</exhausted></otherCandidate>"
        "</indicativeCountDetails>";
    auto revised = qecRecheckFixture({"700","200","100"},{},{"One","Two"},true,details);
    auto const key = Results2::Election::countKey(1,100007,Results2::VoteType::Ordinary);
    assert(revised.liveTcpBases.contains(key));
    auto const& basis = revised.liveTcpBases.at(key);
    assert(LivePartialCount::validBasis({basis.fp,basis.tcp,basis.transfers}));
    assert(basis.fp != Counts(revised.booths.at(100007).fpVotes.begin(),revised.booths.at(100007).fpVotes.end()));
    auto official = qecRecheckFixture({"700","200","100"},{"750","250"},{"One","Two"},true,details);
    assert(official.liveTcpBases.empty()); // Selected official TCP supersedes the indicative account.
    auto bad = details; bad.replace(bad.find(">50<"),4,">50.5<");
    assert(qecRecheckFixture({"700","200","100"},{},{"One","Two"},true,bad).liveTcpBases.empty());
    auto fpTotal = [](auto const& e) { return e.seats.at(1).totalVotesFp(); };
    auto tcpTotal = [](auto const& e) {
        int total = 0;
        for (auto const& [id, votes] : e.booths.at(100007).tcpVotesCandidate) total += votes;
        return total;
    };
    // A tiny or reset official batch must not replace a complete preliminary
    // account. FP and TCP can catch up separately without adding rounds together.
    auto partial = qecRecheckFixture({"2", "8", "0"});
    assert(fpTotal(partial) == 1000 && tcpTotal(partial) == 1000);
    auto fpKey = std::make_pair(Results2::Election::countKey(1,100007,Results2::VoteType::Ordinary),false);
    auto tcpKey = std::make_pair(fpKey.first,true);
    assert(partial.liveCountStages.at(fpKey) == CountStage::Preliminary);
    assert(fpTotal(qecRecheckFixture({"0", "0", "0"})) == 1000);
    auto caughtUp = qecRecheckFixture({"610", "300", "100"}, {"66", "34"});
    assert(fpTotal(caughtUp) == 1010 && tcpTotal(caughtUp) == 1000);
    assert(caughtUp.liveCountStages.at(fpKey) == CountStage::Rechecked);
    assert(caughtUp.liveCountStages.at(tcpKey) == CountStage::Preliminary);
    // The existing allowance is max(10 votes, 5% of the new count). These two
    // values lie immediately either side of its relative comparison boundary.
    assert(fpTotal(qecRecheckFixture({"551", "300", "100"})) == 1000);
    assert(fpTotal(qecRecheckFixture({"553", "300", "100"})) == 953);

    // A complete, consistent official pair permits a real downward correction,
    // including an explicit zero. Merely having both fields is not sufficient.
    auto corrected = qecRecheckFixture({"300", "200", "100"}, {"360", "240"});
    assert(fpTotal(corrected) == 600 && tcpTotal(corrected) == 600);
    assert(corrected.liveCountStages.at(tcpKey) == CountStage::Rechecked);
    auto mismatch = qecRecheckFixture({"300", "200", "100"}, {"300", "200"});
    assert(fpTotal(mismatch) == 1000 && tcpTotal(mismatch) == 1000);
    auto impossibleFlow = qecRecheckFixture({"500", "50", "50"}, {"100", "500"});
    assert(fpTotal(impossibleFlow) == 1000 && tcpTotal(impossibleFlow) == 1000);
    auto zero = qecRecheckFixture({"0", "0", "0"}, {"0", "0"});
    assert(fpTotal(zero) == 0 && tcpTotal(zero) == 0);
    auto officialOnly = qecRecheckFixture({"300", "200", "100"}, {"360", "240"}, {"One", "Two"}, false);
    assert(fpTotal(officialOnly) == 600 && tcpTotal(officialOnly) == 600);
    auto withdrawn = qecRecheckFixture({"600", "300", "100"}, {}, {"One", "Two"}, false);
    assert(withdrawn.expectedLiveTcpWithdrawals.contains(fpKey.first));
    assert(partial.expectedLiveTcpWithdrawals.empty());
    assert(officialOnly.expectedLiveTcpWithdrawals.empty());

    // Malformed and incomplete candidate vectors must reach ordinary recovery;
    // they cannot be treated as measured small counts during round selection.
    auto malformed = qecRecheckFixture({"broken", "200", "100"});
    assert(!malformed.invalidLiveCounts.empty());
    auto omitted = qecRecheckFixture({"300", "300"}, {"350", "250"});
    assert(omitted.booths.at(100007).fpVotes.size() == 2 && tcpTotal(omitted) == 1000);
    auto changedPair = qecRecheckFixture({"600", "300", "100"}, {"180", "120"}, {"One", "Three"});
    assert(tcpTotal(changedPair) == 300 && changedPair.booths.at(100007).tcpVotesCandidate.contains(3));
}

void checkSyntheticCategoryProvenance() {
    tinyxml2::XMLDocument preload;
    std::string xml = "<HouseOfAssemblyDetail><districts><district><district_name>Example</district_name><district_id>1</district_id><first_preferences>";
    for (int id : {1,2}) {
        xml += "<candidate><candidate_name>Candidate " + std::to_string(id) + "</candidate_name><candidate_id>" + std::to_string(id)
            + "</candidate_id><affiliation>" + (id == 1 ? std::string("ALP") : std::string("LIB")) + "</affiliation><polling_places>";
        for (auto const& [booth,name] : std::map<int,std::string>{{11,"School"},{12,"Example Early Voting Centre"}})
            xml += "<polling_place><polling_place_id>"+std::to_string(booth)+"</polling_place_id><polling_place_name>"+name+"</polling_place_name></polling_place>";
        xml += "</polling_places></candidate>";
    }
    xml += "</first_preferences></district></districts></HouseOfAssemblyDetail>";
    assert(preload.Parse(xml.c_str()) == tinyxml2::XML_SUCCESS);
    json history = {{"Example", {
        {"candidates",{{"0",{{"name","Candidate 1"},{"party","ALP"}}},{"1",{{"name","Candidate 2"},{"party","LIB"}}}}},
        {"booths",{{"School",{{"fp",{{"0",100},{"1",100}}},{"tcp",{{"0",100},{"1",100}}}}},
                   {"Declaration Votes",{{"fp",{{"0",1000},{"1",1000}}},{"tcp",{{"0",1000},{"1",1000}}}}}}}
    }}};
    auto election=Results2::Election::createEcsa(history,preload,"fictional");
    assert(!election.booths.at(11).syntheticBaseline);
    assert(election.booths.at(12).syntheticBaseline);
    assert(election.seats.at(1).syntheticBaselineTypes.contains(Results2::VoteType::Postal));
    assert(!election.seats.at(1).syntheticBaselineTypes.contains(Results2::VoteType::Ordinary));
}

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
    election.liveCountStages[{Results2::Election::countKey(1,7,Results2::VoteType::Ordinary),false}] = CountStage::Rechecked;
    election.expectedLiveTcpWithdrawals.insert(Results2::Election::countKey(1,7,Results2::VoteType::Ordinary));
    TurnoutModelIO::Artifact artifact;
    artifact.prior.election = "fictional"; artifact.prior.seats = {"North"}; artifact.prior.groups = {"other"};
    artifact.prior.enrolment = {1000}; artifact.prior.counts = {{600}};
    TurnoutModel::Unit unit; unit.name = "School"; unit.kind = "ordinary"; unit.weight = .5;
    artifact.prior.units.push_back(unit); unit.name = "Postal"; unit.kind = "declaration"; artifact.prior.units.push_back(unit);
    auto source = LiveInputAdapter::identify(election,artifact,"source");
    assert(source.records.front().fp.stage == CountStage::Rechecked);
    assert(source.records.front().expectedTcpWithdrawal);
    assert(!source.records.back().expectedTcpWithdrawal);
    auto old = recover(source,{}).snapshot;
    source.sourceTime = "2026-03-30T09:00:00"; source.records[0].fp.error = "Malformed";
    auto recovered = recover(source,old);
    LiveInputAdapter::apply(election,recovered.snapshot);
    assert(election.seats.at(1).totalVotesFp() == 400);
    assert(election.booths.at(7).totalVotesFp() == 300);
    assert(!election.liveCountStatus.at("North/ordinary/School").first.fresh());
    assert(election.liveCountStatus.at("North/ordinary/School").first.status == Status::Restored);
    auto const& effectiveStatus = election.liveCountStatus.at("North/ordinary/School");
    assert(effectiveStatus.first.stage == CountStage::Rechecked);
    assert(!explicitlyRecheckedPair(effectiveStatus.first,effectiveStatus.second));
    // Only two available, explicitly rechecked effective records qualify.
    // Restored preliminary TCP must not inherit a newer source's official tag.
    auto fpStatus = effectiveStatus.first, tcpStatus = effectiveStatus.second;
    tcpStatus.stage = CountStage::Preliminary;
    tcpStatus.status = Status::Restored;
    assert(!explicitlyRecheckedPair(fpStatus,tcpStatus));
    tcpStatus.stage = CountStage::Rechecked;
    assert(explicitlyRecheckedPair(fpStatus,tcpStatus));
    tcpStatus.available = false;
    assert(!explicitlyRecheckedPair(fpStatus,tcpStatus));
    tcpStatus.available = true; fpStatus.stage = CountStage::Unspecified;
    assert(!explicitlyRecheckedPair(fpStatus,tcpStatus));
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
    // An official recheck replaces only the booth with an actual published
    // account. Empty placeholders must not erase the other booth's preliminary
    // count. Final TCP can arrive without the indicative round still existing.
    auto& district = *qecFeed.FirstChildElement("ecq")->FirstChildElement("election")->FirstChildElement("districts")->FirstChildElement("district");
    auto secondBooth = qecBooth; secondBooth.id = 100008; secondBooth.name = "Hall";
    qec.booths.emplace(secondBooth.id, secondBooth);
    qec.seats.at(1).booths.push_back(secondBooth.id);
    auto preliminaryBooths = district.FirstChildElement("countRound")->FirstChildElement("booths");
    auto copied = preliminaryBooths->FirstChildElement("booth")->DeepClone(&qecFeed)->ToElement();
    copied->SetAttribute("id", 8); preliminaryBooths->InsertEndChild(copied);
    tinyxml2::XMLDocument official;
    official.Parse("<root><countRound countName='Official First Preference Count'><booths><booth id='7'><primaryVoteResults><candidate ballotName='One'><count>170</count></candidate><candidate ballotName='Two'><count>120</count></candidate></primaryVoteResults></booth><booth id='8'/></booths></countRound><countRound countName='Official Distribution of Preferences Count'><booths><booth id='7'><twoCandidateVotes><candidate ballotName='One'><count>180</count></candidate><candidate ballotName='Two'><count>110</count></candidate></twoCandidateVotes></booth></booths></countRound></root>");
    for (auto round = official.RootElement()->FirstChildElement(); round; round = round->NextSiblingElement())
        district.InsertEndChild(round->DeepClone(&qecFeed));
    district.DeleteChild(district.FirstChildElement("countRound")->NextSiblingElement("countRound"));
    qec.updateLive(qecFeed,Results2::Election::Format::QEC);
    assert(qec.booths.at(100007).fpVotes.at(11) == 170);
    assert(qec.booths.at(100008).fpVotes.at(11) == 180);
    assert(qec.booths.at(100007).tcpVotesCandidate.at(11) == 180);
    assert(qec.seats.at(1).totalVotesFp() == 590);

    // Category labels are represented once, separately from physical booths.
    // Early absents use PrePoll in both years; mobile polling is a physical
    // service, and historical parties retain their identities when not running.
    tinyxml2::XMLDocument roster;
    roster.Parse("<ecq><election id='99'><districts><district number='1' districtName='North District' enrolment='1000'><candidates><candidate ballotName='One' party='Australian Labor Party (State of Queensland)'/><candidate ballotName='Two' party='Liberal National Party of Queensland'/></candidates><countRound><booths><booth id='7' name='School'/><booth id='8' name='Mobile Polling'/><booth id='9' name='Absent Early Voting'/><booth id='10' name='Postal Declaration Votes'/></booths></countRound></district></districts></election></ecq>");
    auto rosterElection = Results2::Election::createQec(nlohmann::json(),roster,"fictional");
    assert(rosterElection.id == 99); // Identity is available before the first update.
    assert(rosterElection.matchesSnapshotElection(qecFeed, Results2::Election::Format::QEC));
    assert(rosterElection.seats.at(1).booths == std::vector<int>({100007,100008}));
    assert(rosterElection.booths.at(100008).type == Results2::Booth::Type::Hospital);
    auto history = nlohmann::json::parse(R"json({"North District":{"candidates":{"0":{"name":"One","party":"Australian Labor Party (State of Queensland)"},"1":{"name":"Old","party":"Clive Palmer's United Australia Party"}},"booths":{"Absent Early Voting":{"fp":{"0":40,"1":60},"tcp":{"0":45,"1":55}},"Mobile Polling":{"fp":{"0":10,"1":20},"tcp":{"0":15,"1":15}}}}})json");
    auto priorQec = Results2::Election::createQec(history,roster,"fictional");
    int historicalCandidate = 0;
    for (auto const& [id, candidate] : priorQec.candidates)
        if (candidate.name == "Old") historicalCandidate = id;
    assert(priorQec.parties.at(priorQec.candidates.at(historicalCandidate).party).shortCode == "UAP");
    assert(priorQec.seats.at(1).fpVotes.at(historicalCandidate).at(Results2::VoteType::PrePoll) == 60);
    assert(priorQec.seats.at(1).totalVotesFp() == 130);
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

void checkTcpRevisionBasis(std::filesystem::path const& folder) {
    auto initial = example();
    initial.records.resize(1); initial.districts["North"].enrolment = 10000;
    auto& booth = initial.records.front();
    booth.candidates = {11,12,13,14};
    booth.fp.votes = Counts{{11,1122},{12,2862},{13,466},{14,299}};
    booth.tcp.votes = Counts{{11,1557},{12,3192}};
    booth.fp.stage = booth.tcp.stage = CountStage::Preliminary;
    booth.tcpBasis = PreferenceCohort{*booth.fp.votes,*booth.tcp.votes,initial.sourceTime,initial.sourceTime,
        {{13,{{11,362},{12,104}}},{14,{{11,73},{12,226}}}}};
    auto old = recover(initial,{}).snapshot;
    auto changed = initial; changed.sourceTime = "2026-03-30T09:00:00";
    changed.records[0].fp.votes = Counts{{11,1422},{12,2562},{13,468},{14,298}};
    changed.records[0].fp.stage = CountStage::Rechecked;
    // Starting directly at the corrected snapshot can use the embedded source
    // details. Replaying via history must give precisely the same association.
    auto direct = recover(changed,{}).snapshot;
    auto recovered = recover(changed,old).snapshot;
    assert(hasCurrentTcpBasis(direct.records[0]));
    assert(recovered.records[0].fp.votes == changed.records[0].fp.votes);
    assert(recovered.records[0].tcpBasis->fp == *old.records[0].fp.votes);
    assert(!recovered.records[0].tcpBasis->recheckedReconciledPair);
    commit(folder,recovered);
    auto next = changed; next.sourceTime = "2026-03-30T10:00:00";
    next.records[0].tcpBasis.reset();
    auto saved = loadCompatiblePrevious(folder,next);
    assert(saved && saved->records[0].tcpBasis->transfers == old.records[0].tcpBasis->transfers);
    auto repeat = recover(next,saved).snapshot;
    assert(repeat.records[0].tcpBasis->fp == *old.records[0].fp.votes);
    auto history = loadPreferenceHistory(folder,repeat);
    assert(history.at(booth.key).matchingCurrentTcp->fp == *old.records[0].fp.votes);
    // An equal total with a large candidate revision must never become a false
    // matched pair. Neither are candidate contradictions grounds to discard a
    // known older batch; the forecast instead revises that batch's account.
    next.records[0].fp.votes = Counts{{11,1800},{12,2184},{13,466},{14,299}};
    auto equal = recover(next,recovered).snapshot;
    assert(hasCurrentTcpBasis(equal.records[0]) && equal.records[0].tcp.votes);
    auto equalHistory = loadPreferenceHistory(folder,equal);
    assert(equalHistory.at(booth.key).matchingCurrentTcp->fp == *old.records[0].fp.votes);
    next.records[0].tcp.votes.reset(); next.records[0].expectedTcpWithdrawal = true;
    auto withdrawal = recover(next,recovered).snapshot;
    assert(withdrawal.records[0].tcp.status == Status::Restored);
    assert(hasCurrentTcpBasis(withdrawal.records[0]));
    next.records[0].fp = changed.records[0].fp;
    next.records[0].tcp.votes = Counts{{11,1850},{12,2900}};
    next.records[0].tcp.stage = CountStage::Rechecked;
    auto final = recover(next,recovered).snapshot;
    assert(!final.records[0].tcpBasis && final.records[0].tcp.status == Status::Current);
    auto finalHistory = loadPreferenceHistory(folder,final);
    assert(finalHistory.at(booth.key).matchingCurrentTcp->fp == *final.records[0].fp.votes);
    assert(finalHistory.at(booth.key).matchingCurrentTcp->recheckedReconciledPair);
    // Optional-preferential elections retain their existing exhaustion rules.
    next.optionalPreferential = true;
    assert(loadPreferenceHistory(folder,next).empty());
}

void checkRetainedRecheckStatus(std::filesystem::path const& folder) {
    // A verified pair remains verified if only FP changes later. This status
    // belongs to that original account, including after save/reload/withdrawal.
    auto source = example(); source.records.resize(1);
    auto& record = source.records.front();
    record.candidates = {11,12,13};
    record.fp.votes = Counts{{11,180},{12,120},{13,100}};
    record.tcp.votes = Counts{{11,260},{12,140}};
    record.fp.stage = record.tcp.stage = CountStage::Rechecked;
    auto original = recover(source,{}).snapshot;
    auto changed = source; changed.sourceTime = "2026-03-30T09:00:00";
    changed.records[0].fp.votes->at(13) += 5;
    auto revised = recover(changed,original).snapshot;
    assert(revised.records[0].tcpBasis->recheckedReconciledPair);
    commit(folder,revised);
    auto next = changed; next.sourceTime = "2026-03-30T10:00:00";
    auto saved = loadCompatiblePrevious(folder,next);
    assert(saved && saved->records[0].tcpBasis->recheckedReconciledPair);
    auto repeated = recover(next,saved).snapshot;
    auto history = loadPreferenceHistory(folder,repeated);
    assert(history.at(record.key).tcpBasis->recheckedReconciledPair);
    retainPreferenceHistory(repeated,history);
    assert(repeated.records[0].matchedPreferences->recheckedReconciledPair);
    next.records[0].tcp.votes.reset(); next.records[0].expectedTcpWithdrawal = true;
    auto withdrawn = recover(next,revised).snapshot;
    assert(withdrawn.records[0].tcpBasis->recheckedReconciledPair);

    // Existing accepted-state files remain readable. Without saved provenance
    // an unmatched pair cannot be promoted just because its latest stages say
    // rechecked; the ordinary unknown-stage outlier protection remains available.
    auto file = folder/"2026-03-30T09-00-00.json";
    json legacy;
    { std::ifstream input(file); input >> legacy; }
    legacy["records"][0]["tcp_basis"].erase("rechecked_reconciled_pair");
    { std::ofstream output(file); output << legacy; }
    auto restored = loadPrevious(folder,source.election,next.sourceTime);
    assert(restored && restored->records[0].tcpBasis);
    assert(!restored->records[0].tcpBasis->recheckedReconciledPair);
    assert(restored->records[0].tcpBasis->fp == *original.records[0].fp.votes);
}

void checkFpTotalHistory(std::filesystem::path const& folder) {
    std::filesystem::create_directories(folder);
    std::optional<Snapshot> previous;
    for (int hour = 8; hour <= 12; ++hour) {
        auto source = example("2026-03-30T"+std::string(hour < 10 ? "0" : "")+std::to_string(hour)+":00:00");
        auto& r = source.records[0]; r.incremental = true;
        int votes = hour <= 9 ? 300 : hour == 10 ? 800 : hour == 11 ? 780 : 900;
        r.fp.votes = Counts{{11,votes},{12,0}}; r.tcp.votes = Counts{{11,votes},{12,0}};
        previous = recover(source,previous).snapshot;
        commit(folder,*previous);
    }
    std::map<std::string,std::set<std::string>> selection{{"North",{"North/ordinary/School"}}};
    auto query = [&](std::string const& time, HistoryReadMode mode) {
        return loadFpTotalHistory(folder,"fictional",time,selection,mode);
    };
    auto indexed = query("2026-03-30T12:00:00",HistoryReadMode::Indexed);
    assert(indexed == query("2026-03-30T12:00:00",HistoryReadMode::SourceFiles));
    assert(indexed.size() == 1 && indexed.begin()->second == std::vector<double>({300,800,780}));
    assert(query("2026-03-30T10:00:00",HistoryReadMode::Indexed).begin()->second == std::vector<double>({300}));
    assert(query("2026-03-30T08:00:00",HistoryReadMode::Indexed).empty());
}

int main(int argc, char** argv) {
    auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
    auto folder = root/"cli-build"/"input-recovery-tests";
    std::filesystem::remove_all(folder);
    checkLocalRecovery(); checkDistrictRecoveryAndProcedures(); checkRegistry();
    checkExpectedTcpWithdrawal();
    checkReconciliationHold();
    checkFreshProgressEvidence();
    checkHistory(folder); checkFailedUpdateTransaction(folder);
    checkIncrementalHistory(folder/"incremental");
    checkFpTotalHistory(folder/"fp-totals");
    checkOlderCompatibleCounts(folder/"older-compatible");
    checkReconciliationHistory(folder/"reconciliation");
    checkPreferenceHistory(folder/"preferences");
    checkTcpRevisionBasis(folder/"tcp-revision");
    checkRetainedRecheckStatus(folder/"retained-recheck-status");
    checkRecheckedPrimaryRecovery(folder/"rechecked-primaries");
#ifdef LIVE_INPUT_PARSER_TESTS
    checkSnapshotElectionIdentity();
    checkQecPartialRechecks();
    checkSyntheticCategoryProvenance();
    checkParserAndAdapter();
#endif
    std::filesystem::remove_all(folder);
    std::cout << "Live input recovery checks passed.\n";
}
