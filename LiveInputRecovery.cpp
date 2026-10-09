#include "LiveInputRecovery.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace LiveInputRecovery {
std::int64_t total(std::optional<Counts> const& votes) {
    std::int64_t value = 0;
    if (votes) for (auto const& [candidate, count] : *votes) value += count;
    return value;
}

std::string canonicalTime(std::string value) {
    TurnoutModel::sourceHour(value);
    // Equivalent fractional clocks belong to the same source revision. Keep
    // chronological string ordering without treating .1 and .10 as new counts.
    if (value.size() > 19) {
        while (value.back() == '0') value.pop_back();
        if (value.back() == '.') value.pop_back();
    }
    return value;
}

void Registry::add(Issue issue) {
    auto key = std::make_pair(issue.type, issue.key);
    auto found = cases.find(key);
    if (found == cases.end() || issue.impact > found->second.impact)
        cases[key] = std::move(issue);
}

namespace {
using json = nlohmann::json;
json describe(Issue const& issue) {
    static std::map<std::string,std::string> const labels{
        {"invalid_fp","Invalid first-preference count"},{"invalid_tcp","Invalid two-candidate count"},
        {"record_exceeds_enrolment","Service FP reaches enrolment"},{"district_exceeds_enrolment","District FP reaches enrolment"},
        {"tcp_district_exceeds_enrolment","District TCP reaches enrolment"},
        {"invalid_summary","Unreadable district summary"},
        {"incomplete_tcp","Incomplete booth preference count"},{"preferences_pending","Preferences still reporting"},
        {"history_write_failed","Count history could not be saved"}};
    auto label = labels.find(issue.type);
    return {{"type",issue.type},{"label",label == labels.end() ? issue.type : label->second},
        {"district",issue.district},{"record",issue.name},
        {"explanation",issue.explanation},{"action",issue.action},
        {"impact_votes",issue.impact},{"observed_at",issue.observedAt},{"routine",issue.routine},
        {"rejected_total",issue.rejected ? json(total(issue.rejected)) : json(nullptr)},
        {"effective_total",issue.effective ? json(total(issue.effective)) : json(nullptr)}};
}

std::string invalidFp(Record const& record) {
    // Normally every measured candidate belongs to this district's ballot.
    // Availability and service closure are separate: absent measurements are
    // allowed, while positive votes in an explicitly closed service are not.
    if (!record.fp.error.empty()) return record.fp.error;
    if (!record.fp.votes) return {};
    if (record.closed && total(record.fp.votes) > 0)
        return "Votes are reported for a service explicitly configured as closed; check the source and closure configuration.";
    for (auto const& [candidate, count] : *record.fp.votes) {
        if (count < 0) return "A first-preference count is negative.";
        if (std::find(record.candidates.begin(),record.candidates.end(),candidate) == record.candidates.end())
            return "An FP candidate does not belong to this contest.";
    }
    // An explicit zero is a measurement. A missing candidate's entry is not:
    // accepting the rest as a complete booth vector would invent that zero.
    for (int candidate : record.candidates) if (!record.fp.votes->count(candidate))
        return "The FP record omits a candidate count from this ballot.";
    return {};
}

// Declaration TCP can legitimately lag FP while batches are being processed.
// Ordinary/PPVC partial TCP retains LiveV2's existing 95% comparability rule.
// Exhaustion under OPV is not a missing-preferences error. A small allowance
// also preserves savings provisions and asynchronous rechecking differences.
std::string invalidTcp(Record const& record, bool opv, bool& routine) {
    if (!record.tcp.error.empty()) return record.tcp.error;
    auto fp = total(record.fp.votes), tcp = total(record.tcp.votes);
    if (!record.tcp.votes || tcp == 0) return {};
    if (!record.fp.votes || fp == 0) { routine = true; return "TCP is present before usable FP."; }
    if (record.tcp.votes->size() != 2) return "TCP does not identify exactly two candidates.";
    for (auto const& [candidate,count] : *record.tcp.votes) {
        if (count < 0) return "A two-candidate count is negative.";
        if (!record.fp.votes->count(candidate)) return "A TCP candidate has no corresponding FP record.";
    }
    if (!opv && tcp-fp > std::max(AbsoluteTcpAllowance,RelativeTcpAllowance*fp))
        return "TCP exceeds FP beyond the allowance for small count differences.";
    if (!opv && record.role != "declaration" && tcp < OrdinaryTcpComparableFraction*fp) {
        routine = true; return "The ordinary/PPVC TCP count is not yet comparable with its FP count.";
    }
    return {};
}

using RecordIndex = std::map<std::string,Record const*>;
RecordIndex indexPrevious(Snapshot const* previous) {
    RecordIndex index;
    if (previous) for (auto const& record : previous->records) index.emplace(record.key,&record);
    return index;
}

Record const* compatiblePrevious(Record const& record, RecordIndex const& previous) {
    // A federal source has thousands of booths. Index the one needed previous
    // state once rather than scanning every booth again for each fallback.
    auto found = previous.find(record.key);
    if (found == previous.end()) return nullptr;
    auto const* old = found->second;
    return old->identity == record.identity && old->district == record.district && old->role == record.role &&
        old->candidates == record.candidates && old->closed == record.closed ? old : nullptr;
}

struct PreviousCounts {
    std::optional<Snapshot> earlier;
    RecordIndex records;
};

PreviousCounts selectEarlierCounts(Snapshot const& current, Snapshot const* previous,
    RecordIndex const& nearest, CountHistoryLookup const& history, bool tcp) {
    // Keep the selected snapshot alive for as long as its record index is used.
    // The normal nearest snapshot remains owned by the preparation caller.
    PreviousCounts result;
    result.earlier = history ? history(current,previous,tcp) : std::optional<Snapshot>{};
    result.records = nearest;
    if (result.earlier) for (auto const& record : result.earlier->records) result.records[record.key] = &record;
    return result;
}

void replaceCount(Count& count, Count const* previous, std::string const& now) {
    // A restored count keeps its own source clock. Missing data never creates
    // a measured zero or changes whether the electoral service is available.
    if (previous && previous->votes && previous->observedAt < now) {
        count = *previous; count.status = Status::Restored; count.error.clear();
    } else {
        count = {}; count.status = Status::Unavailable;
    }
}

void recordRecovery(Registry& registry, Record const& record, std::string type,
    Count const& rejected, Count const& effective, std::string reason, bool routine = false) {
    double impact = rejected.votes ? std::abs(double(total(rejected.votes)-total(effective.votes))) :
        (effective.votes ? double(total(effective.votes)) : record.expected);
    if (type == "invalid_tcp" && rejected.votes)
        impact = std::abs(double(total(rejected.votes)-total(record.fp.votes)));
    if (!std::isfinite(impact)) impact = record.expected;
    std::string action = effective.status == Status::Restored
        ? "Using the previous accepted count; inspect or correct the source record."
        : "Treating this count as unreported; inspect or correct the source record.";
    registry.add({std::move(type),record.key,record.district,record.name,std::move(reason),
        std::move(action),effective.observedAt,impact,routine,rejected.votes,effective.votes});
}

void recoverFpRecords(Result& result, RecordIndex const& previous) {
    // Valid current records pass through unchanged. Replace only an identified
    // invalid or absent FP vector; ambiguous district totals are handled later.
    for (auto& record : result.snapshot.records) {
        auto reason = invalidFp(record);
        double enrolment = result.snapshot.districts.at(record.district).enrolment;
        bool parentViolation = reason.empty() && record.fp.votes && total(record.fp.votes) >= enrolment;
        if (parentViolation) reason = "This service's FP count alone reaches or exceeds district enrolment.";
        if (reason.empty() && record.fp.votes) continue;
        auto rejected = record.fp;
        auto old = compatiblePrevious(record,previous);
        replaceCount(record.fp,old && invalidFp(*old).empty() && total(old->fp.votes) < enrolment ? &old->fp : nullptr,result.snapshot.sourceTime);
        if (!parentViolation && (!reason.empty() || record.fp.status == Status::Restored))
            recordRecovery(result.issues,record,"invalid_fp",rejected,record.fp,
                reason.empty() ? "The previously reported FP record is absent from this source." : reason);
        if (parentViolation)
            result.issues.add({"record_exceeds_enrolment",record.key,record.district,record.name,reason,
                record.fp.status == Status::Restored ? "Restored the previous service count; inspect its source figures." : "The service count is unreported; inspect its source figures.",
                record.fp.observedAt,double(total(rejected.votes))-enrolment,false,rejected.votes,record.fp.votes});
        if (!record.closed) result.snapshot.districts.at(record.district).finalised = false;
    }
}

void recoverDistrictAccounts(Result& result, Snapshot const* previous, RecordIndex const& records) {
    for (auto& [name,district] : result.snapshot.districts) {
        std::int64_t counted = 0;
        for (auto const& record : result.snapshot.records) if (record.district == name) counted += total(record.fp.votes);
        if (counted < district.enrolment) continue;
        // The aggregate contradiction does not identify a guilty booth. Roll
        // back the whole FP account, instead of selecting records to delete
        // until the sum happens to fit. Other districts retain current counts.
        RecordIndex const* accountRecords = &records;
        RecordIndex wholeIndex;
        District const* oldDistrict = nullptr;
        if (previous) {
            auto whole = previous->wholeDistricts.find(name);
            if (whole != previous->wholeDistricts.end()) {
                for (auto const& record : whole->second.records) wholeIndex.emplace(record.key,&record);
                accountRecords = &wholeIndex;
                oldDistrict = &whole->second.district;
            } else if (previous->districts.count(name) && previous->districts.at(name).wholeAccount)
                oldDistrict = &previous->districts.at(name);
        }
        bool compatible = oldDistrict != nullptr;
        std::int64_t oldTotal = 0;
        for (auto const& record : result.snapshot.records) if (record.district == name) {
            auto old = compatiblePrevious(record,*accountRecords);
            compatible = compatible && old && invalidFp(*old).empty();
            if (old) oldTotal += total(old->fp.votes);
        }
        compatible = compatible && oldTotal < district.enrolment;
        for (auto& record : result.snapshot.records) if (record.district == name) {
            auto rejected = record.fp;
            auto old = compatiblePrevious(record,*accountRecords);
            replaceCount(record.fp,compatible && old ? &old->fp : nullptr,result.snapshot.sourceTime);
            // Parent contradictions are ranked by excess district votes, not
            // by which booth happened to change most during the rollback.
            result.issues.add({"district_exceeds_enrolment",record.key,record.district,record.name,
                "District FP total " + std::to_string(counted) + " reaches or exceeds enrolment " + std::to_string(int(district.enrolment)) + ".",
                record.fp.status == Status::Restored ? "Restored the previous district FP account; inspect the district's source totals." : "District FP is unreported; inspect the district's source totals.",
                record.fp.observedAt, double(counted)-district.enrolment, false, rejected.votes, record.fp.votes});
        }
        district.finalised = compatible && oldDistrict->finalised;
    }
}

void recoverTcpRecords(Result& result, RecordIndex const& previous) {
    for (auto& record : result.snapshot.records) {
        bool routine = false;
        auto reason = invalidTcp(record,result.snapshot.optionalPreferential,routine);
        if (reason.empty() && record.tcp.votes) {
            if (!result.snapshot.optionalPreferential && total(record.fp.votes) > total(record.tcp.votes))
                result.issues.add({"preferences_pending",record.key,record.district,record.name,
                    "First preferences are ahead of the preference count.",
                    "Retaining partial preferences and estimating those still unreported; no action is normally needed.",
                    record.tcp.observedAt,double(total(record.fp.votes)-total(record.tcp.votes)),true,
                    record.tcp.votes,record.tcp.votes});
            continue;
        }
        auto rejected = record.tcp;
        auto old = compatiblePrevious(record,previous);
        replaceCount(record.tcp,old ? &old->tcp : nullptr,result.snapshot.sourceTime);
        // A previously valid TCP may be incompatible with newly corrected FP.
        // Validate against the effective current account, not the old FP map.
        bool ignored = false;
        if (!invalidTcp(record,result.snapshot.optionalPreferential,ignored).empty())
            replaceCount(record.tcp,nullptr,result.snapshot.sourceTime);
        if (!reason.empty() || record.tcp.status == Status::Restored)
            recordRecovery(result.issues,record,routine ? "incomplete_tcp" : "invalid_tcp",
                rejected,record.tcp,reason.empty() ? "The previously reported TCP record is absent from this source." : reason,routine);
        else if (total(record.fp.votes) > 0)
            result.issues.add({"preferences_pending",record.key,record.district,record.name,
                "First preferences are present but preferences have not reported.",
                "Estimating unreported preferences; no action is normally needed.","",
                double(total(record.fp.votes)),true,{}, {}});
    }
}

void validateCompletion(Result& result) {
    for (auto& record : result.snapshot.records) {
        // Ordinary/PPVC reported FP is normally approximately complete. An
        // unavailable record can never acquire completion from the bad source.
        if (record.fp.status == Status::Current)
            record.fp.complete = record.closed || (record.fp.votes &&
                (result.snapshot.districts.at(record.district).finalised ||
                    (record.role != "declaration" && total(record.fp.votes) > 0)));
        if (record.fp.status == Status::Unavailable) record.fp.complete = record.closed;
        record.tcp.complete = record.tcp.votes && record.fp.complete &&
            (result.snapshot.optionalPreferential || total(record.tcp.votes) >= OrdinaryTcpComparableFraction*total(record.fp.votes));
    }
}

void recoverDistrictTcpAccounts(Result& result, RecordIndex const& previous) {
    // Usually the FP/TCP record comparisons suffice. Near full turnout their
    // small asynchronous-count allowances can still sum past enrolment. This
    // is an independent hard contradiction, including under optional voting.
    for (auto const& [name,district] : result.snapshot.districts) {
        std::int64_t counted = 0;
        for (auto const& record : result.snapshot.records) if (record.district == name) counted += total(record.tcp.votes);
        if (counted < district.enrolment) continue;
        std::int64_t previousTotal = 0;
        for (auto const& record : result.snapshot.records) if (record.district == name) {
            auto old = compatiblePrevious(record,previous);
            if (old) previousTotal += total(old->tcp.votes);
        }
        bool previousFits = previousTotal < district.enrolment;
        for (auto& record : result.snapshot.records) if (record.district == name) {
            auto rejected = record.tcp;
            auto old = compatiblePrevious(record,previous);
            replaceCount(record.tcp,old && previousFits ? &old->tcp : nullptr,result.snapshot.sourceTime);
            bool routine = false;
            if (!invalidTcp(record,result.snapshot.optionalPreferential,routine).empty())
                replaceCount(record.tcp,nullptr,result.snapshot.sourceTime);
            result.issues.add({"tcp_district_exceeds_enrolment",record.key,name,record.name,
                "District TCP reaches or exceeds enrolment.",
                record.tcp.status == Status::Restored ? "Restored earlier preferences; inspect the district's source totals." : "Preferences are unreported for this record; inspect the district's source totals.",
                record.tcp.observedAt,double(counted)-district.enrolment,false,rejected.votes,record.tcp.votes});
        }
    }
}

void checkEffectiveAccounts(Result const& result) {
    // Recovery is upstream of the model's strict accounting checks. A failed
    // check here indicates a programming/cache contract problem, not another
    // occasion to delete votes or catch an arbitrary numerical exception.
    std::map<std::string,std::int64_t> totals;
    std::map<std::string,std::int64_t> tcpTotals;
    for (auto const& record : result.snapshot.records) {
        if (!invalidFp(record).empty()) throw std::logic_error("Recovery left an invalid FP record.");
        bool routine = false;
        if (!invalidTcp(record,result.snapshot.optionalPreferential,routine).empty())
            throw std::logic_error("Recovery left an invalid TCP record.");
        totals[record.district] += total(record.fp.votes);
        tcpTotals[record.district] += total(record.tcp.votes);
    }
    for (auto const& [name,district] : result.snapshot.districts)
        if (totals[name] >= district.enrolment) throw std::logic_error("Recovery left a district outside its enrolment parent.");
        else if (tcpTotals[name] >= district.enrolment) throw std::logic_error("Recovery left TCP outside its enrolment parent.");
}

void validateSourceRecords(Snapshot& current) {
    // This stage checks interpretability, rather than judging how many records
    // are bad. Record-level numeric errors remain recoverable in later stages.
    if (current.election.empty() || current.sourceTime.empty() || current.districts.empty())
        throw std::runtime_error("The live source lacks essential election/time/district structure.");
    current.sourceTime = canonicalTime(current.sourceTime);
    for (auto const& [name,district] : current.districts)
        if (!(district.enrolment > 0) || !std::isfinite(district.enrolment))
            throw std::runtime_error("Invalid configured district enrolment: " + name);
    std::set<std::string> keys;
    for (auto& record : current.records) {
        record.fp.observedAt = record.tcp.observedAt = current.sourceTime;
        if (!current.districts.count(record.district)) throw std::runtime_error("Live count record has no district.");
        if (record.key.empty() || !keys.insert(record.key).second)
            throw std::runtime_error("Ambiguous live count record identity.");
    }
}
}

