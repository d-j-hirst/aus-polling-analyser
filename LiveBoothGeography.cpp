// Prepare booth expectations from previous election results around each site.
// Federal booths inform One Nation's ordinary reference. In Victorian early
// centres, the preceding state election informs local FP and classic TPP.
// The forecast supplies the current district level and expected booth sizes.
#include "LiveV2.h"
#include "LiveBoothGeographyInput.h"
#include "LiveVecCentreGeography.h"
#include "LiveNarracanPrimaries.h"
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

// Bind the reporting-district account before preparing *any* geography.
// The federal One Nation adjustment can then balance its opponents against
// the same home early-voter population that was used before the split.
void LiveV2::Election::linkVecCentres(TurnoutModelIO::Artifact const& input) {
    if (input.earlyCentres.empty()) return;
    for (auto const& seat : seats) {
        auto const& roster = input.earlyCentres.at(seat.name);
        int parent = -1;
        for (int i : seat.booths)
            if (readBooths()[i].voteType == Results2::VoteType::EvcEarly) parent = i;
        if (parent < 0) throw std::runtime_error("Missing district early-vote account in "+seat.name);
        size_t found = 0;
        for (int i : seat.booths) {
            auto& booth = preparationBooths()[i];
            if (!booth.incrementalFp || !roster.contains(booth.name)) continue;
            booth.evcDistrictAccount = parent;
            booth.evcDisplayName = roster.at(booth.name).displayName;
            ++found;
        }
        if (found != roster.size()) throw std::runtime_error("Incomplete named early-centre roster in "+seat.name);
    }
}

// The common forecast supplies the level (including independents, One
// Nation and any previous district preference evidence). Only the differences
// between centres come from the five matched ordinary booths around each
// location. Store forecast shares separately from last election's counts:
// a new venue has no measured swing against these geographic estimates.
void LiveV2::Election::prepareVecCentreEstimates(TurnoutModelIO::Artifact const& input) {
    namespace Evc = LiveVecCentreGeography;
    if (input.earlyCentres.empty()) return;
    for (auto const& seat : seats) {
        std::vector<int> indexes;
        std::vector<Evc::Centre> sites;
        for (int i : seat.booths) {
            auto const& booth = readBooths()[i];
            if (booth.evcDistrictAccount < 0) continue;
            indexes.push_back(i);
            sites.push_back(input.earlyCentres.at(seat.name).at(booth.name));
        }
        int parent = readBooths()[indexes.at(0)].evcDistrictAccount;
        auto const& account = readBooths()[parent];
        auto district = projectUnreportedFp(false,parent,1.);
        auto factors = Evc::primaryFactors(sites,district,natPartyIndex,run.grnPartyIndex);
        std::vector<Evc::Shares> ballotShares;
        for (size_t j=0; j<indexes.size(); ++j) {
            auto& booth = preparationBooths()[indexes[j]];
            booth.evcPrimaryFactors = factors[j];
            booth.evcOneNationContrast = sites[j].oneNationContrast;
            auto local = Evc::allocate(district,factors[j],natPartyIndex,run.grnPartyIndex,
                boothGeography ? boothGeography->party : -1,booth.evcOneNationContrast);
            Evc::Shares ballot;
            for (int candidate : booth.node.runningParties) {
                int id = candidate == seat.independentPartyIndex ? run.indPartyIndex : candidate;
                double share = local.at(id);
                ballot[candidate] = share;
                booth.node.fpSwingsBaseline.erase(id);
                booth.node.fpSharesBaseline[id] = 25*LivePartialCount::logOdds(share);
            }
            ballotShares.push_back(std::move(ballot));
        }
        auto flows = classicPreferenceFlows(account,coalitionFinalist(account));
        auto offsets = Evc::preferenceOffsets(sites,ballotShares,flows,preferenceRateOffset(account,false).value_or(0)/25);
        for (size_t j=0; j<indexes.size(); ++j) {
            auto& booth = preparationBooths()[indexes[j]];
            booth.evcPreferenceOffset = offsets[j];
            booth.evcOpeningTpp = LiveNewVotes::firstShare(ballotShares[j],flows,offsets[j]);
        }
    }
}

