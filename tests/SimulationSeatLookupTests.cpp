// Check that run-local seat/region lookup preserves identity and summation
// order, and is rebuilt against the owning project rather than a GUI copy.
#include "../SimulationSeatLookup.h"

#include <cassert>
#include <iostream>
#include <map>
#include <string>

namespace {
struct Seat { std::string name; int region; double margin; };
using Seats = std::map<int,Seat>;
using Lookup = SimulationSeatLookup<Seat>;
int regionIndex(int id) { return id==7 ? 0 : id==42 ? 2 : -1; }

void checkIdentityAndOrder() {
    // Both seat and region IDs have gaps. One region is empty, and the name
    // order differs from ID order: the cache must not sort seats by name.
    Seats configured{{2,{"Z",7,1e16}},{13,{"A",42,10}},
        {19,{"B",7,1}},{31,{"C",7,-1e16}}};
    Lookup prepared;
    prepared.prepare(configured,3,regionIndex);
    assert(prepared.regionSeats(0)==std::vector<int>({0,2,3}));
    assert(prepared.regionSeats(1).empty());
    assert(prepared.regionSeats(2)==std::vector<int>({1}));
    std::size_t i=0;
    for(auto const& [id,seat]:configured) {
        (void)id;
        assert(&prepared.seat(i)==&seat);
        assert(prepared.region(i)==regionIndex(seat.region));
        ++i;
    }
    for(int r=0;r<3;++r) {
        double original=0, cached=0;
        for(auto const& [id,seat]:configured) {
            (void)id;
            if(regionIndex(seat.region)==r) original+=seat.margin;
        }
        for(int index:prepared.regionSeats(r)) cached+=prepared.seat(index).margin;
        assert(original==cached); // Exact equality, including rounding order.
    }
}

void checkContextRebinding() {
    Seats gui{{8,{"Example",7,1}}};
    Seats worker=gui;
    Lookup prepared;
    prepared.prepare(worker,3,regionIndex);
    assert(&prepared.seat(0)==&worker.at(8));
    assert(&prepared.seat(0)!=&gui.at(8));
    gui.at(8).margin=99;
    assert(prepared.seat(0).margin==1);

    // A later run imports another configuration. Rebuild before using it;
    // no removed seat, old region membership or prior-context pointer survives.
    Seats later{{5,{"Replacement",42,5}},{27,{"Extra",42,7}}};
    prepared.prepare(later,3,regionIndex);
    assert(prepared.regionSeats(0).empty());
    assert(prepared.regionSeats(2)==std::vector<int>({0,1}));
    assert(&prepared.seat(0)==&later.at(5));
    assert(prepared.seat(1).name=="Extra");
}
}

int main() {
    checkIdentityAndOrder();
    checkContextRebinding();
    std::cout<<"Simulation seat lookup tests passed\n";
}
