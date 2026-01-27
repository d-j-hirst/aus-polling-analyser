#pragma once

#include "ElectionData.h"
#include "General.h"
#include "Log.h"
#include "SpecialPartyCodes.h"

#include <cstdint>
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
  std::optional<float> tppSharePrevious; // 2pp share at previous election, estimate if not directly available
  std::optional<float> tppShare; // transformed vote share, only filled if 2pp is available at both elections 
  std::optional<float> tppSwing; // change in transformed vote share, only filled if 2pp is available at both elections
  std::map<int, float> fpSharesBaseline; // transformed vote share, median result from baseline (no-results) simulation
  std::map<int, float> fpSwingsBaseline; // change in transformed vote share, median result from baseline (no-results) simulation
  std::map<int, float> tcpSharesBaseline; // transformed vote share, median result from baseline (no-results) simulation
  std::map<int, float> tcpSwingsBaseline; // change in transformed vote share, median result from baseline (no-results) simulation
  std::optional<float> tppShareBaseline; // transformed vote share, median result from baseline (no-results) simulation
  std::optional<float> tppSwingBaseline; // change in transformed vote share, median result from baseline (no-results) simulation
  std::map<int, float> fpDeviations; // observed deviation from baseline
  std::optional<float> tppDeviation; // observed deviation from baseline
  std::map<int, float> specificFpDeviations; // deviations reduced according to confidence level
  std::optional<float> specificTppDeviation; // deviations reduced according to confidence level
  std::optional<float> preferenceFlowDeviation; // deviation from baseline preference flows
  std::optional<float> specificPreferenceFlowDeviation; // deviations reduced according to confidence level
  std::map<int, float> fpVotesProjected; // median projected vote count
  std::map<int, float> tppVotesProjected; // median projected vote count, use this for the process of calculating underlying tpp estimates
  std::map<int, float> tcpVotesProjected; // median projected vote count, Important: only calculated when a real TCP count is available.
  std::map<int, float> tempFpVotesProjected; // temporary storage for projected vote counts, used for determining seat-level variability
  std::map<int, float> tempTppVotesProjected; // temporary storage for projected vote counts, used for determining seat-level variability
  std::map<int, float> tempTcpVotesProjected; // temporary storage for projected vote counts, used for determining seat-level variability
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
  float totalFpVotesProjected() const;
  float totalTcpVotesProjected() const;
};

class Booth {
public:
  // For ordinary/ppvc booths
  Booth(
    Results2::Booth const& currentBooth,
    std::optional<Results2::Booth const*> previousBooth,
    std::function<int(int, bool)> partyMapper,
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
    std::function<int(int, bool)> partyMapper,
    int parentSeatId,
    int natPartyIndex
  );

  void calculateTppSwing(int natPartyIndex);

  void log() const;

  std::string name;

  const int parentSeatId;

  Results2::VoteType voteType = Results2::VoteType::Ordinary;
  Results2::Booth::Type boothType = Results2::Booth::Type::Normal;
  std::pair<float, float> coords = { 0.0f, 0.0f }; // latitude, longitude

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

  int independentPartyIndex = InvalidPartyIndex;

  std::map<int, float> finalSpecificFpDeviations; // deviations taking into account change in voter categories
  std::optional<float> finalSpecificTppDeviation; // deviations taking into account change in voter categories
  std::map<int, float> offsetSpecificFpDeviations; // offset to account for new booths, redistribution, etc
  std::optional<float> offsetSpecificTppDeviation; // offset to account for new booths, redistribution, etc
  std::map<int, float> fpAllBoothsStdDev; // variability of (transformed) fp votes across all remaining booths
  float tppAllBoothsStdDev; // variability of (transformed) tpp votes across all remaining booths
  std::optional<float> tcpAllBoothsStdDev; // variability of (transformed) tcp votes across all remaining booths
  float livePreferenceFlowDeviation; // deviations from the "expected" pre-election preference flows
  std::map<Results2::VoteType, float> tppVoteTypeSensitivity; // expected number of (entirely) uncounted votes in this category
  std::map<Results2::Booth::Type, float> tppBoothTypeSensitivity; // expected number of (entirely) uncounted votes in this category
  std::optional<int> tcpFocusPartyIndex;
  std::optional<float> tcpFocusPartyPrefFlow; // batched among all parties not in the top 2
  std::optional<float> tcpFocusPartyConfidence;
  std::optional<float> nationalsProportion;

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

