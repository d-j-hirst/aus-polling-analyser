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
		seat.miscTppModifier = 0.0f;
	}
}

void SeatCollection::resetBettingOdds()
{
	for (auto& [id, seat] : seats) {
		seat.bettingOdds.clear();
	}
}

constexpr std::string_view PreviousName = "sPreviousName";
constexpr std::string_view UseFpResults = "sUseFpResults";
constexpr std::string_view Incumbent = "sIncumbent";
constexpr std::string_view Challenger = "sChallenger";
constexpr std::string_view Region = "sRegion";
constexpr std::string_view TppMargin = "fTppMargin";
constexpr std::string_view PreviousTppSwing = "fPreviousTppSwing";
constexpr std::string_view MiscTppModifier = "fMiscTppModifier";
constexpr std::string_view TransposedFederalSwing = "fTransposedFederalSwing";
constexpr std::string_view ByElectionSwing = "fByElectionSwing";
constexpr std::string_view SophomoreCandidate = "bSophomoreCandidate";
constexpr std::string_view SophomoreParty = "bSophomoreParty";
constexpr std::string_view Retirement = "bRetirement";
constexpr std::string_view Disendorsement = "bDisendorsement";
constexpr std::string_view PreviousDisendorsement = "bPreviousDisendorsement";
constexpr std::string_view IncumbentRecontestConfirmed = "bIncumbentRecontestConfirmed";
constexpr std::string_view ConfirmedProminentIndependent = "bConfirmedProminentIndependent";
constexpr std::string_view ProminentMinors = "sProminentMinors";
constexpr std::string_view BettingOdds = "sBettingOdds";
constexpr std::string_view Polls = "sPolls";
constexpr std::string_view TppPolls = "sTppPolls";
constexpr std::string_view RunningParties = "sRunningParties";
constexpr std::string_view TcpChange = "sTcpChange";
constexpr std::string_view MinorViability = "sMinorViability";
constexpr std::string_view CandidateNames = "sCandidateNames";
constexpr std::string_view KnownPrepollPercent = "fKnownPrepollPercent";
constexpr std::string_view KnownPostalPercent = "fKnownPostalPercent";
constexpr std::string_view KnownAbsentCount = "iKnownAbsentCount";
constexpr std::string_view KnownProvisionalCount = "iKnownProvisionalCount";
constexpr std::string_view KnownDecPrepollCount = "iKnownDecPrepollCount";
constexpr std::string_view KnownPostalCount = "iKnownPostalCount";

