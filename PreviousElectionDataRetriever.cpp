#include "PreviousElectionDataRetriever.h"

#include "General.h"
#include "Log.h"

#include "tinyxml2.h"

#include <fstream>

const std::string PreviousElectionDataRetriever::UnzippedFileName = "downloads/previous_results.xml";

namespace {

	inline int extractSeatOfficialId(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<eml:ContestIdentifier Id=\"(\\d+)", searchIt);
	}

	inline std::string extractSeatName(std::string const& xmlString, SearchIterator& searchIt) {
		return extractString(xmlString, "<Name>([^<]{1,20})</Name>", searchIt);
	}

	inline int extractSeatEnrolment(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "<Enrolment CloseOfRolls=\"(\\d+)", searchIt);
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
		return extractBool(xmlString, "<Candidate( Independent=\"(yes|true)\")?", searchIt);
	}

	inline int extractCandidateId(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "CandidateIdentifier Id=\"(\\d+)", searchIt);
	}

	inline std::string extractCandidateName(std::string const& xmlString, SearchIterator& searchIt) {
		return extractString(xmlString, "<eml:CandidateName>([^<]{1,40})</eml:CandidateName>", searchIt);
	}

	inline int extractAffiliationId(std::string const& xmlString, SearchIterator& searchIt) {
		return extractInt(xmlString, "AffiliationIdentifier Id=\"(\\d+)", searchIt);
	}

	inline std::string extractAffiliationShortCode(std::string const& xmlString, SearchIterator& searchIt) {
		return extractString(xmlString, "ShortCode=\"([^\"]+)", searchIt);
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
		seekTo(xmlString, "PollingPlaces", searchIt);
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
		return comesBefore(xmlString, "<Contest", "<Analysis>", searchIt);
	}

}

void PreviousElectionDataRetriever::collectData()
{
	std::ifstream file(UnzippedFileName);
	std::string xmlString;
	transferFileToString(file, xmlString);

	//try {
	//	std::string::const_iterator searchIt = xmlString.begin();
	//	do {
	//		Results::Seat seatData;
	//		extractGeneralSeatInfo(xmlString, searchIt, seatData);
	//		extractFpResults(xmlString, searchIt, seatData);
	//		extractTcpResults(xmlString, searchIt, seatData);
	//		extractBoothResults(xmlString, searchIt, seatData);
	//		auto newSeat = seatMap.insert({ seatData.officialId, seatData });
	//		if (!newSeat.second) {
	//			logger << seatData.officialId << " - Duplicate seat detected!\n"; // this shouldn't happen
	//		}
	//	} while (moreSeatData(xmlString, searchIt));
	//	affiliations.insert({ 0, {"IND"} });
	//	logger << "Previous election download complete!\n";
	//}
	//catch (const std::regex_error& e) {
	//	logger << "regex_error caught: " << e.what() << "\n";
	//}
	file.close();

	tinyxml2::XMLDocument doc;
	doc.LoadFile(UnzippedFileName.c_str());
	auto contests = doc.FirstChildElement("MediaFeed")->FirstChildElement("Results")->FirstChildElement("Election")
		->FirstChildElement("House")->FirstChildElement("Contests");
	auto currentContest = contests->FirstChildElement("Contest");
	do {
		auto districtElement = currentContest->FirstChildElement("PollingDistrictIdentifier");
		auto districtId = districtElement->FindAttribute("Id")->IntValue();
		auto districtName = districtElement->FirstChildElement("Name")->GetText();
		auto enrolment = currentContest->FirstChildElement("Enrolment")->IntText();
		logger << "Contest name: " << districtName << "  Id: " << districtId << "  Enrolment: " << enrolment << "\n";
		auto currentCandidate = currentContest->FirstChildElement("FirstPreferences")->FirstChildElement("Candidate");
		do {
			auto isIndependent = (currentCandidate->FindAttribute("Independent") != nullptr || 
				currentCandidate->FindAttribute("NoAffiliation") != nullptr);
			auto candidateIdElement = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
			auto candidateName = candidateIdElement->FirstChildElement("eml:CandidateName")->GetText();
			auto candidateId = candidateIdElement->FindAttribute("Id")->IntValue();
			auto votes = currentCandidate->FirstChildElement("Votes")->IntText();
			int affiliationId = 0;
			std::string affiliationName = "";
			std::string affiliationShortCode = "";
			if (!isIndependent) {
				auto affiliationElement = currentCandidate->FirstChildElement("eml:AffiliationIdentifier");
				affiliationName = affiliationElement->FirstChildElement("eml:RegisteredName")->GetText();
				affiliationId = affiliationElement->FindAttribute("Id")->IntValue();
				affiliationShortCode = affiliationElement->FindAttribute("ShortCode")->Value();
			}
			// *** Next: get fp votes by vote type (ordinary, postal, etc.)
			logger << " Candidate: " << candidateName << "  Id: " << candidateId;
			if (isIndependent) logger << " - Independent - ";
			else logger << " - " << affiliationName << " (" << affiliationShortCode << ", " << affiliationId << ") - ";
			logger << "Votes: " << votes << "\n";
			currentCandidate = currentCandidate->NextSiblingElement("Candidate");
		} while (currentCandidate);
		currentContest = currentContest->NextSiblingElement("Contest");
	} while (currentContest);
}

void PreviousElectionDataRetriever::extractGeneralSeatInfo(std::string const & xmlString, SearchIterator & searchIt, Results::Seat & seatData)
{
	seatData.officialId = extractSeatOfficialId(xmlString, searchIt);
	seatData.name = extractSeatName(xmlString, searchIt);
	seatData.enrolment = extractSeatEnrolment(xmlString, searchIt);
}

