#include "ResultCoordinator.h"

#include "LatestResultsDataRetriever.h"
#include "Log.h"
#include "PollingProject.h"
#include "PreloadDataRetriever.h"
#include "PreviousElectionDataRetriever.h"
#include "SeatCollection.h"

// Note: A large amount of code in this file is commented out as the "previous results"
// was updated to a new (better) format but the "latest results" was not. Further architectural
// improvement, including removing cached election results from project seat data, cannot be
// properly done unless this is fixed, and the fixing is decidedly non-trivial. In order to
// expedite the initial web release, which does not require live election updating, these have
// been disabled and code producing errors commented out and replaced with stubs,
// until the project is prepared to work on restoring the live results.

ResultCoordinator::ResultCoordinator(PollingProject& project)
	: project(project)
{
}

void ResultCoordinator::incorporatePreviousElectionResults(PreviousElectionDataRetriever const& dataRetriever)
{
	matchPreviousElectionSeatsWithProjectSeats(dataRetriever);
	collectPreviousElectionBoothsAndCandidates(dataRetriever);
	collectPreviousElectionAffiliations(dataRetriever);
	relateCandidatesAndAffiliations(dataRetriever);
}

void ResultCoordinator::incorporatePreloadData(PreloadDataRetriever const& dataRetriever)
{
	collectCandidatesFromPreload(dataRetriever);
	collectBoothsFromPreload(dataRetriever);
}

void ResultCoordinator::incorporateLatestResults(LatestResultsDataRetriever const& dataRetriever)
{
	matchBoothsFromLatestResults(dataRetriever);
	matchSeatsFromLatestResults(dataRetriever);
	updateOutcomesFromLatestResults();
}

Results::Booth const& ResultCoordinator::getBooth(int boothId) const
{
	return booths.at(boothId);
}

Point2Df ResultCoordinator::boothLatitudeRange() const
{
	if (!booths.size()) return { 0.0f, 0.0f };
	bool latitudeInitiated = false;
	float minLatitude = 0.0f;
	float maxLatitude = 0.0f;
	for (auto const& booth : booths) {
		float latitude = booth.second.coords.latitude;
		if (std::abs(latitude) < 0.000001f) continue; // discontinued booths won't have a location
		if (!latitudeInitiated) {
			minLatitude = latitude;
			maxLatitude = latitude;
			latitudeInitiated = true;
		}
		minLatitude = std::min(minLatitude, latitude);
		maxLatitude = std::max(maxLatitude, latitude);
	}
	if (!latitudeInitiated) return { 0.0f, 0.0f };
	return { minLatitude, maxLatitude };
}

Point2Df ResultCoordinator::boothLongitudeRange() const
{
	if (!booths.size()) return { 0.0f, 0.0f };
	bool longitudeInitiated = false;
	float minLongitude = 0.0f;
	float maxLongitude = 0.0f;
	for (auto const& booth : booths) {
		float longitude = booth.second.coords.longitude;
		if (std::abs(longitude) < 0.000001f) continue; // discontinued booths won't have a location
		if (longitude > 180.0f) {
			logger << booth.second.name << " - bad longitude - " << longitude << "\n";
		}
		if (!longitudeInitiated) {
			minLongitude = longitude;
			maxLongitude = longitude;
			longitudeInitiated = true;
		}
		minLongitude = std::min(minLongitude, longitude);
		maxLongitude = std::max(maxLongitude, longitude);
	}
	if (!longitudeInitiated) return { 0.0f, 0.0f };
	return { minLongitude, maxLongitude };
}

Party::Id ResultCoordinator::getPartyByCandidate(int candidateId) const
{
	auto candidateIt = candidateParties.find(candidateId);
	if (candidateIt == candidateParties.end()) return Party::InvalidId;
	return candidateIt->second;
}

Party::Id ResultCoordinator::getPartyByAffiliation(int affiliationId) const
{
	auto affiliationIt = affiliationParties.find(affiliationId);
	if (affiliationIt == affiliationParties.end()) return Party::InvalidId;
	return affiliationIt->second;
}

Results::Candidate const * ResultCoordinator::getCandidateById(int candidateId) const
{
	auto candidateIt = candidates.find(candidateId);
	if (candidateIt == candidates.end()) return nullptr;
	return &candidateIt->second;
}

