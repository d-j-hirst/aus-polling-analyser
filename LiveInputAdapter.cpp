#include "LiveInputAdapter.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace {
using namespace LiveInputRecovery;
using VoteType = Results2::VoteType;

VoteType declarationType(std::string const& name) {
    for (int i = int(VoteType::Absent); i <= int(VoteType::MarkedAsVoted); ++i)
        if (Results2::voteTypeName(VoteType(i)) == name) return VoteType(i);
    throw std::runtime_error("Unknown configured declaration service: " + name);
}

Results2::Seat const& districtByName(Results2::Election const& election, std::string const& name) {
    auto found = std::find_if(election.seats.begin(), election.seats.end(), [&](auto const& row) {
        return row.second.name == name;
    });
    if (found == election.seats.end()) throw std::runtime_error("Live source lacks district: " + name);
    return found->second;
}

Count declarationCount(Results2::Seat::VotesByType const& values, VoteType type) {
    Counts counts;
    for (auto const& [candidate, types] : values) {
        auto found = types.find(type);
        if (found != types.end()) counts.emplace(candidate, found->second);
    }
    // A category absent from the source has no measured count. Explicit zero
    // candidate vectors remain available and are distinguishable from absence.
    Count result;
    if (!counts.empty()) result.votes = std::move(counts);
    return result;
}

void attachParseError(Count& count, Results2::Election const& election, std::string const& key, bool tcp) {
    auto error = election.invalidLiveCounts.find({key,tcp});
    if (error == election.invalidLiveCounts.end()) return;
    // Parser placeholders must not become measured zeros in any calculation.
    count.votes.reset(); count.error = error->second;
}

Record identifyUnit(Results2::Election const& election, TurnoutModel::Prior const& prior,
    TurnoutModel::Unit const& unit) {
    auto const& seat = districtByName(election, prior.seats.at(unit.seat));
    Record record;
    record.district = seat.name; record.name = unit.name; record.role = unit.kind;
    record.closed = unit.closed;
    record.key = seat.name + "/" + unit.kind + "/" + unit.name;
    std::set<int> roster;
    auto knownRoster = election.liveCandidateRosters.find(seat.id);
    if (knownRoster != election.liveCandidateRosters.end()) roster = knownRoster->second;
    if (roster.empty()) {
        for (auto const& [candidate, counts] : seat.fpVotes) roster.insert(candidate);
        for (int id : seat.booths) for (auto const& [candidate, count] : election.booths.at(id).fpVotes) roster.insert(candidate);
    }
    record.candidates.assign(roster.begin(),roster.end());
    if (record.candidates.empty()) throw std::runtime_error("Live source has no candidate identities for " + seat.name);
    // Include candidate names and affiliations as well as IDs. Reusing a local
    // numerical identifier for a changed contest cannot make old counts fit it.
    record.identity = std::to_string(election.id) + "/" + std::to_string(seat.id);
    for (int id : record.candidates) {
        auto const& candidate = election.candidates.at(id);
        record.identity += "/" + std::to_string(id) + ":" + candidate.name + ":" + std::to_string(candidate.party);
    }
    std::string rawKey;
    if (unit.kind == "declaration") {
        auto type = declarationType(unit.name);
        rawKey = Results2::Election::countKey(seat.id,0,type);
        record.identity += "/category:" + std::to_string(int(type));
        record.fp = declarationCount(seat.fpVotes,type);
        record.tcp = declarationCount(seat.tcpVotesCandidate,type);
    } else {
        Results2::Booth const* booth = nullptr;
        for (int id : seat.booths) if (election.booths.at(id).name == unit.name) {
            if (booth) throw std::runtime_error("Ambiguous live booth identity: " + record.key);
            booth = &election.booths.at(id);
        }
        if (!booth && !unit.closed) throw std::runtime_error("Missing live booth identity: " + record.key);
        if (booth) {
            bool ppvc = booth->type == Results2::Booth::Type::Ppvc;
            if (ppvc != (unit.kind == "ppvc")) throw std::runtime_error("Live booth role changed: " + record.key);
            record.identity += "/booth:" + std::to_string(booth->id) + ":" + std::to_string(int(booth->type));
            rawKey = Results2::Election::countKey(seat.id,booth->id,VoteType::Ordinary);
            if (!booth->fpVotes.empty()) record.fp.votes = Counts(booth->fpVotes.begin(),booth->fpVotes.end());
            if (!booth->tcpVotesCandidate.empty()) record.tcp.votes = Counts(booth->tcpVotesCandidate.begin(),booth->tcpVotesCandidate.end());
        }
    }
    attachParseError(record.fp,election,rawKey,false);
    attachParseError(record.tcp,election,rawKey,true);
    if (election.liveCountsRead) {
        if (!election.presentLiveCounts.count({rawKey,false})) record.fp.votes.reset();
        if (!election.presentLiveCounts.count({rawKey,true})) record.tcp.votes.reset();
    }
    for (auto const& row : prior.counts)
        record.expected += row.at(unit.seat*prior.groups.size()+unit.group)*unit.weight/prior.counts.size();
    return record;
}

