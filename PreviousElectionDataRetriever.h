#pragma once

#include "ElectionData.h"

#include <array>
#include <unordered_map>
#include <vector>

class PreviousElectionDataRetriever {
public:

	typedef std::unordered_map<int, std::string> AffiliationMap;

	const static std::string UnzippedFileName;

	// collects booth data etc. from the downloaded results file.
	// Make sure this has been downloaded by ResultsDownloader first.
	void collectData();

	Results::SeatMap::const_iterator beginSeats() const { return seatMap.cbegin(); }
	Results::SeatMap::const_iterator endSeats() const { return seatMap.cend(); }

	Results::BoothMap::const_iterator beginBooths() const { return boothMap.cbegin(); }
	Results::BoothMap::const_iterator endBooths() const { return boothMap.cend(); }

	AffiliationMap::const_iterator beginAffiliations() const { return affiliations.cbegin(); }
	AffiliationMap::const_iterator endAffiliations() const { return affiliations.cend(); }

private:

	// Map between AEC's seat ID and data for that seat
	Results::SeatMap seatMap;
	// Map between AEC's booth ID and data for that booth
	Results::BoothMap boothMap;

	AffiliationMap affiliations;
};