Results::Affiliation const * ResultCoordinator::getAffiliationById(int affiliationId) const
{
	auto affiliationIt = affiliations.find(affiliationId);
	if (affiliationIt == affiliations.end()) return nullptr;
	return &affiliationIt->second;
}

int ResultCoordinator::getCandidateAffiliationId(int candidateId) const
{
	auto affiliationIt = candidateAffiliations.find(candidateId);
	if (affiliationIt == candidateAffiliations.end()) return -1;
	return affiliationIt->second;
}

void ResultCoordinator::adjustCandidatesAfterPartyRemoval(PartyCollection::Index, Party::Id partyId)
{
	for (auto& candidate : candidateParties) {
		if (candidate.second == partyId) {
			candidate.second = -1;
		}
	}
}

void ResultCoordinator::adjustAffiliationsAfterPartyRemoval(PartyCollection::Index, Party::Id partyId)
{
	for (auto& affiliation : affiliationParties) {
		if (affiliation.second == partyId) affiliation.second = -1;
	}
}

float ResultCoordinator::calculateSwingToIncumbent(Seat const & seat)
{
	seat;
	return 0.0f; // temporary, remove when restoring this section
	//std::array<int, 2> seatTotalVotes = { 0, 0 };
	//std::array<int, 2> seatTotalVotesOld = { 0, 0 };
	//for (auto booth : seat.latestResults->booths) {
	//	Results::Booth thisBooth = booths[booth];
	//	int totalOld = thisBooth.tcpVote[0] + thisBooth.tcpVote[1];
	//	int totalNew = thisBooth.newTcpVote[0] + thisBooth.newTcpVote[1];
	//	if (totalOld && totalNew) {
	//		bool matchedSame = (affiliationParties[thisBooth.tcpAffiliationId[0]] == candidateParties[seat.latestResults->finalCandidates[0].candidateId]);
	//		if (matchedSame) {
	//			seatTotalVotes[0] += thisBooth.newTcpVote[0];
	//			seatTotalVotes[1] += thisBooth.newTcpVote[1];
	//			seatTotalVotesOld[0] += thisBooth.tcpVote[0];
	//			seatTotalVotesOld[1] += thisBooth.tcpVote[1];
	//		}
	//		else {
	//			seatTotalVotes[0] += thisBooth.newTcpVote[1];
	//			seatTotalVotes[1] += thisBooth.newTcpVote[0];
	//			seatTotalVotesOld[0] += thisBooth.tcpVote[1];
	//			seatTotalVotesOld[1] += thisBooth.tcpVote[0];
	//		}
	//	}
	//}

	//int totalOldSeat = seatTotalVotesOld[0] + seatTotalVotesOld[1];
	//int totalNewSeat = seatTotalVotes[0] + seatTotalVotes[1];

	//if (totalOldSeat && totalNewSeat) {
	//	float swing = (float(seatTotalVotes[0]) / float(totalNewSeat) -
	//		float(seatTotalVotesOld[0]) / float(totalOldSeat)) * 100.0f;
	//	float swingToIncumbent = swing * (seat.incumbent == candidateParties[seat.latestResults->finalCandidates[0].candidateId] ? 1 : -1);
	//	return swingToIncumbent;
	//}
	//return 0;
}

float ResultCoordinator::calculate2cpPercentComplete(Seat const & seat)
{
	seat;
	return 0.0f; // temporary, remove when restoring this section
	//if (seat.latestResults->enrolment <= 0) return 0;
	//int totalVotes = 0;
	//for (auto booth : seat.latestResults->booths) {
	//	Results::Booth thisBooth = booths[booth];
	//	totalVotes += thisBooth.newTcpVote[0];
	//	totalVotes += thisBooth.newTcpVote[1];
	//}

	//totalVotes += seat.latestResults->declarationVotes();

	//return float(totalVotes) / float(seat.latestResults->enrolment) * 100.0f;
}

float ResultCoordinator::calculateFpPercentComplete(Seat const & seat)
{
	seat;
	return 0.0f; // temporary, remove when restoring this section
//	if (seat.latestResults->enrolment <= 0) return 0;
//	int totalVotes = seat.latestResults->totalFpVotes();
//
//	return float(totalVotes) / float(seat.latestResults->enrolment) * 100.0f;
}

