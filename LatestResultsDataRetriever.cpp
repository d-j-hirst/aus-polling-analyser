#include "LatestResultsDataRetriever.h"
#include "Log.h"

#include <fstream>

const std::string LatestResultsDataRetriever::UnzippedFileName = "downloads/custom_results.xml";
const std::string LatestResultsDataRetriever::DirectoryListingFileName = "downloads/results_directory_listing.xml";

namespace {

	inline int extractSeatOfficialId(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<eml:ContestIdentifier Id=\"(\\d+)", searchIt);
	}

	inline std::string extractSeatName(std::string const& xmlString, SearchIterator& searchIt) {
		return extractString(xmlString, "Name>([^<]{1,20})<", searchIt);
	}

	inline int extractSeatEnrolment(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<Enrolment[^>]*>(\\d+)", searchIt);
	}

	// Skip ahead to the first preference section of this seat's results
	inline void seekToFp(std::string const& xmlString, SearchIterator& searchIt) {
		seekTo(xmlString, "FirstPreferences", searchIt);
	}

	// Skip ahead to the two-candidate preferred section of this seat's results
	inline void seekToTcp(std::string const& xmlString, SearchIterator& searchIt) {
		seekTo(xmlString, "TwoCandidatePreferred", searchIt);
	}

	inline bool candidateIsIndependent(std::string const& xmlString, SearchIterator& searchIt) {
		return extractBool(xmlString, "<Candidate( Independent=\"true\")?>", searchIt);

	}

	inline int extractCandidateId(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "CandidateIdentifier Id=\"(\\d+)", searchIt);
	}

	inline int extractAffiliationId(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "AffiliationIdentifier Id=\"(\\d+)", searchIt);
	}

	inline int extractOrdinaryVotes(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<Votes Type=\"Ordinary\"[^>]*>(\\d+)</Votes>", searchIt);
	}

	inline int extractAbsentVotes(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<Votes Type=\"Absent\"[^>]*>(\\d+)</Votes>", searchIt);
	}

	inline int extractProvisionalVotes(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<Votes Type=\"Provisional\"[^>]*>(\\d+)</Votes>", searchIt);
	}

	inline int extractPrepollVotes(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<Votes Type=\"PrePoll\"[^>]*>(\\d+)</Votes>", searchIt);
	}

	inline int extractPostalVotes(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<Votes Type=\"Postal\"[^>]*>(\\d+)</Votes>", searchIt);
	}

	// Skip ahead to the two-candidate preferred section of this seat's results
	inline void seekToBooths(std::string const& xmlString, SearchIterator& searchIt) {
		seekTo(xmlString, "<PollingPlaces>", searchIt);
	}

	inline int extractBoothOfficialId(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<PollingPlaceIdentifier Id=\"(\\d+)", searchIt);
	}

	inline std::string extractBoothName(std::string const& xmlString, SearchIterator& searchIt) {
		return extractString(xmlString, "Name=\"([^\"]{1,60})\"", searchIt);
	}

	inline int extractBoothVotes(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<Votes[^>]*>(\\d+)</Votes>", searchIt);
	}

	inline bool moreFpData(std::string const& xmlString, SearchIterator const& searchIt) {
		return comesBefore(xmlString, "<Candidate", "</FirstPreferences>", searchIt);
	}

	inline bool moreBoothData(std::string const& xmlString, SearchIterator const& searchIt) {
		return comesBefore(xmlString, "<PollingPlace", "</PollingPlaces>", searchIt);
	}

	inline bool moreSeatData(std::string const& xmlString, SearchIterator const& searchIt) {
		return comesBefore(xmlString, "<Contest", "</House>", searchIt);
	}

}

void LatestResultsDataRetriever::collectData()
{
	std::ifstream file(UnzippedFileName);
	std::string xmlString;
	transferFileToString(file, xmlString);

	try {
		std::string::const_iterator searchIt = xmlString.begin();
		do {
			auto seatData = extractSeatData(xmlString, searchIt);
			extractBoothData(xmlString, searchIt, seatData);
			auto newSeat = seatMap.insert({ seatData.officialId, seatData });
			if (!newSeat.second) {
				logger << seatData.officialId << " - Duplicate seat detected!\n"; // this shouldn't happen
			}
		} while (moreSeatData(xmlString, searchIt));
		logger << "Latest results download complete!\n";
	}
	catch (const std::regex_error& e) {
		logger << "regex_error caught: " << e.what() << "\n";
	}
}

