// Component checks for linking historical VEC counts to current polling-place
// IDs. This optional reader test needs TinyXML, but no GUI or forecast run.
#include "../ElectionData.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
using Election = Results2::Election;
using json = nlohmann::json;
using Aliases = std::map<std::string, std::string>;

void preload(tinyxml2::XMLDocument& candidates, tinyxml2::XMLDocument& places)
{
    candidates.Parse(R"(<EML><CandidateList><EventIdentifier Id="99"/><Election>
      <Contest><PollingDistrictIdentifier Id="1"/><ContestIdentifier><ContestName>New District</ContestName></ContestIdentifier><Enrolment>1000</Enrolment>
      <Candidate><CandidateIdentifier Id="11"><CandidateName>Current One</CandidateName></CandidateIdentifier><Affiliation><AffiliationIdentifier Id="5"><RegisteredName>Australian Labor Party - Victorian Branch</RegisteredName></AffiliationIdentifier></Affiliation></Candidate>
      <Candidate><CandidateIdentifier Id="12"><CandidateName>Current Two</CandidateName></CandidateIdentifier><Affiliation><AffiliationIdentifier Id="8"><RegisteredName>Liberal Party of Australia - Victorian Division</RegisteredName></AffiliationIdentifier></Affiliation></Candidate></Contest>
      <Contest><PollingDistrictIdentifier Id="2"/><ContestIdentifier><ContestName>Other District</ContestName></ContestIdentifier><Enrolment>1000</Enrolment></Contest>
      </Election></CandidateList></EML>)");
    places.Parse(R"(<PollingDistrictList>
      <PollingDistrict><PollingDistrictIdentifier Id="1"/><PollingPlaces>
      <PollingPlace><PollingPlaceIdentifier Id="7" Name="Unique"/></PollingPlace>
      <PollingPlace><PollingPlaceIdentifier Id="8" Name="School"/></PollingPlace></PollingPlaces></PollingDistrict>
      <PollingDistrict><PollingDistrictIdentifier Id="2"/><PollingPlaces>
      <PollingPlace><PollingPlaceIdentifier Id="9" Name="School"/></PollingPlace></PollingPlaces></PollingDistrict>
      </PollingDistrictList>)");
}

json history()
{
    return json::parse(R"({"Old": {
      "candidates":{"0":{"name":"Old One","party":"AUSTRALIAN LABOR PARTY"},"1":{"name":"Old Two","party":"LIBERAL"}},
      "booths":{"Unique":{"fp":{"0":20,"1":30},"tcp":{"0":25,"1":25}},
        "School":{"fp":{"0":60,"1":40},"tcp":{"0":65,"1":35}},
        "Postal Votes":{"fp":{"0":8,"1":12},"tcp":{"0":10,"1":10}}}},
      "Other":{"candidates":{"0":{"name":"Other One","party":"AUSTRALIAN LABOR PARTY"},"1":{"name":"Other Two","party":"LIBERAL"}},
      "booths":{"School":{"fp":{"0":10,"1":90},"tcp":{"0":15,"1":85}}}}})");
}

int fpTotal(Election const& election, int boothId)
{
    auto found = election.booths.find(boothId);
    if (found == election.booths.end()) return 0;
    int total = 0;
    for (auto const& [candidate, votes] : found->second.fpVotes) total += votes;
    return total;
}

template<class Counts>
std::map<int,int> byParty(Election const& e, Counts const& counts)
{
    std::map<int,int> result;
    for (auto const& [candidate, votes] : counts) result[e.candidates.at(candidate).party] += votes;
    return result;
}

std::map<std::string,std::pair<int,int>> seatTotals(Election const& e)
{
    std::map<std::string,std::pair<int,int>> result;
    for (auto const& [id, seat] : e.seats)
        result[seat.name] = {seat.totalVotesFp(), seat.totalVotesTcp({})};
    return result;
}