void SeatCollection::exportInfo() const
{
	std::string termCode = project.models().viewByIndex(0).getTermCode();
	std::ofstream os("analysis/seats/" + termCode + ".txt");
	for (auto const& [id, seat] : seats) {
		os << "#" << seat.name << "\n";
		if (seat.previousName.size()) os << PreviousName << "=" << seat.previousName << "\n";
		if (seat.useFpResults.size()) os << UseFpResults << "=" << seat.useFpResults << "\n";
		os << Incumbent << "=" << project.parties().view(seat.incumbent).abbreviation << "\n";
		os << Challenger << "=" << project.parties().view(seat.challenger).abbreviation << "\n";
		os << Region << "=" << project.regions().view(seat.region).name << "\n";
		os << TppMargin << "=" << seat.tppMargin << "\n";
		os << PreviousTppSwing << "=" << seat.previousSwing << "\n";
		if (seat.miscTppModifier) os << MiscTppModifier << "=" << seat.miscTppModifier << "\n";
		if (seat.transposedTppSwing) os << TransposedFederalSwing << "=" << seat.transposedTppSwing << "\n";
		if (seat.byElectionSwing) os << ByElectionSwing << "=" << seat.byElectionSwing << "\n";
		if (seat.sophomoreCandidate) os << SophomoreCandidate << "=" << seat.sophomoreCandidate << "\n";
		if (seat.sophomoreParty) os << SophomoreParty << "=" << seat.sophomoreParty << "\n";
		if (seat.retirement) os << Retirement << "=" << seat.retirement << "\n";
		if (seat.disendorsement) os << Disendorsement << "=" << seat.disendorsement << "\n";
		if (seat.previousDisendorsement) os << PreviousDisendorsement << "=" << seat.previousDisendorsement << "\n";
		if (seat.incumbentRecontestConfirmed) os << IncumbentRecontestConfirmed << "=" << seat.incumbentRecontestConfirmed << "\n";
		if (seat.confirmedProminentIndependent) os << ConfirmedProminentIndependent << "=" << seat.confirmedProminentIndependent << "\n";
		if (seat.prominentMinors.size()) os << ProminentMinors << "=" << joinString(seat.prominentMinors, ";") << "\n";

		auto stringFloatConversion = [&](decltype(seat.bettingOdds)::value_type a) {return a.first + "," + formatFloat(a.second, 2); };
		auto bettingOddsTransformed = mapTransform<std::string>(seat.bettingOdds, stringFloatConversion);
		if (seat.bettingOdds.size()) os << BettingOdds << "=" << joinString(bettingOddsTransformed, ";") << "\n";

		auto pollConversion = [&](decltype(seat.polls)::value_type a) {
			auto itemConversion = [&](decltype(a.second)::value_type b) {return a.first + "," + formatFloat(b.first, 1) + "," + std::to_string(b.second); };
			return joinString(vecTransform<std::string>(a.second, itemConversion), ";");
		};
		auto pollsTransformed = mapTransform<std::string>(seat.polls, pollConversion);
		if (seat.polls.size()) os << Polls << "=" << joinString(pollsTransformed, ";") << "\n";
		if (seat.runningParties.size()) os << RunningParties << "=" << joinString(seat.runningParties, ",") << "\n";
		auto tcpChangeTransformed = mapTransform<std::string>(seat.tcpChange, stringFloatConversion);
		if (seat.tcpChange.size()) os << TcpChange << "=" << joinString(tcpChangeTransformed, ";") << "\n";
		auto minorViabilityTransformed = mapTransform<std::string>(seat.minorViability, stringFloatConversion);
		if (seat.minorViability.size()) os << MinorViability << "=" << joinString(minorViabilityTransformed, ";") << "\n";
		auto twoStringsConversion = [&](decltype(seat.candidateNames)::value_type a) {return a.first + "," + a.second; };
		auto candidateNamesTransformed = mapTransform<std::string>(seat.candidateNames, twoStringsConversion);
		if (seat.candidateNames.size()) os << CandidateNames << "=" << joinString(candidateNamesTransformed, ";") << "\n";
		if (seat.knownPrepollPercent) os << KnownPrepollPercent << "=" << seat.knownPrepollPercent << "\n";
		if (seat.knownPostalPercent) os << KnownPostalPercent << "=" << seat.knownPostalPercent << "\n";
		if (seat.knownAbsentCount) os << KnownAbsentCount << "=" << seat.knownAbsentCount << "\n";
		if (seat.knownProvisionalCount) os << KnownProvisionalCount << "=" << seat.knownProvisionalCount << "\n";
		if (seat.knownDecPrepollCount) os << KnownDecPrepollCount << "=" << seat.knownDecPrepollCount << "\n";
		if (seat.knownPostalCount) os << KnownPostalCount << "=" << seat.knownPostalCount << "\n";
	}
}

