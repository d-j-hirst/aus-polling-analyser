#pragma once

#include <array>
#include <unordered_map>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace Results {

	struct Booth {
		struct Candidate {
			int affiliationId = -1;
			int candidateId = -1;
			int fpVotes = 0;
		};
		std::string name;
		int officialId = -1;
		std::array<int, 2> tcpVote = { 0, 0 };
		std::array<int, 2> newTcpVote = { 0, 0 };
		std::array<int, 2> tcpAffiliationId = { -1, -1 }; // independent = 0
		std::array<int, 2> tcpCandidateId = { -1, -1 };
		std::vector<Candidate> oldFpCandidates;
		std::vector<Candidate> fpCandidates;
		bool newResultsZero = false;
		float percentVote(int whichCandidate) const { return float(tcpVote[whichCandidate]) / float(tcpVote[0] + tcpVote[1]) * 100.0f; }
		bool hasOldResults() const { return tcpVote[0] + tcpVote[1]; }
		bool hasNewResults() const { return newResultsZero || (newTcpVote[0] + newTcpVote[1]); }
		bool hasOldAndNewResults() const { return hasOldResults() && hasNewResults(); }
		bool hasValidPreferenceData() const {
			// sometimes Fp and Tcp votes for a booth are not properly synchronised, this makes sure they're about the same
			return hasNewResults() && totalNewFpVotes() &&
				abs(totalNewTcpVotes() - totalNewFpVotes()) < std::min(10, totalNewTcpVotes() / 50 - 1);
		}
		int totalOldTcpVotes() const { return tcpVote[0] + tcpVote[1]; }
		int totalNewTcpVotes() const { return newTcpVote[0] + newTcpVote[1]; }
		int totalOldFpVotes() const { return std::accumulate(oldFpCandidates.begin(), oldFpCandidates.end(), 0, [](int val, Candidate c) {return val + c.fpVotes; }); }
		int totalNewFpVotes() const { return std::accumulate(fpCandidates.begin(), fpCandidates.end(), 0, [](int val, Candidate c) {return val + c.fpVotes; }); }
		bool isPPVC() const { return name.find("PPVC") != std::string::npos; }
		// raw Swing in proportion terms (not percentage)
		float rawSwing(int candidate = 0) const { 
			if (!(totalNewTcpVotes() && totalOldTcpVotes())) return 0.0f; 
			return float(newTcpVote[candidate]) / float(totalNewTcpVotes()) - float(tcpVote[candidate]) / float(totalOldTcpVotes());
		}
		struct Coords {
			float latitude = 0.0f; float longitude = 0.0f;
		};
		Coords coords;
	};

	struct Candidate {
		int affiliationId = 0;
		std::string name;
	};

	struct Affiliation {
		std::string shortCode;
	};

	struct Seat {
		struct Candidate {
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

		std::string name;
		int officialId = -1;
		int enrolment = 0;
		bool classic2pp = true;
		std::array<Candidate, 2> finalCandidates;
		std::vector<Candidate> fpCandidates;
		std::vector<Candidate> oldFpCandidates;
		std::vector<int> booths; // stores official booth id
		Candidate const& leadingCandidate() const { return finalCandidates[0].totalVotes() > finalCandidates[1].totalVotes() ? finalCandidates[0] : finalCandidates[1]; }
		Candidate const& trailingCandidate() const { return finalCandidates[1].totalVotes() > finalCandidates[0].totalVotes() ? finalCandidates[0] : finalCandidates[1]; }
		int ordinaryVotes() const { return finalCandidates[0].ordinaryVotes + finalCandidates[1].ordinaryVotes; }
		int declarationVotes() const { return finalCandidates[0].declarationVotes() + finalCandidates[1].declarationVotes(); }
		int absentVotes() const { return finalCandidates[0].absentVotes + finalCandidates[1].absentVotes; }
		int provisionalVotes() const { return finalCandidates[0].provisionalVotes + finalCandidates[1].provisionalVotes; }
		int prepollVotes() const { return finalCandidates[0].prepollVotes + finalCandidates[1].prepollVotes; }
		int postalVotes() const { return finalCandidates[0].postalVotes + finalCandidates[1].postalVotes; }
		int total2cpVotes() const { return finalCandidates[0].totalVotes() + finalCandidates[1].totalVotes(); }
		float ordinaryVotePercent() const { return float(ordinaryVotes()) / float(total2cpVotes()) * 100.0f; }
		float declarationVotePercent() const { return float(declarationVotes()) / float(total2cpVotes()) * 100.0f; }
		float absentVotePercent() const { return float(absentVotes()) / float(total2cpVotes()) * 100.0f; }
		float provisionalVotePercent() const { return float(provisionalVotes()) / float(total2cpVotes()) * 100.0f; }
		float prepollVotePercent() const { return float(prepollVotes()) / float(total2cpVotes()) * 100.0f; }
		float postalVotePercent() const { return float(postalVotes()) / float(total2cpVotes()) * 100.0f; }
		int totalFpVotes() const { return std::accumulate(fpCandidates.begin(), fpCandidates.end(), 0, [](int val, Candidate c) {return val + c.totalVotes(); }); }
	};

	typedef std::unordered_map<int, Booth> BoothMap;
	typedef std::unordered_map<int, Seat> SeatMap;
}

namespace Results2 {
	struct Affiliation {
		int id;
		std::string name;
		std::string shortCode;
	};

	struct Candidate {
		int id;
		std::string name;
		int affiliation; // -1 for independent
	};

	struct Booth {
		enum class Type {
			Normal,
			Ppvc,
			Remote,
			Prison,
			Hospital,
			Invalid
		};
		int id;
		std::string name;
		Type type;
		std::unordered_map<int, int> fpVotes; // map candidate id -> vote count
		std::unordered_map<int, int> tcpVotes; // map candidate id -> vote count
	};

	struct Seat {
		int id;
		std::string name;
		int enrolment;
		std::vector<int> booths;
		std::unordered_map<int, int> ordinaryVotesFp; // map candidate id -> vote count
		std::unordered_map<int, int> absentVotesFp; // map candidate id -> vote count
		std::unordered_map<int, int> provisionalVotesFp; // map candidate id -> vote count
		std::unordered_map<int, int> prepollVotesFp; // map candidate id -> vote count
		std::unordered_map<int, int> postalVotesFp; // map candidate id -> vote count
		std::unordered_map<int, int> ordinaryVotes2cp; // map candidate id -> vote count
		std::unordered_map<int, int> absentVotes2cp; // map candidate id -> vote count
		std::unordered_map<int, int> provisionalVotes2cp; // map candidate id -> vote count
		std::unordered_map<int, int> prepollVotes2cp; // map candidate id -> vote count
		std::unordered_map<int, int> postalVotes2cp; // map candidate id -> vote count
	};

	struct Election {
		std::string name;
		std::unordered_map<int, Affiliation> affiliations; // map affiliation id -> affiliation info
		std::unordered_map<int, Candidate> candidates; // map candidate id -> candidate info
		std::unordered_map<int, Booth> booths; // map booth id -> booth info
		std::unordered_map<int, Seat> seats; // map seat id -> seat info
	};
}