nlohmann::json Registry::summary() const {
    struct Group { std::size_t records = 0; std::set<std::string> districts; Issue const* worst = nullptr; };
    std::map<std::string,Group> groups;
    for (auto const& [key,issue] : cases) {
        auto& group = groups[issue.type]; ++group.records; group.districts.insert(issue.district);
        if (!group.worst || issue.impact > group.worst->impact ||
            (issue.impact == group.worst->impact && issue.key < group.worst->key)) group.worst = &issue;
    }
    auto rows = nlohmann::json::array();
    for (auto const& [type,group] : groups) {
        auto row = describe(*group.worst);
        row["affected_records"] = group.records; row["affected_districts"] = group.districts.size();
        rows.push_back(std::move(row));
    }
    return rows;
}
nlohmann::json Registry::details() const {
    auto rows = nlohmann::json::array();
    for (auto const& [key,issue] : cases) {
        auto row = describe(issue); row["key"] = issue.key;
        auto encode = [](std::optional<Counts> const& counts) {
            auto values = nlohmann::json::array();
            if (counts) for (auto const& [candidate,count] : *counts) values.push_back({candidate,count});
            return counts ? values : nlohmann::json(nullptr);
        };
        row["rejected"] = encode(issue.rejected); row["effective"] = encode(issue.effective);
        rows.push_back(std::move(row));
    }
    return rows;
}
bool tcpFits(Record const& record, Count const& count, bool optionalPreferential) {
    auto candidate = record;
    candidate.tcp = count;
    bool routine = false;
    return count.votes && invalidTcp(candidate,optionalPreferential,routine).empty();
}