  std::map<int, float> finalSpecificFpDeviations; // deviations taking into account change in voter categories
  std::optional<float> finalSpecificTppDeviation; // deviations taking into account change in voter categories
  std::map<int, float> offsetSpecificFpDeviations; // offset to account for new booths, redistribution, etc
  std::optional<float> offsetSpecificTppDeviation; // offset to account for new booths, redistribution, etc

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
  friend class SimulationIteration;

  struct FloatInformation {
    float value = 0.0f;
    float confidence = 0.0f;
  };

  struct FloatBaselineInformation {
    float deviation = 0.0f;
    float confidence = 0.0f;
    float baseline = 0.0f;
  };

  struct Internals {
    std::map<Results2::Booth::Type, float> boothTypeBiases;
    std::map<Results2::VoteType, float> voteTypeBiases;
    std::map<Results2::Booth::Type, float> boothTypeBiasStdDev;
    std::map<Results2::VoteType, float> voteTypeBiasStdDev;
    std::map<Results2::Booth::Type, float> boothTypeBiasesRaw;
    std::map<Results2::VoteType, float> voteTypeBiasesRaw;
    float projected2pp = 0.0f;
    float raw2ppDeviation = 0.0f;
  };

	Election(Results2::Election const& previousElection, Results2::Election const& currentElection, PollingProject& project, Simulation& sim, SimulationRun& run);

  float getTppShareBaseline() const {
    return node.tppShareBaseline.value_or(50.0f);
  }

  float getTransformedBaselineFp(int partyIndex) const {
    if (node.fpSharesBaseline.contains(partyIndex)) {
      return node.fpSharesBaseline.at(partyIndex);
    }
    return 1.0f;
  }

  float getRegionTppBaseline(int regionIndex) const {
    return largeRegions[regionIndex].node.tppShareBaseline.value_or(50.0f);
  }

   // baseline: revert "simulated" 2PP to this as confidence increases
   // deviation: deviation from prior baseline
   // confidence: confidence in the deviation, should determine how this is weighted
   // Note this should be used from a randomized instance of the live results simulation
   // to reflect uncertainty in the remaining live results
  FloatBaselineInformation getFinalSpecificTppInformation() const {
    float baseline = node.tppShareBaseline.value_or(50.0f);
    float deviation = finalSpecificTppDeviation.value_or(0.0f);
    float confidence = node.tppConfidence;
    return {deviation, confidence, baseline};
  }

  std::map<int, float> getFinalSpecificFpDeviations() const {
    return finalSpecificFpDeviations;
  }

  float getRegionFinalSpecificTppDeviation(int regionIndex) const {
    return largeRegions[regionIndex].finalSpecificTppDeviation.value_or(0.0f);
  }

  std::map<int, float> getRegionFinalSpecificFpDeviations(int regionIndex) const {
    return largeRegions[regionIndex].finalSpecificFpDeviations;
  }

  FloatBaselineInformation getSeatTppInformation(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
		if (seatIndex != int(seats.size())) {
      float deviation = seats[seatIndex].finalSpecificTppDeviation.value_or(0.0f);
      float confidence = seats[seatIndex].node.tppConfidence;
      float baseline = seats[seatIndex].node.tppShareBaseline.value_or(50.0f);
      return {deviation, confidence, baseline};
		}
    return {0.0f, 0.0f, 50.0f};
  }

  std::map<int, FloatBaselineInformation> getSeatFpInformation(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
		if (seatIndex != int(seats.size())) {
      std::map<int, FloatBaselineInformation> result;
      for (auto [party, deviation] : seats[seatIndex].finalSpecificFpDeviations) {
        if (!seats[seatIndex].node.fpSharesBaseline.contains(party)) continue;
        result[party] = {deviation, seats[seatIndex].node.fpConfidence, seats[seatIndex].node.fpSharesBaseline.at(party)};
      }
			return result;
		}
    return {};
  }

  std::map<int, float> getSeatOverallFpDeviations(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
		if (seatIndex != int(seats.size())) {
      std::map<int, float> seatDeviations = seats[seatIndex].finalSpecificFpDeviations;
      for (auto const& [partyId, deviation] : largeRegions[seats[seatIndex].parentRegionId].finalSpecificFpDeviations) {
        if (seatDeviations.contains(partyId)) {
          seatDeviations[partyId] += deviation;
        }
      }
      for (auto const& [partyId, deviation] : finalSpecificFpDeviations) {
        if (seatDeviations.contains(partyId)) {
          seatDeviations[partyId] += deviation;
        }
      }
			return seatDeviations;
		}
    return {};
  }

