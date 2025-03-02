#pragma once

#include "ElectionData.h"
#include "General.h"

#include <map>

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

  float fpConfidence = 0.0f;
  float tcpConfidence = 0.0f;
  float tppConfidence = 0.0f;

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
  Booth(
    Results2::Booth const& currentBooth,
    std::optional<Results2::Booth const*> previousBooth,
    std::function<int(int)> partyMapper,
    int parentSeatId,
    int natPartyIndex
  );

  void log() const;

  std::string name;

  const int parentSeatId;

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

  // Propagates information from lower levels to higher levels
  void aggregate();

private:
  template<typename T, typename U>
  void aggregateCollection(T& parent, const std::vector<int>& childIndices, 
    const std::vector<U>& childNodes) const;

  void getNatPartyIndex();

  void aggregateToSeat(LiveV2::Seat& seat);
  void aggregateToLargeRegion(LargeRegion& largeRegion);
  void aggregateToElection();

  Node aggregateFromChildren(const std::vector<Node const*>& nodesToAggregate, Node const* parentNode = nullptr) const;

  void initializePartyMappings();

  void createNodesFromElectionData();

  void doAggregationForFpTotals();

  void includeBaselineResults();

  void extrapolateBaselineSwings();

  void calculateDeviationsFromBaseline();

  // map AEC party IDs to internal party IDs
  int mapPartyId(int ecCandidateId);

  void log(bool includeLargeRegions = false, bool includeSeats = false, bool includeBooths = false) const;

  Node node;

  std::vector<LargeRegion> largeRegions;
  std::vector<Booth> booths;
  std::vector<LiveV2::Seat> seats;

  std::map<int, int> ecPartyToInternalParty;
  std::map<int, int> ecBoothToInternalBooth;

  std::map<std::string, int> ecAbbreviationToInternalParty;

  int natPartyIndex;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;

  Results2::Election const& previousElection;
  Results2::Election const& currentElection;
};

} // namespace LiveV2
