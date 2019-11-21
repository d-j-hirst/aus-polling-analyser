#pragma once

#include "ElectionData.h"
#include "RegexNavigation.h"

#include <array>
#include <unordered_map>
#include <vector>

class PreviousElectionDataRetriever {
public:

	typedef std::unordered_map<int, Results::Affiliation> AffiliationMap;
	typedef std::unordered_map<int, Results::Candidate> CandidateMap;

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

	CandidateMap::const_iterator beginCandidates() const { return candidates.cbegin(); }
	CandidateMap::const_iterator endCandidates() const { return candidates.cend(); }

private:
	void extractGeneralSeatInfo(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData);

	void extractFpResults(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData);
	void extractFpCandidate(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData);

	void extractTcpResults(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData);
	void extractTcpCandidate(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData, int candidateNum);

	void extractBoothResults(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData);
	void extractBooth(std::string const& xmlString, SearchIterator& searchIt, Results::Seat& seatData);

	// Map between AEC's seat ID and data for that seat
	Results::SeatMap seatMap;
	// Map between AEC's booth ID and data for that booth
	Results::BoothMap boothMap;

	AffiliationMap affiliations;
	CandidateMap candidates;
};