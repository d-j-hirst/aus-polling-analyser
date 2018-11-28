#pragma once

#include "ElectionData.h"

#include <array>
#include <unordered_map>
#include <vector>

class PreviousElectionDataRetriever {
public:


	// collects booth data etc. from the downloaded results file.
	// Make sure this has been downloaded by ResultsDownloader first.
	void collectData();

	SeatMap::const_iterator beginSeats() const { return seatMap.cbegin(); }
	SeatMap::const_iterator endSeats() const { return seatMap.cend(); }

	BoothMap::const_iterator beginBooths() const { return boothMap.cbegin(); }
	BoothMap::const_iterator endBooths() const { return boothMap.cend(); }

private:

	// Map between AEC's seat ID and data for that seat
	SeatMap seatMap;
	// Map between AEC's booth ID and data for that booth
	BoothMap boothMap;
};