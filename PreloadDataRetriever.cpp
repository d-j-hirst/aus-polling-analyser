#include "PreloadDataRetriever.h"

#include "Debug.h"
#include "General.h"
#include "RegexNavigation.h"

#include <fstream>

const std::string PreloadDataRetriever::UnzippedFileName = "downloads/preload.xml";

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

inline bool moreCandidateData(std::string const& xmlString, SearchIterator const& searchIt) {
	return comesBefore(xmlString, "<Candidate", "<Formal>", searchIt);
}

inline void seekToNextCandidate(std::string const& xmlString, SearchIterator& searchIt) {
	seekTo(xmlString, "</Candidate>", searchIt);
}

inline bool moreSeatData(std::string const& xmlString, SearchIterator const& searchIt) {
	return comesBefore(xmlString, "<Contest", "</Election>", searchIt);
}

void PreloadDataRetriever::collectData()
{
	std::ifstream file(UnzippedFileName);
	std::string xmlString;
	transferFileToString(file, xmlString);

	try {
		std::string::const_iterator searchIt = xmlString.begin();
		do {
			seekToFirstPreferences(xmlString, searchIt);
			do {
				seekToNextCandidate(xmlString, searchIt);
				bool independent = candidateIsIndependent(xmlString, searchIt);
				int candidateId = extractCandidateId(xmlString, searchIt);
				int affiliationId = 0;
				if (candidateId == 17322) {
					PrintDebugLine("SCOTT, Duncan");
				}
				if (candidateId == 28049) {
					PrintDebugLine("Ken O'Dowd");
				}
				if (!independent) {
					affiliationId = extractAffiliationId(xmlString, searchIt);
				}
				candidates.insert({ candidateId, affiliationId });
			} while (moreCandidateData(xmlString, searchIt));
		} while (moreSeatData(xmlString, searchIt));

		PrintDebugLine("Download complete!");
	}
	catch (const std::regex_error& e) {
		PrintDebug("regex_error caught: ");
		PrintDebugLine(e.what());
	}
}
