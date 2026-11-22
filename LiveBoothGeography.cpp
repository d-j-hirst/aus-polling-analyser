// Prepare the local One Nation reference once per count. The election input
// supplies federal first preferences at nearby ordinary booths. The no-count
// simulation and turnout prior supply today's level and expected booth sizes.
// Only the resulting references and candidate factors survive into scenarios.
#include "LiveV2.h"
#include "LiveBoothGeographyInput.h"
#include "LiveHistoryScope.h"
#include "PollingProject.h"
#include "SimulationRun.h"
#include <fstream>

namespace {
namespace Geo = LiveBoothGeography;
// A mode contrast uses only genuine counts from a previous candidate in the
// same district. This pools the available old ordinary booths. None of their
// individual One Nation results determine today's differences between booths.
Geo::Place placeFor(LiveV2::Booth const& booth, Geo::RosterPlace const& roster, int party) {
    if (roster.sourceId >= 0 && booth.sourceId != roster.sourceId)
        throw std::runtime_error("Federal geography polling-place ID changed for "+booth.name);
    Geo::Place result{roster.mode,roster.size,roster.federal};
    bool measuredFp = !booth.syntheticPreviousCategory || booth.measuredFpInSyntheticCategory;
    if (booth.sameSeat && measuredFp && booth.node.fpVotesPrevious.contains(party)) {
        result.previousOn = booth.node.fpVotesPrevious.at(party);
        result.previousFormal = booth.node.totalVotesPrevious();
    }
    return result;
}
}

void LiveV2::Election::prepareBoothGeography(TurnoutModel::Prior const& prior, LiveFederalDeclarations::Prior const& declarations) {
    // An SA experiment belongs to its watched run, including its private
    // accepted-count history. A foreground simulation retains the SA inputs
    // associated with the ordinary project-wide history.
    if (prior.election == "2026sa" && LiveHistoryScope::active.empty()) return;
    auto path = Geo::inputPath(project.paths().root(),prior.election,project.automationSettings());
    if (!path) return;
    std::ifstream input(*path);
    if (!input) throw std::runtime_error("Missing prepared federal booth geography: "+path->string());
    auto roster = Geo::readRoster(nlohmann::json::parse(input),prior);
    // SA replays retain their existing blind allocation. The Victorian
    // preparation also distributes each district median across vote types.
    Geo::ModeShifts modeShifts;
    if (prior.election == "2026vic")
        for (auto [type,shift] : declarations) modeShifts.emplace(LiveData::voteTypeName(type),shift);
    auto prepared = std::make_shared<Geo::Prepared>();
    int party = project.parties().indexByShortCode("ONP");
    if (party < 0) party = project.parties().indexByShortCode("ON");
    if (party < 0) throw std::runtime_error("Federal booth geography requires a modelled One Nation party.");
    prepared->party = party;
    prepared->units.resize(readBooths().size());
    prepared->districts.resize(seats.size());
    for (size_t s = 0; s < seats.size(); ++s) {
        auto const& seat = seats[s];
        // The ballot establishes where the party stands. The district's
        // no-count forecast supplies the level for its local estimates.
        std::vector<int> indexes;
        std::vector<Geo::Place> places;
        std::vector<Geo::Return> accepted;
        for (int i : seat.booths) {
            auto const& booth = readBooths()[i];
            if (!booth.node.runningParties.contains(party)) continue;
            auto unit = roster.find({seat.name,booth.name});
            if (unit == roster.end())
                throw std::runtime_error("Missing turnout location for federal geography: "+seat.name+"/"+booth.name);
            indexes.push_back(i);
            places.push_back(placeFor(booth,unit->second,party));
            auto on = booth.node.fpVotesCurrent.find(party);
            accepted.push_back(on != booth.node.fpVotesCurrent.end()
                ? Geo::Return{double(on->second),double(booth.node.totalFpVotesCurrent())} : Geo::Return{});
        }
        if (indexes.empty()) continue;
        if (!seat.node.fpSharesBaseline.contains(party))
            throw std::runtime_error("Missing One Nation district forecast for "+seat.name);
        double seatShare = detransformVoteShare(seat.node.fpSharesBaseline.at(party))*.01;
        auto& district = prepared->districts[s];
        auto references = Geo::prepareDistrict(places,accepted,seatShare,district,modeShifts);
        for (size_t j = 0; j < indexes.size(); ++j) {
            int i = indexes[j];
            auto const& ref = references[j];
            prepared->units[i] = ref;
            auto& n = preparationBooths()[i].node;
            // Store the actual comparison reference in the booth account, so
            // its exported baseline and local deviation describe the same vote.
            // In the five returning seats the real historical counts and raw
            // swing are still available independently of this forecast.
            n.fpSwingsBaseline.erase(party);
            n.fpSharesBaseline[party] = 25*LivePartialCount::logOdds(ref.share);
            if (ref.ordinary && accepted[j].formal > 0) {
                n.geographicPoolingDeviation = 25*district.ordinaryMovement.value();
                n.fpPoolingVotes[party] = accepted[j].formal;
            }
        }
        balanceGeographicOpponents(indexes,places,*prepared);
    }
    boothGeography = std::move(prepared);
}

// The previous booth pattern still informs the other candidates. Allow for
// the known change in votes available to them at each unit, preserving their
// combined district mix. The same factors shape the blind and live projections.
void LiveV2::Election::balanceGeographicOpponents(std::vector<int> const& indexes,
    std::vector<Geo::Place> const& places, Geo::Prepared& geography) {
    std::vector<LiveFpEvidence::GeographicRow> rows;
    std::vector<int> active;
    for (size_t j = 0; j < indexes.size(); ++j) {
        int i = indexes[j];
        if (places[j].size <= 0) continue;
        rows.push_back({projectUnreportedFp(false,i,places[j].size),geography.units[i]->share});
        active.push_back(i);
    }
    if (active.empty()) return;
    auto& district = geography.districts.at(readBooths()[active.front()].parentSeatId);
    district.opponentFactors = LiveFpEvidence::opponentFactors(rows,geography.party);
    for (size_t j = 0; j < rows.size(); ++j) {
        auto& n = preparationBooths().at(active[j]).node;
        auto const blind = LiveNewVotes::normalize(std::move(rows[j].blind));
        auto adjusted = blind;
        Geo::allocate(adjusted,geography.party,rows[j].share,1.,district.opponentFactors);
        for (auto const& [id,share] : adjusted) if (id != geography.party) {
            double offset = 25*(LivePartialCount::logOdds(share)-LivePartialCount::logOdds(blind.at(id)));
            if (n.fpSwingsBaseline.contains(id)) n.fpSwingsBaseline.at(id) += offset;
            else if (n.fpSharesBaseline.contains(id)) n.fpSharesBaseline.at(id) += offset;
        }
    }
}

