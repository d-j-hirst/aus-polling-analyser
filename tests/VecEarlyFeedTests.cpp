// Check the information consumed by the Victorian forecast against independent
// candidate totals in a V2 feed. With no paths, use a small fictional count.
// Local operators can also supply candidate XML, location XML and capture XMLs.
#include "../ElectionData.h"
#include "../LiveInputAdapter.h"
#include "../LiveBoothGeographyInput.h"
#include "../LiveFederalDeclarations.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

using Election = Results2::Election;
using Document = tinyxml2::XMLDocument;
using Node = tinyxml2::XMLElement;
using Type = Results2::VoteType;

void checkCounts(Node const& group, Results2::Seat::VotesByType const& actual)
{
    for (auto c = group.FirstChildElement("Candidate"); c; c = c->NextSiblingElement("Candidate")) {
        int id = c->FirstChildElement("eml:CandidateIdentifier")->IntAttribute("Id");
        auto breakdown = c->FirstChildElement("VotesByType");
        if (!breakdown) {
            assert(!actual.contains(id)); // A district total alone supplies no allocation.
            continue;
        }
        auto const& observed = actual.at(id);
        int total = 0;
        for (auto const& [type, count] : observed) total += count;
        std::map<Type,int> early;
        for (auto v = breakdown->FirstChildElement("Votes"); v; v = v->NextSiblingElement("Votes")) {
            std::string type = v->Attribute("Type");
            if (type == "Early") early[Type::OtherEarly] = v->IntText();
            if (type == "EVCEarly") early[Type::EvcEarly] = v->IntText();
            if (type == "MobileVotingEarly") early[Type::MobileEarly] = v->IntText();
        }
        assert(total == c->FirstChildElement("Votes")->IntText() && !observed.contains(Type::Early));
        for (auto type : {Type::EvcEarly,Type::MobileEarly,Type::OtherEarly}) {
            assert(observed.contains(type) == early.contains(type));
            if (early.contains(type)) assert(observed.at(type) == early.at(type));
        }
    }
}

void checkCapture(Election& election, Document const& capture, std::set<int> const& ordinary)
{
    assert(election.matchesSnapshotElection(capture, Election::Format::VEC));
    election.updateLive(capture, Election::Format::VEC);
    auto contests = capture.RootElement()->FirstChildElement("Election")->FirstChildElement("House")->FirstChildElement("Contests");
    for (auto c = contests->FirstChildElement("Contest"); c; c = c->NextSiblingElement("Contest")) {
        auto const& seat = election.seats.at(c->FirstChildElement("PollingDistrictIdentifier")->IntAttribute("Id"));
        checkCounts(*c->FirstChildElement("FirstPreferences"), seat.fpVotes);
        if (auto tcp = c->FirstChildElement("TwoCandidatePreferred")) checkCounts(*tcp, seat.tcpVotesCandidate);
        for (int id : seat.booths) assert(ordinary.contains(id));
    }
    assert(election.booths.size() == ordinary.size());
    assert(election.invalidLiveCounts.empty());
}