void PreviousElectionDataRetriever::extractFpResults(std::string const & xmlString, SearchIterator & searchIt, Results::Seat & seatData)
{
	seekToFp(xmlString, searchIt);
	if (comesBefore(xmlString, "<Candidate>", "TwoCandidatePreferred", searchIt)) {
		do {
			extractFpCandidate(xmlString, searchIt, seatData);
		} while (moreFpData(xmlString, searchIt));
		std::sort(seatData.oldFpCandidates.begin(), seatData.oldFpCandidates.end(),
			[](Results::Seat::Candidate lhs, Results::Seat::Candidate rhs) {return lhs.totalVotes() > rhs.totalVotes(); });
	}
}

void PreviousElectionDataRetriever::extractFpCandidate(std::string const & xmlString, SearchIterator & searchIt, Results::Seat & seatData)
{
	Results::Seat::Candidate candidateData;
	Results::Candidate candidate;
	bool independent = candidateIsIndependent(xmlString, searchIt);
	candidateData.candidateId = extractCandidateId(xmlString, searchIt);
	candidate.name = extractCandidateName(xmlString, searchIt);
	int affiliationId = 0;
	if (!independent && comesBefore(xmlString, "Affiliation", "</Candidate>", searchIt)) {
		affiliationId = extractAffiliationId(xmlString, searchIt);
		affiliations.insert({ affiliationId ,{ extractAffiliationShortCode(xmlString, searchIt) } });
	}
	candidate.affiliationId = affiliationId;
	candidateData.ordinaryVotes = extractOrdinaryVotes(xmlString, searchIt);
	candidateData.absentVotes = extractAbsentVotes(xmlString, searchIt);
	candidateData.provisionalVotes = extractProvisionalVotes(xmlString, searchIt);
	candidateData.prepollVotes = extractPrepollVotes(xmlString, searchIt);
	candidateData.postalVotes = extractPostalVotes(xmlString, searchIt);
	seatData.oldFpCandidates.push_back(candidateData);
	candidates.insert({ candidateData.candidateId, candidate });
}

void PreviousElectionDataRetriever::extractTcpResults(std::string const & xmlString, SearchIterator & searchIt, Results::Seat & seatData)
{
	seekToTcp(xmlString, searchIt);
	for (size_t candidateNum = 0; candidateNum < 2; ++candidateNum) {
		extractTcpCandidate(xmlString, searchIt, seatData, candidateNum);
	}
}

void PreviousElectionDataRetriever::extractTcpCandidate(std::string const & xmlString, SearchIterator & searchIt, Results::Seat & seatData, int candidateNum)
{
	Results::Seat::Candidate candidateData;
	bool independent = candidateIsIndependent(xmlString, searchIt);
	candidateData.candidateId = extractCandidateId(xmlString, searchIt);
	if (!independent) {
		candidateData.affiliationId = extractAffiliationId(xmlString, searchIt);
		affiliations.insert({ candidateData.affiliationId ,{ extractAffiliationShortCode(xmlString, searchIt) } });
	}
	candidateData.ordinaryVotes = extractOrdinaryVotes(xmlString, searchIt);
	candidateData.absentVotes = extractAbsentVotes(xmlString, searchIt);
	candidateData.provisionalVotes = extractProvisionalVotes(xmlString, searchIt);
	candidateData.prepollVotes = extractPrepollVotes(xmlString, searchIt);
	candidateData.postalVotes = extractPostalVotes(xmlString, searchIt);
	seatData.finalCandidates[candidateNum] = (candidateData);
}

void PreviousElectionDataRetriever::extractBoothResults(std::string const & xmlString, SearchIterator & searchIt, Results::Seat & seatData)
{
	seekToBooths(xmlString, searchIt);
	do {
		extractBooth(xmlString, searchIt, seatData);
	} while (moreBoothData(xmlString, searchIt));
}

void PreviousElectionDataRetriever::extractBooth(std::string const & xmlString, SearchIterator & searchIt, Results::Seat & seatData)
{
	Results::Booth boothData;
	boothData.officialId = extractBoothOfficialId(xmlString, searchIt);
	boothData.name = extractBoothName(xmlString, searchIt);
	seekToFp(xmlString, searchIt);
	while (comesBefore(xmlString, "<Candidate>", "TwoCandidatePreferred", searchIt)) {
		Results::Booth::Candidate candidate;
		candidate.candidateId = extractCandidateId(xmlString, searchIt);
		auto matchedCandidate = std::find_if(seatData.oldFpCandidates.begin(), seatData.oldFpCandidates.end(),
			[&candidate](Results::Seat::Candidate const& c) {return c.candidateId == candidate.candidateId; });
		if (matchedCandidate != seatData.oldFpCandidates.end()) candidate.affiliationId = matchedCandidate->affiliationId;
		candidate.fpVotes = extractBoothVotes(xmlString, searchIt);
		boothData.oldFpCandidates.push_back(candidate);
	}
	seekToTcp(xmlString, searchIt);
	boothData.tcpVote[0] = extractBoothVotes(xmlString, searchIt);
	boothData.tcpVote[1] = extractBoothVotes(xmlString, searchIt);
	boothData.tcpAffiliationId[0] = seatData.finalCandidates[0].affiliationId;
	boothData.tcpAffiliationId[1] = seatData.finalCandidates[1].affiliationId;
	seatData.booths.push_back(boothData.officialId);
	auto newBooth = boothMap.insert({ boothData.officialId, boothData });
	if (!newBooth.second) {
		logger << boothData.officialId << " - Duplicate booth detected!\n";
	}
}
