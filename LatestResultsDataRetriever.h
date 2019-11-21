#pragma once

#include "ElectionData.h"
#include "General.h"
#include "RegexNavigation.h"

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

	Results::Seat extractSeatData(std::string const& xmlString, SearchIterator& searchIt);

	Results::Seat::Candidate extractSeatCandidate(std::string const& xmlString, SearchIterator& searchIt);

	void extractBoothData(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData);

	Results::Booth extractIndividualBooth(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData);

	Results::Booth::Candidate extractBoothCandidate(std::string const& xmlString, SearchIterator& searchIt);

	void extractBooth2cp(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData, Results::Booth& boothData);

	// Map between AEC's seat ID and data for that seat
	Results::SeatMap seatMap;
	// Map between AEC's booth ID and data for that booth
	Results::BoothMap boothMap;
};