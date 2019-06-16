#include "PreloadDataRetriever.h"

#include "General.h"
#include "Log.h"
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
	return extractBool(xmlString, "<Candidate( Independent=\"(yes|true)\")?", searchIt);
}

inline int extractCandidateId(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "CandidateIdentifier Id=\"(\\d+)", searchIt);
}

inline std::string extractCandidateName(std::string const& xmlString, SearchIterator& searchIt) {
	return extractString(xmlString, "<CandidateName>([^<]+)", searchIt);
}

inline int extractAffiliationId(std::string const& xmlString, SearchIterator& searchIt) {
	return extractInt(xmlString, "AffiliationIdentifier Id=\"(\\d+)", searchIt);
}

inline std::string extractAffiliationShortCode(std::string const& xmlString, SearchIterator& searchIt) {
	return extractString(xmlString, "ShortCode=\"([^\"]+)", searchIt);
}

inline bool moreCandidateData(std::string const& xmlString, SearchIterator const& searchIt) {
	return comesBefore(xmlString, "<CandidateIdentifier", "</CandidateList>", searchIt);
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

inline float extractPollingPlaceLatitude(std::string const& xmlString, SearchIterator& searchIt) {
	// negative sign because southerly longitude being a higher values
	// fits with the wxWidgets display coordinate system
	return -extractFloat(xmlString, "<xal:AddressLatitude>([^<]*)<", searchIt);
}

inline float extractPollingPlaceLongitude(std::string const& xmlString, SearchIterator& searchIt) {
	return extractFloat(xmlString, "<xal:AddressLongitude>([^<]*)<", searchIt);
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
				Results::Candidate candidate;
				bool independent = candidateIsIndependent(xmlString, searchIt);
				int candidateId = extractCandidateId(xmlString, searchIt);
				candidate.name = extractCandidateName(xmlString, searchIt);
				int affiliationId = 0;
				if (!independent && comesBefore(xmlString, "<Affiliation", "</Candidate>", searchIt)) {
					affiliationId = extractAffiliationId(xmlString, searchIt);
					affiliations.insert({ affiliationId , {extractAffiliationShortCode(xmlString, searchIt) } });
				}
				candidate.affiliationId = affiliationId;
				candidates.insert({ candidateId, candidate });
				if (!moreCandidateData(xmlString, searchIt)) break;
				seekToNextCandidate(xmlString, searchIt);
			} while (true);

		} while (moreSeatData(xmlString, searchIt));

		logger << "Preload (candidates) download complete!\n";
	}
	catch (const std::regex_error& e) {
		logger << "regex_error caught: " << e.what() << "\n";
	}

	std::ifstream boothsFile(UnzippedBoothsFileName);
	xmlString.clear();
	transferFileToString(boothsFile, xmlString);

	try {
		std::string::const_iterator searchIt = xmlString.begin();
		do {
			Results::Booth boothData;
			boothData.coords.latitude = extractPollingPlaceLatitude(xmlString, searchIt);
			boothData.coords.longitude = extractPollingPlaceLongitude(xmlString, searchIt);
			boothData.officialId = extractPollingPlaceId(xmlString, searchIt);
			boothData.name = extractPollingPlaceName(xmlString, searchIt);

			booths.insert({ boothData.officialId, boothData });
		} while (moreBoothData(xmlString, searchIt));

		logger << "Preload (booths) download complete!" << "\n";
	}
	catch (const std::regex_error& e) {
		logger << "regex_error caught: " << e.what() << "\n";
	}
}