void ResultCoordinator::matchPreviousElectionSeatsWithProjectSeats(PreviousElectionDataRetriever const& dataRetriever)
{
	dataRetriever;
	//int seatMatchCount = 0;
	//for (auto seatIt = dataRetriever.beginSeats(); seatIt != dataRetriever.endSeats(); ++seatIt) {
	//	auto seatData = seatIt->second;
	//	auto seatMatchFunc = [seatData](SeatCollection::SeatContainer::value_type const& seat)
	//	{ return seat.second.name == seatData.name || seat.second.previousName == seatData.name; };
	//	auto matchedSeat = std::find_if(project.seats().begin(), project.seats().end(), seatMatchFunc);
	//	if (matchedSeat != project.seats().end()) {
	//		matchedSeat->second.officialId = seatData.officialId;
	//		matchedSeat->second.previousResults = seatData;
	//		++seatMatchCount;
	//	}
	//	else {
	//		logger << "Note - No seat match found for " << seatData.name << ".\n";
	//		logger << "If this seat was abolished then this is ok, otherwise check the spelling of the existing seat data.\n";
	//		logger << "Also check that the existing seat data download link is correct and hasn't been set to skip the download.\n";
	//	}
	//}
	//logger << seatMatchCount << " seats matched.\n";
}

void ResultCoordinator::collectPreviousElectionBoothsAndCandidates(PreviousElectionDataRetriever const & dataRetriever)
{
	dataRetriever;
	//std::copy(dataRetriever.beginBooths(), dataRetriever.endBooths(), std::inserter(booths, booths.end()));
	//std::copy(dataRetriever.beginCandidates(), dataRetriever.endCandidates(), std::inserter(candidates, candidates.end()));
}

void ResultCoordinator::collectPreviousElectionAffiliations(PreviousElectionDataRetriever const & dataRetriever)
{
	dataRetriever;
	//affiliationParties.insert({ -1, Party::InvalidId });
	//affiliations.insert({ -1,{ "Invalid" } });
	//for (auto affiliationIt = dataRetriever.beginAffiliations(); affiliationIt != dataRetriever.endAffiliations(); ++affiliationIt) {
	//	affiliations.insert({ affiliationIt->first, affiliationIt->second });
	//	// Don't bother doing any string comparisons if this affiliation is already recorded
	//	if (affiliationParties.find(affiliationIt->first) == affiliationParties.end()) {
	//		for (auto const& party : project.parties()) {
	//			for (auto partyCode : party.second.officialCodes) {
	//				if (affiliationIt->second.shortCode == partyCode) {
	//					affiliationParties.insert({ affiliationIt->first, party.first });
	//				}
	//			}
	//		}
	//	}
	//}
}

void ResultCoordinator::relateCandidatesAndAffiliations(PreviousElectionDataRetriever const & dataRetriever)
{
	dataRetriever;
	//candidateParties.insert({ -1, Party::InvalidId });
	//for (auto candidateIt = dataRetriever.beginCandidates(); candidateIt != dataRetriever.endCandidates(); ++candidateIt) {
	//	candidates.insert(*candidateIt);
	//	int affiliationId = candidateIt->second.affiliationId;
	//	auto affiliationIt = affiliationParties.find(affiliationId);
	//	if (affiliationIt != affiliationParties.end()) {
	//		candidateParties.insert({ candidateIt->first, affiliationIt->second });
	//		candidateAffiliations.insert({ candidateIt->first, affiliationId });
	//	}
	//	else {
	//		// treat unknown party as independent
	//		candidateParties.insert({ candidateIt->first, affiliationParties[0] });
	//		candidateAffiliations.insert({ candidateIt->first, -1 });
	//	}
	//}
}