void SeatCollection::importInfo()
{
	std::string termCode = project.models().viewByIndex(0).getTermCode();
	std::string filename = "analysis/seats/" + termCode + ".txt";
	std::ifstream is(filename);
	if (!is) {
		logger << "Warning: Seat import failed, tried to open: " << filename;
		return;
	}
	seats.clear();
	std::string line;
	while (std::getline(is, line)) {
		if (!line.size()) continue;
		line.erase(line.find_last_not_of(" \n\r\t") + 1); // trim whitespace
		if (line[0] == '#') {
			add(Seat(line.substr(1)));
			continue;
		}
		auto lineEls = splitString(line, "=");
		if (lineEls.size() != 2) {
			logger << "Warning: While loading seats, a line was not correctly entered: " << line;
			continue;
		}
		if (!seats.size()) {
			logger << "Warning: While loading seats, a line for seat data was given before a seat name: " << line;
			continue;
		}
		std::string tag = lineEls[0];
		std::string val = lineEls[1];
		Seat& seat = seats[nextId - 1];
		if (tag == PreviousName) seat.previousName = val;
		if (tag == UseFpResults) seat.useFpResults = val;
		if (tag == Incumbent) seat.incumbent = project.parties().idByAbbreviation(val);
		if (tag == Challenger) seat.challenger = project.parties().idByAbbreviation(val);
		if (tag == Region) seat.region = project.regions().findbyName(val).first;
		if (tag == TppMargin) seat.tppMargin = std::stof(val);
		if (tag == PreviousTppSwing) seat.previousSwing = std::stof(val);
		if (tag == MiscTppModifier) seat.miscTppModifier = std::stof(val);
		if (tag == TransposedFederalSwing) seat.transposedTppSwing = std::stof(val);
		if (tag == ByElectionSwing) seat.byElectionSwing = std::stof(val);
		if (tag == SophomoreCandidate) seat.sophomoreCandidate = val != 0;
		if (tag == SophomoreParty) seat.sophomoreParty = val != 0;
		if (tag == Retirement) seat.retirement = val != 0;
		if (tag == Disendorsement) seat.disendorsement = val != 0;
		if (tag == PreviousDisendorsement) seat.previousDisendorsement = val != 0;
		if (tag == IncumbentRecontestConfirmed) seat.incumbentRecontestConfirmed = val != 0;
		if (tag == ConfirmedProminentIndependent) seat.confirmedProminentIndependent = val != 0;
		if (tag == ProminentMinors) seat.prominentMinors = splitString(val, ",");
		if (tag == BettingOdds) {
			auto splitByParty = splitString(val, ";");
			for (auto party : splitByParty) {
				auto vals = splitString(party, ",");
				seat.bettingOdds[vals[0]] = std::stof(vals[1]);
			}
		}
		if (tag == Polls) {
			auto splitByParty = splitString(val, ";");
			for (auto party : splitByParty) {
				auto vals = splitString(party, ",");
				seat.polls[vals[0]].push_back({ std::stof(vals[1]), std::stoi(vals[2]) });
			}
		}
		if (tag == TppPolls) {
			auto splitByPoll = splitString(val, ";");
			for (auto poll : splitByPoll) {
				auto vals = splitString(poll, ",");
				seat.tppPolls.push_back({ vals[0], std::stof(vals[2]) });
			}
		}
		if (tag == RunningParties) seat.runningParties = splitString(val, ",");
		if (tag == TcpChange) {
			auto splitByParty = splitString(val, ";");
			for (auto party : splitByParty) {
				auto vals = splitString(party, ",");
				seat.tcpChange[vals[0]] = std::stof(vals[1]);
			}
		}
		if (tag == MinorViability) {
			auto splitByParty = splitString(val, ";");
			for (auto party : splitByParty) {
				auto vals = splitString(party, ",");
				seat.minorViability[vals[0]] = std::stof(vals[1]);
			}
		}
		if (tag == CandidateNames) {
			auto splitByParty = splitString(val, ";");
			for (auto party : splitByParty) {
				auto vals = splitString(party, ",");
				seat.candidateNames[vals[0]] = vals[1];
			}
		}
		if (tag == KnownPrepollPercent) seat.knownPrepollPercent = std::stof(val);
		if (tag == KnownPostalPercent) seat.knownPostalPercent = std::stof(val);
		if (tag == KnownAbsentCount) seat.knownAbsentCount = std::stoi(val);
		if (tag == KnownProvisionalCount) seat.knownProvisionalCount = std::stoi(val);
		if (tag == KnownDecPrepollCount) seat.knownDecPrepollCount = std::stoi(val);
		if (tag == KnownPostalCount) seat.knownPostalCount = std::stoi(val);
		// do something with the line
	}
	logger << "Completed importing seat info\n";
}
