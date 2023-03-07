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

bool SeatCollection::exists(Seat::Id id) const
{
	return seats.find(id) != seats.end();
}

Seat const& SeatCollection::view(Seat::Id id) const {
	return seats.at(id);
}

std::pair<Seat::Id, Seat&> SeatCollection::accessByName(std::string name, bool usePreviousNames)
{
	auto seatIt = std::find_if(seats.begin(), seats.end(),
		[name](decltype(seats)::value_type seatPair) {return seatPair.second.name == name; });
	if (seatIt == seats.end()) {
		if (usePreviousNames) seatIt = std::find_if(seats.begin(), seats.end(),
			[name](decltype(seats)::value_type seatPair) {return seatPair.second.previousName == name; });
	}
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

void SeatCollection::logAll(PartyCollection const& parties, RegionCollection const& regions) const
{
	for (auto const& [key, thisSeat] : seats) {
		logger << thisSeat.textReport(parties, regions);
	}
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
	}
}

void SeatCollection::resetLocalModifiers()
{
	for (auto& [id, seat] : seats) {
		seat.localModifier = 0.0f;
	}
}

void SeatCollection::resetBettingOdds()
{
	for (auto& [id, seat] : seats) {
		seat.bettingOdds.clear();
	}
}

void SeatCollection::exportInfo() const
{
	logger << "should be exporting here\n";
	std::string termCode = project.models().viewByIndex(0).getTermCode();
	std::ofstream os("analysis/seats/" + termCode + ".txt");
	for (auto const& [id, seat] : seats) {
		os << "#Seat=" << seat.name << "\n";
		if (seat.previousName.size()) os << "sPreviousName=" << seat.previousName << "\n";
		if (seat.useFpResults.size()) os << "sUseFpResults=" << seat.useFpResults << "\n";
		os << "sIncumbent=" << project.parties().view(seat.incumbent).abbreviation << "\n";
		os << "sChallenger=" << project.parties().view(seat.challenger).abbreviation << "\n";
		os << "sRegion=" << project.regions().view(seat.region).name << "\n";
		os << "fTppMargin=" << seat.tppMargin << "\n";
		os << "fPreviousTppSwing=" << seat.previousSwing << "\n";
		if (seat.localModifier) os << "fMiscTppModifier=" << seat.localModifier << "\n";
		if (seat.transposedTppSwing) os << "fTransposedFederalSwing=" << seat.transposedTppSwing << "\n";
		if (seat.transposedTppSwing) os << "fByElectionSwing=" << seat.byElectionSwing << "\n";
		if (seat.sophomoreCandidate) os << "bSophomoreCandidate=" << seat.sophomoreCandidate << "\n";
		if (seat.sophomoreParty) os << "bSophomoreCandidate=" << seat.sophomoreParty << "\n";
		if (seat.retirement) os << "bRetirement=" << seat.retirement << "\n";
		if (seat.disendorsement) os << "bDisendorsement=" << seat.disendorsement << "\n";
		if (seat.previousDisendorsement) os << "bPreviousDisendorsement=" << seat.previousDisendorsement << "\n";
		if (seat.incumbentRecontestConfirmed) os << "bIncumbentRecontestConfirmed=" << seat.incumbentRecontestConfirmed << "\n";
		if (seat.confirmedProminentIndependent) os << "bConfirmedProminentIndependent=" << seat.confirmedProminentIndependent << "\n";
		if (seat.prominentMinors.size()) os << "sProminentMinors=" << joinString(seat.prominentMinors, ";") << "\n";

		auto stringFloatConversion = [&](decltype(seat.bettingOdds)::value_type a) {return a.first + "," + formatFloat(a.second, 2); };
		auto bettingOddsTransformed = mapTransform<std::string>(seat.bettingOdds, stringFloatConversion);
		if (seat.bettingOdds.size()) os << "sBettingOdds=" << joinString(bettingOddsTransformed, ";") << "\n";

		auto pollConversion = [&](decltype(seat.polls)::value_type a) {
			auto itemConversion = [&](decltype(a.second)::value_type b) {return a.first + "," + formatFloat(b.first, 1) + "," + std::to_string(b.second); };
			return joinString(vecTransform<std::string>(a.second, itemConversion), ";");
		};
		auto pollsTransformed = mapTransform<std::string>(seat.polls, pollConversion);
		if (seat.polls.size()) os << "sPolls=" << joinString(pollsTransformed, ";") << "\n";
		if (seat.runningParties.size()) os << "sRunningParties=" << joinString(seat.runningParties, ",") << "\n";
		auto tcpChangeTransformed = mapTransform<std::string>(seat.tcpChange, stringFloatConversion);
		if (seat.tcpChange.size()) os << "sTcpChange=" << joinString(tcpChangeTransformed, ";") << "\n";
		auto minorViabilityTransformed = mapTransform<std::string>(seat.minorViability, stringFloatConversion);
		if (seat.minorViability.size()) os << "sMinorViability=" << joinString(minorViabilityTransformed, ";") << "\n";
		auto twoStringsConversion = [&](decltype(seat.candidateNames)::value_type a) {return a.first + "," + a.second; };
		auto candidateNamesTransformed = mapTransform<std::string>(seat.candidateNames, twoStringsConversion);
		if (seat.candidateNames.size()) os << "sMinorViability=" << joinString(candidateNamesTransformed, ";") << "\n";
		if (seat.knownPrepollPercent) os << "fKnownPrepollPercent=" << seat.knownPrepollPercent << "\n";
		if (seat.knownPostalPercent) os << "fKnownPostalPercent=" << seat.knownPostalPercent << "\n";
		if (seat.knownAbsentCount) os << "iKnownAbsentCount=" << seat.knownAbsentCount << "\n";
		if (seat.knownProvisionalCount) os << "iKnownProvisionalCount=" << seat.knownProvisionalCount << "\n";
		if (seat.knownDecPrepollCount) os << "iKnownDecPrepollCount=" << seat.knownDecPrepollCount << "\n";
		if (seat.knownPostalCount) os << "iKnownPostalCount=" << seat.knownPostalCount << "\n";
	}
}
