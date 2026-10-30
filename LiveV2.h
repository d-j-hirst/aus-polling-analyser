#pragma once
#include "LivePartialCount.h"
#include "LiveNewVotes.h"
#include "LiveFpEvidence.h"
#include "LiveEstimatedTpp.h"
#include "LiveTppComposition.h"
#include "LivePartyConversion.h"
#include "LivePartyMapping.h"
#include "LiveVariability.h"
#include "LivePreparedStorage.h"

#include "ElectionData.h"
#include "General.h"
#include "Log.h"
#include "SpecialPartyCodes.h"
#include "LiveTurnout.h"
#include "LivePreferenceCorrections.h"

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>

class PollingProject;
struct Region;
class Simulation;
class SimulationRun;

namespace LiveV2 {

// Raw independent candidates occupy their own range, distinct from configured
// parties and other registered parties created by the feed adapter.
inline constexpr int IndependentPartyIdOffset = 100000;

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

  // Confidence = how useful the existing results are for projection remaining results.
  // So this will include estimates from fp, but not include results where swing can't be calculated
  float fpConfidence = 0.0f;
  // Candidate-specific effective votes for movement shared across districts.
  // These never reduce the confidence in this district's own reported FP.
  std::map<int, double> fpPoolingVotes;
  float tcpConfidence = 0.0f;
  float tppConfidence = 0.0f;
  float preferenceFlowConfidence = 0.0f;

  // Completion = how much of this result is counted - for weghting versus pre-election and completion statistics
  // So this will include results where swing can't be calculated, but not include estimates from fp
  float fpCompletion = 0.0f;
  float tcpCompletion = 0.0f;
  float tppCompletion = 0.0f;

  float relevanceModifier = 1.0f;

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
  int totalTcpVotesPrevious() const;
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
    int natPartyIndex,
    bool sameSeat
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
    int natPartyIndex,
    bool sameSeat
  );

  void calculateTppSwing(int natPartyIndex);

  void log() const;

  std::string name;

  const int parentSeatId;

  Results2::VoteType voteType = Results2::VoteType::Ordinary;
  Results2::Booth::Type boothType = Results2::Booth::Type::Normal;
  std::optional<std::pair<float, float>> coords; // latitude, longitude; unset if the source booth has none
  std::map<int, float> tppVotesEstimated; // estimate-only count for tpp votes, used for calculating general bias in TPP estimates

  // Prepared once from compatible accepted counts. Main scenarios share this
  // frozen measurement, never retaining source documents or disk history.
  std::optional<LivePartialCount::Evidence> preferenceEvidence;
  std::optional<LivePartialCount::TcpBasis> tcpBasis;
  bool hasTcpRevision() const {
    if (!tcpBasis || tcpBasis->tcp != node.tcpVotesCurrent || tcpBasis->fp == node.fpVotesCurrent ||
      tcpBasis->fp.size() != node.fpVotesCurrent.size() || node.totalFpVotesCurrent() <= 0) return false;
    for (auto const& [candidate,votes] : tcpBasis->fp)
      if (!node.fpVotesCurrent.contains(candidate)) return false;
    return true;
  }
  std::map<int,double> tcpLowerBounds() const {
    // Share the same guaranteed vote account between turnout and recheck
    // scenarios; neither may reinstate the obsolete TCP after an FP revision.
    return hasTcpRevision() ? LivePartialCount::revisedLowerBounds(node.fpVotesCurrent,node.tcpVotesCurrent,*tcpBasis)
      : std::map<int,double>(node.tcpVotesCurrent.begin(),node.tcpVotesCurrent.end());
  }
  // Scratch shares of genuinely new voters, with raw ballot identities. FP
  // and pair preparation consume the same composition; preparation samples
  // own separate copies. Main draws use the prepared seat-level responses.
  LiveNewVotes::Shares futureFpShares;

  // Booths from a different seat are much less reliable for extrapolation
  // so we use this to make sure they're treated differently
  bool sameSeat = false; 

  // Source district survives redistribution matching; historical party flows
  // must follow the old booth's electorate, not its new district's name.
  bool syntheticPreviousCategory = false;
  bool incrementalFp = false;
  bool receivesFpBatches() const { return incrementalFp || voteType != Results2::VoteType::Ordinary; }
  std::string previousPreferenceSeat;
  std::map<int, double> previousPartyPreferenceFlows;

  Node node;
};

class Seat {
public:
  // A separately modelled principal campaign can inform other independents.
  // A generic emerging/recontest possibility has no identified candidate to
  // compare and supplies no pooling baseline; it still receives local updates.
  double independentPoolingBaselineShare = 0;
  std::map<int, LiveTppComposition::CategoryHistory> categoryHistory;
  std::map<int, double> partyPreferenceFlows; // Prepared district priors, fractions.
  Seat(Results2::Seat const& seat, int parentRegionIndex);

  void log(Election const& election, bool includeBooths = false) const;

  std::string name;

