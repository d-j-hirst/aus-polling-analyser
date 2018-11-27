#pragma once

#include <array>
#include <unordered_map>
#include <vector>

class PreviousElectionDataRetriever {
public:

	struct BoothData {
		std::string name;
		int officialId = -1;
		std::array<int, 2> tcpVote;
		std::array<int, 2> affiliationId; // independent = 0
	};

	struct CandidateData {
		std::string name;
		int affiliationId = 0; // independent = 0
		int ordinaryVotes = 0;
		int absentVotes = 0;
		int provisionalVotes = 0;
		int prepollVotes = 0;
		int postalVotes = 0;
	};

	struct SeatData {
		std::string name;
		int officialId = -1;
		std::array<CandidateData, 2> finalCandidates;
		std::vector<BoothData> booths;
	};

	// collects booth data etc. from the downloaded results file.
	// Make sure this has been downloaded by ResultsDownloader first.
	void collectData();

private:

	std::vector<SeatData> allSeatData;
	// Map between AEC's booth ID and data for that booth
	std::unordered_map<int, BoothData> boothMap;
	// Map between AEC's seat ID and data for that seat
	std::unordered_map<int, SeatData> seatMap;
};