void checkFictionalLinks()
{
    tinyxml2::XMLDocument candidates, places;
    preload(candidates,places);
    auto source = history();
    auto before = Election::createVec(source,candidates,places,"fictional");
    auto after = Election::createVec(source,candidates,places,"fictional",{{"New","Old"}});
    assert(fpTotal(before,7) == 50 && fpTotal(after,7) == 50);
    assert(fpTotal(before,8) == 0 && fpTotal(after,8) == 100);
    assert(fpTotal(before,9) == 100 && fpTotal(after,9) == 100);
    assert((byParty(after,after.booths.at(8).fpVotes) == std::map<int,int>{{5,60},{8,40}}));
    assert((byParty(after,after.booths.at(8).tcpVotesCandidate) == std::map<int,int>{{5,65},{8,35}}));
    assert((byParty(after,after.booths.at(9).fpVotes) == std::map<int,int>{{5,10},{8,90}}));
    assert(after.seats.at(after.booths.at(8).parentSeat).name == "Old");
    assert(seatTotals(before) == seatTotals(after));
    assert((seatTotals(after).at("Old") == std::pair<int,int>{170,170}));

    // Two configured successors, or a predecessor that is still a current
    // district, cannot silently send the old School votes to either one.
    auto conflicting = Election::createVec(source,candidates,places,"fictional",{{"New","Old"},{"Other","Old"}});
    assert(fpTotal(conflicting,8) == 0 && fpTotal(conflicting,9) == 100);
    assert(seatTotals(conflicting) == seatTotals(before));
    auto retainedName = Election::createVec(source,candidates,places,"fictional",{{"New","Other"}});
    assert(fpTotal(retainedName,8) == 0 && fpTotal(retainedName,9) == 0);
    assert(seatTotals(retainedName) == seatTotals(before));

    // The existing rule for two historical accounts converging on one unique
    // current venue must still keep both histories unattached, without pooling.
    source["Extra"] = source["Old"];
    source["Extra"]["booths"] = {{"Unique",source["Old"]["booths"]["Unique"]}};
    auto converging = Election::createVec(source,candidates,places,"fictional",{{"New","Old"}});
    assert(fpTotal(converging,7) == 0);
    assert(fpTotal(converging,8) == 100 && fpTotal(converging,9) == 100);
}

// Optional local evidence check: compare the entire original/fixed historical
// account using received preloads and configured predecessor names. These private
// downloads are not required by the fictional fixtures or a public checkout.
void checkLocalArchive(std::filesystem::path const& root)
{
    tinyxml2::XMLDocument candidates, places;
    assert(candidates.LoadFile((root/"downloads/2022vic_candidates.xml").string().c_str()) == tinyxml2::XML_SUCCESS);
    assert(places.LoadFile((root/"downloads/2022vic_booths.xml").string().c_str()) == tinyxml2::XML_SUCCESS);
    std::ifstream input(root/"analysis/Booth Results/2018vic.json");
    json source; input >> source;
    Aliases aliases;
    std::ifstream settings(root/"analysis/seats/2022vic.txt");
    std::string line, current;
    while (std::getline(settings,line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with('#')) current = line.substr(1);
        if (line.starts_with("sPreviousName=")) aliases[current] = line.substr(14);
    }
    auto before = Election::createVec(source,candidates,places,"2018vic");
    auto after = Election::createVec(source,candidates,places,"2018vic",aliases);
    auto roster = Election::createVec(candidates,places,"2022vic");
    assert(seatTotals(before) == seatTotals(after));
    std::set<std::pair<std::string,std::string>> changed;
    for (auto const& [id, booth] : roster.booths) {
        int oldFp = fpTotal(before,id), newFp = fpTotal(after,id);
        if (oldFp == newFp) continue;
        auto const& seat = roster.seats.at(booth.parentSeat).name;
        changed.emplace(seat,booth.name);
        assert(oldFp == 0);
        auto const& original = source.at(aliases.at(seat)).at("booths").at(booth.name);
        int expected = 0;
        for (auto const& votes : original.at("fp")) expected += votes.get<int>();
        assert(newFp == expected);
        int tcp = 0, expectedTcp = 0;
        for (auto const& [candidate,votes] : after.booths.at(id).tcpVotesCandidate) tcp += votes;
        for (auto const& votes : original.at("tcp")) expectedTcp += votes.get<int>();
        assert(tcp == expectedTcp);
        assert(after.seats.at(after.booths.at(id).parentSeat).name == aliases.at(seat));
        std::cout << seat << " / " << booth.name << ": restored " << newFp << " FP, " << tcp << " TCP\n";
    }
    assert((changed == std::set<std::pair<std::string,std::string>>{
        {"Berwick","Avebury"},{"Berwick","Berwick Lodge"},{"Berwick","Harkaway"},{"Berwick","Timbarra"},
        {"Eureka","Caledonian"},{"Ashwood","Starling"},{"Glen Waverley","Mitcham South"},
        {"Glen Waverley","Nunawading South"},{"Glen Waverley","Vermont North"}}));
    std::cout << "Nine restored links; complete historical district totals unchanged.\n";
}
}

int main(int argc, char** argv)
{
    checkFictionalLinks();
    if (argc == 2) checkLocalArchive(argv[1]);
    std::cout << "VEC historical matching checks passed.\n";
}
