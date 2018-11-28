#pragma once

#include <array>
#include <unordered_map>
#include <vector>

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
	std::vector<int> booths; // stores official booth id
};

typedef std::unordered_map<int, BoothData> BoothMap;
typedef std::unordered_map<int, SeatData> SeatMap;