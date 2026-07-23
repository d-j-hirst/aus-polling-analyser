#include "SeatCollection.h"

#include "PartyCollection.h"
#include "PollingProject.h"
#include "RegionCollection.h"

#include <cctype>
#include <cmath>
#include <exception>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

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

int SeatCollection::indexByName(std::string name, bool usePreviousNames)
{
	return idToIndex(accessByName(name, usePreviousNames).first);
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
constexpr std::string_view TppMrpPolls = "sTppMrpPolls";
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
// Retained in a few seat files as human-readable poll metadata. It was never
// represented in Seat and remains intentionally ignored by the core pipeline.
constexpr std::string_view PollSources = "sPollSources";

namespace {
	float parseFiniteFloat(std::string const& value)
	{
		std::size_t consumed = 0;
		float const parsed = std::stof(value, &consumed);
		while (consumed < value.size() &&
			std::isspace(static_cast<unsigned char>(value[consumed]))) {
			++consumed;
		}
		if (consumed != value.size() || !std::isfinite(parsed)) {
			throw std::runtime_error("expected a finite number");
		}
		return parsed;
	}

	int parseInteger(std::string const& value)
	{
		std::size_t consumed = 0;
		int const parsed = std::stoi(value, &consumed);
		while (consumed < value.size() &&
			std::isspace(static_cast<unsigned char>(value[consumed]))) {
			++consumed;
		}
		if (consumed != value.size()) {
			throw std::runtime_error("expected an integer");
		}
		return parsed;
	}

	std::vector<std::string> splitSeatList(std::string const& value)
	{
		return splitString(value, value.find(';') == std::string::npos ?
			"," : ";");
	}
}

void SeatCollection::exportInfo() const
{
	std::string termCode = project.models().viewByIndex(0).getTermCode();
	std::ofstream os(project.paths().resolveString(
		"analysis/seats/" + termCode + ".txt"));
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

void SeatCollection::configureImportSource(
	std::filesystem::path sourcePath, bool strict)
{
	configuredImportSource = std::move(sourcePath);
	strictImportSource = strict;
}

std::filesystem::path SeatCollection::importSourcePath() const
{
	if (!configuredImportSource.empty()) {
		return project.paths().resolve(configuredImportSource);
	}
	if (!project.models().count()) return {};
	auto const termCode = project.models().viewByIndex(0).getTermCode();
	if (termCode.empty()) return {};
	return project.paths().resolve(
		std::filesystem::path("analysis/seats") / (termCode + ".txt"));
}

void SeatCollection::importInfo()
{
	auto const sourcePath = importSourcePath();
	auto const filename = sourcePath.string();
	auto issue = [&](std::size_t line, std::string message) {
		std::ostringstream output;
		output << "Seat import failed for " << filename;
		if (line) output << " at line " << line;
		output << ": " << message;
		if (strictImportSource) {
			throw SeatImportException(output.str());
		}
		logger << "Warning: " << output.str() << "\n";
	};

	if (sourcePath.empty()) {
		issue(0, "no seat source is configured");
		return;
	}
	std::ifstream is(sourcePath);
	if (!is) {
		issue(0, "could not open the file");
		return;
	}

	std::map<std::string, Seat> oldSeats;
	for (auto const& [id, seat] : seats) {
		oldSeats[seat.name] = seat;
	}

	SeatContainer importedSeats;
	int importedNextId = 0;
	Seat* currentSeat = nullptr;
	std::set<std::string> seatNames;
	std::string line;
	std::size_t lineNumber = 0;
	while (std::getline(is, line)) {
		++lineNumber;
		auto const lastCharacter = line.find_last_not_of(" \n\r\t");
		if (lastCharacter == std::string::npos) continue;
		line.erase(lastCharacter + 1);
		if (line[0] == '#') {
			auto const name = line.substr(1);
			if (name.empty()) {
				issue(lineNumber, "seat name is empty");
				continue;
			}
			if (!seatNames.insert(name).second) {
				issue(lineNumber, "duplicate seat name " + name);
				continue;
			}
			auto seatIt = importedSeats.emplace(
				importedNextId++, Seat(name)).first;
			currentSeat = &seatIt->second;
			continue;
		}

		auto const separator = line.find('=');
		if (separator == std::string::npos ||
			line.find('=', separator + 1) != std::string::npos) {
			issue(lineNumber, "expected one key=value pair");
			continue;
		}
		if (!currentSeat) {
			issue(lineNumber, "seat data appears before the first seat name");
			continue;
		}

		auto const tag = line.substr(0, separator);
		auto const value = line.substr(separator + 1);
		auto partyId = [&](std::string const& partyCode) {
			auto id = project.parties().idByAbbreviation(partyCode);
			if (id != Party::InvalidId) return id;
			auto const index =
				project.parties().indexByShortCode(partyCode);
			if (index != PartyCollection::InvalidIndex) {
				return project.parties().indexToId(index);
			}
			throw std::runtime_error(
				"unknown party code " + partyCode);
		};
		auto fields = [&](std::string const& item, std::size_t required) {
			auto values = splitString(item, ",");
			if (values.size() != required) {
				throw std::runtime_error(
					"expected " + std::to_string(required) +
					" comma-separated values");
			}
			return values;
		};

		try {
			auto& seat = *currentSeat;
			if (tag == PreviousName) seat.previousName = value;
			else if (tag == UseFpResults) seat.useFpResults = value;
			else if (tag == Incumbent) seat.incumbent = partyId(value);
			else if (tag == Challenger) seat.challenger = partyId(value);
			else if (tag == Region) {
				seat.region = project.regions().findbyName(value).first;
				if (seat.region == Region::InvalidId) {
					throw std::runtime_error("unknown region " + value);
				}
			}
			else if (tag == TppMargin)
				seat.tppMargin = parseFiniteFloat(value);
			else if (tag == PreviousTppSwing)
				seat.previousSwing = parseFiniteFloat(value);
			else if (tag == MiscTppModifier)
				seat.miscTppModifier = parseFiniteFloat(value);
			else if (tag == TransposedFederalSwing)
				seat.transposedTppSwing = parseFiniteFloat(value);
			else if (tag == ByElectionSwing)
				seat.byElectionSwing = parseFiniteFloat(value);
			else if (tag == SophomoreCandidate)
				seat.sophomoreCandidate = parseInteger(value) != 0;
			else if (tag == SophomoreParty)
				seat.sophomoreParty = parseInteger(value) != 0;
			else if (tag == Retirement)
				seat.retirement = parseInteger(value) != 0;
			else if (tag == Disendorsement)
				seat.disendorsement = parseInteger(value) != 0;
			else if (tag == PreviousDisendorsement)
				seat.previousDisendorsement = parseInteger(value) != 0;
			else if (tag == IncumbentRecontestConfirmed)
				seat.incumbentRecontestConfirmed =
					parseInteger(value) != 0;
			else if (tag == ConfirmedProminentIndependent)
				seat.confirmedProminentIndependent =
					parseInteger(value) != 0;
			else if (tag == ProminentMinors)
				seat.prominentMinors = splitSeatList(value);
			else if (tag == BettingOdds) {
				for (auto const& item : splitString(value, ";")) {
					auto const values = fields(item, 2);
					seat.bettingOdds[values[0]] =
						parseFiniteFloat(values[1]);
				}
			}
			else if (tag == Polls) {
				for (auto const& item : splitString(value, ";")) {
					auto const values = fields(item, 3);
					seat.polls[values[0]].push_back({
						parseFiniteFloat(values[1]),
						parseInteger(values[2]) });
				}
			}
			else if (tag == TppPolls || tag == TppMrpPolls) {
				auto& polls =
					tag == TppPolls ? seat.tppPolls : seat.tppMrpPolls;
				for (auto const& item : splitString(value, ";")) {
					auto const values = fields(item, 3);
					polls.push_back(
						{ values[0], parseFiniteFloat(values[2]) });
				}
			}
			else if (tag == RunningParties)
				seat.runningParties = splitString(value, ",");
			else if (tag == TcpChange || tag == MinorViability) {
				auto& valuesByParty = tag == TcpChange ?
					seat.tcpChange : seat.minorViability;
				for (auto const& item : splitString(value, ";")) {
					auto const values = fields(item, 2);
					valuesByParty[values[0]] =
						parseFiniteFloat(values[1]);
				}
			}
			else if (tag == CandidateNames) {
				if (!value.empty()) {
					for (auto const& item : splitString(value, ";")) {
						auto const values = fields(item, 2);
						seat.candidateNames[values[0]] = values[1];
					}
				}
			}
			else if (tag == KnownPrepollPercent)
				seat.knownPrepollPercent = parseFiniteFloat(value);
			else if (tag == KnownPostalPercent)
				seat.knownPostalPercent = parseFiniteFloat(value);
			else if (tag == KnownAbsentCount)
				seat.knownAbsentCount = parseInteger(value);
			else if (tag == KnownProvisionalCount)
				seat.knownProvisionalCount = parseInteger(value);
			else if (tag == KnownDecPrepollCount)
				seat.knownDecPrepollCount = parseInteger(value);
			else if (tag == KnownPostalCount)
				seat.knownPostalCount = parseInteger(value);
			else if (tag != PollSources) {
				throw std::runtime_error("unknown field " + tag);
			}
		}
		catch (std::exception const& exception) {
			issue(lineNumber, exception.what());
		}
	}

	if (importedSeats.empty()) {
		issue(0, "the file contains no seats");
		return;
	}
	for (auto const& [id, seat] : importedSeats) {
		if (seat.incumbent == Party::InvalidId) {
			issue(0, "seat " + seat.name + " has no valid incumbent");
		}
		if (seat.challenger == Party::InvalidId) {
			issue(0, "seat " + seat.name + " has no valid challenger");
		}
		if (seat.incumbent == seat.challenger) {
			issue(0, "seat " + seat.name +
				" has the same incumbent and challenger");
		}
		if (seat.region == Region::InvalidId) {
			issue(0, "seat " + seat.name + " has no valid region");
		}
	}

	for (auto const& [name, oldSeat] : oldSeats) {
		for (auto& [id, newSeat] : importedSeats) {
			if (newSeat.name == name) {
				newSeat.livePartyOne = oldSeat.livePartyOne;
				newSeat.livePartyTwo = oldSeat.livePartyTwo;
				newSeat.liveUseTpp = oldSeat.liveUseTpp;
				newSeat.partyTwoProb = oldSeat.partyTwoProb;
				newSeat.partyThreeProb = oldSeat.partyThreeProb;
			}
		}
	}

	seats = std::move(importedSeats);
	nextId = importedNextId;
	logger << "Completed importing seat info from " << filename << "\n";
}