  // Int handles are used to index into the booths vector
  // This is preferred to using references as it avoids dangling references
  // and allows the vector to be resized
  std::vector<int> booths;

  // Live candidate ID matched to the baseline/prominent Independent
  // (ecCandidateId + 100000). Booth vote maps keep this raw ID; deviations
  // and projected votes remap it onto run.indPartyIndex.
  int independentPartyIndex = InvalidPartyIndex;
  // Highest-polling live Independent, which can differ from the candidate
  // matched to the prominent Independent in the baseline projection.
  int liveIndependentPartyIndex = InvalidPartyIndex;

  std::map<int, float> finalSpecificFpDeviations; // deviations taking into account change in voter categories
  std::optional<float> finalSpecificTppDeviation; // deviations taking into account change in voter categories
  std::map<int, float> offsetSpecificFpDeviations; // offset to account for new booths, redistribution, etc
  std::optional<float> offsetSpecificTppDeviation; // offset to account for new booths, redistribution, etc
  std::map<int, float> fpAllBoothsStdDev; // variability of (transformed) fp votes across all remaining booths
  std::optional<float> tppAllBoothsStdDev; // optional legacy RMS diagnostic; joint TPP uncertainty is always prepared
  // Frozen received-source evidence, shared by all scenarios in this snapshot.
  // Inferred TPP does not supply a counted lower bound, even at full FP count.
  LiveEstimatedTpp::Evidence estimatedTppEvidence;
  float estimatedTppInfluence = 0.0f;
  std::optional<float> tcpAllBoothsStdDev; // variability of (transformed) tcp votes across all remaining booths
  float livePreferenceFlowDeviation = 0.0f; // deviations from the "expected" pre-election preference flows
  std::map<Results2::VoteType, float> tppVoteTypeSensitivity; // expected number of (entirely) uncounted votes in this category
  std::map<Results2::Booth::Type, float> tppBoothTypeSensitivity; // expected number of (entirely) uncounted votes in this category
  std::map<int, std::map<Results2::VoteType, float>> fpVoteTypeSensitivity; // expected number of (entirely) uncounted votes in this category
  std::map<int, std::map<Results2::Booth::Type, float>> fpBoothTypeSensitivity; // expected number of (entirely) uncounted votes in this category
  std::optional<int> tcpFocusPartyIndex;
  std::optional<float> tcpFocusPartyPrefFlow; // batched among all parties not in the top 2
  std::optional<float> tcpFocusPartyConfidence;
  std::optional<float> nationalsProportion;

  // Index into Election::largeRegions, not the persistent project region ID.
  const int parentRegionIndex;

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
class Election : public LiveData::Provider {
public:
  friend class LargeRegion;
  friend class LiveV2::Seat;
  friend class SimulationIteration;

  using FloatInformation = LiveData::ValueInformation;
  using FloatBaselineInformation = LiveData::BaselineInformation;
  using TcpShareInformation = LiveData::TcpShareInformation;
  using Internals = LiveData::Internals;
  using BoothSnapshot = LiveData::BoothSnapshot;

	Election(Results2::Election const& previousElection, Results2::Election const& currentElection, PollingProject& project, Simulation& sim, SimulationRun& run, TurnoutModelIO::Artifact turnoutInput,
    LiveInputRecovery::PreferenceHistories preferenceHistory = {});

  float getTppShareBaseline() const {
    return node.tppShareBaseline.value_or(0.0f);
  }

  float getTransformedBaselineFp(int partyIndex) const {
    if (node.fpSharesBaseline.contains(partyIndex)) {
      return node.fpSharesBaseline.at(partyIndex);
    }
    return 1.0f;
  }

  float getRegionTppBaseline(int regionIndex) const {
    return largeRegions[regionIndex].node.tppShareBaseline.value_or(0.0f);
  }

   // baseline: revert "simulated" 2PP to this as confidence increases
   // deviation: deviation from prior baseline
   // confidence: confidence in the deviation for purposes of projecting remaining results
   // completion: how much of the result is in, should determine how this is weighted versus pre-election and completion statistics
   // Note this should be used from a randomized instance of the live results simulation
   // to reflect uncertainty in the remaining live results
  FloatBaselineInformation getFinalSpecificTppInformation() const override {
    float baseline = node.tppShareBaseline.value_or(0.0f);
    float deviation = finalSpecificTppDeviation.value_or(0.0f);
    float confidence = node.tppConfidence;
    float completion = node.tppCompletion;
    return { baseline, completion, confidence, deviation, };
  }

  std::map<int, float> getFinalSpecificFpDeviations() const override {
    return finalSpecificFpDeviations;
  }

  float getRegionFinalSpecificTppDeviation(int regionIndex) const override {
    return largeRegions[regionIndex].finalSpecificTppDeviation.value_or(0.0f);
  }

  std::map<int, float> getRegionFinalSpecificFpDeviations(int regionIndex) const override {
    return largeRegions[regionIndex].finalSpecificFpDeviations;
  }

