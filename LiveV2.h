#pragma once

#include "ElectionData.h"
#include "General.h"

#include <map>
#include <set>

class PollingProject;
struct Region;
class Simulation;
class SimulationRun;

namespace LiveV2 {

class Election;

/**
 * Represents a node in the election data hierarchy.
 * 
 * A Node stores vote counts, vote shares, and swings for both first preferences
 * and two-candidate/two-party preferred counts. It serves as the common data
 * structure for booths, seats, and regions.
 */
class Node {
public:
  Node();

  void log() const;

  std::map<int, int> fpVotesCurrent;
  std::map<int, int> fpVotesPrevious;
  std::map<int, int> tcpVotesCurrent;
  std::map<int, int> tcpVotesPrevious;
  std::map<int, float> fpShares; // transformed vote share
  std::map<int, float> fpSwings; // change in transformed vote share
  std::map<int, float> tcpShares; // transformed vote share
  std::map<int, float> tcpSwings; // change in transformed vote share
  std::optional<float> tppShare; // transformed vote share, only filled if 2pp is available at both elections 
  std::optional<float> tppSwing; // change in transformed vote share, only filled if 2pp is available at both elections
  std::map<int, float> fpSharesBaseline; // transformed vote share, median result from baseline (no-results) simulation
  std::map<int, float> fpSwingsBaseline; // change in transformed vote share, median result from baseline (no-results) simulation
  std::optional<float> tppShareBaseline; // transformed vote share, median result from baseline (no-results) simulation
  std::optional<float> tppSwingBaseline; // change in transformed vote share, median result from baseline (no-results) simulation
  std::map<int, float> fpDeviations; // observed deviation from baseline
  std::optional<float> tppDeviation; // observed deviation from baseline
  std::map<int, float> specificFpDeviations; // deviations reduced according to confidence level
  std::optional<float> specificTppDeviation; // deviations reduced according to confidence level
  std::optional<float> preferenceFlowDeviation; // deviation from baseline preference flows
  std::optional<float> specificPreferenceFlowDeviation; // deviations reduced according to confidence level
  std::map<int, float> fpVotesProjected; // median projected vote count
  std::map<int, float> tcpVotesProjected; // median projected vote count
  std::set<int> runningParties;

  float fpConfidence = 0.0f;
  float tcpConfidence = 0.0f;
  float tppConfidence = 0.0f;
  float preferenceFlowConfidence = 0.0f;

  auto fpSharesPercent() const {
    std::map<int, float> result;
    for (auto const& [partyId, share] : fpShares) {
      result[partyId] = detransformVoteShare(share);
    }
    return result;
  }

  auto tcpSharesPercent() const {
    std::map<int, float> result;
    for (auto const& [partyId, share] : tcpShares) {
      result[partyId] = detransformVoteShare(share);
    }
    return result;
  }

  int totalFpVotesCurrent() const;
  int totalVotesPrevious() const;
  int totalTcpVotesCurrent() const;
};

class Booth {
public:
  // For ordinary/ppvc booths
  Booth(
    Results2::Booth const& currentBooth,
    std::optional<Results2::Booth const*> previousBooth,
    std::function<int(int)> partyMapper,
    int parentSeatId,
    int natPartyIndex
  );

  // For incremental vote categories, not ordinary polling locations
  Booth(
    Results2::Seat::VotesByType const& currentFpVotes,
    Results2::Seat::VotesByType const& currentTcpVotes,
    std::optional<Results2::Seat::VotesByType const*> previousFpVotes,
    std::optional<Results2::Seat::VotesByType const*> previousTcpVotes,
    Results2::VoteType voteType,
    std::function<int(int)> partyMapper,
    int parentSeatId,
    int natPartyIndex
  );

  void calculateTppSwing(int natPartyIndex);

  void log() const;

  std::string name;

  const int parentSeatId;

