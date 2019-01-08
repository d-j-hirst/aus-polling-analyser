#pragma once

#include "ElectionData.h"

#include <array>
#include <unordered_map>
#include <vector>

// Downloads data for candidates and possibly other things
class PreloadDataRetriever {
public:

	// Map from a candidate's ID to their party ID
	typedef std::unordered_map<int, int> CandidateMap;

	const static std::string UnzippedFileName;

	// collects booth data etc. from the downloaded results file.
	// Make sure this has been downloaded by ResultsDownloader first.
	void collectData();

	CandidateMap::const_iterator beginCandidates() const { return candidates.cbegin(); }
	CandidateMap::const_iterator endCandidates() const { return candidates.cend(); }

	typedef std::vector<std::vector<int>> BoothDistribution;
private:

	CandidateMap candidates;
};