  FloatBaselineInformation getSeatTppInformation(std::string const& seatName) const override {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
		if (seatIndex != int(seats.size())) {
      float deviation = seats[seatIndex].finalSpecificTppDeviation.value_or(0.0f);
      float confidence = seats[seatIndex].node.tppConfidence;
      float completion = seats[seatIndex].node.tppCompletion;
      float baseline = seats[seatIndex].node.tppShareBaseline.value_or(0.0f);
      return { baseline, completion, confidence, deviation };
		}
    return {0.0f, 0.0f, 0.0f, 0.0f};
  }

  std::map<int, FloatBaselineInformation> getSeatFpInformation(std::string const& seatName) const override {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
		if (seatIndex != int(seats.size())) {
      std::map<int, FloatBaselineInformation> result;
      for (auto [party, deviation] : seats[seatIndex].finalSpecificFpDeviations) {
        if (!seats[seatIndex].node.fpSharesBaseline.contains(party)) continue;
        result[party] = { seats[seatIndex].node.fpSharesBaseline.at(party), seats[seatIndex].node.fpCompletion, seats[seatIndex].node.fpConfidence, deviation };
      }
			return result;
		}
    return {};
  }

  LiveData::FpIntegrationInformation const* getSeatFpIntegrationInformation(
    std::string const& seatName) const override;