void ResultCoordinator::collectCandidatesFromPreload(PreloadDataRetriever const & dataRetriever)
{
	affiliationParties.insert({ -1, Party::InvalidId });
	for (auto affiliationIt = dataRetriever.beginAffiliations(); affiliationIt != dataRetriever.endAffiliations(); ++affiliationIt) {
		affiliations.insert({ affiliationIt->first, affiliationIt->second });
		// Don't bother doing any string comparisons if this affiliation is already recorded
		if (affiliationParties.find(affiliationIt->first) == affiliationParties.end()) {
			for (auto const& party : project.parties()) {
				for (auto partyCode : party.second.officialCodes) {
					if (affiliationIt->second.shortCode == partyCode) {
						affiliationParties.insert({ affiliationIt->first, party.first });
					}
				}
			}
		}
	}

	candidateParties.insert({ -1, Party::InvalidId });
	for (auto candidateIt = dataRetriever.beginCandidates(); candidateIt != dataRetriever.endCandidates(); ++candidateIt) {
		candidates.insert(*candidateIt);
		int affiliationId = candidateIt->second.affiliationId;
		candidateAffiliations.insert({ candidateIt->first, affiliationId });
		auto affiliationIt = affiliationParties.find(affiliationId);
		if (affiliationIt != affiliationParties.end()) {
			candidateParties.insert({ candidateIt->first, affiliationIt->second });
		}
		else {
			// treat unknown party as independent
			candidateParties.insert({ candidateIt->first, affiliationParties[0] });
		}
	}
}

void ResultCoordinator::collectBoothsFromPreload(PreloadDataRetriever const & dataRetriever)
{
	for (auto boothIt = dataRetriever.beginBooths(); boothIt != dataRetriever.endBooths(); ++boothIt) {
		auto foundBooth = booths.find(boothIt->first);
		if (foundBooth == booths.end()) {
			booths.insert({ boothIt->first, boothIt->second });
		}
		else {
			foundBooth->second.coords = boothIt->second.coords;
			foundBooth->second.name = boothIt->second.name;
		}
	}
}

void ResultCoordinator::matchBoothsFromLatestResults(LatestResultsDataRetriever const & dataRetriever)
{
	for (auto booth = dataRetriever.beginBooths(); booth != dataRetriever.endBooths(); ++booth) {
		auto const& newBooth = booth->second;

		// Determine which booth (if any) from the previous election this corresponds to
		auto oldBoothIt = booths.find(newBooth.officialId);
		if (oldBoothIt == booths.end()) continue;

		auto& matchedBooth = oldBoothIt->second;
		matchedBooth.fpCandidates = newBooth.fpCandidates; // always record fp candidates regardless of whether booth matching is successful

		// Check if the parties match
		bool allValid = true;
		Party::Id newParty[2] = { candidateParties[newBooth.tcpCandidateId[0]], candidateParties[newBooth.tcpCandidateId[1]] };
		Party::Id oldParty[2] = { affiliationParties[matchedBooth.tcpAffiliationId[0]], affiliationParties[matchedBooth.tcpAffiliationId[1]] };
		for (auto& a : newParty) if (a == Party::InvalidId) { allValid = false; };
		for (auto& a : oldParty) if (a == Party::InvalidId) { allValid = false; };
		bool matchedDirect = newParty[0] == oldParty[0] && (newParty[1] == oldParty[1]) && allValid;
		bool matchedOpposite = newParty[0] == oldParty[1] && (newParty[1] == oldParty[0]) && allValid;
		bool noOldResults = !matchedBooth.hasOldResults(); // no old results, therefore don't need to match for swing purposes, just get the results in whatever order
		bool newResults = newBooth.totalNewTcpVotes();

		// Transfer all the downloaded booth data to the existing booth records
		// Need to do this differently depending on how the previous booth data (if any) was stored
		if (matchedDirect || matchedOpposite || noOldResults) {
			if (matchedDirect || noOldResults) {
				matchedBooth.newTcpVote = newBooth.newTcpVote;
				matchedBooth.tcpCandidateId = newBooth.tcpCandidateId;
				if (noOldResults) {
					matchedBooth.tcpAffiliationId[0] = candidateAffiliations[newBooth.tcpCandidateId[0]];
					matchedBooth.tcpAffiliationId[1] = candidateAffiliations[newBooth.tcpCandidateId[1]];
				}
			}
			else if (matchedOpposite) {
				matchedBooth.newTcpVote = { newBooth.newTcpVote[1], newBooth.newTcpVote[0] };
				matchedBooth.tcpCandidateId = { newBooth.tcpCandidateId[1], newBooth.tcpCandidateId[0] };
			}
			matchedBooth.newResultsZero = newBooth.newResultsZero;
		}
		else if (newResults) {
			// Could not match parties and there are some results, wipe previous results
			matchedBooth.newTcpVote = newBooth.newTcpVote;
			matchedBooth.tcpCandidateId = newBooth.tcpCandidateId;
			matchedBooth.tcpAffiliationId[0] = candidateAffiliations[newBooth.tcpCandidateId[0]];
			matchedBooth.tcpAffiliationId[1] = candidateAffiliations[newBooth.tcpCandidateId[1]];
			matchedBooth.tcpVote = { 0, 0 };
			matchedBooth.newResultsZero = newBooth.newResultsZero;
		}
	}
}

