// Check candidate-specific modelling labels without changing received votes.
#include "LiveIndependentMapping.h"
#include <cassert>
#include <iostream>

struct Candidate { static constexpr int Independent = -1; std::string name; int party; };
struct Seat { std::string name; std::map<int, int> fpVotes; };
struct Election { std::map<int, Candidate> candidates; std::map<int, Seat> seats; };

int main() {
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
