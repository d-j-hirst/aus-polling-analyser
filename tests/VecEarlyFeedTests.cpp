// Check the information consumed by the Victorian forecast against independent
// candidate totals in a V2 feed. With no paths, use a small fictional count.
// Local operators can also supply candidate XML, location XML and capture XMLs.
#include "../ElectionData.h"
#include "../LiveInputAdapter.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

using Election = Results2::Election;
using Document = tinyxml2::XMLDocument;
using Node = tinyxml2::XMLElement;

void checkCounts(Node const& group, Results2::Seat::VotesByType const& actual)
{
    for (auto c = group.FirstChildElement("Candidate"); c; c = c->NextSiblingElement("Candidate")) {
        int id = c->FirstChildElement("eml:CandidateIdentifier")->IntAttribute("Id");
        auto const& observed = actual.at(id);
        int total = 0, early = 0;
        for (auto const& [type, count] : observed) total += count;
        for (auto v = c->FirstChildElement("VotesByType")->FirstChildElement("Votes"); v; v = v->NextSiblingElement("Votes")) {
            std::string type = v->Attribute("Type");
            if (type == "Early" || type == "EVCEarly" || type == "MobileVotingEarly") early += v->IntText();
        }
        if (total != c->FirstChildElement("Votes")->IntText() || observed.at(Results2::VoteType::Early) != early)
            throw std::runtime_error("District total or early count differs for candidate " + std::to_string(id));
    }
}

void checkCapture(Election& election, Document const& capture, std::set<int> const& ordinary)
{
    assert(election.matchesSnapshotElection(capture, Election::Format::VEC));
    election.updateLive(capture, Election::Format::VEC);
    auto contests = capture.RootElement()->FirstChildElement("Election")->FirstChildElement("House")->FirstChildElement("Contests");
    for (auto c = contests->FirstChildElement("Contest"); c; c = c->NextSiblingElement("Contest")) {
        auto const& seat = election.seats.at(c->FirstChildElement("PollingDistrictIdentifier")->IntAttribute("Id"));
        if (!c->FirstChildElement("PollingPlaces")) {
            assert(!c->FirstChildElement("FirstPreferences")->FirstChildElement("Candidate")->FirstChildElement("VotesByType"));
            assert(seat.fpVotes.empty() && seat.tcpVotesCandidate.empty());
            continue;
        }
        checkCounts(*c->FirstChildElement("FirstPreferences"), seat.fpVotes);
        if (auto tcp = c->FirstChildElement("TwoCandidatePreferred")) checkCounts(*tcp, seat.tcpVotesCandidate);
        for (int id : seat.booths) assert(ordinary.contains(id));
    }
    assert(election.booths.size() == ordinary.size());
    assert(election.invalidLiveCounts.empty());
}

// Exercise the same private support material the GUI watcher will load. The
// public test remains runnable with the fictional XML alone.
void checkPrepared(std::filesystem::path const& root)
{
    Document candidates, places, zero;
    assert(candidates.LoadFile((root/"downloads/2026vic_candidates.xml").string().c_str()) == tinyxml2::XML_SUCCESS);
    assert(places.LoadFile((root/"downloads/2026vic_booths.xml").string().c_str()) == tinyxml2::XML_SUCCESS);
    assert(zero.LoadFile((root/"downloads/2026vic_zeros.xml").string().c_str()) == tinyxml2::XML_SUCCESS);
    std::ifstream history(root/"analysis/Booth Results/2022vic.json");
    nlohmann::json previous;
    history >> previous;
    auto baseline = Election::createVec(previous, candidates, places, "2022vic");
    for (auto const& [id, seat] : baseline.seats) {
        auto const& booths = previous.at(seat.name).at("booths");
        int fp = 0, tcp = 0;
        for (auto const& b : booths) {
            for (auto const& count : b.at("fp")) fp += count.get<int>();
            for (auto const& count : b.at("tcp")) tcp += count.get<int>();
        }
        assert(fp == seat.totalVotesFp() && tcp == seat.totalVotesTcp({}));
    }
    auto current = Election::createVec(candidates, places, "2026vic");
    assert(current.matchesSnapshotElection(zero, Election::Format::VEC));
    current.updateLive(zero, Election::Format::VEC);
    auto prior = TurnoutModelIO::loadForElection(root, "2026vic");
    auto snapshot = LiveInputAdapter::identify(current, prior, "zero-check");
    assert(snapshot.districts.size() == 88);
    assert(snapshot.records.size() == current.booths.size() + 5 * snapshot.districts.size());
    std::cout << "Matched historical candidate counts and " << snapshot.records.size() << " current turnout services.\n";
}

