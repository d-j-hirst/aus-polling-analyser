#include "LatestResultsDataRetriever.h"

#include "Debug.h"
#include "General.h"
#include "RegexNavigation.h"

#include <fstream>

const std::string LatestResultsDataRetriever::UnzippedFileName = "downloads/latest_results.xml";

namespace {

	inline int extractSeatOfficialId(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<eml:ContestIdentifier Id=\"(\\d+)", searchIt);
	}

	inline std::string extractSeatName(std::string const& xmlString, SearchIterator& searchIt) {
		return extractString(xmlString, "Name>([^<]{1,20})<", searchIt);
	}

	inline int extractSeatEnrolment(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<Enrolment>(\\d+)", searchIt);
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

	inline int extractBoothTcp(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<Votes[^>]*>(\\d+)</Votes>", searchIt);
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
			Results::Seat seatData;
			seatData.officialId = extractSeatOfficialId(xmlString, searchIt);
			seatData.name = extractSeatName(xmlString, searchIt);
			seatData.enrolment = extractSeatEnrolment(xmlString, searchIt);
			seekToTcp(xmlString, searchIt);
			if (comesBefore(xmlString, "<Candidate>", "<PollingPlaces>", searchIt)) {
				for (size_t candidateNum = 0; candidateNum < 2; ++candidateNum) {
					Results::Candidate candidateData;
					candidateData.candidateId = extractCandidateId(xmlString, searchIt);
					candidateData.ordinaryVotes = extractOrdinaryVotes(xmlString, searchIt);
					candidateData.absentVotes = extractAbsentVotes(xmlString, searchIt);
					candidateData.provisionalVotes = extractProvisionalVotes(xmlString, searchIt);
					candidateData.prepollVotes = extractPrepollVotes(xmlString, searchIt);
					candidateData.postalVotes = extractPostalVotes(xmlString, searchIt);
					seatData.finalCandidates[candidateNum] = (candidateData);
				}
			}
			seekToBooths(xmlString, searchIt);
			do {
				Results::Booth boothData;
				boothData.officialId = extractBoothOfficialId(xmlString, searchIt);
				seekToTcp(xmlString, searchIt);
				if (comesBefore(xmlString, "<Candidate>", "</PollingPlace>", searchIt)) {
					bool resultsIn = comesBefore(xmlString, "Updated", "</PollingPlace>", searchIt);
					boothData.newTcpVote[0] = extractBoothTcp(xmlString, searchIt);
					boothData.newTcpVote[1] = extractBoothTcp(xmlString, searchIt);
					boothData.candidateId[0] = seatData.finalCandidates[0].candidateId;
					boothData.candidateId[1] = seatData.finalCandidates[1].candidateId;
					boothData.affiliationId[0] = seatData.finalCandidates[0].affiliationId;
					boothData.affiliationId[1] = seatData.finalCandidates[1].affiliationId;
					if (resultsIn & !(boothData.newTcpVote[0] + boothData.newTcpVote[1])) boothData.newResultsZero = true;
				}
				seatData.booths.push_back(boothData.officialId);
				auto newBooth = boothMap.insert({ boothData.officialId, boothData });
				if (!newBooth.second) {
					PrintDebugInt(boothData.officialId);
					PrintDebugLine(" - Duplicate booth detected!");
				}
			} while (moreBoothData(xmlString, searchIt));
			auto newSeat = seatMap.insert({ seatData.officialId, seatData });
			PrintDebugLine(seatData.name);
			if (!newSeat.second) {
				PrintDebugInt(seatData.officialId);
				PrintDebugLine(" - Duplicate seat detected!"); // this shouldn't happen
			}
		} while (moreSeatData(xmlString, searchIt));
		PrintDebugLine("Download complete!");
	}
	catch (const std::regex_error& e) {
		PrintDebug("regex_error caught: ");
		PrintDebugLine(e.what());
	}
}