  Results2::VoteType voteType = Results2::VoteType::Ordinary;
  Results2::Booth::Type boothType = Results2::Booth::Type::Normal;

  Node node;
};

class Seat {
public:
  Seat(Results2::Seat const& seat, int parentRegionId);

  void log(Election const& election, bool includeBooths = false) const;

  std::string name;

  // Int handles are used to index into the booths vector
  // This is preferred to using references as it avoids dangling references
  // and allows the vector to be resized
  std::vector<int> booths;

  const int parentRegionId;

  Node node;
};

class LargeRegion {
public:
  LargeRegion(Region const& region);

  void log(Election const& election, bool includeSeats = false, bool includeBooths = false) const;

  std::string name;

  // Int handles are used to index into the seats vector
  // This is preferred to using references as it avoids dangling references
  // and allows the vector to be resized
  std::vector<int> seats;

  Node node;
};

/**
 * Manages live election data aggregation and analysis.
 * 
 * The Election class coordinates the loading, mapping, and aggregation of
 * election data from both current and previous elections. It maintains
 * hierarchical relationships between booths, seats, and regions, and provides
 * methods to calculate swings and project results.
 */
class Election {
public:
  friend class LargeRegion;
  friend class LiveV2::Seat;

	Election(Results2::Election const& previousElection, Results2::Election const& currentElection, PollingProject& project, Simulation& sim, SimulationRun& run);


private:
  template<typename T, typename U>
  void aggregateCollection(T& parent, const std::vector<int>& childIndices, 
    const std::vector<U>& childNodes) const;

  void getNatPartyIndex();

  void loadEstimatedPreferenceFlows();

  void initializePartyMappings();

  void createNodesFromElectionData();

  template<typename T>
  std::vector<Node const*> getThisAndParents(T& child) const;

  void calculateTppEstimates(bool withTpp);

  void calculatePreferenceFlowDeviations();

  void determineElectionPreferenceFlowDeviations();
  void determineLargeRegionPreferenceFlowDeviations();
  void determineSeatPreferenceFlowDeviations();
  void determineBoothPreferenceFlowDeviations();

  void includeBaselineResults();

  void extrapolateBaselineSwings();

  void calculateDeviationsFromBaseline();

  // Propagates information from lower levels to higher levels
  void aggregate();

  void aggregateToSeat(LiveV2::Seat& seat);
  void aggregateToLargeRegion(LargeRegion& largeRegion);
  void aggregateToElection();

  Node aggregateFromChildren(const std::vector<Node const*>& nodesToAggregate, Node const* parentNode = nullptr) const;

  // Propagates information from higher levels to lower levels
  void determineSpecificDeviations();
  
  void determineElectionSpecificDeviations();
  void determineLargeRegionSpecificDeviations();
  void determineSeatSpecificDeviations();
  void determineBoothSpecificDeviations();

  void recomposeVoteCounts();

  void recomposeBoothFpVotes(int boothIndex);
  void recomposeBoothTcpVotes(int boothIndex);
  void recomposeBoothTppVotes(int boothIndex);

  // map AEC candidate IDs to internal party IDs
  int mapPartyId(int ecCandidateId);

  void log(bool includeLargeRegions = false, bool includeSeats = false, bool includeBooths = false) const;

  Node node;

  std::vector<LargeRegion> largeRegions;
  std::vector<Booth> booths;
  std::vector<LiveV2::Seat> seats;

  std::map<int, int> ecPartyToInternalParty;
  std::map<int, int> ecBoothToInternalBooth;

  std::map<std::string, int> ecAbbreviationToInternalParty;

  std::map<int, float> preferenceFlowMap;
  std::map<int, float> preferenceExhaustMap;
  std::map<int, float> prevPreferenceOverrides; // Covers cases where the preference flow last election is expected to be different

  int natPartyIndex;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;

  Results2::Election const& previousElection;
  Results2::Election const& currentElection;
};

} // namespace LiveV2