void applyRecord(Results2::Election& election, Results2::Seat& seat, Record const& record) {
    auto type = record.role == "declaration" ? declarationType(record.name) : VoteType::Ordinary;
    Results2::Booth* booth = nullptr;
    if (record.role != "declaration") for (int id : seat.booths)
        if (election.booths.at(id).name == record.name) booth = &election.booths.at(id);
    if (booth) { booth->fpVotes.clear(); booth->tcpVotes.clear(); booth->tcpVotesCandidate.clear(); booth->tppVotes.clear(); }
    // The older share model needs the contest's candidate identities even when
    // a count is unavailable. Its zero placeholders carry no counted lower
    // bound; the separate availability tags preserve the missing-data meaning.
    Counts fp = record.fp.votes.value_or(Counts{});
    for (int candidate : record.candidates) if (!fp.count(candidate)) fp[candidate] = 0;
    for (auto const& [candidate,count] : fp) {
        seat.fpVotes[candidate][type] += count;
        if (booth) booth->fpVotes[candidate] = count;
    }
    if (record.tcp.votes) for (auto const& [candidate,count] : *record.tcp.votes) {
        int party = election.candidates.at(candidate).party;
        seat.tcpVotesCandidate[candidate][type] += count;
        seat.tcpVotes[party][type] += count;
        if (booth) { booth->tcpVotesCandidate[candidate] = count; booth->tcpVotes[party] += count; }
    }
    election.liveCountStatus[seat.name + "/" + record.role + "/" + record.name] = {record.fp.metadata(),record.tcp.metadata()};
}

void validateElectionIdentity(Results2::Election const& election, TurnoutModelIO::Artifact const& artifact) {
    // Structural identity and source-clock checks precede local recovery. A
    // source that cannot identify its election/contests is not a new forecast.
    TurnoutModel::sourceHour(election.sourceTime);
    if (election.termCode != artifact.prior.election)
        throw std::runtime_error("Live source and turnout configuration identify different elections.");
    std::map<std::string,double> districts;
    for (auto const& [id,seat] : election.seats) {
        if (!districts.emplace(seat.name,double(seat.totalVotesFp()) + seat.totalVotesTcp({})).second)
            throw std::runtime_error("Duplicate live district identity: " + seat.name);
    }
    TurnoutModelIO::validateDistrictPopulation(artifact,districts);
}

