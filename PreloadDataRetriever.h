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

	typedef std::unordered_map<int, std::string> AffiliationMap;

	// Maps a booth's ID to it's data (ID and name)
	typedef std::unordered_map<int, Results::Booth> BoothMap;

	const static std::string UnzippedCandidatesFileName;
	const static std::string UnzippedBoothsFileName;

	const static std::string CandidateMatch;
	const static std::string BoothsMatch;

	// collects booth data etc. from the downloaded results file.
	// Make sure this has been downloaded by ResultsDownloader first.
	void collectData();

	CandidateMap::const_iterator beginCandidates() const { return candidates.cbegin(); }
	CandidateMap::const_iterator endCandidates() const { return candidates.cend(); }

	AffiliationMap::const_iterator beginAffiliations() const { return affiliations.cbegin(); }
	AffiliationMap::const_iterator endAffiliations() const { return affiliations.cend(); }

	BoothMap::const_iterator beginBooths() const { return booths.cbegin(); }
	BoothMap::const_iterator endBooths() const { return booths.cend(); }

	typedef std::vector<std::vector<int>> BoothDistribution;
private:

	CandidateMap candidates;
	BoothMap booths;

	AffiliationMap affiliations;
};