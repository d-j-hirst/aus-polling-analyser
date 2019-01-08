#pragma once

#include <array>
#include <unordered_map>
#include <vector>

namespace Results {
	struct Booth {
		std::string name;
		int officialId = -1;
		std::array<int, 2> tcpVote = { 0, 0 };
		std::array<int, 2> newTcpVote = { 0, 0 };
		std::array<int, 2> affiliationId = { -1, -1 }; // independent = 0
		std::array<int, 2> candidateId = { -1, -1 };
		float percentVote(int whichCandidate) { return float(tcpVote[whichCandidate]) / float(tcpVote[0] + tcpVote[1]) * 100.0f; }
	};

	struct Candidate {
		std::string name;
		int candidateId = 0;
		int affiliationId = 0; // independent = 0
		int ordinaryVotes = 0;
		int absentVotes = 0;
		int provisionalVotes = 0;
		int prepollVotes = 0;
		int postalVotes = 0;
		int declarationVotes() const { return absentVotes + provisionalVotes + prepollVotes + postalVotes; }
		int totalVotes() const { return ordinaryVotes + declarationVotes(); }
	};

	struct Seat {
		std::string name;
		int officialId = -1;
		int enrolment = 0;
		std::array<Candidate, 2> finalCandidates;
		std::vector<int> booths; // stores official booth id
		Candidate const& leadingCandidate() const { return finalCandidates[0].totalVotes() > finalCandidates[1].totalVotes() ? finalCandidates[0] : finalCandidates[1]; }
		Candidate const& trailingCandidate() const { return finalCandidates[1].totalVotes() > finalCandidates[0].totalVotes() ? finalCandidates[0] : finalCandidates[1]; }
		int ordinaryVotes() const { return finalCandidates[0].ordinaryVotes + finalCandidates[1].ordinaryVotes; }
		int declarationVotes() const { return finalCandidates[0].declarationVotes() + finalCandidates[1].declarationVotes(); }
		int absentVotes() const { return finalCandidates[0].absentVotes + finalCandidates[1].absentVotes; }
		int provisionalVotes() const { return finalCandidates[0].provisionalVotes + finalCandidates[1].provisionalVotes; }
		int prepollVotes() const { return finalCandidates[0].prepollVotes + finalCandidates[1].prepollVotes; }
		int postalVotes() const { return finalCandidates[0].postalVotes + finalCandidates[1].postalVotes; }
		int totalVotes() const { return finalCandidates[0].totalVotes() + finalCandidates[1].totalVotes(); }
		float ordinaryVotePercent() const { return float(ordinaryVotes()) / float(totalVotes()) * 100.0f; }
		float declarationVotePercent() const { return float(declarationVotes()) / float(totalVotes()) * 100.0f; }
		float absentVotePercent() const { return float(absentVotes()) / float(totalVotes()) * 100.0f; }
		float provisionalVotePercent() const { return float(provisionalVotes()) / float(totalVotes()) * 100.0f; }
		float prepollVotePercent() const { return float(prepollVotes()) / float(totalVotes()) * 100.0f; }
		float postalVotePercent() const { return float(postalVotes()) / float(totalVotes()) * 100.0f; }
	};

	typedef std::unordered_map<int, Booth> BoothMap;
	typedef std::unordered_map<int, Seat> SeatMap;
}