bool fpFits(Record const& record, Count const& count, double enrolment) {
    auto candidate = record;
    candidate.fp = count;
    return count.votes && invalidFp(candidate).empty() && total(count.votes) < enrolment;
}

Result recover(Snapshot current, std::optional<Snapshot> const& previous,
    CountHistoryLookup const& history) {
    validateSourceRecords(current);
    auto old = previous && previous->election == current.election &&
        canonicalTime(previous->sourceTime) < current.sourceTime ? &*previous : nullptr;
    auto records = indexPrevious(old);
    Result result{std::move(current),{}};
    auto fpHistory = selectEarlierCounts(result.snapshot,old,records,history,false);
    recoverFpRecords(result,fpHistory.records);
    recoverDistrictAccounts(result,old,records);
    // TCP compatibility depends on the effective FP, including whole-district
    // rollback. Search older TCP only after that account is settled, and merge
    // the selected records without retaining the historical JSON sequence.
    auto tcpHistory = selectEarlierCounts(result.snapshot,old,records,history,true);
    recoverTcpRecords(result,tcpHistory.records);
    recoverDistrictTcpAccounts(result,tcpHistory.records);
    checkEffectiveAccounts(result);
    validateCompletion(result);
    return result;
}

TurnoutModel::Observation freshObservation(TurnoutModel::Prior const& prior,
    std::vector<TurnoutModel::Unit> const& units, RecordStatuses const& statuses,
    std::string const& sourceTime) {
    TurnoutModel::Observation observation;
    observation.hour = TurnoutModel::sourceHour(sourceTime);
    std::set<std::pair<std::string,std::string>> unavailableGroups;
    for (auto const& unit : units) {
        auto const& district = prior.seats.at(unit.seat);
        auto status = statuses.find(district + "/" + unit.kind + "/" + unit.name);
        bool fresh = status != statuses.end() && status->second.first.fresh() &&
            status->second.first.observedAt == sourceTime;
        auto group = std::make_pair(district,"allocation:" + prior.groups.at(unit.group));
        // Restored counts retain their lower bound in the model, but cannot
        // create new activity, a current zero, or a newly observed pause.
        if (!unit.closed && !fresh) unavailableGroups.insert(group);
        if (unit.closed || !fresh) continue;
        if (unit.kind == "declaration") observation.counts[{district,unit.category}] += unit.counted;
        if (prior.groups[unit.group] != "postal") observation.counts[group] += unit.counted;
    }
    for (auto const& key : unavailableGroups) observation.counts.erase(key);
    return observation;
}
}