// Exercise the same private support material the GUI watcher will load. The
// public test remains runnable with the fictional XML alone.
void checkPrepared(std::filesystem::path const& root, std::filesystem::path const& forecast)
{
    Document candidates, places, zero;
    assert(candidates.LoadFile((root/"downloads/2026vic_candidates.xml").string().c_str()) == tinyxml2::XML_SUCCESS);
    assert(places.LoadFile((root/"downloads/2026vic_booths.xml").string().c_str()) == tinyxml2::XML_SUCCESS);
    assert(zero.LoadFile((root/"downloads/2026vic_zeros.xml").string().c_str()) == tinyxml2::XML_SUCCESS);
    auto load = [](std::filesystem::path const& path) { return nlohmann::json::parse(std::ifstream(path)); };
    auto original = load(root/"analysis/Booth Results/2022vic.json");
    auto previous = load(root/"forecasts/2026vic/live-inputs/historical-baseline.json");
    auto baseline = Election::createVec(previous, candidates, places, "2022vic");
    for (auto const& [id, seat] : baseline.seats) {
        auto const& booths = previous.at(seat.name).at("booths");
        auto const& pooled = original.at(seat.name).at("booths").at("Early Votes");
        assert(!booths.contains("Early Votes"));
        for (auto stage : {"fp","tcp"}) for (auto const& [candidate,votes] : pooled.at(stage).items()) {
            int split = 0;
            for (auto name : {"EVC Early Votes","Mobile Early Votes","Other Early Votes"})
                split += booths.at(name).at(stage).at(candidate).get<int>();
            assert(split == votes.get<int>());
        }
        assert(seat.measuredFpInSyntheticTypes.contains(Type::EvcEarly) == (seat.name != "Narracan"));
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
    auto artifact = TurnoutModelIO::loadForElection(root, "2026vic");
    auto const& prior = artifact.prior;
    auto snapshot = LiveInputAdapter::identify(current, artifact, "zero-check");
    assert(snapshot.districts.size() == 88);
    assert(snapshot.records.size() == current.booths.size() + 7 * snapshot.districts.size());
    for (auto const& name : prior.seats) {
        double earlyWeight = 0;
        std::set<std::string> earlyTypes;
        for (auto const& u : prior.units) if (prior.seats.at(u.seat) == name && prior.groups.at(u.group) == "early") {
            earlyWeight += u.weight;
            earlyTypes.insert(u.name);
        }
        assert((earlyTypes == std::set<std::string>{"EVC early","Mobile early","Other early"}));
        assert(std::abs(earlyWeight - 1) < 1e-12);
    }
    std::cout << "Matched historical candidate counts and " << snapshot.records.size() << " current turnout services.\n";
    if (forecast.empty()) return;

    namespace Geo = LiveBoothGeography;
    auto report = load(forecast).at("report");
    assert(report.at("termCode") == "2026vic");
    auto const& bands = report.at("probabilityBands");
    auto mid = std::find(bands.begin(),bands.end(),50) - bands.begin();
    int on = -1;
    for (auto const& abbr : report.at("partyAbbr")) if (abbr[1] == "ONP") on = abbr[0];
    assert(on >= 0 && mid < int(bands.size()));
    auto roster = Geo::readRoster(load(root/"forecasts/2026vic/live-inputs/one-nation-booths.json"),prior);
    Geo::ModeShifts shifts;
    for (auto [mode,shift] : LiveFederalDeclarations::readPrior(*LiveFederalDeclarations::inputPath(root,"2026vic"),"2026vic"))
        shifts.emplace(LiveData::voteTypeName(mode),shift);
    std::map<std::string,std::string> historicalTypes{{"EVC early","EVC Early Votes"},{"Mobile early","Mobile Early Votes"},
        {"Other early","Other Early Votes"},{"Absent","Absent Votes"},{"Postal","Postal Votes"},
        {"Marked as voted","Marked As Voted Votes"},{"Provisional","Provisional Votes"}};
    int verified = 0, returning = 0;
    for (size_t i = 0; i < report.at("seatNames").size(); ++i) {
        std::string name = report.at("seatNames").at(i);
        double target = 0;
        for (auto const& row : report.at("seatFpBands").at(i)) if (row[0] == on) target = row[1].at(mid).get<double>()/100;
        assert(target > 0 && target < 1);
        auto const& old = previous.at(name);
        std::string previousCandidate;
        for (auto const& [id,c] : old.at("candidates").items())
            if (c.at("party") == "PAULINE HANSON'S ONE NATION") previousCandidate = id;
        if (!previousCandidate.empty()) ++returning;
        std::vector<Geo::Place> units;
        for (auto const& [key,r] : roster) if (key.first == name) {
            Geo::Place place{r.mode,r.size,r.federal};
            std::string oldName = r.mode == "ordinary" ? key.second : historicalTypes.at(r.mode);
            if (!previousCandidate.empty() && old.at("booths").contains(oldName)) {
                auto const& b = old.at("booths").at(oldName);
                if (!b.value("synthetic",false) || b.value("measured_fp",false)) {
                    place.previousOn = b.at("fp").at(previousCandidate);
                    for (auto const& count : b.at("fp")) place.previousFormal += count.get<int>();
                }
            }
            units.push_back(place);
        }
        auto refs = Geo::baseline(units,target,shifts);
        double votes = 0, formal = 0, homeVotes = 0, homeFormal = 0;
        for (size_t j = 0; j < units.size(); ++j) {
            votes += refs[j].share * units[j].size;
            formal += units[j].size;
            if (units[j].mode == "ordinary" || units[j].mode == "EVC early") {
                homeVotes += refs[j].share * units[j].size;
                homeFormal += units[j].size;
            }
        }
        for (size_t j = 0; j < units.size(); ++j) if (shifts.contains(units[j].mode)) {
            double difference = LivePartialCount::logOdds(refs[j].share) - LivePartialCount::logOdds(homeVotes/homeFormal);
            assert(std::abs(difference-shifts.at(units[j].mode)) < 1e-11);
        }
        assert(std::abs(votes/formal - target) < 1e-12);
        ++verified;
    }
    assert(verified == 88 && returning == 5);
    std::cout << "Reproduced all " << verified << " published district medians with the prepared turnout and geography.\n";
}

int main(int argc, char** argv) try
{
    Document candidates, places;
    if ((argc == 3 || argc == 4) && std::string(argv[1]) == "--prepared") {
        checkPrepared(argv[2],argc == 4 ? argv[3] : "");
        return 0;
    }
    if (argc == 1) {
        candidates.Parse(R"(<EML><CandidateList><EventIdentifier Id="1"/><Election><Contest>
            <PollingDistrictIdentifier Id="2"/><ContestIdentifier><ContestName>Example District</ContestName></ContestIdentifier>
            <Enrolment>1000</Enrolment><Candidate><CandidateIdentifier Id="11"><CandidateName>Example</CandidateName>
            </CandidateIdentifier></Candidate><Candidate><CandidateIdentifier Id="12"><CandidateName>Other</CandidateName>
            </CandidateIdentifier><Affiliation><AffiliationIdentifier Id="99">
              <RegisteredName>Pauline Hanson's One Nation</RegisteredName>
            </AffiliationIdentifier></Affiliation></Candidate></Contest></Election></CandidateList></EML>)");
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
    if (argc == 1) assert(election.parties.at(99).shortCode == "ONP");
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
        auto const& tcp = election.seats.at(2).tcpVotes.at(-1);
        assert(tcp.at(Type::EvcEarly) == 7 && tcp.at(Type::MobileEarly) == 3 && tcp.at(Type::OtherEarly) == 6);
        // A lost or malformed mobile row must not erase the measured home or residual rows.
        auto damaged = xml;
        damaged[damaged.find("Early\">3")+7] = 'x';
        capture.Parse(damaged.c_str());
        election.updateLive(capture, Election::Format::VEC);
        assert(election.invalidLiveCounts.contains({Election::countKey(2,0,Type::MobileEarly),false}));
        assert(!election.invalidLiveCounts.contains({Election::countKey(2,0,Type::EvcEarly),false}));
        assert(election.seats.at(2).fpVotes.at(11).at(Type::OtherEarly) == 6);
        // The old V1 feed used Early for the whole pool. A pre-V2 replay
        // using an ordinary-only preload must retain its original category ID.
        auto oldPlaces = places.RootElement()->FirstChildElement("PollingDistrict")->FirstChildElement("PollingPlaces");
        while (oldPlaces->FirstChildElement() != oldPlaces->LastChildElement())
            oldPlaces->DeleteChild(oldPlaces->LastChildElement());
        auto legacy = Election::createVec(candidates,places,"2022vic");
        capture.Parse(xml.c_str());
        auto contest = capture.RootElement()->FirstChildElement("Election")->FirstChildElement("House")
            ->FirstChildElement("Contests")->FirstChildElement("Contest");
        contest->FirstChildElement("PollingPlaces")->DeleteChildren();
        for (auto stage : {"FirstPreferences","TwoCandidatePreferred"}) {
            auto c = contest->FirstChildElement(stage)->FirstChildElement("Candidate");
            auto votes = c->FirstChildElement("VotesByType");
            votes->DeleteChild(votes->FirstChildElement());
            votes->DeleteChild(votes->FirstChildElement());
            c->FirstChildElement("Votes")->SetText(10);
        }
        legacy.updateLive(capture, Election::Format::VEC);
        assert(legacy.seats.at(2).fpVotes.at(11).at(Type::Early) == 6);
        assert(!legacy.seats.at(2).fpVotes.at(11).contains(Type::OtherEarly));
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
