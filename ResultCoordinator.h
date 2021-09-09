#pragma once

#include "ElectionData.h"
#include "Party.h"
#include "PartyCollection.h"
#include "Points.h"

#include <unordered_map>

class LatestResultsDataRetriever;
class PollingProject;
class PreloadDataRetriever;
class PreviousElectionDataRetriever;
class Seat;

class ResultCoordinator {
public:
	ResultCoordinator(PollingProject& project);

	void incorporatePreviousElectionResults(PreviousElectionDataRetriever const& dataRetriever);

	void incorporatePreloadData(PreloadDataRetriever const& dataRetriever);

	void incorporateLatestResults(LatestResultsDataRetriever const& dataRetriever);

	// Gets the booth matching this official ID.
	Results::Booth const& getBooth(int boothId) const;

	Point2Df boothLatitudeRange() const;

	Point2Df boothLongitudeRange() const;

	// Returns the party that this candidate ID refers to.
	// Returns nullptr if candidate did not match any known party
	Party::Id getPartyByCandidate(int candidateId) const;

	// Returns the party that this affiliation ID refers to.
	// Returns nullptr if affiliation did not match any known party
	Party::Id getPartyByAffiliation(int affiliationId) const;

	// Gets data for the candidate this id refers to
	// Returns nullptr if the id does not match a known candidate
	Results::Candidate const* getCandidateById(int candidateId) const;

	// Gets data for the candidate this id refers to
	// Returns nullptr if the id does not match a known candidate
	Results::Affiliation const* getAffiliationById(int affiliationId) const;

	// Gets the affiliation Id for the given candidate (-1 if candidate not found).
	int getCandidateAffiliationId(int candidateId) const;

	void adjustCandidatesAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId);
	void adjustAffiliationsAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId);

	// Assuming there is live results data added for this seat, calculates the swing to the incumbent here.
	float calculateSwingToIncumbent(Seat const& seat);

	// Assuming there is live results data added for this seat, calculates the % completion of the two-party preferred vote.
	float calculate2cpPercentComplete(Seat const& seat);

	// Assuming there is live results data added for this seat, calculates the % completion of the two-party preferred vote.
	float calculateFpPercentComplete(Seat const& seat);

	// Booth data from a download
	std::unordered_map<int, Results::Booth> booths;

	typedef std::unordered_map<int, Party::Id> IdPartyMap;
	typedef std::unordered_map<int, int> CandidateAffiliationMap;
	typedef std::unordered_map<int, Results::Candidate> CandidateMap;
	typedef std::unordered_map<int, Results::Affiliation> AffiliationMap;
	IdPartyMap affiliationParties;
	IdPartyMap candidateParties;
	CandidateAffiliationMap candidateAffiliations;
	CandidateMap candidates;
	AffiliationMap affiliations;

private:
	// Returns number of seats matched
	void matchPreviousElectionSeatsWithProjectSeats(PreviousElectionDataRetriever const& dataRetriever);
	void collectPreviousElectionBoothsAndCandidates(PreviousElectionDataRetriever const& dataRetriever);
	// Creates the map between what affiliation numbers and the parties in the project that those
	// affiliation numbers correspond to.
	void collectPreviousElectionAffiliations(PreviousElectionDataRetriever const& dataRetriever);
	void relateCandidatesAndAffiliations(PreviousElectionDataRetriever const& dataRetriever);

	// Creates the map between candidates and parties that they belong to.
	void collectCandidatesFromPreload(PreloadDataRetriever const& dataRetriever);

	// Adds booth information from preload data to the project. Necessary because some booths may not have
	// existed prior to this election.
	void collectBoothsFromPreload(PreloadDataRetriever const& dataRetriever);

	void matchBoothsFromLatestResults(LatestResultsDataRetriever const& dataRetriever);
	void matchSeatsFromLatestResults(LatestResultsDataRetriever const& dataRetriever);
	void updateOutcomesFromLatestResults();

	int findMatchingSeatIndex(Results::Seat seatData);

	PollingProject& project;
};