void LiveV2::Election::shareVecCentreReturns(std::vector<LiveNewVotes::Shares>& contexts) {
    // FP and TCP can arrive separately. Only comparable, measured preferences
    // inform the shared flow correction; first preferences alone enter through
    // the projected primary mix. The standard partial-count projection uses
    // each venue's own batch, so exclude it from the sibling comparison.
    if (run.getTermCode() != "2026vic") return;
    for (auto const& seat : seats) {
        std::vector<int> venues;
        std::vector<LiveNewVotes::Shares> original;
        double districtSize = 0;
        for (int i : seat.booths) {
            auto const& booth = readBooths()[i];
            if (booth.evcDistrictAccount < 0 && booth.voteType != Results2::VoteType::EvcEarly) continue;
            venues.push_back(i);
            original.push_back(contexts[i]);
            districtSize += turnoutFpTarget(i);
        }
        for (int i : venues) {
            auto& target = preparationBooths()[i];
            if (target.evcDistrictAccount < 0 || contexts[i].empty()) continue;
            LiveNewVotes::Shares counted, expected;
            double flowVotes = 0, weightedShift = 0;
            for (size_t j=0; j<venues.size(); ++j) {
                int otherIndex = venues[j];
                if (otherIndex == i || original[j].empty()) continue;
                auto const& other = readBooths()[otherIndex];
                double size = other.node.totalFpVotesCurrent();
                for (auto [id,n] : other.node.fpVotesCurrent) counted[id] += n;
                for (auto [id,share] : original[j]) expected[id] += share*size;
                auto const& flow = other.tppPreferenceEvidence();
                if (other.evcDistrictAccount >= 0 && flow && flow->first == 0 && flow->second == coalitionFinalist(target)) {
                    flowVotes += flow->preferenceVotes;
                    weightedShift += flow->preferenceVotes*(flow->offset-other.evcPreferenceOffset);
                }
            }
            contexts[i] = LiveVecCentreGeography::fromOtherCentres(std::move(contexts[i]),counted,expected,districtSize);
            // Use the existing matched-batch preference weight. Keep the
            // count-blind offset intact for other venues' comparisons.
            target.evcSiblingPreferenceShift = weightedShift/(flowVotes+LivePartialCount::PriorPreferenceVotes);
        }
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
    int const seatIndex=readBooths()[active.front()].parentSeatId;
    auto& district = geography.districts.at(seatIndex);
    district.opponentFactors = LiveFpEvidence::opponentFactors(rows,geography.party);
    if (seats.at(seatIndex).name=="Narracan" && run.getTermCode()=="2026vic")
        prepareNarracanPrimaryPattern(active,rows,geography);
    for (size_t j = 0; j < rows.size(); ++j) {
        auto& n = preparationBooths().at(active[j]).node;
        auto const blind = LiveNewVotes::normalize(std::move(rows[j].blind));
        auto adjusted = blind;
        Geo::allocate(adjusted,geography.party,rows[j].share,1.,district.factorsFor(active[j]));
        for (auto const& [id,share] : adjusted) if (id != geography.party) {
            if (district.olderPrimaries && district.olderPrimaries->covers(id)) {
                // The comparison is now the expected share at this site. An
                // apparent swing from the supplementary ballot would mix two
                // different starting points. Keep actual counts intact.
                n.fpSwingsBaseline.erase(id);
                n.fpSharesBaseline[id]=25*LivePartialCount::logOdds(share);
                continue;
            }
            double offset = 25*(LivePartialCount::logOdds(share)-LivePartialCount::logOdds(blind.at(id)));
            if (n.fpSwingsBaseline.contains(id)) n.fpSwingsBaseline.at(id) += offset;
            else if (n.fpSharesBaseline.contains(id)) n.fpSharesBaseline.at(id) += offset;
        }
    }
}

// Most seats retain the previous state election's local pattern. In Narracan
// compare ordinary sites to 2018, when Labor and the Greens both stood.
// Use frozen projected turnout and the blind federal ON allocation throughout.
// Unmatched sites and declarations share the district adjustment; the named
// early centres subsequently divide their parent account in the usual way.
void LiveV2::Election::prepareNarracanPrimaryPattern(std::vector<int> const& active,
    std::vector<LiveFpEvidence::GeographicRow> const& rows, Geo::Prepared& geography) {
    namespace Historical = LiveNarracanPrimaries;
    auto const& seat=seats.at(readBooths().at(active.front()).parentSeatId);
    auto& district=geography.districts.at(readBooths().at(active.front()).parentSeatId);
    int const greens=run.grnPartyIndex;
    if (!seat.node.fpSharesBaseline.contains(0) || !seat.node.fpSharesBaseline.contains(greens)) return;
    auto path=Historical::historyPath(project.paths().root());
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Missing 2018 Narracan booth comparison: "+path.string());
    auto history=Historical::readHistory(nlohmann::json::parse(input).at(seat.name));
    Geo::District::OlderPrimaries older{greens,{},{}};
    std::vector<Historical::Venue> venues;
    double formal=0., available=0.;
    for (size_t j=0;j<active.size();++j) {
        int index=active[j];
        auto const& b=readBooths().at(index);
        auto votes=rows[j].blind;
        double size=LiveFpEvidence::total(votes);
        Geo::allocate(votes,geography.party,rows[j].share,size,district.opponentFactors);
        double pool=size-votes.at(geography.party);
        double labor=LiveFpEvidence::value(votes,0), green=LiveFpEvidence::value(votes,greens);
        Historical::Venue v{{labor,green,std::max(0.,pool-labor-green)},{}};
        // Match the current site to Narracan in 2018 directly. It need not
        // also have appeared on the supplementary election's roster.
        if (b.voteType==Results2::VoteType::Ordinary && b.boothType==Results2::Booth::Type::Normal) {
            auto found=history.find(b.name);
            if (found!=history.end()) {
                v.history=found->second;
                older.counts.emplace(index,found->second);
            }
        }
        venues.push_back(std::move(v));
        formal+=size;
        available+=pool;
    }
    double labor=formal*detransformVoteShare(seat.node.fpSharesBaseline.at(0))*.01;
    double green=formal*detransformVoteShare(seat.node.fpSharesBaseline.at(greens))*.01;
    if (available<=labor+green) return; // Marginal district medians cannot always coexist.
    auto fit=Historical::fit(venues,{labor,green,available-labor-green});
    if (!fit) return; // No usable two-party comparison: retain the normal geographic allocation.
    auto factors=[&](Historical::Mix const& scale) {
        auto combined=district.opponentFactors;
        // The other candidates share the common denominator. Multiplying
        // just ALP and GRN relative to that group preserves its internal mix.
        for (auto [id,j] : {std::pair{0,0},std::pair{greens,1}})
            combined[id]=getAt(district.opponentFactors,id,1.)*scale[j]/scale[2];
        return combined;
    };
    for (size_t j=0;j<active.size();++j) if (venues[j].history)
        older.factors.emplace(active[j],factors(fit->local[j]));
    district.opponentFactors=factors(fit->common);
    district.olderPrimaries=std::move(older);
}

