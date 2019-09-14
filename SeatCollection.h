#pragma once

#include "PartyCollection.h"
#include "Seat.h"

#include <map>

class PollingProject;

class SeatDoesntExistException : public std::runtime_error {
public:
	SeatDoesntExistException() : std::runtime_error("") {}
};

class SeatCollection {
public:
	// Collection is a map between ID values and seats
	// IDs are not preserved between sessions, and are used to ensure
	// consistent display with seat deletions etc. while making sure references
	// from other components are preserved
	// Map must be ordered to ensure order of seats is in order they are added.
	typedef std::map<Seat::Id, Seat> SeatContainer;

	// Seat index refers to the position of the seat in the order of currently existing seats
	// Should not be stored persistently as removal of a seat will change the indices
	// (use the SeatKey for that)
	typedef int Index;
	constexpr static Index InvalidIndex = -1;

	constexpr static int NumRequiredSeats = 1;

	SeatCollection(PollingProject& project);

	// Any post-processing of files that must be done after loading from the file
	void finaliseFileLoading();

	enum class Result {
		Ok,
		CantRemoveRequiredSeat,
		SeatDoesntExist,
	};

	// Adds the seat "seat".
	// Throws an exception if the number of seats is over the limit, check this beforehand using canAdd();
	void add(Seat seat);

	// Replaces the seat with index "seatIndex" by "seat".
	void replace(Seat::Id id, Seat seat);

	// Checks if it is currently possible to add the given seat
	// Returns Result::Ok if it's possible and Result::TooManySeats if there are too many seats
	Result canRemove(Seat::Id id);

	// Removes the seat with index "seatIndex".
	void remove(Seat::Id id);

	// Gives access to the seat with index "seatIndex".
	Seat& access(Seat::Id id);

	// Returns true if a seat with the given ID is present and false otherwise.
	bool exists(Seat::Id id) const;

	// Returns the seat with index "seatIndex".
	Seat const& view(Seat::Id id) const;

	// Returns the seat with index "seatIndex".
	Seat const& viewByIndex(Index seatIndex) const { return view(indexToId(seatIndex)); }

	std::pair<Seat::Id, Seat&> accessByName(std::string name);

	Index idToIndex(Seat::Id id) const;
	Seat::Id indexToId(Index id) const;

	// Returns the number of seats.
	int count() const;

	// If a party is removed, seats need to be adjusted to deal with this
	void adjustAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId);

	Seat& back() { return std::prev(seats.end())->second; }

	// Gets the begin iterator for the seat list.
	SeatContainer::iterator begin() { return seats.begin(); }

	// Gets the end iterator for the seat list.
	SeatContainer::iterator end() { return seats.end(); }

	// Gets the begin iterator for the seat list.
	SeatContainer::const_iterator begin() const { return seats.begin(); }

	// Gets the end iterator for the seat list.
	SeatContainer::const_iterator end() const { return seats.end(); }

	// Gets the begin iterator for the seat list.
	SeatContainer::const_iterator cbegin() const { return seats.cbegin(); }

	// Gets the end iterator for the seat list.
	SeatContainer::const_iterator cend() const { return seats.cend(); }

private:

	// what the next ID for an item in the container will be
	int nextId = 0;

	SeatContainer seats;

	PollingProject& project;
};