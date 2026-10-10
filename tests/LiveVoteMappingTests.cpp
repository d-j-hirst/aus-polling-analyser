// Component fixtures for the commission reader and live booth boundary.
// This optional test needs TinyXML and LiveV2, beyond the portable core build;
// it uses fictional counts and does not run a simulation or fit a model.
#include "../LiveV2.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

void checkHistoricalCandidateCounts()
{
    tinyxml2::XMLDocument candidates, places;
    candidates.Parse(R"(<EML><CandidateList><EventIdentifier Id="99"/><Election><Contest>
      <PollingDistrictIdentifier Id="1"/><ContestIdentifier><ContestName>North District</ContestName></ContestIdentifier><Enrolment>1000</Enrolment>
      <Candidate><CandidateIdentifier Id="11"><CandidateName>One</CandidateName></CandidateIdentifier><Affiliation><AffiliationIdentifier Id="5"><RegisteredName>Australian Labor Party - Victorian Branch</RegisteredName></AffiliationIdentifier></Affiliation></Candidate>
      <Candidate><CandidateIdentifier Id="12"><CandidateName>Two</CandidateName></CandidateIdentifier><Affiliation><AffiliationIdentifier Id="8"><RegisteredName>Liberal Party of Australia - Victorian Division</RegisteredName></AffiliationIdentifier></Affiliation></Candidate>
      <Candidate><CandidateIdentifier Id="13"><CandidateName>Three</CandidateName></CandidateIdentifier><Affiliation><AffiliationIdentifier Id="9"><RegisteredName>National Party of Australia - Victoria</RegisteredName></AffiliationIdentifier></Affiliation></Candidate>
      <Candidate><CandidateIdentifier Id="14"><CandidateName>Four</CandidateName></CandidateIdentifier><Affiliation><AffiliationIdentifier Id="4"><RegisteredName>The Australian Greens - Victoria</RegisteredName></AffiliationIdentifier></Affiliation></Candidate>
      </Contest></Election></CandidateList></EML>)");
    places.Parse(R"(<PollingDistrictList><PollingDistrict><PollingDistrictIdentifier Id="1"/><PollingPlaces><PollingPlace><PollingPlaceIdentifier Id="7" Name="School"/></PollingPlace></PollingPlaces></PollingDistrict></PollingDistrictList>)");
    auto historical = nlohmann::json::parse(R"({"North": {
      "candidates": {"0":{"name":"One","party":"AUSTRALIAN LABOR PARTY"},"1":{"name":"Two","party":"LIBERAL"},"2":{"name":"Three","party":"THE NATIONALS"},"3":{"name":"Four","party":"AUSTRALIAN GREENS"}},
      "booths": {"School":{"fp":{"0":60,"1":90,"2":30,"3":20},"tcp":{"0":80,"2":120}},
      "Postal Votes":{"fp":{"0":30,"1":20,"2":40,"3":10},"tcp":{"0":45,"2":55}}}}})");
    Results2::Election previous("fictional");
    previous.update2022VicPrev(historical, candidates, places);
    assert(previous.parties.at(8).shortCode == "LNP");
    assert(previous.parties.at(9).shortCode == "NAT");
    assert(previous.booths.at(7).tcpVotesCandidate.at(11) == 80);
    assert(previous.booths.at(7).tcpVotesCandidate.at(13) == 120);
    auto const& seat = previous.seats.at(1);
    assert(seat.tcpVotesCandidate.at(11).at(Results2::VoteType::Ordinary) == 80);
    assert(seat.tcpVotesCandidate.at(13).at(Results2::VoteType::Postal) == 55);
    assert(seat.totalVotesFp() == 300);
    assert(seat.totalVotesTcp({}) == 300);

    // NSW shares the historical booth adapter's two representations. Its
    // ordinary TCP must survive alongside declaration TCP as it does for VIC.
    tinyxml2::XMLDocument nswPreload;
    nswPreload.Parse(R"(<MediaFeed><Election><House><Contests><Contest>
      <PollingDistrictIdentifier Id="North"/><Enrolment>1000</Enrolment><FirstPreferences>
      <Candidate><CandidateIdentifier Id="One"/><Affiliation><AffiliationIdentifier Id="5"><RegisteredName>Australian Labor Party (NSW Branch)</RegisteredName></AffiliationIdentifier></Affiliation></Candidate>
      <Candidate><CandidateIdentifier Id="Two"/><Affiliation><AffiliationIdentifier Id="8"><RegisteredName>The Liberal Party of Australia, New South Wales Division</RegisteredName></AffiliationIdentifier></Affiliation></Candidate>
      <Candidate><CandidateIdentifier Id="Three"/><Affiliation><AffiliationIdentifier Id="9"><RegisteredName>National Party of Australia - NSW</RegisteredName></AffiliationIdentifier></Affiliation></Candidate>
      <Candidate><CandidateIdentifier Id="Four"/><Affiliation><AffiliationIdentifier Id="4"><RegisteredName>The Greens NSW</RegisteredName></AffiliationIdentifier></Affiliation></Candidate>
      </FirstPreferences><PollingPlaces><PollingPlace><PollingPlaceIdentifier Id="7" Name="School"/></PollingPlace></PollingPlaces>
      </Contest></Contests></House></Election></MediaFeed>)");
    for (auto& [name, booth] : historical["North"]["booths"].items()) booth["tpp"] = nlohmann::json::object();
    Results2::Election nsw("fictional");
    nsw.preloadNswec(historical, nswPreload);
    assert(nsw.parties.at(9).shortCode == "NAT");
    auto school = std::find_if(nsw.booths.begin(), nsw.booths.end(),
        [](auto const& entry) { return entry.second.name == "School"; });
    assert(school != nsw.booths.end());
    assert(school->second.tcpVotesCandidate.size() == 2);
    auto const& nswSeat = nsw.seats.at(school->second.parentSeat);
    assert(nswSeat.totalVotesTcp({}) == 300);
    int ordinaryTcp = 0;
    for (auto const& [candidate, counts] : nswSeat.tcpVotesCandidate) {
        auto ordinary = counts.find(Results2::VoteType::Ordinary);
        if (ordinary != counts.end()) ordinaryTcp += ordinary->second;
    }
    assert(ordinaryTcp == 200);
}

