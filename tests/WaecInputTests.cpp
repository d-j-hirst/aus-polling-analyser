// Exercise WA resident-account identities and both feed generations without
// constructing a forecast or launching the GUI.
#include "../ElectionData.h"
#include "../Log.h"
#include "../LiveInputAdapter.h"
#include "../LivePartyMapping.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <fstream>

using namespace Results2;

namespace {
void load(tinyxml2::XMLDocument& document, std::filesystem::path const& path) {
    if (document.LoadFile(path.string().c_str()) != tinyxml2::XML_SUCCESS)
        throw std::runtime_error("Cannot load WA fixture: " + path.string());
}
long long sum(auto const& counts) {
    long long total = 0;
    for (auto const& [candidate, value] : counts) total += value;
    return total;
}
long long parsedFp(Election const& election) {
    long long total = 0;
    for (auto const& [id, booth] : election.booths) total += sum(booth.fpVotes);
    for (auto const& [id, seat] : election.seats)
        for (auto const& [candidate, byType] : seat.fpVotes) total += sum(byType);
    return total;
}
long long parsedTcp(Election const& election) {
    long long total = 0;
    for (auto const& [id, booth] : election.booths) total += sum(booth.tcpVotesCandidate);
    for (auto const& [id, seat] : election.seats)
        for (auto const& [candidate, byType] : seat.tcpVotesCandidate) total += sum(byType);
    return total;
}
long long sourceCount(tinyxml2::XMLDocument const& document, char const* definition) {
    long long total = 0;
    auto root = document.FirstChildElement("ElectionEvent");
    for (auto region = root->FirstChildElement("ElectionRegion"); region; region = region->NextSiblingElement("ElectionRegion"))
        for (auto district = region->FirstChildElement("ElectionDistrict"); district; district = district->NextSiblingElement("ElectionDistrict"))
            for (auto round = district->FirstChildElement("LA")->FirstChildElement("DistrictVotes"); round; round = round->NextSiblingElement("DistrictVotes")) {
                if (!round->Attribute("CountDefinitionCode", definition)) continue;
                for (auto row = round->FirstChildElement(); row; row = row->NextSiblingElement())
                    if (std::string(row->Name()) == "OrdinaryPollingPlaceVotes" || std::string(row->Name()) == "CategoryVotes")
                        for (auto candidate = row->FirstChildElement("CandidateVotes"); candidate; candidate = candidate->NextSiblingElement("CandidateVotes"))
                            total += candidate->IntAttribute("Votes");
            }
    return total;
}

// Check party identity as well as grand totals. The current WA feed and the
// prepared historical baseline assign different meanings to several numeric
// party IDs; an erroneous merge can conserve every vote while changing its party.
void auditPartyTotals(Election const& previous, Election const& current,
    tinyxml2::XMLDocument const& roster, tinyxml2::XMLDocument const& snapshot)
{
    using Source = LivePartyMapping::SourceElection;
    LivePartyMapping::CommissionIds ids;
    std::map<std::string,int> codes{{"ALP",0}, {"LIB",1}, {"GRN",2},
        {"ONP",3}, {"PHON",3}, {"NAT",4}, {"NATS",4}, {"IND",5}, {"UAP",6}};
    auto seed = [&](Election const& election, Source source) {
        for (auto const& [id, party] : election.parties)
            if (codes.contains(party.shortCode)) ids[{source,id}] = codes.at(party.shortCode);
    };
    seed(current,Source::Current);
    seed(previous,Source::Previous);
    auto mapCandidate = [&](int candidateId, Source source) {
        auto party = LivePartyMapping::candidateParty(candidateId,source,previous,current);
        assert(party && party->hasPartyRecord);
        if (party->partyId == Candidate::Independent) return 5; // Aggregate independents for this audit.
        return LivePartyMapping::mapParty({party->source,party->partyId},party->abbreviation,
            7,100000,ids,codes);
    };
    auto checkCandidates = [&](Election const& election, Source source) {
        for (auto const& [id, candidate] : election.candidates) {
            int mapped = mapCandidate(id,source);
            auto const& code = election.parties.at(candidate.party).shortCode;
            if (codes.at(code) != mapped) throw std::runtime_error("Wrong party mapping for " + code);
        }
    };
    checkCandidates(current,Source::Current);
    checkCandidates(previous,Source::Previous);
    checkCandidates(current,Source::Current); // Reading history must not change current identities.

    std::map<std::pair<std::string,std::string>,std::string> sourceParties;
    auto rosterRoot = roster.FirstChildElement("ElectionEvent");
    for (auto region = rosterRoot->FirstChildElement("ElectionRegion"); region; region = region->NextSiblingElement("ElectionRegion"))
        for (auto district = region->FirstChildElement("ElectionDistrict"); district; district = district->NextSiblingElement("ElectionDistrict"))
            for (auto candidate = district->FirstChildElement("LA")->FirstChildElement("Candidate"); candidate; candidate = candidate->NextSiblingElement("Candidate")) {
                auto code = candidate->Attribute("RegisteredPartyAbbreviation");
                sourceParties[{district->Attribute("Name"),candidate->Attribute("BallotPaperName")}] = code && *code ? code : "OTH";
            }
    for (bool tcp : {false,true}) {
        std::map<int,long long> expected, actual;
        auto root = snapshot.FirstChildElement("ElectionEvent");
        for (auto region = root->FirstChildElement("ElectionRegion"); region; region = region->NextSiblingElement("ElectionRegion"))
            for (auto district = region->FirstChildElement("ElectionDistrict"); district; district = district->NextSiblingElement("ElectionDistrict"))
                for (auto round = district->FirstChildElement("LA")->FirstChildElement("DistrictVotes"); round; round = round->NextSiblingElement("DistrictVotes")) {
                    if (!round->Attribute("CountDefinitionCode",tcp ? "LAND" : "LAPC")) continue;
                    for (auto row = round->FirstChildElement(); row; row = row->NextSiblingElement())
                        if (std::string(row->Name()) == "OrdinaryPollingPlaceVotes" || std::string(row->Name()) == "CategoryVotes")
                            for (auto candidate = row->FirstChildElement("CandidateVotes"); candidate; candidate = candidate->NextSiblingElement("CandidateVotes")) {
                                auto const& code = sourceParties.at({district->Attribute("Name"),candidate->Attribute("CandidateBallotPaperName")});
                                int votes = candidate->IntAttribute("Votes");
                                if (votes) expected[codes.at(code)] += votes;
                            }
                }
        for (auto const& [id,booth] : current.booths)
            for (auto const& [candidate,votes] : tcp ? booth.tcpVotesCandidate : booth.fpVotes)
                if (votes) actual[mapCandidate(candidate,Source::Current)] += votes;
        for (auto const& [id,seat] : current.seats)
            for (auto const& [candidate,byType] : tcp ? seat.tcpVotesCandidate : seat.fpVotes)
                for (auto const& [type,votes] : byType)
                    if (votes) actual[mapCandidate(candidate,Source::Current)] += votes;
        if (actual != expected) throw std::runtime_error(tcp ? "Mapped TCP party totals differ from source." : "Mapped FP party totals differ from source.");
    }
}

void fictional() {
    TurnoutModel::Prior prior;
    prior.election="fictional"; prior.seats={"North"}; prior.groups={"early","postal"}; prior.enrolment={4000};
    prior.subdivisions={"State"};
    prior.totals={{2000},{2200}}; prior.counts={{1500,500},{1600,600}};
    TurnoutModel::Unit unit; unit.name="Mixed"; unit.category="early"; unit.kind="ppvc";
    unit.weight=1; unit.counted=200; unit.incremental=true;
    TurnoutModel::Unit postal; postal.name="Postal"; postal.category="Postal"; postal.kind="declaration";
    postal.group=1; postal.weight=1; prior.units={unit,postal};
    auto partial=TurnoutModel::update(prior,{unit,postal},{false},1);
    assert(!partial.complete[0] && partial.unitCounts[0][0]>200);
    auto finalised=TurnoutModel::update(prior,{unit,postal},{true},1);
    assert(finalised.complete[0] && finalised.unitCounts[0][0]==200);
    unit.incremental=false;
    auto ordinary=TurnoutModel::update(prior,{unit,postal},{false},1);
    assert(ordinary.complete[0] && ordinary.unitCounts[0][0]==200);
    tinyxml2::XMLDocument candidates, booths, results;
    assert(candidates.Parse(R"(<ElectionEvent ElectionDate="2025-03-08"><ElectionRegion>
      <ElectionDistrict Name="North"><LA><Candidate BallotPaperName="SAME, A" RegisteredPartyAbbreviation="ALP" RegisteredPartyBallotPaperName="Labor"/></LA></ElectionDistrict>
      <ElectionDistrict Name="South"><LA><Candidate BallotPaperName="SAME, A" RegisteredPartyAbbreviation="ALP" RegisteredPartyBallotPaperName="Labor"/></LA></ElectionDistrict>
      </ElectionRegion></ElectionEvent>)") == tinyxml2::XML_SUCCESS);
    assert(booths.Parse(R"(<ElectionEvent ElectionDate="2025-03-08"><ElectionRegion>
      <ElectionDistrict Name="North" Enrolment="100"><OrdinaryPollingPlace Number="1" Name="Shared Early Polling Place" VenueId="same-guid"/></ElectionDistrict>
      <ElectionDistrict Name="South" Enrolment="100"><OrdinaryPollingPlace Number="1" Name="Shared Early Polling Place" VenueId="same-guid"/></ElectionDistrict>
      </ElectionRegion></ElectionEvent>)") == tinyxml2::XML_SUCCESS);
    Election election("fictional"); election.preloadWaec(candidates, booths);
    assert(election.booths.size() == 2 && election.candidates.size() == 2);
    for (auto const& [id, booth] : election.booths) assert(booth.type == Booth::Type::Ppvc);
    assert(results.Parse(R"(<ElectionEvent ElectionDate="2025-03-08"><ElectionRegion>
      <ElectionDistrict Name="North"><LA><DistrictVotes CountDefinitionCode="LAND"><OrdinaryPollingPlaceVotes OrdinaryPollingPlaceNumber="1"><CandidateVotes CandidateBallotPaperName="SAME, A" Votes="7"/></OrdinaryPollingPlaceVotes></DistrictVotes>
      <DistrictVotes CountDefinitionCode="LAPC"><OrdinaryPollingPlaceVotes OrdinaryPollingPlaceNumber="1"><CandidateVotes CandidateBallotPaperName="SAME, A" Votes="8"/></OrdinaryPollingPlaceVotes><CategoryVotes CategoryCode="POV"><CandidateVotes CandidateBallotPaperName="SAME, A" Votes="3"/></CategoryVotes></DistrictVotes></LA></ElectionDistrict>
      <ElectionDistrict Name="South"><LA><DistrictVotes CountDefinitionCode="LAPC"><OrdinaryPollingPlaceVotes OrdinaryPollingPlaceNumber="1"><CandidateVotes CandidateBallotPaperName="SAME, A" Votes="9"/></OrdinaryPollingPlaceVotes></DistrictVotes></LA></ElectionDistrict>
      </ElectionRegion></ElectionEvent>)") == tinyxml2::XML_SUCCESS);
    assert(election.matchesSnapshotElection(results, Election::Format::WAEC));
    election.updateLive(results, Election::Format::WAEC);
    assert(parsedFp(election) == 20);
    for (auto const& [id, booth] : election.booths) {
        auto const& seat = election.seats.at(booth.parentSeat);
        assert(sum(booth.fpVotes) == (seat.name == "North" ? 8 : 9));
        assert(booth.fpVotes.size() == 1);
        int candidate = booth.fpVotes.begin()->first;
        assert(election.liveCandidateRosters.at(seat.id).contains(candidate));
        if (seat.name == "North") assert(sum(booth.tcpVotesCandidate) == 7);
        else assert(booth.tcpVotesCandidate.empty());
    }
    results.FirstChildElement()->SetAttribute("ElectionDate", "2021-03-13");
    assert(!election.matchesSnapshotElection(results, Election::Format::WAEC));
    // Identical district/number in another year must not manufacture a match.
    candidates.FirstChildElement()->SetAttribute("ElectionDate", "2021-03-13");
    booths.FirstChildElement()->SetAttribute("ElectionDate", "2021-03-13");
    Election previous("fictional"); previous.preloadWaec(candidates,booths);
    for (auto const& [id, booth] : previous.booths) assert(!election.booths.contains(id));
}
}

int main(int argc, char** argv) try {
    logger.setEnabled(false);
    fictional();
    // Optional operator-local audit compares all raw FP rows against the
    // parsed accounts. No private official export is needed for the fixture.
    if (argc > 1) {
        auto root = std::filesystem::path(argv[1]);
        for (auto period : {"prev", "current"}) {
            tinyxml2::XMLDocument candidates, booths;
            load(candidates, root / "downloads" / (std::string("2025wa_candidates_") + period + ".xml"));
            load(booths, root / "downloads" / (std::string("2025wa_booths_") + period + ".xml"));
            Election election("audit"); election.preloadWaec(candidates, booths);
            for (auto const& [id,candidate] : election.candidates)
                if (election.parties.at(candidate.party).shortCode == "IND")
                    assert(candidate.party == Candidate::Independent);
            auto audit = [&](std::filesystem::path const& path) {
                tinyxml2::XMLDocument results; load(results, path);
                assert(election.matchesSnapshotElection(results, Election::Format::WAEC));
                election.updateLive(results, Election::Format::WAEC);
                if (parsedFp(election) != sourceCount(results, "LAPC")) throw std::runtime_error("FP accounting differs: " + path.string());
                if (parsedTcp(election) != sourceCount(results, "LAND")) throw std::runtime_error("TCP accounting differs: " + path.string());
            };
            if (std::string(period) == "prev") audit(root / "downloads/2025wa_results_prev.xml");
            else for (auto const& file : std::filesystem::recursive_directory_iterator(root / "forecasts/2025wa/live-snapshots/feeds"))
                if (file.path().extension() == ".xml") audit(file.path());
        }
        std::ifstream input(root / "forecasts/2025wa/live-inputs/historical-baseline.json");
        nlohmann::json baseline; input >> baseline;
        Election previous("2021wa"); previous.loadWaecBaseline(baseline);
        for (auto const& [id,candidate] : previous.candidates)
            if (previous.parties.at(candidate.party).shortCode == "IND")
                assert(candidate.party == Candidate::Independent);
        assert(previous.seats.size() == 59);
        assert(parsedFp(previous) == 1411990);
        assert(parsedTcp(previous) > 1400000);
        tinyxml2::XMLDocument candidates,roster;
        load(candidates,root / "downloads/2025wa_candidates_current.xml");
        load(roster,root / "downloads/2025wa_booths_current.xml");
        auto current = Election::createWaec(candidates,roster,"2025wa");
        int matches = 0, mixed = 0;
        for (auto const& [id,booth] : current.booths) {
            if (booth.incrementalFp) ++mixed;
            if (!previous.booths.contains(id)) continue;
            auto const& old = previous.booths.at(id);
            assert(old.name == booth.name);
            assert(old.type == booth.type);
            ++matches;
            if (booth.name == "Albany PCYC") {
                assert(old.comparisonSeat == "Albany");
                assert(sum(old.fpVotes) < 263 && sum(old.fpVotes) > 220);
            }
        }
        assert(matches > 650 && mixed == 61);
        std::cout << matches << " checked historical venue links; " << mixed << " incremental early places.\n";
        auto prior = TurnoutModelIO::loadForElection(root,"2025wa");
        assert(prior.prior.units.size() == 979);
        for (auto code : {"20250308180640", "20250308195331", "20250406125852"}) {
            tinyxml2::XMLDocument snapshot;
            load(snapshot,root / "forecasts/2025wa/live-snapshots/feeds" / (std::string("waec-la-verbose-")+code+".xml"));
            current.updateLive(snapshot,Election::Format::WAEC);
            auditPartyTotals(previous,current,candidates,snapshot);
            for (auto const& [id,seat] : current.seats)
                assert(seat.fpFinalised == (std::string(code) == "20250406125852"));
            current.sourceTime = "2025-03-08T19:53:31";
            auto identified = LiveInputAdapter::identify(current,prior,code);
            auto effective = LiveInputRecovery::recover(std::move(identified),{}).snapshot;
            assert(effective.records.size() == 979);
            for (auto const& row : effective.records)
                if (row.role == "ppvc") {
                    assert(row.incremental);
                    // Recovery can withhold district completion when another
                    // source account is missing; declaration does not bypass it.
                    bool expected = effective.districts.at(row.district).finalised && row.fp.votes.has_value();
                    if (row.fp.complete != expected)
                        throw std::runtime_error(std::string("Wrong completion at ")+code+" "+row.key+
                            " declared="+std::to_string(effective.districts.at(row.district).finalised));
                }
            LiveInputAdapter::apply(current,effective);
            for (auto const& [id,seat] : current.seats) {
                std::set<VoteType> types;
                for (auto const& [candidate,values] : seat.fpVotes)
                    for (auto const& [type,count] : values) if (type != VoteType::Ordinary) types.insert(type);
                assert(types.size() == 4);
            }
        }
    }
    std::cout << "WA identities, count rounds, category mappings and per-party source accounting passed.\n";
    return 0;
} catch (std::exception const& error) {
    std::cerr << "WA input test failed: " << error.what() << '\n';
    return 1;
}
