#pragma once

#include "tinyxml2.h"
#include "json.h"

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
	enum class VoteType {
		Invalid,
		Ordinary,
		Absent,
		Provisional,
		PrePoll,
		Postal,
		Early,
		IVote
	};

	inline std::string voteTypeName(VoteType v) {
		static const auto nameMap = std::unordered_map<VoteType, std::string>{ {VoteType::Absent, "Absent"},
			{VoteType::Invalid, "Invalid"}, {VoteType::Ordinary, "Ordinary"}, {VoteType::Postal, "Postal"},
			{VoteType::PrePoll, "PrePoll"}, {VoteType::Provisional, "Provisional"}, {VoteType::Early, "Early"} };
		return nameMap.at(v);
	}

	struct Party {
		int32_t id;
		std::string name;
		std::string shortCode;
	};

	struct Candidate {
		constexpr static int Independent = -1;
		int32_t id;
		std::string name;
		int32_t party = Independent;
	};

	struct Coalition {
		int32_t id;
		std::string name;
		std::string shortCode;
	};

	struct Booth {
		enum class Type {
			Normal,
			Ppvc,
			Remote,
			Prison,
			Hospital,
			Other,
			Invalid
		};
		int32_t id;
		std::string name;
		Type type = Type::Normal;
		int32_t parentSeat;
		std::unordered_map<int32_t, int32_t> fpVotes; // map candidate id -> vote count
		std::unordered_map<int32_t, int32_t> tcpVotes; // map candidate id -> vote count

		std::unordered_map<int32_t, float> fpPercent; // as percentage
		std::unordered_map<int32_t, float> fpSwing; // as percentage
		std::unordered_map<int32_t, float> fpTransformedSwing;
		std::unordered_map<int32_t, float> tcpPercent; // as percentage
		std::unordered_map<int32_t, float> tcpSwing; // as percentage
		std::unordered_map<int32_t, float> tcpEstimate; // as percentage, for booths with fp but no tcp
		std::unordered_map<int32_t, float> tcpEstimateSwing; // as percentage, for booths with fp but no tcp

		int totalVotesFp() const {
			return std::accumulate(fpVotes.begin(), fpVotes.end(), 0,
				[](int acc, decltype(fpVotes)::value_type v) {return acc + v.second; });
		}
		int totalVotesTcp() const {
			return std::accumulate(tcpVotes.begin(), tcpVotes.end(), 0,
				[](int acc, decltype(tcpVotes)::value_type v) {return acc + v.second; });
		}
	};

	struct Seat {
		int32_t id;
		std::string name;
		int32_t enrolment;
		std::vector<int32_t> booths;
		std::unordered_map<int32_t, std::unordered_map<VoteType, int>> fpVotes; // map candidate id -> (vote type -> vote count)
		std::unordered_map<int32_t, std::unordered_map<VoteType, int>> tcpVotes; // map candidate id -> (vote type -> vote count)
		std::unordered_map<int32_t, int> tppVotes; // map coalition id -> vote count
		float fpProgress; // as percentage
		float tcpProgress; // as percentage
		float fpSwingProgress; // as percentage
		float tcpSwingProgress; // as percentage
		std::unordered_map<int32_t, float> fpPercent; // total booth as percentage
		std::unordered_map<int32_t, float> fpSwing; // as percentage
		std::unordered_map<int32_t, float> fpTransformedSwing; // as percentage
		std::unordered_map<int32_t, float> tcpPercent; // as percentage
		std::unordered_map<int32_t, float> tcpSwing; // as percentage
		float tcpSwingBasis; // indicates how much & how reliable data for the swing in this seat is
		bool isTpp = true;

		int totalVotesFp(VoteType exclude = VoteType::Invalid) const {
			return std::accumulate(fpVotes.begin(), fpVotes.end(), 0,
				[&](int acc, decltype(fpVotes)::value_type val) {return acc +
				std::accumulate(val.second.begin(), val.second.end(), 0,
					[&](int acc, decltype(val.second)::value_type val2) {return val2.first != exclude ? acc + val2.second : acc; }
			); }
			);
		}

		int totalVotesFpCandidate(int candidate) const {
			return std::accumulate(fpVotes.at(candidate).begin(), fpVotes.at(candidate).end(), 0,
					[&](int acc, std::unordered_map<VoteType, int>::value_type val2) {return acc + val2.second; }
			);
		}

		int totalVotesTcp(std::vector<VoteType> const& exclude) const {
			return std::accumulate(tcpVotes.begin(), tcpVotes.end(), 0,
				[&](int acc, decltype(tcpVotes)::value_type val) {return acc +
				std::accumulate(val.second.begin(), val.second.end(), 0,
					[&](int acc, decltype(val.second)::value_type val2) {
						return std::find(exclude.begin(), exclude.end(), val2.first) == exclude.end() ? 
							acc + val2.second : acc;
					}
			); }
			);
		}

		int totalVotesTcpParty(int party) const {
			return std::accumulate(tcpVotes.at(party).begin(), tcpVotes.at(party).end(), 0,
				[&](int acc, std::unordered_map<VoteType, int>::value_type val2) {return acc + val2.second; }
			);
		}

		// The below are obsolete, kept around for file compatibility purposes but not longer used
		std::unordered_map<int32_t, int32_t> ordinaryVotesFp; // map candidate id -> vote count
		std::unordered_map<int32_t, int32_t> absentVotesFp; // map candidate id -> vote count
		std::unordered_map<int32_t, int32_t> provisionalVotesFp; // map candidate id -> vote count
		std::unordered_map<int32_t, int32_t> prepollVotesFp; // map candidate id -> vote count
		std::unordered_map<int32_t, int32_t> postalVotesFp; // map candidate id -> vote count
		std::unordered_map<int32_t, int32_t> ordinaryVotesTcp; // map candidate id -> vote count
		std::unordered_map<int32_t, int32_t> absentVotesTcp; // map candidate id -> vote count
		std::unordered_map<int32_t, int32_t> provisionalVotesTcp; // map candidate id -> vote count
		std::unordered_map<int32_t, int32_t> prepollVotesTcp; // map candidate id -> vote count
		std::unordered_map<int32_t, int32_t> postalVotesTcp; // map candidate id -> vote count
	};

	struct Election {
		typedef int32_t Id;
		std::string name;
		Id id = 0;
		std::unordered_map<int32_t, Party> parties; // map affiliation id -> affiliation info
		std::unordered_map<int32_t, Candidate> candidates; // map candidate id -> candidate info
		std::unordered_map<int32_t, Coalition> coalitions; // map coalition id -> coalition info
		std::unordered_map<int32_t, Booth> booths; // map booth id -> booth info
		std::unordered_map<int32_t, Seat> seats; // map seat id -> seat info

		enum class Format {
			AEC,
			VEC,
			NSWEC
		};

		Election() {parties.insert({-1, {-1, "Independent", "IND"}});}

		static Election createAec(tinyxml2::XMLDocument const& xml);
		static Election createVec(
			nlohmann::json const& results,
			tinyxml2::XMLDocument const& input_candidates,
			tinyxml2::XMLDocument const& input_booths
		);
		static Election createVec(
			tinyxml2::XMLDocument const& input_candidates,
			tinyxml2::XMLDocument const& input_booths
		);
		static Election createNswec(nlohmann::json const& results, tinyxml2::XMLDocument const& xml);

		void update(tinyxml2::XMLDocument const& xml, Format format = Format::AEC);

		void update2022VicPrev(nlohmann::json const& results, tinyxml2::XMLDocument const& input_candidates,
			tinyxml2::XMLDocument const& input_booths);
		void preload2022Vic(tinyxml2::XMLDocument const& input_candidates,
			tinyxml2::XMLDocument const& input_booths, bool includeSeatBooths = false);

		void preloadNswec(nlohmann::json const& results, tinyxml2::XMLDocument const& zeros, bool includeSeatBooths = false);
	};
}