Results::Seat LatestResultsDataRetriever::extractSeatData(std::string const& xmlString, SearchIterator& searchIt)
{
	Results::Seat seatData;
	seatData.officialId = extractSeatOfficialId(xmlString, searchIt);
	seatData.name = extractSeatName(xmlString, searchIt);
	seatData.enrolment = extractSeatEnrolment(xmlString, searchIt);
	seekToFp(xmlString, searchIt);
	if (comesBefore(xmlString, "<Candidate>", "TwoCandidatePreferred", searchIt)) {
		do {
			auto candidateData = extractSeatCandidate(xmlString, searchIt);
			seatData.fpCandidates.push_back(candidateData);
		} while (moreFpData(xmlString, searchIt));
		std::sort(seatData.fpCandidates.begin(), seatData.fpCandidates.end(),
			[](Results::Seat::Candidate lhs, Results::Seat::Candidate rhs) {return lhs.totalVotes() > rhs.totalVotes(); });
	}
	seekToTcp(xmlString, searchIt);
	if (!comesBefore(xmlString, "<PollingPlaces>", "<Candidate>", searchIt)) {
		for (size_t candidateNum = 0; candidateNum < 2; ++candidateNum) {
			auto candidateData = extractSeatCandidate(xmlString, searchIt);
			seatData.finalCandidates[candidateNum] = (candidateData);
		}
	}
	else if (!comesBefore(xmlString, "<PollingPlaces>", "Maverick=\"true\"", searchIt)) {
		seatData.classic2pp = false;
	}
	return seatData;
}

Results::Seat::Candidate LatestResultsDataRetriever::extractSeatCandidate(std::string const& xmlString, SearchIterator& searchIt)
{
	Results::Seat::Candidate candidateData;
	candidateData.candidateId = extractCandidateId(xmlString, searchIt);
	candidateData.ordinaryVotes = extractOrdinaryVotes(xmlString, searchIt);
	candidateData.absentVotes = extractAbsentVotes(xmlString, searchIt);
	candidateData.provisionalVotes = extractProvisionalVotes(xmlString, searchIt);
	candidateData.prepollVotes = extractPrepollVotes(xmlString, searchIt);
	candidateData.postalVotes = extractPostalVotes(xmlString, searchIt);
	return candidateData;
}

void LatestResultsDataRetriever::extractBoothData(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData)
{
	seekToBooths(xmlString, searchIt);
	do {
		Results::Booth boothData = extractIndividualBooth(xmlString, searchIt, seatData);
		seatData.booths.push_back(boothData.officialId);
		auto newBooth = boothMap.insert({ boothData.officialId, boothData });
		if (!newBooth.second) logger << boothData.officialId << " - Duplicate booth detected!\n";
	} while (moreBoothData(xmlString, searchIt));
}

Results::Booth LatestResultsDataRetriever::extractIndividualBooth(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData)
{
	Results::Booth boothData;
	boothData.officialId = extractBoothOfficialId(xmlString, searchIt);
	seekToFp(xmlString, searchIt);
	while (comesBefore(xmlString, "<Candidate>", "TwoCandidatePreferred", searchIt)) {
		Results::Booth::Candidate candidateData = extractBoothCandidate(xmlString, searchIt);
		boothData.fpCandidates.push_back(candidateData);
	}
	extractBooth2cp(xmlString, searchIt, seatData, boothData);
	return boothData;
}

Results::Booth::Candidate LatestResultsDataRetriever::extractBoothCandidate(std::string const& xmlString, SearchIterator& searchIt)
{
	Results::Booth::Candidate candidateData;
	candidateData.candidateId = extractCandidateId(xmlString, searchIt);
	candidateData.fpVotes = extractBoothVotes(xmlString, searchIt);
	return candidateData;
}

void LatestResultsDataRetriever::extractBooth2cp(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData, Results::Booth& boothData)
{
	seekToTcp(xmlString, searchIt);
	if (comesBefore(xmlString, "<Candidate>", "</PollingPlace>", searchIt)) {
		bool resultsIn = !comesBefore(xmlString, "</PollingPlace>", "Updated", searchIt);
		boothData.newTcpVote[0] = extractBoothVotes(xmlString, searchIt);
		boothData.newTcpVote[1] = extractBoothVotes(xmlString, searchIt);
		boothData.tcpCandidateId[0] = seatData.finalCandidates[0].candidateId;
		boothData.tcpCandidateId[1] = seatData.finalCandidates[1].candidateId;
		boothData.tcpAffiliationId[0] = seatData.finalCandidates[0].affiliationId;
		boothData.tcpAffiliationId[1] = seatData.finalCandidates[1].affiliationId;
		if (resultsIn & !(boothData.newTcpVote[0] + boothData.newTcpVote[1])) boothData.newResultsZero = true;
	}
}