void requireMappedCategories(Results2::Election const& election, Snapshot const& identified) {
    // The normal configured population covers all measured categories. A newly
    // named positive category cannot silently disappear when we rebuild totals:
    // it needs an explicit mapping before that source can be interpreted.
    std::set<std::string> mapped;
    for (auto const& record : identified.records) mapped.insert(record.key);
    for (auto const& [id,seat] : election.seats) {
        if (!identified.districts.count(seat.name)) continue;
        for (auto const* counts : {&seat.fpVotes,&seat.tcpVotesCandidate})
            for (auto const& [candidate,types] : *counts) for (auto const& [type,count] : types) {
                if (type == VoteType::Ordinary || count == 0) continue;
                if (!mapped.count(seat.name + "/declaration/" + Results2::voteTypeName(type)))
                    throw std::runtime_error("Measured live category has no configured turnout mapping: " + seat.name + "/" + Results2::voteTypeName(type));
            }
    }
}

void recordUnreadableSummaries(Results2::Election const& election, Result& result) {
    // Service vectors own the effective account. A malformed ordinary district
    // summary can therefore be rebuilt rather than replacing valid booth counts,
    // but the source error must still be visible to the operator.
    for (auto const& [id,seat] : election.seats) for (bool tcp : {false,true}) {
        auto error = election.invalidLiveCounts.find({Results2::Election::countKey(id,0,VoteType::Ordinary),tcp});
        if (error == election.invalidLiveCounts.end()) continue;
        Counts counts;
        for (auto const& record : result.snapshot.records) if (record.district == seat.name) {
            auto const& effective = tcp ? record.tcp : record.fp;
            if (effective.votes) for (auto const& [candidate,count] : *effective.votes) counts[candidate] += count;
        }
        auto name = tcp ? "TCP summary" : "FP summary";
        result.issues.add({"invalid_summary",seat.name + "/" + name,seat.name,name,error->second,
            "Rebuilt the summary from validated service records; inspect the source summary.","",
            double(total(counts)),false,{},std::move(counts)});
    }
}
}

namespace LiveInputAdapter {
LiveInputRecovery::Snapshot identify(Results2::Election const& election,
    TurnoutModelIO::Artifact const& artifact, std::string const& sourceHash) {
    validateElectionIdentity(election,artifact);
    Snapshot result;
    result.election = election.termCode; result.sourceTime = canonicalTime(election.sourceTime);
    result.sourceHash = sourceHash;
    result.optionalPreferential = artifact.optionalPreferential;
    for (std::size_t s = 0; s < artifact.prior.seats.size(); ++s) {
        auto const& seat = districtByName(election,artifact.prior.seats[s]);
        result.districts.emplace(seat.name,District{artifact.prior.enrolment[s],seat.fpFinalised});
    }
    for (auto const& unit : artifact.prior.units) result.records.push_back(identifyUnit(election,artifact.prior,unit));
    requireMappedCategories(election,result);
    return result;
}

void apply(Results2::Election& election, Snapshot const& effective) {
    // All seat summaries are rebuilt from the same effective records as the
    // booths. Current summary totals must never override restored constituents.
    election.sourceTime = effective.sourceTime;
    for (auto& [id,seat] : election.seats) {
        auto district = effective.districts.find(seat.name);
        if (district == effective.districts.end()) continue;
        seat.fpVotes.clear(); seat.tcpVotes.clear(); seat.tcpVotesCandidate.clear(); seat.tppVotes.clear();
        seat.fpFinalised = district->second.finalised;
        for (auto const& record : effective.records) if (record.district == seat.name) applyRecord(election,seat,record);
    }
}

Result prepare(Results2::Election& election, TurnoutModelIO::Artifact const& artifact,
    std::filesystem::path const& root, std::filesystem::path const& source) {
    auto identified = identify(election,artifact,fingerprint(source));
    auto folder = directory(root,election.termCode);
    auto previous = loadCompatiblePrevious(folder,identified);
    auto result = recover(std::move(identified),previous,[&](Snapshot const& effective, Snapshot const* old, HistoryAccount account) {
        return loadEarlierCounts(folder,effective,old,account);
    });
    recordUnreadableSummaries(election,result);
    apply(election,result.snapshot);
    return result;
}
}