  float getSeatRawTppSwing(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
		if (seatIndex != int(seats.size())) {
      if (!seats[seatIndex].node.tppShareBaseline.has_value()) return 0.0f;
      float originalTpp = detransformVoteShare(seats[seatIndex].node.tppShareBaseline.value() - seats[seatIndex].node.tppSwingBaseline.value_or(0.0f));
      float currentTpp = detransformVoteShare(seats[seatIndex].node.tppShareBaseline.value() + seats[seatIndex].node.tppDeviation.value_or(0.0f));
      return currentTpp - originalTpp;
		}
    return 0.0f;
  }

  float getSeatFpConfidence(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
		if (seatIndex != int(seats.size())) {
			return seats[seatIndex].node.fpConfidence;
		}
    return 0.0f;
  }

  float getSeatTppConfidence(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
    if (seatIndex != int(seats.size())) {
      return seats[seatIndex].node.tppConfidence;
    }
    return 0.0f;
  }

  float getSeatTcpConfidence(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
    if (seatIndex != int(seats.size())) {
      return seats[seatIndex].node.tcpConfidence;
    }
    return 0.0f;
  }

  // Returns *specific* preference flow deviations ()
  FloatInformation getSeatPreferenceFlowInformation(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
    if (seatIndex != int(seats.size())) {
      float totalPreferenceFlowDeviation = 0.0f;
      float totalPreferenceFlowConfidence = 0.0f;
      auto const& seatNode = seats[seatIndex].node;
      totalPreferenceFlowDeviation += seatNode.specificPreferenceFlowDeviation.value_or(0.0f);
      totalPreferenceFlowConfidence += seatNode.preferenceFlowConfidence;
      auto const& largeRegionNode = largeRegions[seats[seatIndex].parentRegionId].node;
      totalPreferenceFlowDeviation += largeRegionNode.specificPreferenceFlowDeviation.value_or(0.0f);
      totalPreferenceFlowConfidence += largeRegionNode.preferenceFlowConfidence;
      totalPreferenceFlowDeviation += node.specificPreferenceFlowDeviation.value_or(0.0f);
      totalPreferenceFlowConfidence += node.preferenceFlowConfidence;
      float finalConfidence = totalPreferenceFlowConfidence / 3.0f;
      return {totalPreferenceFlowDeviation, finalConfidence};
    }
    return {0.0f, 0.0f};
  }

  FloatInformation getSeatLivePreferenceFlowDeviation(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
    if (seatIndex != int(seats.size())) {
      float confidence = std::min(seats[seatIndex].node.fpConfidence, seats[seatIndex].node.tcpConfidence);
      return {seats[seatIndex].livePreferenceFlowDeviation, confidence};
    }
    return {0.0f, 0.0f};
  }

  // Returns Labor's share of the major party vote (including Nationals)
  FloatInformation getSeatMajorPartyBalance(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
    if (seatIndex != int(seats.size())) {
      auto const& fpVotes = seats[seatIndex].node.fpVotesCurrent;
      if (fpVotes.empty() || !fpVotes.contains(0) || fpVotes.at(0) == 0) return {0.0f, 0.0f};
      float totalMajorPartyVotes = 0.0f;
      std::vector<int> majorPartyIds = {0, 1, natPartyIndex};
      for (auto const& partyId : majorPartyIds) {
        if (fpVotes.contains(partyId)) {
          totalMajorPartyVotes += fpVotes.at(partyId);
        }
      }
      float confidence = seats[seatIndex].node.fpConfidence;
      return {static_cast<float>(fpVotes.at(0)) / static_cast<float>(totalMajorPartyVotes), confidence};
    }
    return {0.0f, 0.0f};
  }

  struct TcpShareInformation {
    std::map<int, float> shares; // transformed vote share
    float confidence = 0.0f;
  };

  TcpShareInformation getSeatTcpInformation(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
    if (seatIndex != int(seats.size())) {
      return {seats[seatIndex].node.tcpShares, seats[seatIndex].node.tcpConfidence};
    }
    return {std::map<int, float>(), 0.0f};
  }

  std::optional<float> getSeatNationalsProportion(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
    if (seatIndex != int(seats.size())) {
      return seats[seatIndex].nationalsProportion;
    }
    return std::nullopt;
  }

  void setVariabilitySeed(std::uint64_t seed) { variabilityBaseSeed = seed; }

  // Returns untransformed vote share belonging to parties not included among the project's
  // significant parties, along with independents who don't make the threshold for significance
  FloatInformation getSeatOthersInformation(std::string const& seatName) const;