int main(int argc, char** argv) try
{
    Document candidates, places;
    if (argc == 3 && std::string(argv[1]) == "--prepared") {
        checkPrepared(argv[2]);
        return 0;
    }
    if (argc == 1) {
        candidates.Parse(R"(<EML><CandidateList><EventIdentifier Id="1"/><Election><Contest>
            <PollingDistrictIdentifier Id="2"/><ContestIdentifier><ContestName>Example District</ContestName></ContestIdentifier>
            <Enrolment>1000</Enrolment><Candidate><CandidateIdentifier Id="11"><CandidateName>Example</CandidateName>
            </CandidateIdentifier></Candidate></Contest></Election></CandidateList></EML>)");
        places.Parse(R"(<PollingDistrictList><PollingDistrict><PollingDistrictIdentifier Id="2"/><PollingPlaces>
            <PollingPlace><PollingPlaceIdentifier Id="3" Name="School" Classification="VotingCentre"/></PollingPlace>
            <PollingPlace><PollingPlaceIdentifier Id="4" Name="Early venue" Classification="EarlyVotingCentre"/></PollingPlace>
            <PollingPlace><PollingPlaceIdentifier Id="5" Name="Mobile" Classification="MobileVoting"/></PollingPlace>
            </PollingPlaces></PollingDistrict></PollingDistrictList>)");
    } else {
        assert(argc >= 4);
        assert(candidates.LoadFile(argv[1]) == tinyxml2::XML_SUCCESS && places.LoadFile(argv[2]) == tinyxml2::XML_SUCCESS);
    }
    auto election = Election::createVec(candidates, places, "2026vic");
    std::set<int> ordinary;
    for (auto d = places.RootElement()->FirstChildElement("PollingDistrict"); d; d = d->NextSiblingElement("PollingDistrict"))
        for (auto p = d->FirstChildElement("PollingPlaces")->FirstChildElement("PollingPlace"); p; p = p->NextSiblingElement("PollingPlace")) {
            auto id = p->FirstChildElement("PollingPlaceIdentifier");
            if (id->Attribute("Classification","VotingCentre")) ordinary.insert(id->IntAttribute("Id"));
        }
    if (argc == 1) {
        std::string candidate = R"(<Candidate><eml:CandidateIdentifier Id="11"/><Votes>20</Votes><VotesByType>
            <Votes Type="EVCEarly">7</Votes><Votes Type="MobileVotingEarly">3</Votes><Votes Type="Ordinary">4</Votes>
            <Votes Type="Early">6</Votes></VotesByType></Candidate>)";
        std::string xml = R"(<MediaFeed><eml:EventIdentifier Id="1"><eml:EventName>Example</eml:EventName></eml:EventIdentifier>
            <Election><House><Contests><Contest><PollingDistrictIdentifier Id="2"><Name>Example District</Name></PollingDistrictIdentifier>
            <Enrolment>1000</Enrolment><FirstPreferences>)" + candidate + "</FirstPreferences><TwoCandidatePreferred>" + candidate +
            R"(</TwoCandidatePreferred><PollingPlaces><PollingPlace><PollingPlaceIdentifier Id="4"/></PollingPlace>
            <PollingPlace><PollingPlaceIdentifier Id="5"/></PollingPlace></PollingPlaces></Contest></Contests></House></Election></MediaFeed>)";
        Document capture;
        capture.Parse(xml.c_str());
        checkCapture(election, capture, ordinary);
        checkCapture(election, capture, ordinary);
        assert(election.seats.at(2).tcpVotes.at(-1).at(Results2::VoteType::Early) == 16);
    } else {
        for (int i = 3; i < argc; ++i) {
            Document capture;
            assert(capture.LoadFile(argv[i]) == tinyxml2::XML_SUCCESS);
            checkCapture(election, capture, ordinary);
        }
    }
    std::cout << "VEC district early counts and ordinary-location roster verified.\n";
    return 0;
} catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
