#pragma once

#include "General.h"

#include "ElectionData.h"

#include <array>
#include <unordered_map>
#include <vector>

class LatestResultsDataRetriever {
public:
	const static std::string UnzippedFileName;
	const static std::string DirectoryListingFileName;

	// collects booth data etc. from the downloaded results file.
	// Make sure this has been downloaded by ResultsDownloader first.
	void collectData();

	Results::SeatMap::const_iterator beginSeats() const { return seatMap.cbegin(); }
	Results::SeatMap::const_iterator endSeats() const { return seatMap.cend(); }

	Results::BoothMap::const_iterator beginBooths() const { return boothMap.cbegin(); }
	Results::BoothMap::const_iterator endBooths() const { return boothMap.cend(); }

private:

	// Map between AEC's seat ID and data for that seat
	Results::SeatMap seatMap;
	// Map between AEC's booth ID and data for that booth
	Results::BoothMap boothMap;
};