  std::map<int, float> getSeatOverallFpDeviations(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
		if (seatIndex != int(seats.size())) {
      std::map<int, float> seatDeviations = seats[seatIndex].finalSpecificFpDeviations;
      for (auto const& [partyId, deviation] : largeRegions[seats[seatIndex].parentRegionIndex].finalSpecificFpDeviations) {
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

  float getSeatEstimatedTppSwing(std::string const& seatName) const override {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
		if (seatIndex != int(seats.size())) {
      auto const& seatNode = seats[seatIndex].node;
      // The Results column describes the prepared, seat-wide live estimate:
      // counted votes plus the expected composition of outstanding votes. The
      // historical booth comparisons still train the model, but their weighted
      // swing is not a seat-wide result, particularly after redistribution.
      // With no current evidence, leave the column blank rather than presenting
      // the pre-count forecast as a live swing.
      if (!seatNode.tppShareBaseline || !seatNode.tppSwingBaseline
          || (!seatNode.totalFpVotesCurrent() && !seatNode.totalTcpVotesCurrent())
          || !seatNode.tppVotesProjected.contains(0)
          || !seatNode.tppVotesProjected.contains(1)) {
        return std::numeric_limits<float>::quiet_NaN();
      }
      float const pairTotal = seatNode.tppVotesProjected.at(0)
        + seatNode.tppVotesProjected.at(1);
      if (!std::isfinite(pairTotal) || pairTotal <= 0.0f) {
        return std::numeric_limits<float>::quiet_NaN();
      }
      float const originalTpp = detransformVoteShare(
        *seatNode.tppShareBaseline - *seatNode.tppSwingBaseline);
      float const currentTpp = preparedTppShare(seatIndex);
      return currentTpp - originalTpp;
		}
    return std::numeric_limits<float>::quiet_NaN();
  }

  float getSeatRawTppSwing(std::string const& seatName) const override;

  float getSeatFpCompletion(std::string const& seatName) const override {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
		if (seatIndex != int(seats.size())) {
			return seats[seatIndex].node.fpCompletion;
		}
    return 0.0f;
  }

  float getSeatTppCompletion(std::string const& seatName) const override {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
    if (seatIndex != int(seats.size())) {
      return seats[seatIndex].node.tppCompletion;
    }
    return 0.0f;
  }

  float getSeatTcpCompletion(std::string const& seatName) const override {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
    if (seatIndex != int(seats.size())) {
      return seats[seatIndex].node.tcpCompletion;
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
      auto const& largeRegionNode = largeRegions[seats[seatIndex].parentRegionIndex].node;
      totalPreferenceFlowDeviation += largeRegionNode.specificPreferenceFlowDeviation.value_or(0.0f);
      totalPreferenceFlowConfidence += largeRegionNode.preferenceFlowConfidence;
      totalPreferenceFlowDeviation += node.specificPreferenceFlowDeviation.value_or(0.0f);
      totalPreferenceFlowConfidence += node.preferenceFlowConfidence;
      float finalConfidence = totalPreferenceFlowConfidence / 3.0f;
      return {totalPreferenceFlowDeviation, 0, finalConfidence};
    }
    return {0.0f, 0.0f};
  }

  FloatInformation getSeatLivePreferenceFlowDeviation(std::string const& seatName) const {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
    if (seatIndex != int(seats.size())) {
      float confidence = std::min(
        seats[seatIndex].node.fpConfidence,
        seats[seatIndex].node.tppConfidence);
      return {seats[seatIndex].livePreferenceFlowDeviation, 0, confidence};
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
      float completion = seats[seatIndex].node.fpCompletion;
      float confidence = seats[seatIndex].node.fpConfidence;
      return {static_cast<float>(fpVotes.at(0)) / static_cast<float>(totalMajorPartyVotes), completion, confidence};
    }
    return {0.0f, 0.0f, 0.0f};
  }

  TcpShareInformation getSeatTcpInformation(std::string const& seatName) const override {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
    if (seatIndex != int(seats.size())) {
      return {seats[seatIndex].node.tcpShares, seats[seatIndex].node.tcpCompletion, seats[seatIndex].node.tcpConfidence};
    }
    return {std::map<int, float>(), 0.0f, 0.0f};
  }

  std::optional<FloatInformation> getSeatNationalsProportion(std::string const& seatName) const override {
    int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
    if (seatIndex != int(seats.size()) && seats[seatIndex].nationalsProportion.has_value()) {
      return std::make_optional<FloatInformation>(
        seats[seatIndex].nationalsProportion.value_or(0.0f),
        seats[seatIndex].node.fpCompletion,
        seats[seatIndex].node.fpConfidence
      );
    }
    return std::nullopt;
  }

  void setVariabilitySeed(std::uint64_t seed) { variabilityBaseSeed = seed; }

  // Returns untransformed vote share belonging to parties not included among the project's
  // significant parties, along with independents who don't make the threshold for significance
  FloatInformation getSeatOthersInformation(std::string const& seatName, std::map<int, float> const& representedParties) const override;

  std::optional<LiveData::VoteCountAccount> getSeatVoteCountAccount(
    std::string const& seatName, LiveData::CountKind kind) const override;

  // Expose some internals for diagnostics/analysis
  std::optional<LiveData::CountProgress> getCountProgress() const override { return countProgress; }

  Internals getInternals() const override {
    Internals internals;
    internals.boothTypeBiases = boothTypeTppBiases;
    internals.voteTypeBiases = voteTypeTppBiases;
    internals.boothTypeBiasStdDev = boothTypeTppBiasStdDev;
    internals.voteTypeBiasStdDev = voteTypeTppBiasStdDev;
    internals.boothTypeBiasesRaw = boothTypeTppBiasesRaw;
    internals.voteTypeBiasesRaw = voteTypeTppBiasesRaw;
    internals.boothTypeSourceCount = boothTypeTppSourceCount;
    internals.voteTypeSourceCount = voteTypeTppSourceCount;
    internals.boothTypeVoteCount = boothTypeTppVoteCount;
    internals.voteTypeVoteCount = voteTypeTppVoteCount;
    internals.projected2pp =
      node.tppVotesProjected.at(0)
      / (node.tppVotesProjected.at(0) + node.tppVotesProjected.at(1))
      * 100.0f;
    internals.raw2ppDeviation = node.tppDeviation.value_or(0);
    return internals;
  }

  std::vector<BoothSnapshot> getBoothSnapshots() const override;

  nlohmann::json getDiagnosticSnapshot() const;

  std::unique_ptr<LiveData::Provider> generateScenario(
    int iterationIndex) const override;

  std::optional<std::vector<double>> integrateTpp(std::vector<double> const& prior) const override;

private:
  // Callers choose a named copy path. Preparation samples own mutable booths;
  // forecast scenarios share frozen booths but still copy seat/region state.
  Election(Election const&) = default;
  Election copyForPreparation() const;
  std::unique_ptr<Election> copyForScenario() const;
  std::vector<Booth> const& readBooths() const { return boothStorage.read(); }
  std::vector<Booth>& preparationBooths() { return boothStorage.edit(); }
  void freezePreparedBooths() { boothStorage.freeze(); }

  void prepareTppIntegration();
  void identifyTppIntegrationSeats();
  void prepareTppCategoryAccounts();
  void prepareMatchedCategoryHistory(int seatIndex);
  void prepareTppReportingOrder();
  LiveTppIntegration::Moments tppJointPrior();
  LiveTppIntegration::Matrix tppReferenceMixtures() const;
  std::vector<LiveTppIntegration::Observation> tppJointObservations();
  double tppExpectedVotes() const;
  std::map<int, double> tppMatchedPreferenceUnits() const;
  double tppPooledPreferenceUnits() const;
  std::optional<double> tppInferredEvidencePercent() const;
  LiveTppComposition::ObservationBlock tppSeatObservations(int seatIndex, double electionExpected,
    double matchedUnits) const;
  std::pair<std::vector<LiveTppIntegration::Observation>, LiveTppIntegration::Matrix>
    tppSeatObservationRows(int seatIndex, double matchedUnits) const;
  // Expected final continuing votes: turnout mean FP under compulsory
  // preferences, projected continuing TPP count under optional preferences.
  double tppBoothTarget(int boothIndex) const;
  // Prior booth expectation in 25*log-odds units: previous booth TPP plus
  // baseline seat swing, or baseline seat absolute TPP when unmatched.
  double tppBoothBaseline(int boothIndex) const;
  void prepareCategoryHistory(int seatIndex, Results2::Seat const& previousSeat,
    Results2::Election const& previousElection, Results2::Election const& currentElection);

  double tppSeatReferenceShift(int seatIndex, double priorShare) const;
  double tppBoothMatchedPreferenceUnit(int boothIndex) const;
  void prepareMatchedConversion();
  double heldOutPreferenceOffset(int excludedSeat) const;
  LivePartyConversion::MatchedAccount matchedConversionAccount(int seatIndex) const;
  // seatIndex is the live seat-vector index. referenceShift aligns its booth
  // expectations to the pre-count baseline on the transformed scale. The source
  // group partitions information by TCP coverage, never by map coordinates;
  // qualityFloors supplies category-specific minimum comparison weights [0,1].
  std::map<int, LiveTppIntegration::CategoryEvidence> tppCategoryEvidence(int seatIndex, double referenceShift,
    LiveTppIntegration::EvidenceSourceGroup sourceGroup = LiveTppIntegration::EvidenceSourceGroup::All, std::map<int, double> const& qualityFloors = {}) const;
  void appendTppBoothEvidence(int boothIndex, double referenceShift,
    std::map<int, LiveTppIntegration::CategoryEvidence>& categories,
    LiveTppIntegration::EvidenceSourceGroup sourceGroup, std::map<int, double> const& qualityFloors) const;
  double tppScenarioCountChange(int seatIndex) const;
  float preparedTppShare(int seatIndex) const;
  nlohmann::json tppIntegrationDiagnostic() const;
  nlohmann::json tppConversionDiagnostic() const;
  std::shared_ptr<LiveTppIntegration::Update const> tppIntegration;
  std::vector<int> tppProjectSeatIndices;
  std::vector<int> tppCategoryCodes;
  std::vector<int> tppSyntheticCategoryCodes;
  struct TppCategoryAccount {
    double referenceShift = 0;
    std::map<int, LiveTppComposition::BaselineSource> sources;
    LiveTppComposition::DirectVotes direct;
    std::map<int, LiveTppIntegration::CategoryEvidence> evidence;
    std::array<std::map<int, LiveTppIntegration::CategoryEvidence>, 2> observationEvidence;
    // Only the current preparation's compact sums are retained. Historical
    // weighting supplies the small-sample variance bound after completion lifts
    // comparison weights; it is not another observation or prior draw.
    std::map<int, LiveTppIntegration::CategoryEvidence> unfadedEvidence;
    std::array<std::map<int, LiveTppIntegration::CategoryEvidence>, 2> unfadedObservationEvidence;
    std::map<int, LiveTppIntegration::ReportingProgress> fpProgress;
    std::vector<int> codes;
    LiveTppComposition::Account account;
    double expectedVotes() const;
  };
  TppCategoryAccount prepareTppCategoryAccount(int seatIndex);
  void identifySyntheticTppCategories(std::vector<TppCategoryAccount> const& accounts);
  void prepareDirectTppVotes(int seatIndex, TppCategoryAccount& account) const;
  double accountedTpp(int seatIndex, double transformed, bool sample = false) const;
  double accountedTppDerivative(int seatIndex, double transformed) const;
  void applyTppCategoryCompletion(int seatIndex, TppCategoryAccount& account) const;
  void applyTppObservationCoverage(TppCategoryAccount const& account, int group, int code,
    LiveTppIntegration::Observation& observation) const;
  std::shared_ptr<std::vector<TppCategoryAccount> const> tppCategoryAccounts;
  std::shared_ptr<std::vector<LiveTppIntegration::Observation> const> tppReceivedObservations;
  std::shared_ptr<LiveTppComposition::Extension const> tppPriorExtension;
  std::shared_ptr<LivePartyConversion::Calibration const> tppTransferCalibration;
  double tppCompositionBudgetScale = 1;
  LiveTppIntegration::ReportingProgress tppOrdinaryReportingProgress;
  template<typename T, typename U>
  void aggregateCollection(T& parent, const std::vector<int>& childIndices, 
    const std::vector<U>& childNodes) const;

  void getNatPartyIndex();

  void loadEstimatedPreferenceFlows();
  void preparePartyConversionPriors(Results2::Election const& previousElection,
    Results2::Election const& currentElection);
  void prepareHistoricalBoothFlows(LivePartyConversion::History const& history,
    Results2::Election const& previousElection);
  void prepareDistrictPartyFlows(LivePartyConversion::History const& history,
    Results2::Election const& currentElection);
  void prepareReturningIndependentFlows(Seat& seat, LivePartyConversion::History const& history,
    Results2::Election const& currentElection);
  double districtGreensStrength(Seat const& seat, int party) const;
  double districtGreensResidual(Seat const& seat, LivePartyConversion::History const& history) const;

  // Raw electoral-commission data is only needed while constructing the
  // derived live model. Scenarios copy that model without retaining the raw
  // elections or depending on their lifetime.
  void initializePartyMappings(
    Results2::Election const& previousElection,
    Results2::Election const& currentElection);

  void createNodesFromElectionData(
    Results2::Election const& previousElection,
    Results2::Election const& currentElection);

  template<typename T>
  std::vector<Node const*> getThisAndParents(T& child) const;

  void calculateTppEstimates(bool withTpp);
  double estimateBoothTpp(Booth const& booth) const;
  void calculatePreviousTpp(Booth& booth) const;
  void calculatePreferenceFlowEvidence(Booth& booth);
  std::optional<LivePartialCount::PreferenceTransfer> historicalPreferenceTransfer(Booth const& booth) const;
  void reportLimitedHistoricalPreferenceEvidence(std::string const& sourceTime);
  double limitedHistoricalPreferenceImpact(Booth const& booth,
    LivePartialCount::PreferenceTransfer const& historical, double reliability) const;
  void recordLimitedHistoricalPreferenceIssue(Booth const& booth,
    LivePartialCount::PreferenceTransfer const& historical, double reliability,
    double impact, std::string const& sourceTime);
  std::string describePreferenceCounts(std::map<int,int> const& counts) const;

  void preparePreferenceCohorts(Results2::Election const& previousElection,
    Results2::Election const& currentElection, LiveInputRecovery::PreferenceHistories const& history);
  void prepareUnreportedPreferenceCohorts(Results2::Election const& previousElection,
    Results2::Election const& currentElection, LiveInputRecovery::PreferenceHistories const& history);
  void preparePreferenceCohort(Booth& booth, Results2::Election const& previousElection,
    Results2::Election const& currentElection, LiveInputRecovery::PreferenceHistory const& history);
  LivePartialCount::Flows classicPreferenceFlows(Booth const& booth, int coalitionParty, bool previous = false) const;
  LivePartialCount::Flows pairPreferenceFlows(Booth const& booth, int first, int second) const;
  std::optional<float> preferenceRateOffset(Booth const& booth, bool current) const;
  double preferencePriorOffset(Booth const& booth, bool includeLocal = true) const;
  std::map<int,float> projectCountedTpp(int boothIndex, double contextShare);
  std::optional<double> rawPendingFirstVotes(Booth const& booth) const;
  void prepareTcpBases(Results2::Election const& previousElection, Results2::Election const& currentElection,
    LiveInputRecovery::PreferenceHistories const& history);
  std::optional<LivePartialCount::RevisedTcp> revisedBoothTcp(Booth const& booth,
    int first, int second, double variation = 0) const;
  std::optional<LiveTppIntegration::ReceivedTpp> receivedBoothTpp(Booth const& booth) const;
  std::map<int,float> projectDeclarationTpp(int boothIndex);
  bool usesUnmatchedFpTpp(Booth const& booth) const;
  double projectUnmatchedBoothTpp(int boothIndex, double futureShare, double preferenceVariation) const;
  int coalitionFinalist(Booth const& booth) const;
  double futurePreferenceOffset(Booth const& booth, int first, int second) const;

  void calculatePreferenceFlowDeviations();

  void determineElectionPreferenceFlowDeviations();
  void determineLargeRegionPreferenceFlowDeviations();
  void determineSeatPreferenceFlowDeviations();
  void determineBoothPreferenceFlowDeviations();

  void includeBaselineResults(Results2::Election const& currentElection);

  void includeSeatBaselineResults(Results2::Election const& currentElection);
  void includeLargeRegionBaselineResults();
  void includeElectionBaselineResults();

  void extrapolateBaselineSwings();

  void calculateDeviationsFromBaseline();

  // Propagates information from lower levels to higher levels
  void aggregate();

  void aggregateToSeat(LiveV2::Seat& seat);
  void aggregateToLargeRegion(LargeRegion& largeRegion);
  void aggregateToElection();

  LiveNewVotes::FpEvidence primaryEvidence(Node const& node) const;
  void prepareIndependentPooling(LiveNewVotes::FpEvidence& evidence, Seat const& seat) const;
  void prepareSeatIndependentPooling(Seat& seat) const;
  float fpPoolingObservationWeight(Node const& node, int party, float strength) const;
  std::map<int,double> fpPoolingObservationWeights(LiveNewVotes::FpEvidence const& evidence, float strength) const;
  void retainObservedIndependentPooling(Node const& node, std::map<int,float>& finalDeviations) const;

  Node aggregateFromChildren(const std::vector<Node const*>& nodesToAggregate, Node const* parentNode = nullptr) const;

  // Propagates information from higher levels to lower levels
  void determineSpecificDeviations();
  
  void determineElectionSpecificDeviations();
  void determineLargeRegionSpecificDeviations();
  void determineSeatSpecificDeviations();
  void determineBoothSpecificDeviations();

  void measureFpBoothTypeBiases();
  void measureTppBoothTypeBiases();

  void recomposeVoteCounts();
  void refreshProjectedVoteAggregates();
  void prepareFpEvidence();
  LiveFpEvidence::Unit fpEvidenceUnit(int boothIndex, bool varied = false) const;
  void applyFpEvidence(int boothIndex);
  void refreshFpEvidenceProjections();
  void prepareTppProjectionSensitivities();
  void prepareSeatTppProjectionSensitivities(int seatIndex);
  int fpEvidenceCandidate(int seatIndex, int partyId) const;
  float fpEvidenceNormalStdDev(int seatIndex, int partyId, float localStdDev) const;
  float fpEvidenceDisagreement(int seatIndex, int partyId, double quantile) const;
  void prepareFpIntegration();
  std::vector<LiveData::FpIntegrationInformation> fpIntegrationCentres() const;
  std::vector<LiveData::FpDistributionMoments> measureFpCountMoments();
  void generateSharedFpVariability();
  std::map<int, double> drawSeatFpChanges(int seatIndex) const;

  void calculateTcpPreferenceFlows();

  void calculateNationalsProportions();

  // The turnout component supplies one finite count account to every live
  // projection. Prepare counts, refresh progress, then attach party composition.
  // Pair targets include preferences missing on already-counted FP votes.
  void configureInactiveTurnoutContests(TurnoutModelIO::Artifact const& artifact);
  void prepareTurnout(Results2::Election const& currentElection, TurnoutModelIO::Artifact artifact);
  float turnoutFpTarget(int boothIndex) const;
  float turnoutPairTarget(int boothIndex) const;
  void refreshTurnoutPairProgress();
  void prepareTurnoutComposition();
  void drawTurnoutCounts(int iterationIndex);
  void refreshTurnoutScenarioProgress();

  void refreshTurnoutFpProgress();

  void recomposeBoothFpVotes(bool allowCurrentData, int boothIndex);
  struct OrdinaryFpHierarchy {
    LiveNewVotes::FpEvidence election;
    std::vector<LiveNewVotes::FpEvidence> regions, seats;
  };
  OrdinaryFpHierarchy collectOrdinaryFpEvidence() const;
  void aggregateOrdinaryFpEvidence(OrdinaryFpHierarchy& hierarchy) const;
  void conditionOrdinaryFpEvidence(OrdinaryFpHierarchy& hierarchy) const;
  void prepareNewVoteContexts();
  std::vector<LiveNewVotes::Shares> declarationFpContexts(OrdinaryFpHierarchy const& hierarchy) const;
  std::map<int,float> projectUnreportedFp(bool allowCurrentData, int boothIndex,
    OrdinaryFpHierarchy const* hierarchy = nullptr) const;
  void assignBlindFpOthers(std::map<int,float>& projection, std::set<int> const& candidates,
    int projectSeatIndex, float target, float accounted) const;
  void projectDeclarationFp(int boothIndex);
  LiveNewVotes::Shares composeDeclarationFp(int boothIndex) const;
  void updateDeclarationFpProgress(int boothIndex);
  void projectCountedOrdinaryFp(int boothIndex);
  void clearFpProjection(int boothIndex);
  std::map<int,float> mapFpProjection(int boothIndex, LiveNewVotes::Shares const& raw) const;
  void storeFpProjection(int boothIndex, std::map<int,float> projection);
  void recordFpProjectionSensitivity(int boothIndex, float additions);
  // recomposing 2CP votes is only ever done with current data
  // as there is no useful offset to calculate
  void recomposeBoothTcpVotes(int boothIndex);
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

  void calculateTppEstimateBias();
  LiveEstimatedTpp::Unit estimatedTppUnit(int boothIndex) const;
  void prepareEstimatedTppEvidence();
  void calculateLivePreferenceFlowDeviations();

  void prepareVariability();
  void resetPreparedVariability();
  std::vector<LiveVariability::Baseline> prepareVariabilityBaselines(bool includeTpp) const;
  std::vector<LiveVariability::Sample> runVariabilitySamples(
    std::vector<LiveVariability::Baseline> const& baselines) const;
  LiveVariability::Sample sampleVariability(int sampleIndex,
    std::vector<LiveVariability::Baseline> const& baselines) const;
  void storePreparedVariability(std::vector<LiveVariability::Sample> const& samples);
  void logPreparedSummary() const;

  // Rechecking is independent of additions. Prepare its distributions once
  // from current comparable booths, then change only scenario pair accounts.
  void preparePreferenceCorrections(LiveInputRecovery::RecordStatuses const& countStatuses);
  void applyPreferenceCorrections(int iterationIndex);
  void applyCountRevisions(int iterationIndex);

  void generateVariability(int iterationIndex);

  // Map a commission candidate to a forecast party using the election whose
  // votes are being read; current and historical numeric IDs may be reused.
  int mapPartyId(
    int ecCandidateId,
    bool isPrevious,
    Results2::Election const& previousElection,
    Results2::Election const& currentElection);

  float variabilityNormal(float mean, float sd, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;
  std::uint64_t variabilityKey(int itemIndex, std::uint64_t partyId, std::uint32_t tag) const;
  float drawFpVariability(int seatIndex, int partyId, float localStdDev) const;

  void log(bool includeLargeRegions = false, bool includeSeats = false, bool includeBooths = false) const;

  Node node;

  bool optionalPreferential = false;

  bool createRandomVariation = false;

  std::vector<LargeRegion> largeRegions;
  LivePreparedStorage<Booth> boothStorage;
  std::vector<LiveV2::Seat> seats;

  LivePartyMapping::CommissionIds ecPartyToInternalParty;
  std::map<std::string, int> ecAbbreviationToInternalParty;

  std::map<int, float> preferenceFlowMap; // preference flows as a percentage
  std::map<int, float> preferenceExhaustMap;
  std::map<int, float> prevPreferenceOverrides; // Covers cases where the preference flow last election is expected to be different

  
  std::map<int, float> finalSpecificFpDeviations; // deviations taking into account change in voter categories
  std::optional<float> finalSpecificTppDeviation; // deviations taking into account change in voter categories
  std::map<int, float> offsetSpecificFpDeviations; // offset to account for new booths, redistribution, etc
  std::optional<float> offsetSpecificTppDeviation; // offset to account for new booths, redistribution, etc

  std::map<Results2::Booth::Type, float> boothTypeTppBiases;
  std::map<Results2::VoteType, float> voteTypeTppBiases;
  std::map<Results2::Booth::Type, float> boothTypeTppBiasStdDev;
  std::map<Results2::VoteType, float> voteTypeTppBiasStdDev;
  std::map<Results2::Booth::Type, float> boothTypeTppSourceCount;
  std::map<Results2::VoteType, float> voteTypeTppSourceCount;
  std::map<Results2::Booth::Type, float> boothTypeTppVoteCount;
  std::map<Results2::VoteType, float> voteTypeTppVoteCount;

  std::map<int, std::map<Results2::Booth::Type, float>> boothTypeFpBiases;
  std::map<int, std::map<Results2::VoteType, float>> voteTypeFpBiases;
  std::map<int, std::map<Results2::Booth::Type, float>> boothTypeFpBiasStdDev;
  std::map<int, std::map<Results2::VoteType, float>> voteTypeFpBiasStdDev;
  std::map<int, std::map<Results2::Booth::Type, float>> boothTypeFpIterationVariation;
  std::map<int, std::map<Results2::VoteType, float>> voteTypeFpIterationVariation;
  std::map<int, std::map<Results2::Booth::Type, float>> boothTypeFpSourceCount;
  std::map<int, std::map<Results2::VoteType, float>> voteTypeFpSourceCount;
  std::map<int, std::map<Results2::Booth::Type, float>> boothTypeFpVoteCount;
  std::map<int, std::map<Results2::VoteType, float>> voteTypeFpVoteCount;

  // Debug/internal analysis only
  std::map<Results2::Booth::Type, float> boothTypeTppBiasesRaw;
  std::map<Results2::VoteType, float> voteTypeTppBiasesRaw;
  std::map<int, std::map<Results2::Booth::Type, float>> boothTypeFpBiasesRaw;
  std::map<int, std::map<Results2::VoteType, float>> voteTypeFpBiasesRaw;

  int natPartyIndex;

  float nonClassicTppBiasPercentagePoints = 0.0f;
  float nonClassicTppBiasConfidence = 0.0f;

  int variabilitySampleIndex = 0;
  std::uint64_t variabilityBaseSeed = 0x9e3779b97f4a7c15ULL;

  // Shared immutable preparation; each scenario owns only its projected votes.
  std::shared_ptr<LiveTurnout::Prepared const> turnout;
  std::optional<LiveData::CountProgress> countProgress;
  // One immutable ordinary-based prior per declaration service. Scenarios
  // share these compact maps and own only their varied future compositions.
  std::shared_ptr<std::vector<LiveNewVotes::Shares> const> newVoteContexts;
  std::shared_ptr<std::vector<LiveFpEvidence::Prepared> const> fpEvidence;
  std::shared_ptr<std::vector<LiveData::FpIntegrationInformation> const> fpIntegration;
  std::map<std::string, TurnoutModelIO::InactiveContest> inactiveContests;
  std::shared_ptr<nlohmann::json const> turnoutDiagnostic;

  struct PreferenceCorrectionSeat {
    int partyA = InvalidPartyIndex, partyB = InvalidPartyIndex;
    bool classic = false;
    LivePreferenceCorrections::SeatDistribution distribution;
  };
  struct PreferenceCorrectionPreparation {
    std::vector<PreferenceCorrectionSeat> seats;
    std::vector<std::map<int, double>> countedTpp, countedTcp;
  };
  // Raw counts and prepared distributions are immutable and shared. Revised
  // FP/TCP/TPP accounts belong to one scenario. The final finite-account handoff
  // retains both preference transfers and signed ballot corrections, including
  // when no routine votes remain outstanding.
  std::shared_ptr<PreferenceCorrectionPreparation const> preferenceCorrections;
  std::shared_ptr<LiveTurnout::ScenarioCounts const> scenarioCountedVotes;
  std::shared_ptr<nlohmann::json const> preferenceCorrectionDiagnostic;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;
};

} // namespace LiveV2
