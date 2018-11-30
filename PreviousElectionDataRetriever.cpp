#include "PreviousElectionDataRetriever.h"

#include "Debug.h"
#include "General.h"
#include "RegexNavigation.h"
#include "ResultsDownloader.h" // for location of results file

#include <fstream>

int extractSeatOfficialId(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "<PollingDistrictIdentifier Id=\"(\\d+)", searchIt);
}

std::string extractSeatName(std::string const& xmlString, SearchIterator& searchIt) {
	return extractString(xmlString, "<Name>([^<]{1,20})</Name>", searchIt);
}

int extractSeatEnrolment(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "<Enrolment CloseOfRolls=\"(\\d+)", searchIt);
}

// Skip ahead to the two-candidate preferred section of this seat's results
void seekToTcp(std::string const& xmlString, SearchIterator& searchIt) {
	seekTo(xmlString, "TwoCandidatePreferred", searchIt);
}

bool candidateIsIndependent(std::string const& xmlString, SearchIterator& searchIt) {
	return extractBool(xmlString, "<Candidate( Independent=\"true\")?>", searchIt);
	
}

int extractCandidateId(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "CandidateIdentifier Id=\"(\\d+)", searchIt);
}

std::string extractCandidateName(std::string const& xmlString, SearchIterator& searchIt) {
	return extractString(xmlString, "<eml:CandidateName>([^<]{1,40})</eml:CandidateName>", searchIt);
}

int extractAffiliationId(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "AffiliationIdentifier Id=\"(\\d+)", searchIt);
}

int extractOrdinaryVotes(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "<Votes Type=\"Ordinary\" [^>]*>(\\d+)</Votes>", searchIt);
}

int extractAbsentVotes(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "<Votes Type=\"Absent\" [^>]*>(\\d+)</Votes>", searchIt);
}

int extractProvisionalVotes(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "<Votes Type=\"Provisional\" [^>]*>(\\d+)</Votes>", searchIt);
}

int extractPrepollVotes(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "<Votes Type=\"PrePoll\" [^>]*>(\\d+)</Votes>", searchIt);
}

int extractPostalVotes(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "<Votes Type=\"Postal\" [^>]*>(\\d+)</Votes>", searchIt);
}

// Skip ahead to the two-candidate preferred section of this seat's results
void seekToBooths(std::string const& xmlString, SearchIterator& searchIt) {
	seekTo(xmlString, "PollingPlaces", searchIt);
}

int extractBoothOfficialId(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "<PollingPlaceIdentifier Id=\"(\\d+)", searchIt);
}

std::string extractBoothName(std::string const& xmlString, SearchIterator& searchIt) {
	return extractString(xmlString, "Name=\"([^\"]{1,60})\"", searchIt);
}

int extractBoothTcp(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "<Votes [^>]*>(\\d+)</Votes>", searchIt);
}

bool moreBoothData(std::string const& xmlString, SearchIterator const& searchIt) {
	return comesBefore(xmlString, "<PollingPlace", "</PollingPlaces>", searchIt);
}

bool moreSeatData(std::string const& xmlString, SearchIterator const& searchIt) {
	return comesBefore(xmlString, "<Contest", "<Analysis>", searchIt);
}

void PreviousElectionDataRetriever::collectData()
{
	std::ifstream file(TempResultsXmlFileName);
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
			for (size_t candidateNum = 0; candidateNum < 2; ++candidateNum) {
				Results::Candidate candidateData;
				bool independent = candidateIsIndependent(xmlString, searchIt);
				candidateData.name = extractCandidateName(xmlString, searchIt);
				if (!independent) {
					candidateData.affiliationId = extractAffiliationId(xmlString, searchIt);
				}
				candidateData.ordinaryVotes = extractOrdinaryVotes(xmlString, searchIt);
				candidateData.absentVotes = extractAbsentVotes(xmlString, searchIt);
				candidateData.provisionalVotes = extractProvisionalVotes(xmlString, searchIt);
				candidateData.prepollVotes = extractPrepollVotes(xmlString, searchIt);
				candidateData.postalVotes = extractPostalVotes(xmlString, searchIt);
				seatData.finalCandidates[candidateNum] = (candidateData);
			}
			seekToBooths(xmlString, searchIt);
			do {
				Results::Booth boothData;
				boothData.officialId = extractBoothOfficialId(xmlString, searchIt);
				boothData.name = extractBoothName(xmlString, searchIt);
				seekToTcp(xmlString, searchIt);
				boothData.tcpVote[0] = extractBoothTcp(xmlString, searchIt);
				boothData.tcpVote[1] = extractBoothTcp(xmlString, searchIt);
				boothData.affiliationId[0] = seatData.finalCandidates[0].affiliationId;
				boothData.affiliationId[1] = seatData.finalCandidates[1].affiliationId;
				seatData.booths.push_back(boothData.officialId);
				auto newBooth = boothMap.insert({ boothData.officialId, boothData });
				if (!newBooth.second) {
					PrintDebugInt(boothData.officialId);
					PrintDebugLine(" - Duplicate booth detected!");
				}
			} while (moreBoothData(xmlString, searchIt));
			auto newSeat = seatMap.insert({ seatData.officialId, seatData });
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