void ResultCoordinator::matchSeatsFromLatestResults(LatestResultsDataRetriever const& dataRetriever)
{
	dataRetriever;
	//latestResults.clear();
	//latestResults.resize(project.seats().count());
	//for (auto retrievedSeat = dataRetriever.beginSeats(); retrievedSeat != dataRetriever.endSeats(); ++retrievedSeat) {
	//	auto seatResults = retrievedSeat->second;
	//	int matchingSeatIndex = findMatchingSeatIndex(seatResults);
	//	auto& matchedResult = latestResults[matchingSeatIndex];
	//	matchedResult = seatResults;
	//	for (auto& candidate : matchedResult->finalCandidates) {
	//		candidate.affiliationId = candidateAffiliations[candidate.candidateId];
	//	}
	//	for (auto& candidate : matchedResult->fpCandidates) {
	//		candidate.affiliationId = candidateAffiliations[candidate.candidateId];
	//	}
	//	// *** need something here to check if two-candidate preferred is not recorded because of seat maverick status
	//	if (matchedResult->total2cpVotes() && matchedResult->classic2pp) {
	//		Party::Id partyOne = candidateParties[matchedResult->finalCandidates[0].candidateId];
	//		Party::Id partyTwo = candidateParties[matchedResult->finalCandidates[1].candidateId];
	//		if (!project.parties().oppositeMajors(partyOne, partyTwo)) matchedResult->classic2pp = false;
	//	}
	//	if (previousResults.size() > matchingSeatIndex) {
	//		auto& previousResult = previousResults[matchingSeatIndex];
	//		if (matchingSeat.previousResults.has_value() && matchingSeat.previousResults->total2cpVotes() && matchingSeat.previousResults->classic2pp) {
	//			Party::Id partyOne = affiliationParties[matchingSeat.previousResults->finalCandidates[0].affiliationId];
	//			Party::Id partyTwo = affiliationParties[matchingSeat.previousResults->finalCandidates[1].affiliationId];
	//			if (!project.parties().oppositeMajors(partyOne, partyTwo)) matchingSeat.previousResults->classic2pp = false;
	//		}
	//	}
	//}
}

void ResultCoordinator::updateOutcomesFromLatestResults()
{
	project.updateOutcomesForSeats(); // only overwrite different results
	wxDateTime dateTime = wxDateTime::Now();
	for (auto& seatPair : project.seats()) {
		Seat& seat = seatPair.second;
		float percentCounted2cp = calculate2cpPercentComplete(seat);
		if (!percentCounted2cp) {
			if (seat.isClassic2pp(true)) continue;
			float percentCountedFp = calculateFpPercentComplete(seat);
			if (!percentCountedFp) continue;
			Outcome thisOutcome;
			thisOutcome.seat = seatPair.first;
			thisOutcome.percentCounted = percentCountedFp;
			thisOutcome.updateTime = dateTime;
			if (!seat.outcome || seat.outcome->percentCounted != percentCountedFp) {
				project.outcomes().add(thisOutcome);
			}
			continue;
		}
		float incumbentSwing = calculateSwingToIncumbent(seat);
		Outcome thisOutcome;
		thisOutcome.seat = seatPair.first;
		thisOutcome.incumbentSwing = incumbentSwing;
		thisOutcome.percentCounted = percentCounted2cp;
		thisOutcome.updateTime = dateTime;
		if (!seat.outcome || seat.outcome->percentCounted != percentCounted2cp) {
			project.outcomes().add(thisOutcome);
		}
	}
	project.updateOutcomesForSeats(); // only overwrite different results
}

int ResultCoordinator::findMatchingSeatIndex(Results::Seat seatData)
{
	auto seatMatchFunc = [seatData](SeatCollection::SeatContainer::value_type const& seat)
	{ return seat.second.name == seatData.name || seat.second.previousName == seatData.name; };
	return std::find_if(project.seats().begin(), project.seats().end(), seatMatchFunc)->first;
}
