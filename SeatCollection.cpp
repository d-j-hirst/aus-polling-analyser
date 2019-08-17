#include "SeatCollection.h"

#include "PartyCollection.h"
#include "PollingProject.h"

#include <exception>

SeatCollection::SeatCollection(PollingProject& project)
	: project(project)
{
}

void SeatCollection::finaliseFileLoading() {
}

void SeatCollection::add(Seat seat) {
	seats.insert({ nextId, seat });
	++nextId;
}

void SeatCollection::replace(Seat::Id id, Seat seat) {
	seats[id] = seat;
}

Seat & SeatCollection::access(Seat::Id id)
{
	return seats.at(id);
}

bool SeatCollection::exists(Seat::Id id)
{
	return seats.find(id) != seats.end();
}

Seat const& SeatCollection::view(Seat::Id id) const {
	return seats.at(id);
}

std::pair<Seat::Id, Seat&> SeatCollection::accessByName(std::string name)
{
	auto seatIt = std::find_if(seats.begin(), seats.end(),
		[name](decltype(seats)::value_type seatPair) {return seatPair.second.name == name; });
	if (seatIt == seats.end()) throw SeatDoesntExistException();
	return { seatIt->first, seatIt->second };
}

SeatCollection::Index SeatCollection::idToIndex(Seat::Id id) const
{
	auto foundIt = seats.find(id);
	if (foundIt == seats.end()) return InvalidIndex;
	return std::distance(seats.begin(), foundIt);
}

Seat::Id SeatCollection::indexToId(Index index) const
{
	if (index >= count() || index < 0) return Seat::InvalidId;
	return std::next(seats.begin(), index)->first;
}

SeatCollection::Result SeatCollection::canRemove(Seat::Id id)
{
	if (count() <= NumRequiredSeats) return Result::CantRemoveRequiredSeat;
	auto seatIt = seats.find(id);
	if (seatIt == seats.end()) return Result::SeatDoesntExist;
	return Result::Ok;
}

void SeatCollection::remove(Seat::Id id) {
	// A lot of seat management is simplified by keeping the first two seats consistent,
	// so we forbid removal of these seats to avoid messier code.
	// If the user wants different top-two seats they can just edit them
	// and having less than two seats doesn't make a lot of sense.
	auto removeAllowed = canRemove(id);
	if (removeAllowed == Result::SeatDoesntExist) throw SeatDoesntExistException();
	auto seatIt = seats.find(id);
	seats.erase(seatIt);
}

int SeatCollection::count() const {
	return seats.size();
}

void SeatCollection::adjustAfterPartyRemoval(PartyCollection::Index, Party::Id partyId) {
	for (auto& seat : seats) {
		// Ensures that the incumbent and challenger are never set to the same party
		// (it's ok if the second challenger is the same as one of these)
		if (seat.second.incumbent == partyId) {
			seat.second.incumbent = project.parties().indexToId(0);
			if (project.parties().idToIndex(seat.second.challenger) == 0) {
				seat.second.challenger = project.parties().indexToId(1);
			}
		}
		if (seat.second.challenger == partyId) {
			seat.second.challenger = project.parties().indexToId(1);
			if (project.parties().idToIndex(seat.second.incumbent) == 1) {
				seat.second.challenger = project.parties().indexToId(0);
			}
		}
		if (seat.second.challenger2 == partyId) {
			seat.second.challenger2 = project.parties().indexToId(0);
		}
	}
}