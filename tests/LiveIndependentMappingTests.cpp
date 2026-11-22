// Check candidate-specific modelling labels without changing received votes.
#include "LiveIndependentMapping.h"
#include <cassert>
#include <iostream>

struct Candidate { static constexpr int Independent = -1; std::string name; int party; };
struct Seat { std::string name; std::map<int, int> fpVotes; };
struct Election { std::map<int, Candidate> candidates; std::map<int, Seat> seats; };

void checkHistoricalFp() {
    constexpr int offset = 100000;
    std::map<int, Candidate> previous{{18683, {"CUPPER, Ali", -1}}, {10, {"Other Independent", -1}},
        {11, {"Former Candidate", -1}}, {12, {"Former Party Candidate", 25}}, {13, {"", -1}}};
    auto current = previous;
    current.at(18683).name = "Ali Cupper";
    current.at(11).name = "Different Person";
    current.at(12).party = -1;
    current.emplace(14, Candidate{"New Candidate", -1});
    auto returning = LiveIndependentMapping::returningBallotIds(previous, current, offset);
    assert((returning == std::set<int>{offset + 18683, offset + 10}));
    std::map<int,int> counts{{3, 100}, {4, 42}, {offset + 18683, 15}, {offset + 10, 0}, {offset + 11, 30}};
    auto votes = [&](int id) { return LiveIndependentMapping::previousFpVotes(counts, id, offset, returning); };
    assert(votes(offset + 18683) == 15 && votes(4) == 42);
    assert(votes(offset + 10) == 0 && !votes(offset + 11) && !votes(offset + 14));
    counts.erase(offset + 18683);
    assert(!votes(offset + 18683)); // The generic IND slot is still present.
}

int main() {
    checkHistoricalFp();
    Election current{{{1,{"ROMANO, Sam",17}}, {2,{"STRAUB, Dan",17}},
        {3,{"Paul Hopper",22}}, {4,{"Sam Romano",17}}, {5,{"Ordinary Independent",-1}}},
        {{10,{"Calare",{{1,4000},{5,10}}}}, {20,{"Mallee",{{2,5000},{4,100}}}},
        {30,{"Werribee",{{3,300}}}}}};
    auto original=current;
    auto changed=LiveIndependentMapping::apply(current,{{"Calare",{" sam  romano "}},
        {"Werribee",{"Paul Hopper","Not On Ballot"}}});
    assert((changed==std::vector<int>{1,3}));
    assert(current.candidates.at(1).party==-1 && current.candidates.at(3).party==-1);
    assert(current.candidates.at(2).party==17 && current.candidates.at(4).party==17);
    assert(original.candidates.at(1).party==17);
    for (auto const& [id,seat]:current.seats) assert(seat.fpVotes==original.seats.at(id).fpVotes);
    assert(LiveIndependentMapping::apply(current,{{"Calare",{"Sam Romano"}}}).empty());
    assert(LiveIndependentMapping::apply(original,{{"Calare",{"Romano"}}}).empty());
    original.seats.at(10).fpVotes[4]=1;
    bool rejected=false;
    try {LiveIndependentMapping::apply(original,{{"Calare",{"Sam Romano"}}});}
    catch(std::runtime_error const&) {rejected=true;}
    assert(rejected);
    std::cout<<"Independent mappings: exact names, seat isolation, idempotence, ambiguity and unchanged counts passed.\n";
}