void checkMappedBoothCounts(bool separateNationals)
{
    // Most candidates have distinct party entries. An intentional coalition
    // alias must still preserve both candidates' votes in current and earlier
    // ordinary booths, as well as incremental declaration categories.
    int const nationals = separateNationals ? 7 : -1;
    auto mapper = [separateNationals](int candidate, bool) {
        if (candidate == 11) return 0;
        if (candidate == 12) return 1;
        if (candidate == 13) return separateNationals ? 7 : 1;
        return 2;
    };
    Results2::Booth old;
    old.name = "School";
    old.fpVotes = {{11,60},{12,90},{13,30},{14,20}};
    old.tcpVotesCandidate = {{11,80},{13,120}};
    auto current = old;
    current.fpVotes = {{11,70},{12,85},{13,25},{14,20}};
    current.tcpVotesCandidate = {{11,90},{13,110}};
    LiveV2::Booth booth(current, &old, mapper, 0, nationals, true);
    assert(booth.node.totalFpVotesCurrent() == 200);
    assert(booth.node.totalVotesPrevious() == 200);
    assert(booth.node.totalTcpVotesCurrent() == 200);
    assert(booth.node.fpVotesCurrent.at(1) == (separateNationals ? 85 : 110));
    assert(booth.node.tppSwing);
    assert(std::abs(detransformVoteShare(*booth.node.tppShare)
        - detransformVoteShare(*booth.node.tppSharePrevious) - 5.0f) < 0.0001f);

    Results2::Seat::VotesByType oldFp, newFp, oldTcp, newTcp;
    for (auto const& [id, votes] : old.fpVotes) oldFp[id][Results2::VoteType::Postal] = votes;
    for (auto const& [id, votes] : current.fpVotes) newFp[id][Results2::VoteType::Postal] = votes;
    for (auto const& [id, votes] : old.tcpVotesCandidate) oldTcp[id][Results2::VoteType::Postal] = votes;
    for (auto const& [id, votes] : current.tcpVotesCandidate) newTcp[id][Results2::VoteType::Postal] = votes;
    LiveV2::Booth postal(newFp, newTcp, &oldFp, &oldTcp,
        Results2::VoteType::Postal, mapper, 0, nationals, true);
    assert(postal.node.totalFpVotesCurrent() == 200);
    assert(postal.node.totalVotesPrevious() == 200);
    assert(postal.node.totalTcpVotesCurrent() == 200);
    assert(postal.node.fpVotesPrevious.at(1) == (separateNationals ? 90 : 120));
    assert(postal.node.tppSwing);

    // A historical FP estimate can later establish a comparison even when a
    // partial current FP/TCP account cannot train preference-flow estimates.
    old.tcpVotesCandidate.clear();
    LiveV2::Booth estimated(current, &old, mapper, 0, nationals, true);
    assert(!estimated.node.tppSwing);
    estimated.node.tppSharePrevious = transformVoteShare(40.0f);
    estimated.calculateTppSwing(nationals);
    assert(estimated.node.tppSwing);
}

}

int main()
{
    checkHistoricalCandidateCounts();
    checkMappedBoothCounts(false);
    checkMappedBoothCounts(true);
    std::cout << "Live reader and booth mapping checks passed.\n";
}