  // Expose some internals for diagnostics/analysis
  Internals getInternals() const {
    Internals internals;
    internals.boothTypeBiases = boothTypeBiases;
    internals.voteTypeBiases = voteTypeBiases;
    internals.boothTypeBiasStdDev = boothTypeBiasStdDev;
    internals.voteTypeBiasStdDev = voteTypeBiasStdDev;
    internals.boothTypeBiasesRaw = boothTypeBiasesRaw;
    internals.voteTypeBiasesRaw = voteTypeBiasesRaw;
    internals.projected2pp =
      node.tppVotesProjected.at(0)
      / (node.tppVotesProjected.at(0) + node.tppVotesProjected.at(1))
      * 100.0f;
    internals.raw2ppDeviation = node.tppDeviation.value_or(0);
    return internals;
  }

  LiveV2::Election generateScenario(int iterationIndex) const;

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

  void includeSeatBaselineResults();
  void includeLargeRegionBaselineResults();
  void includeElectionBaselineResults();

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

  void measureBoothTypeBiases();

  void recomposeVoteCounts();

  void calculateTcpPreferenceFlows();

  void calculateNationalsProportions();

  void recomposeBoothFpVotes(bool allowCurrentData, int boothIndex);
  void recomposeBoothTcpVotes(bool allowCurrentData, int boothIndex);
  void recomposeBoothTppVotes(bool allowCurrentData, int boothIndex);

  void recomposeSeatFpVotes(int seatIndex);
  void recomposeSeatTcpVotes(int seatIndex);
  void recomposeSeatTppVotes(int seatIndex);

  void recomposeLargeRegionFpVotes(int largeRegionIndex);
  void recomposeLargeRegionTppVotes(int largeRegionIndex);

  void recomposeElectionFpVotes();
  void recomposeElectionTppVotes();

  void determineElectionFinalFpDeviations(bool allowCurrentData);
  void determineElectionFinalTppDeviation(bool allowCurrentData);

  void determineLargeRegionFinalFpDeviations(bool allowCurrentData, int largeRegionIndex);
  void determineLargeRegionFinalTppDeviation(bool allowCurrentData, int largeRegionIndex);

  void determineSeatFinalFpDeviations(bool allowCurrentData, int seatIndex);
  void determineSeatFinalTppDeviation(bool allowCurrentData, int seatIndex);

  void calculateLivePreferenceFlowDeviations();

  void prepareVariability();

  void generateVariability(int iterationIndex);

  // map AEC candidate IDs to internal party IDs
  int mapPartyId(int ecCandidateId, bool isPrevious);

  float variabilityNormal(float mean, float sd, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;

  void log(bool includeLargeRegions = false, bool includeSeats = false, bool includeBooths = false) const;

  Node node;

  bool createRandomVariation = false;

  std::vector<LargeRegion> largeRegions;
  std::vector<Booth> booths;
  std::vector<LiveV2::Seat> seats;

  std::map<int, int> ecPartyToInternalParty;
  std::map<int, int> ecBoothToInternalBooth;

  std::map<std::string, int> ecAbbreviationToInternalParty;

  std::map<int, float> preferenceFlowMap; // preference flows as a percentage
  std::map<int, float> preferenceExhaustMap;
  std::map<int, float> prevPreferenceOverrides; // Covers cases where the preference flow last election is expected to be different

  
  std::map<int, float> finalSpecificFpDeviations; // deviations taking into account change in voter categories
  std::optional<float> finalSpecificTppDeviation; // deviations taking into account change in voter categories
  std::map<int, float> offsetSpecificFpDeviations; // offset to account for new booths, redistribution, etc
  std::optional<float> offsetSpecificTppDeviation; // offset to account for new booths, redistribution, etc

  std::map<Results2::Booth::Type, float> boothTypeBiases;
  std::map<Results2::VoteType, float> voteTypeBiases;
  std::map<Results2::Booth::Type, float> boothTypeBiasStdDev;
  std::map<Results2::VoteType, float> voteTypeBiasStdDev;
  std::map<Results2::Booth::Type, float> boothTypeIterationVariation;
  std::map<Results2::VoteType, float> voteTypeIterationVariation;

  // Debug/internal analysis only
  std::map<Results2::Booth::Type, float> boothTypeBiasesRaw;
  std::map<Results2::VoteType, float> voteTypeBiasesRaw;

  int natPartyIndex;

  int variabilitySampleIndex = 0;
  std::uint64_t variabilityBaseSeed = 0x9e3779b97f4a7c15ULL;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;

  Results2::Election const& previousElection;
  Results2::Election const& currentElection;
};

} // namespace LiveV2
