#include "PreloadDataRetriever.h"

#include "Debug.h"
#include "General.h"
#include "RegexNavigation.h"

#include <fstream>

const std::string PreloadDataRetriever::UnzippedCandidatesFileName = "downloads/preload_candidates.xml";
const std::string PreloadDataRetriever::UnzippedBoothsFileName = "downloads/preload_booths.xml";

const std::string PreloadDataRetriever::BoothsMatch = "pollingdistricts";
const std::string PreloadDataRetriever::CandidateMatch = "candidates";

// Skip ahead to the two-candidate preferred section of this seat's results
inline void seekToFirstPreferences(std::string const& xmlString, SearchIterator& searchIt) {
	seekTo(xmlString, "</ContestIdentifier>", searchIt);
}

inline bool candidateIsIndependent(std::string const& xmlString, SearchIterator& searchIt) {
	return extractBool(xmlString, "<Candidate( Independent=\"yes\")?", searchIt);
}

inline int extractCandidateId(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "CandidateIdentifier Id=\"(\\d+)", searchIt);
}

inline int extractAffiliationId(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "AffiliationIdentifier Id=\"(\\d+)", searchIt);
}

inline std::string extractAffiliationShortCode(std::string const& xmlString, SearchIterator& searchIt) {
	return extractString(xmlString, "ShortCode=\"([^\"]+)", searchIt);
}

inline bool moreCandidateData(std::string const& xmlString, SearchIterator const& searchIt) {
	return comesBefore(xmlString, "<Candidate", "<Formal>", searchIt);
}

inline void seekToNextCandidate(std::string const& xmlString, SearchIterator& searchIt) {
	seekTo(xmlString, "</Candidate>", searchIt);
}

inline bool moreSeatData(std::string const& xmlString, SearchIterator const& searchIt) {
	return comesBefore(xmlString, "<Contest", "</Election>", searchIt);
}

inline void seekToPollingPlace(std::string const& xmlString, SearchIterator& searchIt) {
	seekTo(xmlString, "<PollingPlace", searchIt);
}

inline int extractPollingPlaceId(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "<PollingPlaceIdentifier Id=\"(\\d+)", searchIt);
}

inline std::string extractPollingPlaceName(std::string const& xmlString, SearchIterator& searchIt) {
	return extractString(xmlString, "Name=\"([^\"]+)", searchIt);
}

inline bool moreBoothData(std::string const& xmlString, SearchIterator const& searchIt) {
	return comesBefore(xmlString, "<PollingPlace", "</MediaFeed>", searchIt);
}

void PreloadDataRetriever::collectData()
{
	std::ifstream candidatesFile(UnzippedCandidatesFileName);
	std::string xmlString;
	transferFileToString(candidatesFile, xmlString);

	try {
		std::string::const_iterator searchIt = xmlString.begin();
		do {
			seekToFirstPreferences(xmlString, searchIt);
			do {
				bool independent = candidateIsIndependent(xmlString, searchIt);
				int candidateId = extractCandidateId(xmlString, searchIt);
				int affiliationId = 0;
				if (!independent && comesBefore(xmlString, "<Affiliation", "</Candidate>", searchIt)) {
					affiliationId = extractAffiliationId(xmlString, searchIt);
					affiliations.insert({ affiliationId , extractAffiliationShortCode(xmlString, searchIt) });
				}
				candidates.insert({ candidateId, affiliationId });
				if (!moreCandidateData(xmlString, searchIt)) break;
				seekToNextCandidate(xmlString, searchIt);
			} while (true);

		} while (moreSeatData(xmlString, searchIt));

		PrintDebugLine("Download complete!");
	}
	catch (const std::regex_error& e) {
		PrintDebug("regex_error caught: ");
		PrintDebugLine(e.what());
	}

	std::ifstream boothsFile(UnzippedBoothsFileName);
	xmlString.clear();
	transferFileToString(boothsFile, xmlString);

	try {
		std::string::const_iterator searchIt = xmlString.begin();
		do {
			Results::Booth boothData;
			boothData.officialId = extractPollingPlaceId(xmlString, searchIt);
			boothData.name = extractPollingPlaceName(xmlString, searchIt);

			booths.insert({ boothData.officialId, boothData });
		} while (moreBoothData(xmlString, searchIt));

		PrintDebugLine("Download complete!");
	}
	catch (const std::regex_error& e) {
		PrintDebug("regex_error caught: ");
		PrintDebugLine(e.what());
	}
}
