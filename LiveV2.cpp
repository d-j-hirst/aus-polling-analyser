#include "LiveV2.h"

#include "General.h"
#include "PollingProject.h"
#include "RandomGenerator.h"
#include "SpecialPartyCodes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <ranges>
#include <set>
#include <sstream>
#include <string_view>
#include <thread>

// LiveV2 turns parsed current/previous election results into the hierarchical
// live-data provider consumed by SimulationIteration. The main processing flow
// is coordinated by Election::Election:
//
// * Node/Booth/Seat/LargeRegion hold mapped counts and derived state.
// * initializePartyMappings/createNodesFromElectionData build the hierarchy.
// * calculateTppEstimates through determineSpecificDeviations derive observed
//   swings relative to the no-results simulation baseline.
// * recomposeVoteCounts projects complete FP/TCP/TPP counts.
// * prepareVariability/generateVariability add deterministic scenario noise.
//
// Raw electoral-commission parsing belongs in ElectionData.cpp; this file
// assumes that parser-level structural validation has already succeeded.

using namespace LiveV2;

constexpr float VoteObsWeightStrength = 250.0f;
constexpr float PreferenceFlowObsWeightStrength = 200.0f;
constexpr float CoalitionLeakagePercent = 20.0f;
constexpr float PreviousTotalVotesGuess = 500.0f;
constexpr float HospitalBoothVotesGuess = 50.0f;
constexpr float DifferentSeatRelevanceModifier = 0.1f;
constexpr float NonClassicTppVariabilityStdDev = 2.5f;
constexpr float NonClassicBiasEffectiveBoothScale = 5.0f;
constexpr float MaxPreferenceVoteTotalDifference = 0.02f;

// Arbitrary offset to ensure independent candidates don't clash with real party IDs
// Candidate IDs are 5-digit (or shorter) numbers, so this offset makes it easy to spot
// what the original EC ID was if necessary.
constexpr int IndependentPartyIdOffset = 100000;

namespace {

std::vector<std::string> normalizeCandidateNameTokens(std::string const& name) {
  std::string cleaned;
  cleaned.reserve(name.size());
  for (char ch : name) {
    unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch)) {
      cleaned.push_back(static_cast<char>(std::tolower(uch)));
    } else {
      cleaned.push_back(' ');
    }
  }

  std::istringstream tokenStream(cleaned);
  std::vector<std::string> tokens;
  std::string token;
  while (tokenStream >> token) {
    tokens.push_back(token);
  }
  std::sort(tokens.begin(), tokens.end());
  return tokens;
}

int candidateNameMatchScore(std::string const& lhs, std::string const& rhs) {
  auto lhsTokens = normalizeCandidateNameTokens(lhs);
  auto rhsTokens = normalizeCandidateNameTokens(rhs);
  if (lhsTokens.empty() || rhsTokens.empty()) return 0;
  if (lhsTokens == rhsTokens) return 1000 + int(lhsTokens.size());

  std::set<std::string> lhsSet(lhsTokens.begin(), lhsTokens.end());
  std::set<std::string> rhsSet(rhsTokens.begin(), rhsTokens.end());
  int overlap = 0;
  for (auto const& token : lhsSet) {
    if (rhsSet.contains(token)) {
      ++overlap;
    }
  }
  if (overlap == 0) return 0;

  int unionSize = int(lhsSet.size() + rhsSet.size() - overlap);
  return overlap * 100 - unionSize;
}

std::string lowerCasePrefix(std::string const& value, size_t length) {
  std::string prefix = value.substr(0, length);
  std::ranges::transform(prefix, prefix.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return prefix;
}

void rejectTcpWithoutFpCandidates(
  std::map<int, int>& tcpVotes,
  std::map<int, int> const& fpVotes,
  std::string const& boothName,
  std::string_view electionDescription) {
  for (auto const& [partyId, _] : tcpVotes) {
    if (fpVotes.contains(partyId)) continue;

    logger << "Warning: Rejecting " << electionDescription << " TCP votes for booth "
      << boothName << " because mapped party " << partyId
      << " has no corresponding FP candidate.\n";
    tcpVotes.clear();
    return;
  }
}

float expectedVotesForAggregation(Node const& node) {
  float const projectedVotes = node.totalFpVotesProjected();
  if (projectedVotes > 0.0f) return projectedVotes;

  int const previousVotes = node.totalVotesPrevious();
  if (previousVotes > 0) return float(previousVotes);

  // Newly created booths have no historical size. Their current count is the
  // best available lower-bound estimate and must not be silently weighted zero.
  return float(node.totalFpVotesCurrent());
}

std::map<int, float> projectedVoteShares(
  std::map<int, float> const& projectedVotes,
  std::string_view scope) {
  float totalVotes = 0.0f;
  for (auto const& [partyId, votes] : projectedVotes) {
    if (!std::isfinite(votes) || votes < 0.0f) {
      throw std::runtime_error(
        std::string(scope) + " has invalid projected FP votes for party "
        + std::to_string(partyId) + ".");
    }
    totalVotes += votes;
  }
  if (!std::isfinite(totalVotes) || totalVotes <= 0.0f) {
    throw std::runtime_error(
      std::string(scope) + " has no positive projected FP vote total.");
  }

  std::map<int, float> shares;
  for (auto const& [partyId, votes] : projectedVotes) {
    shares[partyId] = votes / totalVotes;
  }
  return shares;
}

std::optional<float> projectedTppAlpShare(
  std::map<int, float> const& projectedVotes,
  std::string_view scope) {
  float totalVotes = 0.0f;
  for (auto const& [partyId, votes] : projectedVotes) {
    if (!std::isfinite(votes) || votes < 0.0f) {
      throw std::runtime_error(
        std::string(scope) + " has invalid projected TPP votes for party "
        + std::to_string(partyId) + ".");
    }
    totalVotes += votes;
  }
  if (!std::isfinite(totalVotes)) {
    throw std::runtime_error(
      std::string(scope) + " has a non-finite projected TPP total.");
  }
  if (totalVotes == 0.0f) return std::nullopt;
  if (!projectedVotes.contains(0) || !projectedVotes.contains(1)) {
    throw std::runtime_error(
      std::string(scope) + " does not contain both major-party TPP votes.");
  }
  return projectedVotes.at(0) / totalVotes;
}

}

float obsWeight(float confidence, float strength = VoteObsWeightStrength) {
  // TODO: Calibrate this confidence-to-weight curve against historical live
  // counts. Its shape and the level-specific strengths are currently heuristic.
  return std::min(1.0f, 1.01f - 1.01f / (1.0f + std::pow(confidence, 1.6f) * strength));
}

enum class VariabilityTag : std::uint32_t {
  NonOrdinaryFp = 1,
  BoothProjectionFp = 2,
  NonOrdinaryTcp = 3,
  PreferenceFlow = 4,
  TcpSwing = 5,
  TcpSwingDifferentCandidate = 6,
  NonOrdinaryTpp = 7,
  BoothProjectionTpp = 8,
  GenerateBoothTypeTppVariability = 9,
  GenerateVoteTypeTppVariability = 10,
  GenerateFpVariability = 11,
  GenerateTppVariability = 12,
  GenerateTcpVariability = 13,
  DeclarationVoteSizeVariability = 14,
  GenerateNonClassicTppVariability = 15,
  GenerateBoothTypeFpVariability = 16,
  GenerateVoteTypeFpVariability = 17,
};

void Node::log() const
{
  PA_LOG_VAR(relevanceModifier);
  PA_LOG_VAR(fpVotesCurrent);
  PA_LOG_VAR(fpVotesPrevious);
  PA_LOG_VAR(fpShares);
  PA_LOG_VAR(fpSwings);
  PA_LOG_VAR(tcpVotesCurrent);
  PA_LOG_VAR(tcpVotesPrevious);
  PA_LOG_VAR(tcpShares);
  PA_LOG_VAR(tcpSwings);
  PA_LOG_VAR(tppSharePrevious);
  PA_LOG_VAR(tppShare);
  PA_LOG_VAR(tppSwing);
  PA_LOG_VAR(fpConfidence);
  PA_LOG_VAR(tcpConfidence);
  PA_LOG_VAR(tppConfidence);
  PA_LOG_VAR(fpCompletion);
  PA_LOG_VAR(tcpCompletion);
  PA_LOG_VAR(tppCompletion);
  PA_LOG_VAR(fpSharesPercent());
  PA_LOG_VAR(tcpSharesPercent());
  PA_LOG_VAR(fpSharesBaseline);
  PA_LOG_VAR(fpSwingsBaseline);
  PA_LOG_VAR(tppShareBaseline);
  PA_LOG_VAR(tppSwingBaseline);
  PA_LOG_VAR(fpDeviations);
  PA_LOG_VAR(tppDeviation);
  PA_LOG_VAR(specificFpDeviations);
  PA_LOG_VAR(specificTppDeviation);
  PA_LOG_VAR(preferenceFlowDeviation);
  PA_LOG_VAR(preferenceFlowConfidence);
  PA_LOG_VAR(specificPreferenceFlowDeviation);
  PA_LOG_VAR(runningParties);
  PA_LOG_VAR(fpVotesProjected);
  PA_LOG_VAR(tppVotesProjected);
  PA_LOG_VAR(tcpVotesProjected);
}

int Node::totalFpVotesCurrent() const {
  return std::accumulate(fpVotesCurrent.begin(), fpVotesCurrent.end(), 0,
    [](int sum, const auto& pair) { return sum + pair.second; });
}

int Node::totalTcpVotesCurrent() const {
  return std::accumulate(tcpVotesCurrent.begin(), tcpVotesCurrent.end(), 0,
    [](int sum, const auto& pair) { return sum + pair.second; });
}

int Node::totalTcpVotesPrevious() const {
  return std::accumulate(tcpVotesPrevious.begin(), tcpVotesPrevious.end(), 0,
    [](int sum, const auto& pair) { return sum + pair.second; });
}

int Node::totalVotesPrevious() const {
  return std::accumulate(fpVotesPrevious.begin(), fpVotesPrevious.end(), 0,
    [](int sum, const auto& pair) { return sum + pair.second; });
}

float Node::totalFpVotesProjected() const {
  return std::accumulate(fpVotesProjected.begin(), fpVotesProjected.end(), 0.0f,
    [](float sum, const auto& pair) { return sum + pair.second; });
}

float Node::totalTcpVotesProjected() const {
  return std::accumulate(tcpVotesProjected.begin(), tcpVotesProjected.end(), 0.0f,
    [](float sum, const auto& pair) { return sum + pair.second; });
}

bool isMajorParty(int partyId, int natPartyIndex) {
  return partyId == 0 || partyId == 1 || partyId == natPartyIndex;
}

// Helper function to determine if a given tcp set is a valid tpp set
bool isTppSet(const auto& shares, int natPartyIndex) {
  return shares.contains(0) && (shares.contains(1) || shares.contains(natPartyIndex));
};

Booth::Booth(
  Results2::Booth const& currentBooth,
  std::optional<Results2::Booth const*> previousBooth,
  std::function<int(int, bool)> partyMapper,
  int parentSeatId,
  int natPartyIndex,
  bool sameSeat
)
  : name(currentBooth.name), parentSeatId(parentSeatId), voteType(Results2::VoteType::Ordinary), boothType(currentBooth.type),
  coords(currentBooth.coords), sameSeat(sameSeat)
{

  // Helper function to process votes, calculate shares and swings
  auto processVotes = [this, &currentBooth, &partyMapper](
      const auto& currentVotes, const auto& previousVotes, 
      auto& currentMap, auto& previousMap, auto& sharesMap, auto& swingsMap, bool isTcp = false) {
    // Extract votes from current booth
    for (auto const& [id, votes] : currentVotes) {
      int mappedPartyId = partyMapper(id, false);
      currentMap[mappedPartyId] = votes;
    }

    // Extract votes from previous booth if available
    if (previousVotes) {
      for (auto const& [id, votes] : *previousVotes) {
        int mappedPartyId = partyMapper(id, true);
        previousMap[mappedPartyId] = votes;
      }
    }

    if (isTcp) {
      rejectTcpWithoutFpCandidates(
        currentMap, node.fpVotesCurrent, name, "current-election");
      rejectTcpWithoutFpCandidates(
        previousMap, node.fpVotesPrevious, name, "previous-election");
    }

    for (auto const& [partyId, votes] : currentMap) {
      node.runningParties.insert(partyId);
    }

    if (node.totalFpVotesCurrent() == 0 && isTcp) {
      // If there are no fp votes, we can't process the tcp votes
      // So clear the current map so that we don't have any votes in it
      // This is because it is likely an error, or even if it isn't, having 2CP votes without FP votes may cause errors
      // So just clear it and pretend there are no votes reported at all.
      // We do need to keep the previous map so that we can project the votes
      for (auto& [partyId, votes] : currentMap) {
        votes = 0;
      }
      return;
    }

    int totalCurrentVotesCounted = std::accumulate(currentMap.begin(), currentMap.end(), 0,
      [](int sum, const auto& pair) { return sum + pair.second; });

    // A partial TCP count cannot be compared reliably with a complete booth FP
    // count. Keep this guard separate from the denominator used for TCP shares:
    // FP and TCP totals can legitimately differ slightly.
    if (isTcp && totalCurrentVotesCounted > 0
      && float(totalCurrentVotesCounted) < float(node.totalFpVotesCurrent()) * 0.95f) {
      logger << "Warning: Only " << float(totalCurrentVotesCounted) / float(node.totalFpVotesCurrent()) * 100.0f << "% of current votes were counted in " << name << "\n";
      logger << "Resetting votes as they are not reliable\n";
      for (auto& [partyId, votes] : currentMap) {
        votes = 0;
      }
      return;
    }

    // FP and TCP shares must each use the total from their own count. In
    // particular, savings provisions can make the two totals differ.
    float totalCurrentVotes = static_cast<float>(totalCurrentVotesCounted);
    float totalPreviousVotes = static_cast<float>(std::accumulate(
      previousMap.begin(), previousMap.end(), 0,
      [](int sum, const auto& pair) { return sum + pair.second; }));

    // Calculate shares and swings
    if (totalCurrentVotes > 0) {
      for (auto const& [partyId, votes] : currentMap) {
        float currentShare = static_cast<float>(votes) / totalCurrentVotes * 100.0f;
        float currentTransformed = transformVoteShare(std::clamp(currentShare, 0.01f, 99.99f));
        sharesMap[partyId] = currentTransformed;
        if (previousMap.contains(partyId) && totalPreviousVotes > 0) {
          float previousShare = static_cast<float>(previousMap.at(partyId)) / totalPreviousVotes * 100.0f;
          float previousTransformed = transformVoteShare(std::clamp(previousShare, 0.01f, 99.99f));
          float change = currentTransformed - previousTransformed;
          swingsMap[partyId] = change;
        }
      }
    }
  };

  // Process first preference votes
  processVotes(
    currentBooth.fpVotes, 
    previousBooth ? std::optional(previousBooth.value()->fpVotes) : std::nullopt,
    node.fpVotesCurrent, node.fpVotesPrevious, node.fpShares, node.fpSwings
  );

  // Process two-candidate-preferred votes
  processVotes(
    currentBooth.tcpVotesCandidate, 
    previousBooth ? std::optional(previousBooth.value()->tcpVotesCandidate) : std::nullopt,
    node.tcpVotesCurrent, node.tcpVotesPrevious, node.tcpShares, node.tcpSwings, true
  );


  // Determine tpp share and swing, if available
  calculateTppSwing(natPartyIndex);

  // Booths are either complete or not in, so confidence is 1 if there are votes, 0 otherwise
  // We'll handle partial results (like absent/postal votes that are in different batches) later
  // as well as the usually very minor changes that occur when the check count is performed.
  node.fpConfidence = node.fpSwings.size() > 0 ? 1 : 0;
  node.tcpConfidence = node.tcpSwings.size() > 1 ? 1 : 0;
  node.tppConfidence = node.tppSwing.has_value() ? 1 : 0;

  node.fpCompletion = node.totalFpVotesCurrent() > 0 ? 1 : 0;
  node.tcpCompletion = node.totalTcpVotesCurrent() > 0 ? 1 : 0;
  node.tppCompletion = node.totalTcpVotesCurrent() > 0 && isTppSet(node.tcpVotesCurrent, natPartyIndex) ? 1 : 0;

  // For booths previously different seat, we reduce their relevance for projections
  // as they are less likely to be indicative of the remaining votes in the seat
  // than booths from the same seat
  node.relevanceModifier = sameSeat ? 1.0f : DifferentSeatRelevanceModifier;
}

Booth::Booth(
  Results2::Seat::VotesByType const& currentFpVotes,
  Results2::Seat::VotesByType const& currentTcpVotes,
  std::optional<Results2::Seat::VotesByType const*> previousFpVotes,
  std::optional<Results2::Seat::VotesByType const*> previousTcpVotes,
  Results2::VoteType voteType,
  std::function<int(int, bool)> partyMapper,
  int parentSeatId,
  int natPartyIndex,
  bool sameSeat
)
  : name(Results2::voteTypeName(voteType)), parentSeatId(parentSeatId), voteType(voteType), boothType(Results2::Booth::Type::Other),
  coords({ 0.0f, 0.0f }), sameSeat(sameSeat)
{
  auto processVotes = [this, &partyMapper, voteType](
    Results2::Seat::VotesByType const& currentVotes,
    std::optional<Results2::Seat::VotesByType const*> previousVotes,
    auto& currentMap, auto& previousMap, auto& sharesMap, auto& swingsMap, bool isTcp = false)
    {
      // Extract votes from current booth
      for (auto const& [partyId, votes] : currentVotes) {
        auto const voteIt = votes.find(voteType);
        if (voteIt == votes.end()) continue;
        int mappedPartyId = partyMapper(partyId, false);
        currentMap[mappedPartyId] = voteIt->second;
      }
      // Extract votes from previous booth if available
      if (previousVotes) {
        for (auto const& [partyId, votes] : *(previousVotes.value())) {
          if (!votes.contains(voteType)) continue;
          int mappedPartyId = partyMapper(partyId, true);
          previousMap[mappedPartyId] = votes.at(voteType);
        }
      }

      if (isTcp) {
        rejectTcpWithoutFpCandidates(
          currentMap, node.fpVotesCurrent, name, "current-election");
        rejectTcpWithoutFpCandidates(
          previousMap, node.fpVotesPrevious, name, "previous-election");
      }

      for (auto const& [partyId, votes] : currentMap) {
        node.runningParties.insert(partyId);
      }

      if (node.totalFpVotesCurrent() == 0 && isTcp) {
        // If there are no fp votes, we can't process the tcp votes
        // So clear the current map so that we don't have any votes in it
        // This is because it is likely an error, or even if it isn't, having 2CP votes without FP votes may cause errors
        // So just clear it and pretend there are no votes reported at all.
        // We do need to keep the previous map so that we can project the votes
        for (auto& [partyId, votes] : currentMap) {
          votes = 0;
        }
        return;
      }

      // Calculate total votes for percentages
      // Need to actually calculate this because the fp and tcp can be legitimately different for incremental booths
      float totalCurrentVotes = static_cast<float>(std::accumulate(currentMap.begin(), currentMap.end(), 0,
        [](int sum, const auto& pair) { return sum + pair.second; }));
      float totalPreviousVotes = static_cast<float>(std::accumulate(previousMap.begin(), previousMap.end(), 0,
        [](int sum, const auto& pair) { return sum + pair.second; }));
      // Calculate shares and swings
      if (totalCurrentVotes > 0) {
        for (auto const& [partyId, votes] : currentMap) {
          if (votes == 0 || votes >= totalCurrentVotes) {
            continue;
          }
          float currentTransformed = transformVoteShare(static_cast<float>(votes) / totalCurrentVotes * 100.0f);
          sharesMap[partyId] = currentTransformed;
          if (previousMap.contains(partyId) && totalPreviousVotes > 0) {
            if (previousMap.at(partyId) == 0 || previousMap.at(partyId) >= totalPreviousVotes) {
              continue;
            }
            float previousTransformed = transformVoteShare(static_cast<float>(previousMap.at(partyId)) / totalPreviousVotes * 100.0f);
            float change = currentTransformed - previousTransformed;
            swingsMap[partyId] = change;
          }
        }
      }
    };

  // Process first preference votes
  processVotes(
    currentFpVotes,
    previousFpVotes ? std::optional(previousFpVotes.value()) : std::nullopt,
    node.fpVotesCurrent, node.fpVotesPrevious, node.fpShares, node.fpSwings
  );

  // Process two-candidate-preferred votes
  processVotes(
    currentTcpVotes,
    previousTcpVotes && (*previousTcpVotes)->size() ? std::optional(previousTcpVotes.value()) : std::nullopt,
    node.tcpVotesCurrent, node.tcpVotesPrevious, node.tcpShares, node.tcpSwings, true
  );

  // Determine tpp share and swing, if available
  calculateTppSwing(natPartyIndex);

  // For incremental booths our confidence will increase as more votes are counted
  // but as we don't actually know how many votes are left to be counted
  // the confidence will plateau at 0.95
  // (this needs to be quite high to prevent the offset from incorrectly bleeding into the
  // votes for close TPP seats)
  float const previousVoteTotal = float(node.totalVotesPrevious());
  auto progressFromPrevious = [previousVoteTotal](int currentVotes) {
    if (previousVoteTotal <= 0.0f) return 0.0f;
    return std::min(float(currentVotes) / previousVoteTotal, 0.95f);
  };
  node.fpConfidence = progressFromPrevious(node.totalFpVotesCurrent());
  // If we can't compare tcp swings due to difference matchup, keep tcp confidence to zero 
  node.tcpConfidence = node.tcpSwings.size()
    ? progressFromPrevious(node.totalTcpVotesCurrent())
    : 0.0f;
  node.tppConfidence = std::min(
    std::max(
      // TCP vote progress should only be counted as TPP confidence if it's actually a TPP matchup
      isTppSet(node.tcpVotesCurrent, natPartyIndex)
        ? progressFromPrevious(node.totalTcpVotesCurrent())
        : 0.0f,
      progressFromPrevious(node.totalFpVotesCurrent()) * 0.5f
    ),
    0.95f
  );

  node.fpCompletion = node.fpConfidence;
  node.tcpCompletion = node.tcpConfidence;
  // The same as tppConfidence but without the approximation from FPs
  node.tppCompletion = isTppSet(node.tcpVotesCurrent, natPartyIndex)
    ? progressFromPrevious(node.totalTcpVotesCurrent())
    : 0.0f;

  // For booths previously different seat, we reduce their relevance for projections
  // as they are less likely to be indicative of the remaining votes in the seat
  // than booths from the same seat
  node.relevanceModifier = sameSeat ? 1.0f : DifferentSeatRelevanceModifier;
}

void Booth::calculateTppSwing(int natPartyIndex) {
  if (node.totalTcpVotesCurrent() > 0
    && isTppSet(node.tcpVotesCurrent, natPartyIndex)
    && node.tcpShares.contains(0)) {
    node.tppShare = node.tcpShares.at(0);
  }
  if (!node.tppShare) {
    return;
  }
  if (node.totalTcpVotesPrevious() > 0 && isTppSet(node.tcpVotesPrevious, natPartyIndex)) {
    node.tppSharePrevious = transformVoteShare(std::clamp(static_cast<float>(node.tcpVotesPrevious.at(0)) / static_cast<float>(node.totalTcpVotesPrevious()) * 100.0f, 1.0f, 99.0f));
  }
  if (node.tppSharePrevious) {
    node.tppSwing = node.tppShare.value() - node.tppSharePrevious.value();
  }
}

void Booth::log() const
{
  logger << "Booth: " << name << "\n";
  logger << "Vote type: " << voteTypeName(voteType) << "\n";
  logger << "Booth type: " << Results2::Booth::boothTypeName(boothType) << "\n";
  logger << "Coordinates: (" << coords.first << ", " << coords.second << ")\n";
  logger << "Same seat: " << (sameSeat ? "true" : "false") << "\n";
  logger << "tppVotesEstimated: (" << tppVotesEstimated << ")\n";
  node.log();
}

LiveV2::Seat::Seat(Results2::Seat const& seat, int parentRegionIndex)
  : name(seat.name), parentRegionIndex(parentRegionIndex)
{
}

void LiveV2::Seat::log(Election const& election, bool includeBooths) const
{
  logger << "\nSeat: " << name << "\n";
  PA_LOG_VAR(finalSpecificFpDeviations);
  PA_LOG_VAR(finalSpecificTppDeviation);
  PA_LOG_VAR(offsetSpecificFpDeviations);
  PA_LOG_VAR(offsetSpecificTppDeviation);
  PA_LOG_VAR(fpAllBoothsStdDev);
  PA_LOG_VAR(tppAllBoothsStdDev);
  PA_LOG_VAR(tcpAllBoothsStdDev);
  PA_LOG_VAR(independentPartyIndex);
  PA_LOG_VAR(tcpFocusPartyIndex);
  PA_LOG_VAR(tcpFocusPartyPrefFlow);
  PA_LOG_VAR(tcpFocusPartyConfidence);
  PA_LOG_VAR(nationalsProportion);

  node.log();
  if (includeBooths) {
    for (auto const& booth : booths) {
      election.booths.at(booth).log();
    }
  }
}



LiveV2::LargeRegion::LargeRegion(Region const& region)
  : name(region.name)
{
}

void LiveV2::LargeRegion::log(Election const& election, bool includeSeats, bool includeBooths) const
{
  logger << "\nLargeRegion: " << name << "\n";
  PA_LOG_VAR(finalSpecificFpDeviations);
  PA_LOG_VAR(finalSpecificTppDeviation);
  PA_LOG_VAR(offsetSpecificFpDeviations);
  PA_LOG_VAR(offsetSpecificTppDeviation);
  node.log();
  if (includeSeats) {
    for (auto const& seat : seats) {
      election.seats.at(seat).log(election, includeBooths);
    }
  }
}

std::unique_ptr<LiveData::Provider> LiveV2::Election::generateScenario(
  int iterationIndex) const {
  auto newElection = std::make_unique<LiveV2::Election>(*this);

  newElection->generateVariability(iterationIndex);

  return newElection;
}

LiveV2::Election::FloatInformation LiveV2::Election::getSeatOthersInformation(std::string const& seatName, std::map<int, float> const& representedParties) const {
  int seatIndex = std::find_if(seats.begin(), seats.end(), [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin();
  if (seatIndex != int(seats.size())) {
    float othersVotes = 0.0f;
    float totalVotes = seats[seatIndex].node.totalFpVotesProjected();
    if (totalVotes <= 0.0f) {
      return {0.0f, seats[seatIndex].node.fpCompletion,
        seats[seatIndex].node.fpConfidence};
    }
    for (auto [partyIndex, votes] : seats[seatIndex].node.fpVotesProjected) {
      bool isIndependent = partyIndex == run.indPartyIndex;
      bool isRepresented = representedParties.contains(partyIndex);
      bool isOthers = (partyIndex >= project.parties().count() || !isRepresented) && !isIndependent;
      float voteShare = votes / totalVotes * 100.0f;
      isOthers = isOthers || ( isIndependent && transformVoteShare(voteShare) < run.indEmergence.fpThreshold);
      if (isOthers && votes > 0.0f) {
        othersVotes += votes;
      }
    }
    float othersShare = othersVotes / totalVotes * 100.0f;
    return {othersShare, seats[seatIndex].node.fpCompletion, seats[seatIndex].node.fpConfidence};
  }
  return {0.0f, 0.0f, 0.0f};
}

std::vector<LiveV2::Election::BoothSnapshot> LiveV2::Election::getBoothSnapshots() const {
  std::vector<BoothSnapshot> snapshots;
  snapshots.reserve(booths.size());
  for (auto const& booth : booths) {
    BoothSnapshot snapshot;
    if (booth.parentSeatId >= 0 && booth.parentSeatId < int(seats.size())) {
      snapshot.seatName = seats[booth.parentSeatId].name;
    }
    snapshot.boothName = booth.name;
    snapshot.boothType = booth.boothType;
    snapshot.voteType = booth.voteType;
    snapshot.sameSeat = booth.sameSeat;
    snapshots.push_back(std::move(snapshot));
  }
  return snapshots;
}

LiveV2::Election::Election(Results2::Election const& previousElection, Results2::Election const& currentElection, PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), sim(sim), run(run)
{
  getNatPartyIndex();
  loadEstimatedPreferenceFlows();
  initializePartyMappings(previousElection, currentElection);
  createNodesFromElectionData(previousElection, currentElection);
  calculateTppEstimates(true); // This is done now so that we can observe deviations from preference flows
  aggregate(); // preliminary, for calculating preference flow and seat fp totals
  calculatePreferenceFlowDeviations();
  calculateTppEstimates(false); // Calculate again, this time using the observed deviations from preference flows
  includeBaselineResults(currentElection);
  extrapolateBaselineSwings();
  calculateDeviationsFromBaseline();
  aggregate();
  determineSpecificDeviations();
  measureFpBoothTypeBiases();
  measureTppBoothTypeBiases();
  calculateNationalsProportions();
  calculateTcpPreferenceFlows();
  recomposeVoteCounts();
  calculateLivePreferenceFlowDeviations();
  prepareVariability();
  log(true, true, true);
}

void Election::getNatPartyIndex() {
	natPartyIndex = project.parties().indexByShortCode("NAT");
	if (natPartyIndex == -1) natPartyIndex = InvalidPartyIndex;
}

void Election::loadEstimatedPreferenceFlows() {
  preferenceFlowMap.clear();
	preferenceExhaustMap.clear();
	prevPreferenceOverrides.clear();
	auto lines = extractElectionDataFromFile("analysis/Data/preference-estimates.csv", run.getTermCode());
	for (auto const& line : lines) {
		if (line.size() < 4) {
			throw std::runtime_error(
				"Preference estimate for " + run.getTermCode() + " has fewer than four columns.");
		}
		auto const partyValues = splitString(line[2], " ");
		if (partyValues.empty() || partyValues[0].empty()) {
			throw std::runtime_error(
				"Preference estimate for " + run.getTermCode() + " has no party code.");
		}
		std::string const& party = partyValues[0];
		int const partyIndex = party == OthersCode
			? PartyCollection::InvalidIndex
			: project.parties().indexByShortCode(party);
		// A named party absent from this project must not overwrite the generic
		// Others entry, which deliberately uses the invalid-party map key.
		if (partyIndex == PartyCollection::InvalidIndex && party != OthersCode) {
			continue;
		}
		if (preferenceFlowMap.contains(partyIndex)) {
			throw std::runtime_error(
				"Duplicate preference estimate for " + party + " in "
				+ run.getTermCode() + ".");
		}
		float const thisPreferenceFlow = std::stof(line[3]);
		if (!std::isfinite(thisPreferenceFlow)
			|| thisPreferenceFlow <= 0.0f || thisPreferenceFlow >= 100.0f) {
			throw std::runtime_error(
				"Preference flow for " + party + " in " + run.getTermCode()
				+ " must be finite and strictly between 0 and 100.");
		}
		preferenceFlowMap[partyIndex] = thisPreferenceFlow;
		if (line.size() >= 5 && !line[4].empty() && line[4][0] != '#') {
			float const thisExhaustRate = std::stof(line[4]);
			if (!std::isfinite(thisExhaustRate)
				|| thisExhaustRate < 0.0f || thisExhaustRate >= 100.0f) {
				throw std::runtime_error(
					"Exhaust rate for " + party + " in " + run.getTermCode()
					+ " must be finite and in [0, 100).");
			}
			preferenceExhaustMap[partyIndex] = thisExhaustRate;
		}
		else {
			preferenceExhaustMap[partyIndex] = 0.0f;
		}
	}

	preferenceFlowMap[0] = 100.0f;
	preferenceFlowMap[1] = 0.0f;
	preferenceExhaustMap[0] = 0.0f;
	preferenceExhaustMap[1] = 0.0f;
	if (!preferenceFlowMap.contains(PartyCollection::InvalidIndex)) {
		throw std::runtime_error(
			"No generic Others preference estimate was supplied for "
			+ run.getTermCode() + ".");
	}
  if (run.getTermCode() == "2025fed") {
    int const oneNationIndex = project.parties().indexByShortCode("ONP");
    if (oneNationIndex != -1) {
      prevPreferenceOverrides[oneNationIndex] = 35.7f;
    }
  }
}

void Election::initializePartyMappings(
  Results2::Election const& previousElection,
  Results2::Election const& currentElection) {
  for (auto const& [id, party] : currentElection.parties) {
    int simIndex = project.parties().indexByShortCode(party.shortCode);
    if (simIndex != -1) {
      ecPartyToInternalParty[id] = simIndex;
      ecAbbreviationToInternalParty[party.shortCode] = simIndex;
    }
  }

  for (auto const& [id, party] : previousElection.parties) {
    int simIndex = project.parties().indexByShortCode(party.shortCode);
    if (simIndex != -1) {
      ecPartyToInternalParty[id] = simIndex;
      ecAbbreviationToInternalParty[party.shortCode] = simIndex;
    }
  }
}

void Election::createNodesFromElectionData(
  Results2::Election const& previousElection,
  Results2::Election const& currentElection) {
  for (auto const& [regionId, region] : project.regions()) {
    largeRegions.push_back(LargeRegion(region));
  }
  for (auto const& [id, seat] : currentElection.seats) {
    int seatIndex = seats.size();
    auto const& projectSeat = project.seats().accessByName(seat.name).second;
    int const parentRegionIndex = project.regions().idToIndex(projectSeat.region);
    if (parentRegionIndex == RegionCollection::InvalidIndex) {
      throw std::runtime_error(
        "Seat '" + seat.name + "' refers to a region that is not present.");
    }
    seats.push_back(Seat(seat, parentRegionIndex));
    largeRegions.at(parentRegionIndex).seats.push_back(seatIndex);
    for (auto const& boothId : seat.booths) {
      if (!currentElection.booths.contains(boothId)) {
        continue;
      }
      auto const& currentBooth = currentElection.booths.at(boothId);

      // Find matching booth in previous election if it exists
      std::optional<Results2::Booth const*> previousBoothPtr = std::nullopt;
      std::string previousSeatName = "";
      // IDs are the only unique identifier for booths, as names can change
      // between elections without the ID changing.
      auto const previousBoothIt = previousElection.booths.find(currentBooth.id);
      if (previousBoothIt != previousElection.booths.end()) {
        auto const& previousBooth = previousBoothIt->second;
        // The AEC sometimes reuses an ID for different PPVC booths between
        // elections. For example, ID 34056 referred to Brunswick WILLS PPVC
        // in 2025 but Pascoe Vale WILLS PPVC in 2022. Check the beginning of
        // the name before accepting an ID match for a PPVC.
        bool const mismatchedPpvc =
          currentBooth.type == Results2::Booth::Type::Ppvc
          && lowerCasePrefix(previousBooth.name, 4)
            != lowerCasePrefix(currentBooth.name, 4);
        if (!mismatchedPpvc) {
          previousBoothPtr = &previousBooth;
          previousSeatName = previousElection.seats.at(previousBooth.parentSeat).name;
        }
      }
      bool const sameSeat = previousBoothPtr.has_value() && (
        previousSeatName == seat.name
        || (!projectSeat.previousName.empty()
          && previousSeatName == projectSeat.previousName));

      // Create new booth with mapping function
      booths.emplace_back(Booth(
          currentBooth, 
          previousBoothPtr,
          [this, &previousElection, &currentElection](int partyId, bool isPrevious) {
            return mapPartyId(
              partyId, isPrevious, previousElection, currentElection);
          },
          seatIndex,
          natPartyIndex,
          sameSeat
      ));
      auto& liveSeat = seats.at(seatIndex);
      liveSeat.booths.push_back(int(booths.size()) - 1);
    }

    // Now make the booths for declaration votes
    // Find matching seat in previous election if it exists
    std::vector<std::string> previousSeatNames = {seat.name};
    if (projectSeat.previousName.size()) {
      previousSeatNames.push_back(projectSeat.previousName);
    }
    if (projectSeat.useFpResults.size()) {
      previousSeatNames.push_back(projectSeat.useFpResults);
    }
    std::optional<Results2::Seat const*> previousSeatPtr = std::nullopt;
    for (auto const& [prevSeatId, prevSeat] : previousElection.seats) {
      // IDs are the only unique identifier for seats
      // as names can be changed from one election to the next
      // without changing the ID
      for (auto const& previousSeatName : previousSeatNames) {
        if (prevSeat.name == previousSeatName) {
          previousSeatPtr = &prevSeat;
          break;
        }
      }
      if (previousSeatPtr.has_value()) {
        break;
      }
    }
    std::set<Results2::VoteType> declarationVoteTypes;
    for (auto const& [candidateId, votesByType] : seat.fpVotes) {
      for (auto const& [voteType, votes] : votesByType) {
        if (voteType != Results2::VoteType::Ordinary) {
          declarationVoteTypes.insert(voteType);
        }
      }
    }
    for (auto const voteType : declarationVoteTypes) {
      if (voteType == Results2::VoteType::Ordinary) continue;
      std::optional<Results2::Seat::VotesByType const*> prevFpVotes = std::nullopt;
      std::optional<Results2::Seat::VotesByType const*> prevTcpVotes = std::nullopt;

      if (previousSeatPtr.has_value()) {
        prevFpVotes.emplace(&previousSeatPtr.value()->fpVotes);
        prevTcpVotes.emplace(&previousSeatPtr.value()->tcpVotesCandidate);
      }

      booths.emplace_back(
        seat.fpVotes,
        seat.tcpVotesCandidate,
        prevFpVotes,
        prevTcpVotes,
        voteType,
        [this, &previousElection, &currentElection](int partyId, bool isPrevious) {
          return mapPartyId(
            partyId, isPrevious, previousElection, currentElection);
        },
        seatIndex,
        natPartyIndex,
        true
      );

      auto& liveSeat = seats.at(seatIndex);
      liveSeat.booths.push_back(int(booths.size()) - 1);
    }
  }
}

template<typename T>
std::vector<Node const*> Election::getThisAndParents(T& child) const {
  std::vector<Node const*> parents;
  parents.push_back(&child.node);
  if constexpr (std::is_same_v<T, Booth>) {
    parents.push_back(&seats.at(child.parentSeatId).node);
    parents.push_back(&largeRegions.at(seats.at(child.parentSeatId).parentRegionIndex).node);
    parents.push_back(&node);
  }
  else if constexpr (std::is_same_v<T, Seat>) {
    parents.push_back(&largeRegions.at(child.parentRegionIndex).node);
    parents.push_back(&node);
  }
  else if constexpr (std::is_same_v<T, LargeRegion>) {
    parents.push_back(&node);
  }
  return parents;
}

void Election::calculateTppEstimates(bool withTpp) {
  for (auto& booth : booths) {
    // Pass one handles only observed TPP booths and records preference-flow
    // evidence. After that evidence is aggregated, pass two handles only booths
    // without observed TPP and estimates their TPP from FP votes.
    if (booth.node.tppShare.has_value() != withTpp) continue;

    // The preference flows vary in each booth from the overall election flows, so we need to calculate an
    // offset value to properly compare each booth to the baseline
    // The offset should be applied to the new estimated tpp estimate before calculating
    // the estimated swing
    auto calculatePreferenceRateOffset = [this, &booth](bool current) -> std::optional<float> {
      auto const& tcpVotes = current ? booth.node.tcpVotesCurrent : booth.node.tcpVotesPrevious;
      auto const totalTcpVotes = current
        ? booth.node.totalTcpVotesCurrent()
        : booth.node.totalTcpVotesPrevious();
      if (totalTcpVotes > 0 && isTppSet(tcpVotes, natPartyIndex)) {
        float partyOnePreferenceEstimatePercent = 0.0f;
        int preferredCoalitionParty = tcpVotes.contains(natPartyIndex) ? natPartyIndex : 1;
        float totalFpVotes = current ? booth.node.totalFpVotesCurrent() : booth.node.totalVotesPrevious();
        if (totalFpVotes <= 0.0f) {
          return std::nullopt;
        }

        float const relativeTotalDifference =
          std::abs(float(totalTcpVotes) - totalFpVotes) / totalFpVotes;
        // This comparison is suitable only for compulsory-preferential counts.
        // Under OPV, exhausted ballots legitimately reduce the TCP total. Before
        // using this path for an OPV hindcast, compare TCP against the expected
        // non-exhausted FP total using preferenceExhaustMap instead.
        if (relativeTotalDifference > MaxPreferenceVoteTotalDifference) {
          logger << "Warning: " << (current ? "Current" : "Previous")
            << " FP and TCP totals differ by " << relativeTotalDifference * 100.0f
            << "% for booth " << booth.name
            << "; not calculating its preference rate offset.\n";
          return std::nullopt;
        }

        auto const& fpVotes = current ? booth.node.fpVotesCurrent : booth.node.fpVotesPrevious;
        for (auto const& [partyId, votes] : fpVotes) {
          float partyPercent = float(votes) / totalFpVotes * 100.0f;
          if (partyId == preferredCoalitionParty || partyId == 0) {
            continue;
          }
          if (partyId == natPartyIndex || partyId == 1) {
            // Other coalition party's votes, assume some leakage to Labor
            partyOnePreferenceEstimatePercent +=
              partyPercent * CoalitionLeakagePercent * 0.01f;
          }
          else if (!current && prevPreferenceOverrides.contains(partyId)) {
            // Override the historical flow only. Current results must be
            // compared with the current election's configured expectation.
            partyOnePreferenceEstimatePercent += partyPercent * prevPreferenceOverrides.at(partyId) * 0.01f;
          }
          else if (preferenceFlowMap.contains(partyId)) {
            partyOnePreferenceEstimatePercent += partyPercent * preferenceFlowMap.at(partyId) * 0.01f;
          }
          else {
            partyOnePreferenceEstimatePercent += partyPercent
              * preferenceFlowMap.at(PartyCollection::InvalidIndex) * 0.01f;
          }
        }
        float nonMajorVotes = totalFpVotes - fpVotes.at(0) - fpVotes.at(preferredCoalitionParty);
        if (nonMajorVotes <= 0.0f) {
          // A current booth with no non-major votes contains no preference-flow
          // evidence. For the previous election, a neutral offset retains the
          // seat-wide flow while giving this usually tiny booth little weight.
          return current ? std::nullopt : std::optional<float>(0.0f);
        }
        float nonMajorVotesPercent = nonMajorVotes / totalFpVotes * 100.0f;
        float partyOnePreferenceRateEstimate = partyOnePreferenceEstimatePercent / nonMajorVotesPercent * 100.0f;
        float const tcpToFpScale = totalFpVotes / float(totalTcpVotes);
        float const scaledPartyOneTcpVotes = tcpVotes.at(0) * tcpToFpScale;
        float partyOnePreferenceRateActual =
          (scaledPartyOneTcpVotes - fpVotes.at(0)) / nonMajorVotes * 100.0f;
        if (!std::isfinite(partyOnePreferenceRateActual)
          || !std::isfinite(partyOnePreferenceRateEstimate)
          || partyOnePreferenceRateActual <= 0.0f
          || partyOnePreferenceRateActual >= 100.0f
          || partyOnePreferenceRateEstimate <= 0.0f
          || partyOnePreferenceRateEstimate >= 100.0f) {
          return std::nullopt;
        }
        return transformVoteShare(partyOnePreferenceRateActual)
          - transformVoteShare(partyOnePreferenceRateEstimate);
      }
      return std::nullopt;
    };
    
    std::optional<float> prevPreferenceRateOffset = calculatePreferenceRateOffset(false);

    // Calculate estimate of party one's share of the TPP based on the FP votes
    // This currently assumes every FP vote reaches one of the final two. It is
    // not OPV-compatible: preferenceExhaustMap is loaded but must be applied to
    // both the Labor allocation and continuing-vote denominator before this is
    // used for an OPV election.
    float partyOneShare = 0.0f;
    for (auto const& [partyId, share] : booth.node.fpShares) {

      auto applyOffset = [this, &booth, prevPreferenceRateOffset](float flow) {
        if (flow == 0.0f || flow == 100.0f) return flow;
        auto const& thisAndParents = getThisAndParents(booth);
        float preferenceFlowDeviation = prevPreferenceRateOffset.value_or(0.0f);
        for (auto const& parent : thisAndParents) {
          preferenceFlowDeviation += parent->specificPreferenceFlowDeviation.value_or(0.0f);
        }
        return detransformVoteShare(
          transformVoteShare(flow) + preferenceFlowDeviation);
      };

      // work out which coalition party will make the TPP here
      int preferredCoalitionParty = 1;
      if (booth.node.fpVotesCurrent.contains(natPartyIndex)) {
        if (!booth.node.fpVotesCurrent.contains(1)) {
          preferredCoalitionParty = natPartyIndex;
        }
        else if (booth.node.fpVotesCurrent.at(natPartyIndex) > booth.node.fpVotesCurrent.at(1)) {
          preferredCoalitionParty = natPartyIndex;
        }
      }
      // now allocate preferences for non-major parties
      if (partyId == preferredCoalitionParty) {
        // This party will make the TPP, so will have zero preferences to Labor
        continue;
      }
      else if (partyId == natPartyIndex || partyId == 1) {
        // Other coalition party's votes, assume some leakage to Labor
        partyOneShare += detransformVoteShare(share) * applyOffset(20.0f) * 0.01f;
      }
      else if (preferenceFlowMap.contains(partyId)) {
        partyOneShare += detransformVoteShare(share) * applyOffset(preferenceFlowMap.at(partyId)) * 0.01f;
      }
      else {
        partyOneShare += detransformVoteShare(share)
          * applyOffset(preferenceFlowMap.at(PartyCollection::InvalidIndex)) * 0.01f;
      }
    }

    int previousTotal = booth.node.totalVotesPrevious();
    if (
      booth.node.totalTcpVotesPrevious() > 0
      && isTppSet(booth.node.tcpVotesPrevious, natPartyIndex)
    ) {
      // TPP share is directly available for this booth, so record it
      float previousShare = transformVoteShare(std::clamp(
        static_cast<float>(booth.node.tcpVotesPrevious.at(0))
          / static_cast<float>(booth.node.totalTcpVotesPrevious()) * 100.0f,
        1.0f, 99.0f));
      booth.node.tppSharePrevious = previousShare;
    } else if (previousTotal > 0) {
      // work out which coalition party would have made the TPP here
      int preferredCoalitionParty = 1;
      if (booth.node.fpVotesPrevious.contains(natPartyIndex)) {
        if (!booth.node.fpVotesPrevious.contains(1)) {
          preferredCoalitionParty = natPartyIndex;
        }
        else if (booth.node.fpVotesPrevious.at(natPartyIndex) > booth.node.fpVotesPrevious.at(1)) {
          preferredCoalitionParty = natPartyIndex;
        }
      }

      // Estimate the previous election's TPP share if not available
      float partyOnePreferenceEstimatePercent = 0.0f;

      for (auto const& [partyId, votes] : booth.node.fpVotesPrevious) {
        float share = static_cast<float>(votes) / static_cast<float>(previousTotal) * 100.0f;
        // now allocate preferences for non-major parties
        if (partyId == preferredCoalitionParty) {
          // This party will make the TPP, so will have zero preferences to Labor
          continue;
        }
        else if (partyId == natPartyIndex || partyId == 1) {
          // Other coalition party's votes, assume some leakage to Labor
          partyOnePreferenceEstimatePercent += share * CoalitionLeakagePercent * 0.01f;
        }
        else if (prevPreferenceOverrides.contains(partyId)) {
          // Override preference flow when it differed from the current election.
          partyOnePreferenceEstimatePercent += share * prevPreferenceOverrides.at(partyId) * 0.01f;
        }
        else if (preferenceFlowMap.contains(partyId)) {
          partyOnePreferenceEstimatePercent += share * preferenceFlowMap.at(partyId) * 0.01f;
        }
        else {
          partyOnePreferenceEstimatePercent += share
            * preferenceFlowMap.at(PartyCollection::InvalidIndex) * 0.01f;
        }
      }
      booth.node.tppSharePrevious = transformVoteShare(
        std::clamp(partyOnePreferenceEstimatePercent, 1.0f, 99.0f));
    }

    if (partyOneShare > 0.0f && partyOneShare < 100.0f) {
      std::optional<float> currentPreferenceRateOffset = calculatePreferenceRateOffset(true);
      if (currentPreferenceRateOffset && prevPreferenceRateOffset) {
        bool isBad = false;
        // Error inputs can result in NaN or Inf
        // Inf can also result from legitimate results where the
        // preference flow is 0% or 100%
        // For analysis purposes, it's better to just ignore these booths
        if (!std::isfinite(currentPreferenceRateOffset.value())
          || !std::isfinite(prevPreferenceRateOffset.value())) {
          logger << "Warning: Non-finite preference rate offset for booth " << booth.name << "\n";
          logger << "Current: " << currentPreferenceRateOffset.value() << "\n";
          logger << "Previous: " << prevPreferenceRateOffset.value() << "\n";
          isBad = true;
        }
        if (!isBad) {
          booth.node.preferenceFlowDeviation = currentPreferenceRateOffset.value() - prevPreferenceRateOffset.value();
          // Preference evidence requires both FP and TPP counts. Ordinary
          // booths are complete (confidence 1); incremental categories retain
          // their progress-based confidence rather than becoming fully trusted
          // as soon as their first batch arrives.
          booth.node.preferenceFlowConfidence = std::min(
            booth.node.fpConfidence, booth.node.tppCompletion);
          booth.calculateTppSwing(natPartyIndex);
        }
      }
      if (!withTpp) {
        booth.node.tppShare = transformVoteShare(partyOneShare);
        // An FP-derived TPP estimate is less informative than a direct count,
        // while still respecting how much of an incremental category exists.
        booth.node.tppConfidence = booth.node.fpConfidence * 0.5f;
        booth.calculateTppSwing(natPartyIndex);
      }
    }
  }
}

void Election::calculatePreferenceFlowDeviations() {
  determineElectionPreferenceFlowDeviations();
  determineLargeRegionPreferenceFlowDeviations();
  determineSeatPreferenceFlowDeviations();
  determineBoothPreferenceFlowDeviations();
}

void Election::determineElectionPreferenceFlowDeviations() {
  node.specificPreferenceFlowDeviation = node.preferenceFlowDeviation.value_or(0.0f) * obsWeight(node.preferenceFlowConfidence, PreferenceFlowObsWeightStrength);
}

void Election::determineLargeRegionPreferenceFlowDeviations() {
  for (auto& largeRegion : largeRegions) {
    float preferenceFlowObsWeight = obsWeight(largeRegion.node.preferenceFlowConfidence, PreferenceFlowObsWeightStrength);
    float parentPreferenceFlowDeviation = node.specificPreferenceFlowDeviation.value_or(0.0f);
    float withoutElectionSpecific = largeRegion.node.preferenceFlowDeviation ? largeRegion.node.preferenceFlowDeviation.value() - parentPreferenceFlowDeviation : 0.0f;
    largeRegion.node.specificPreferenceFlowDeviation = withoutElectionSpecific * preferenceFlowObsWeight;
  }
}

void Election::determineSeatPreferenceFlowDeviations() {
  for (auto& largeRegion : largeRegions) {
    for (int seatIndex : largeRegion.seats) {
      auto& seat = seats.at(seatIndex);
      float preferenceFlowObsWeight = obsWeight(seat.node.preferenceFlowConfidence, PreferenceFlowObsWeightStrength);
      float parentPreferenceFlowDeviation = largeRegion.node.specificPreferenceFlowDeviation.value_or(0.0f);
      parentPreferenceFlowDeviation += node.specificPreferenceFlowDeviation.value_or(0.0f);
      float withoutElectionSpecific = seat.node.preferenceFlowDeviation ? seat.node.preferenceFlowDeviation.value() - parentPreferenceFlowDeviation : 0.0f;
      seat.node.specificPreferenceFlowDeviation = withoutElectionSpecific * preferenceFlowObsWeight;
    }
  }
}

void Election::determineBoothPreferenceFlowDeviations() {
  for (auto& seat : seats) {
    for (int boothIndex : seat.booths) {
      auto& booth = booths.at(boothIndex);
      float preferenceFlowObsWeight = obsWeight(booth.node.preferenceFlowConfidence, PreferenceFlowObsWeightStrength);
      float parentPreferenceFlowDeviation = seat.node.specificPreferenceFlowDeviation.value_or(0.0f);
      auto const& largeRegion = largeRegions.at(seat.parentRegionIndex);
      parentPreferenceFlowDeviation += largeRegion.node.specificPreferenceFlowDeviation.value_or(0.0f);
      parentPreferenceFlowDeviation += node.specificPreferenceFlowDeviation.value_or(0.0f);
      float withoutElectionSpecific = booth.node.preferenceFlowDeviation ? booth.node.preferenceFlowDeviation.value() - parentPreferenceFlowDeviation : 0.0f;
      booth.node.specificPreferenceFlowDeviation = withoutElectionSpecific * preferenceFlowObsWeight;
    }
  }
}

void Election::includeBaselineResults(
  Results2::Election const& currentElection) {
  if (!sim.getLiveBaselineReport()) {
    return;
  }
  includeSeatBaselineResults(currentElection);
  includeLargeRegionBaselineResults();
  includeElectionBaselineResults();
}

void Election::includeSeatBaselineResults(
  Results2::Election const& currentElection) {
  auto const& baseline = sim.getLiveBaselineReport().value();
  for (int i = 0; i < int(baseline.seatName.size()); ++i) {
    auto const& name = baseline.seatName.at(i);
    auto seatIt = std::find_if(seats.begin(), seats.end(), [name](Seat const& s) { return s.name == name; });
    if (seatIt == seats.end()) {
      logger << "Warning: Seat " << name << " from baseline report not found in current election results\n";
      continue;
    }
    auto& seat = *seatIt;
    for (auto const& [partyId, probabilityBands] : baseline.seatFpProbabilityBand.at(i)) {
      float median = probabilityBands.at((probabilityBands.size() - 1) / 2);
      // Some categories like emerging-ind will likely have a median of 0,
      // but we want to include them, so assign them a small baseline share
      // This won't affect significant parties or independents as their median will
      // be above 1% even in their worst seats.
      seat.node.fpSharesBaseline[partyId] = transformVoteShare(std::clamp(median, 1.0f, 99.0f));
    }

    if (seat.node.fpSharesBaseline.contains(EmergingIndIndex) && !seat.node.fpSharesBaseline.contains(run.indPartyIndex)) {
      // convert emerging-ind to independent for easier internal processing
      seat.node.fpSharesBaseline[run.indPartyIndex] = seat.node.fpSharesBaseline.at(EmergingIndIndex);
      seat.node.fpSharesBaseline.erase(EmergingIndIndex);
    }

    // Independents can't be matched by party ID, so match the baseline independent
    // candidate name against the current seat's independent candidates.
    std::string baselineIndependentName;
    if (baseline.seatCandidateNames.size() > size_t(i) && baseline.seatCandidateNames.at(i).contains(run.indPartyIndex)) {
      baselineIndependentName = baseline.seatCandidateNames.at(i).at(run.indPartyIndex);
    }

    int bestIndependentScore = 0;
    int bestIndependentId = InvalidPartyIndex;
    auto currentSeatIt = std::find_if(currentElection.seats.begin(), currentElection.seats.end(),
      [&seat](auto const& seatPair) { return seatPair.second.name == seat.name; });
    if (currentSeatIt != currentElection.seats.end()) {
      for (auto const& [candidateId, voteTypes] : currentSeatIt->second.fpVotes) {
        auto candidateIt = currentElection.candidates.find(candidateId);
        if (candidateIt == currentElection.candidates.end()) continue;
        if (candidateIt->second.party != Results2::Candidate::Independent) continue;
        int candidateInternalId = candidateId + IndependentPartyIdOffset;
        if (bestIndependentId == InvalidPartyIndex) {
          bestIndependentId = candidateInternalId;
        }
        if (baselineIndependentName.empty()) {
          continue;
        }
        int score = candidateNameMatchScore(candidateIt->second.name, baselineIndependentName);
        if (score > bestIndependentScore) {
          bestIndependentScore = score;
          bestIndependentId = candidateInternalId;
        }
      }
    }

    seat.independentPartyIndex = bestIndependentId;

    // Independents can't be matched by party ID, so we need to find the best-performing independent
    // to match to the independent party index
    float bestIndependentShare = 0.0f;
    bestIndependentId = seat.independentPartyIndex;
    for (auto const& [prevPartyId, share] : seat.node.fpVotesCurrent) {
      if (prevPartyId < IndependentPartyIdOffset) continue;
      if (share > bestIndependentShare) {
        bestIndependentShare = share;
        bestIndependentId = prevPartyId;
      }
    }
    seat.liveIndependentPartyIndex = bestIndependentId;

    for (auto const& [partyId, swing] : seat.node.fpSharesBaseline) {
      if (seat.node.totalVotesPrevious() > 0) {
        float thisVotesPrevious = 0.0f;
        if (seat.node.fpVotesPrevious.contains(partyId)) {
          thisVotesPrevious = seat.node.fpVotesPrevious.at(partyId);
        }
        else if (partyId == run.indPartyIndex && seat.node.fpVotesPrevious.contains(seat.independentPartyIndex)) {
          thisVotesPrevious = seat.node.fpVotesPrevious.at(seat.independentPartyIndex);
        }
        if (thisVotesPrevious > 0.0f) {
          seat.node.fpSwingsBaseline[partyId] = seat.node.fpSharesBaseline[partyId] - 
            transformVoteShare(float(thisVotesPrevious) / float(seat.node.totalVotesPrevious()) * 100.0f);
          // this ignores redistributions, might have to tweak it to handle some cases better
        }
      }
    }
    
    float tppMedian = baseline.seatTppProbabilityBand.at(i).at((baseline.seatTppProbabilityBand.at(i).size() - 1) / 2);
    if (tppMedian > 0 && tppMedian < 100) {
      seat.node.tppShareBaseline = transformVoteShare(tppMedian);
    }
    int projectSeatIndex = project.seats().indexByName(name);
    ::Seat const& projectSeat = project.seats().viewByIndex(projectSeatIndex);
    float tppExistingShare = transformVoteShare(projectSeat.tppMargin + 50.0f);
    if (seat.node.tppShareBaseline) {
      seat.node.tppSwingBaseline = seat.node.tppShareBaseline.value() - tppExistingShare;
    }
  }
}

void Election::includeLargeRegionBaselineResults() {
  auto const& baseline = sim.getLiveBaselineReport().value();
  for (int i = 0; i < int(baseline.regionName.size()); ++i) {
    auto const& name = baseline.regionName.at(i);
    auto regionIt = std::find_if(largeRegions.begin(), largeRegions.end(), [name](LargeRegion const& r) { return r.name == name; });
    if (regionIt == largeRegions.end()) {
      logger << "Warning: Region " << name << " from baseline report not found in current election results\n";
      continue;
    }
    auto& region = *regionIt;
    for (auto const& [partyId, probabilityBands] : baseline.regionFpProbabilityBand.at(i)) {
      float median = probabilityBands.at((probabilityBands.size() - 1) / 2);
      if (median > 0 && median < 100) {
        region.node.fpSharesBaseline[partyId] = transformVoteShare(median);
      }
    }
    float tppMedian = baseline.regionTppProbabilityBand.at(i).at((baseline.regionTppProbabilityBand.at(i).size() - 1) / 2);
    if (tppMedian > 0 && tppMedian < 100) {
      region.node.tppShareBaseline = transformVoteShare(tppMedian);
    }
  }
}

void Election::includeElectionBaselineResults() {
  auto const& baseline = sim.getLiveBaselineReport().value();
  for (auto const& [partyId, probabilityBands] : baseline.electionFpProbabilityBand) {
    float median = probabilityBands.at((probabilityBands.size() - 1) / 2);
    if (median > 0 && median < 100) {
      node.fpSharesBaseline[partyId] = transformVoteShare(median);
    }
  }
  float tppMedian = baseline.electionTppProbabilityBand.at((baseline.electionTppProbabilityBand.size() - 1) / 2);
  if (tppMedian > 0 && tppMedian < 100) {
    node.tppShareBaseline = transformVoteShare(tppMedian);
  }
}

void Election::extrapolateBaselineSwings() {
  for (auto& seat : seats) {
    for (int boothIndex : seat.booths) {
      auto& booth = booths.at(boothIndex);
      for (auto const& [partyId, swing] : seat.node.fpSwingsBaseline) {
        booth.node.fpSwingsBaseline[partyId] = swing;
      }
      for (auto const& [partyId, share] : seat.node.fpSharesBaseline) {
        // If there isn't a swing baseline, use the share baseline instead
        // TODO: Make this more realistic in situations where the party would be
        // expected to perform differently in different parts of the seat
        if (!booth.node.fpSwingsBaseline.contains(partyId)) {
          booth.node.fpSharesBaseline[partyId] = share;
        }
      }
      if (seat.node.tppSwingBaseline) {
        booth.node.tppSwingBaseline = seat.node.tppSwingBaseline.value();
      }
    }
  }
}

void Election::calculateDeviationsFromBaseline() {
  for (auto& seat : seats) {
    for (int boothIndex : seat.booths) {
      auto& booth = booths.at(boothIndex);
      for (auto const& [partyId, baselineSwing] : seat.node.fpSwingsBaseline) {
        if (booth.node.fpSwings.contains(partyId)) {
          booth.node.fpDeviations[partyId] = booth.node.fpSwings.at(partyId) - baselineSwing;
        }
        else if (partyId == run.indPartyIndex && booth.node.fpSwings.contains(seat.liveIndependentPartyIndex)) {
          if (seat.liveIndependentPartyIndex != InvalidPartyIndex) {
            booth.node.fpDeviations[partyId] = booth.node.fpSwings.at(seat.liveIndependentPartyIndex) - baselineSwing;
          }
        }
      }
      for (auto const& [partyId, baselineShare] : seat.node.fpSharesBaseline) {
        // fp swings are preferred, so only use share baseline if there is no swing baseline
        if (booth.node.fpSwingsBaseline.contains(partyId)) continue;
        if (booth.node.fpShares.contains(partyId)) {
          booth.node.fpDeviations[partyId] = booth.node.fpShares.at(partyId) - baselineShare;
        }
        else if (partyId == run.indPartyIndex && booth.node.fpShares.contains(seat.liveIndependentPartyIndex)) {
          if (seat.liveIndependentPartyIndex != InvalidPartyIndex) {
            booth.node.fpDeviations[partyId] = booth.node.fpShares.at(seat.liveIndependentPartyIndex) - baselineShare;
          }
        }
      }
      
      if (seat.node.tppSwingBaseline) {
        if (booth.node.tppSwing) {
          booth.node.tppDeviation = booth.node.tppSwing.value() - seat.node.tppSwingBaseline.value();
        }
      }
    }
  }
}

void Election::measureFpBoothTypeBiases() {
  // TODO: Calibrate the observation blend and uncertainty formulas used here
  // and in measureTppBoothTypeBiases; they are currently pragmatic heuristics.
  { // biases for polling places (including PPVC)
    // map is party, then booth type
    std::map<int, std::map<Results2::Booth::Type, float>> fpBiasWeightedSums;
    std::map<int, std::map<Results2::Booth::Type, float>> fpWeightSums;
    std::map<int, std::map<Results2::Booth::Type, float>> fpSourceCount;
    for (auto& seat : seats) {
      std::map<int, std::map<Results2::Booth::Type, float>> seatFpDeviationWeightedSums;
      std::map<int, std::map<Results2::Booth::Type, float>> seatFpWeightSums;
      for (auto const& [partyId, _] : seat.node.fpVotesCurrent) {
        for (int boothIndex : seat.booths) {
          auto& booth = booths.at(boothIndex);
          // Handle "other" booths (postals/absent/etc.) separately and ignore "invalid" booths (we don't know what they are)
          if (booth.boothType == Results2::Booth::Type::Other || booth.boothType == Results2::Booth::Type::Invalid) {
            continue;
          }
          if (!booth.node.totalFpVotesCurrent() || !booth.node.fpDeviations.contains(partyId)) {
            // Keep known booth types represented for consistent downstream
            // handling, even when this party has no usable evidence yet.
            seatFpDeviationWeightedSums[partyId][booth.boothType] += 0.0f;
            seatFpWeightSums[partyId][booth.boothType] += 0.0f;
            continue;
          }

          float const weight = booth.node.totalFpVotesCurrent()
            * booth.node.relevanceModifier;
          seatFpDeviationWeightedSums[partyId][booth.boothType] +=
            booth.node.fpDeviations.at(partyId) * weight;
          seatFpWeightSums[partyId][booth.boothType] += weight;
        }
      }
      for (auto const& [partyId, deviationWeightedSums] : seatFpDeviationWeightedSums) {
        if (!deviationWeightedSums.contains(Results2::Booth::Type::Normal)) continue;
        float const normalWeight = seatFpWeightSums[partyId].at(
          Results2::Booth::Type::Normal);
        // A category bias is a within-seat comparison. Do not treat a missing
        // ordinary-booth reference as an observed deviation of zero.
        if (normalWeight <= 0.0f) continue;
        float const normalDeviation = deviationWeightedSums.at(
          Results2::Booth::Type::Normal) / normalWeight;
        for (auto const& [boothType, deviationWeightedSum] : deviationWeightedSums) {
          if (boothType == Results2::Booth::Type::Normal) continue;
          if (seatFpWeightSums[partyId].at(boothType) == 0.0f) {
            // avoid division by zero
            fpBiasWeightedSums[partyId][boothType] += 0.0f;
            fpWeightSums[partyId][boothType] += 0.0f;
            continue;
          }
          float deviation = deviationWeightedSum / seatFpWeightSums[partyId].at(boothType);
          float boothTypeBias = deviation - normalDeviation;
          float weight = seatFpWeightSums[partyId].at(boothType);
          fpBiasWeightedSums[partyId][boothType] += boothTypeBias * weight;
          fpWeightSums[partyId][boothType] += weight;
          ++fpSourceCount[partyId][boothType];
        }
      }
    }

    for (auto const& [partyId, biasWeightedSums] : fpBiasWeightedSums) {
      for (auto const& [boothType, bias] : biasWeightedSums) {
        float votes = fpWeightSums[partyId].at(boothType);
        float overallFpBias = votes ? bias / votes : 0.0f;
        // placeholder formula, works for PPVCs/postals/absents but not smaller categories (but they don't usually have a significant impact)
        float obsProportion = std::pow(votes, 0.9f) / (20000.0f + std::pow(votes, 0.9f));
        float baseline = 0.0f;
        boothTypeFpBiases[partyId][boothType] = overallFpBias * obsProportion + baseline * (1.0f - obsProportion);
        float stdDev = 7.0f * std::exp(-std::pow(votes + 1000.0f, 0.25f) * 0.06f);
        boothTypeFpBiasStdDev[partyId][boothType] = stdDev;
        boothTypeFpBiasesRaw[partyId][boothType] = overallFpBias;
        boothTypeFpSourceCount[partyId][boothType] = fpSourceCount[partyId][boothType];
        boothTypeFpVoteCount[partyId][boothType] = fpWeightSums[partyId][boothType];
      }
    }
  }

  { // biases for declaration votes
    std::map<int, std::map<Results2::VoteType, float>> fpBiasWeightedSums;
    std::map<int, std::map<Results2::VoteType, float>> fpWeightSums;
    std::map<int, std::map<Results2::VoteType, float>> fpSourceCount;
    for (auto& seat : seats) {
      std::map<int, std::map<Results2::VoteType, float>> seatFpDeviationWeightedSums;
      std::map<int, std::map<Results2::VoteType, float>> seatFpWeightSums;
      for (auto const& [partyId, _] : seat.node.fpVotesCurrent) {
        for (int boothIndex : seat.booths) {
          auto& booth = booths.at(boothIndex);
          // make sure that this booth type "exists" (for output/consistency purposes)
          if (booth.voteType != Results2::VoteType::Ordinary) {
            fpBiasWeightedSums[partyId][booth.voteType] += 0.0f;
            fpWeightSums[partyId][booth.voteType] += 0.0f;
          }
          // ignore "invalid" booths (we don't know what they are)
          if (booth.voteType == Results2::VoteType::Invalid) {
            continue;
          }
          if (booth.node.totalFpVotesCurrent() == 0 || !booth.node.fpDeviations.contains(partyId)) {
            // As with booth types, retain known vote types without treating
            // missing deviations as observations.
            seatFpDeviationWeightedSums[partyId][booth.voteType] += 0.0f;
            seatFpWeightSums[partyId][booth.voteType] += 0.0f;
            continue;
          }

          float const weight = booth.node.totalFpVotesCurrent()
            * booth.node.relevanceModifier;
          seatFpDeviationWeightedSums[partyId][booth.voteType] +=
            booth.node.fpDeviations.at(partyId) * weight;
          seatFpWeightSums[partyId][booth.voteType] += weight;
        }
      }

      for (auto const& [partyId, deviationWeightedSums] : seatFpDeviationWeightedSums) {
        if (!seatFpDeviationWeightedSums[partyId].contains(Results2::VoteType::Ordinary)) continue;
        // zero weight sum can occur if a seat's count is being realigned and therefore no TCP results
        if (!seatFpWeightSums[partyId].contains(Results2::VoteType::Ordinary) || seatFpWeightSums[partyId].at(Results2::VoteType::Ordinary) == 0) continue;
        float normalDeviation = seatFpDeviationWeightedSums[partyId].at(Results2::VoteType::Ordinary) / seatFpWeightSums[partyId].at(Results2::VoteType::Ordinary);
        for (auto const& [voteType, deviationWeightedSum] : deviationWeightedSums) {
          if (voteType == Results2::VoteType::Ordinary) continue;
          if (seatFpWeightSums[partyId].at(voteType) == 0.0f) {
            // avoid division by zero
            fpBiasWeightedSums[partyId][voteType] += 0.0f;
            fpWeightSums[partyId][voteType] += 0.0f;
            continue;
          }
          float deviation = deviationWeightedSum / seatFpWeightSums[partyId].at(voteType);
          float voteTypeBias = deviation - normalDeviation;
          float weight = seatFpWeightSums[partyId].at(voteType);
          fpBiasWeightedSums[partyId][voteType] += voteTypeBias * weight;
          fpWeightSums[partyId][voteType] += weight;
          ++fpSourceCount[partyId][voteType];
        }
      }
    }
    for (auto const& [partyId, biasWeightedSums] : fpBiasWeightedSums) {
      for (auto const& [voteType, bias] : biasWeightedSums) {
        float votes = fpWeightSums[partyId].at(voteType);
        float overallFpBias = votes ? bias / votes : 0.0f;
        // placeholder formula, works for PPVCs/postals/absents but not smaller categories (but they don't usually have a significant impact)
        float obsProportion = std::pow(votes, 0.9f) / (20000.0f + std::pow(votes, 0.9f));
        float baseline = 0.0f;
        voteTypeFpBiases[partyId][voteType] = overallFpBias * obsProportion + baseline * (1.0f - obsProportion);
        float stdDev = 7.0f * std::exp(-std::pow(votes + 1000.0f, 0.25f) * 0.06f);
        voteTypeFpBiasStdDev[partyId][voteType] = stdDev;
        voteTypeFpBiasesRaw[partyId][voteType] = overallFpBias;
        voteTypeFpSourceCount[partyId][voteType] = fpSourceCount[partyId][voteType];
        voteTypeFpVoteCount[partyId][voteType] = fpWeightSums[partyId][voteType];
      }
    }
  }
  PA_LOG_VAR(boothTypeFpBiases);
  PA_LOG_VAR(boothTypeFpBiasStdDev);
  PA_LOG_VAR(voteTypeFpBiases);
  PA_LOG_VAR(voteTypeFpBiasStdDev);
}

void Election::measureTppBoothTypeBiases() {
  { // biases for polling places (including PPVC)
    std::map<Results2::Booth::Type, float> tppBiasWeightedSums;
    std::map<Results2::Booth::Type, float> tppWeightSums;
    std::map<Results2::Booth::Type, float> tppSourceCount;
    for (auto& seat : seats) {
      std::map<Results2::Booth::Type, float> seatTppDeviationWeightedSums;
      std::map<Results2::Booth::Type, float> seatTppWeightSums;
      for (int boothIndex : seat.booths) {
        auto& booth = booths.at(boothIndex);
        // Handle "other" booths (postals/absent/etc.) separately and ignore "invalid" booths (we don't know what they are)
        if (booth.boothType == Results2::Booth::Type::Other || booth.boothType == Results2::Booth::Type::Invalid) {
          continue;
        }
        if (booth.node.totalTcpVotesCurrent() == 0
          || !booth.node.tppDeviation.has_value()) {
          // This makes these vote types exist in the sums
          // so that the baseline bias is still included
          // If we don't, there's a sudden snap back when the first booth of that type reports
          // because even though the booth itself has a tiny influence, the baseline
          // is significantly non-zero (since it leans away from the observed swing)
          seatTppDeviationWeightedSums[booth.boothType] += 0.0f;
          seatTppWeightSums[booth.boothType] += 0.0f;
          continue;
        }

        
        float const weight = booth.node.totalTcpVotesCurrent()
          * booth.node.relevanceModifier;
        seatTppDeviationWeightedSums[booth.boothType] +=
          booth.node.tppDeviation.value() * weight;
        seatTppWeightSums[booth.boothType] += weight;
      }
      if (!seatTppDeviationWeightedSums.contains(Results2::Booth::Type::Normal)) continue;
      float const normalWeight = seatTppWeightSums.at(
        Results2::Booth::Type::Normal);
      // A category bias is a within-seat comparison. Do not treat a missing
      // ordinary-booth reference as an observed deviation of zero.
      if (normalWeight <= 0.0f) continue;
      float const normalDeviation = seatTppDeviationWeightedSums.at(
        Results2::Booth::Type::Normal) / normalWeight;
      for (auto const& [boothType, deviationWeightedSum] : seatTppDeviationWeightedSums) {
        if (boothType == Results2::Booth::Type::Normal) continue;
        if (seatTppWeightSums.at(boothType) == 0.0f) {
          // avoid division by zero
          tppBiasWeightedSums[boothType] += 0.0f;
          tppWeightSums[boothType] += 0.0f;
          continue;
        }
        float deviation = deviationWeightedSum / seatTppWeightSums.at(boothType);
        float boothTypeBias = deviation - normalDeviation;
        float weight = seatTppWeightSums.at(boothType);
        tppBiasWeightedSums[boothType] += boothTypeBias * weight;
        tppWeightSums[boothType] += weight;
        ++tppSourceCount[boothType];
      }
    }

    for (auto const& [boothType, bias] : tppBiasWeightedSums) {
      float votes = tppWeightSums.at(boothType);
      float overallTppBias = votes ? tppBiasWeightedSums.at(boothType) / votes : 0.0f;
      // placeholder formula, works for PPVCs/postals/absents but not smaller categories (but they don't usually have a significant impact)
      float obsProportion = std::pow(votes, 0.9f) / (20000.0f + std::pow(votes, 0.9f));
      float baseline = 0.0f;
      // pre-polls tend to move the tpp back towards the baseline
      if (boothType == Results2::Booth::Type::Ppvc) {
        baseline = -0.6f * node.specificTppDeviation.value_or(0.0f);
      }
      boothTypeTppBiases[boothType] = overallTppBias * obsProportion + baseline * (1.0f - obsProportion);
      float stdDev = 7.0f * std::exp(-std::pow(votes + 1000.0f, 0.25f) * 0.06f);
      boothTypeTppBiasStdDev[boothType] = stdDev;
      boothTypeTppBiasesRaw[boothType] = overallTppBias;
      boothTypeTppSourceCount[boothType] = tppSourceCount[boothType];
      boothTypeTppVoteCount[boothType] = tppWeightSums[boothType];
    }
  }

  { // biases for declaration votes
    std::map<Results2::VoteType, float> tppBiasWeightedSums;
    std::map<Results2::VoteType, float> tppWeightSums;
    std::map<Results2::VoteType, float> tppSourceCount;
    for (auto& seat : seats) {
      std::map<Results2::VoteType, float> seatTppDeviationWeightedSums;
      std::map<Results2::VoteType, float> seatTppWeightSums;
      for (int boothIndex : seat.booths) {
        auto& booth = booths.at(boothIndex);
        // make sure that this booth type "exists" (for output/consistency purposes)
        if (booth.voteType != Results2::VoteType::Ordinary) {
          tppBiasWeightedSums[booth.voteType] += 0.0f;
          tppWeightSums[booth.voteType] += 0.0f;
        }
        // ignore "invalid" booths (we don't know what they are)
        if (booth.voteType == Results2::VoteType::Invalid) {
          continue;
        }
        if (booth.node.totalTcpVotesCurrent() == 0
          || !booth.node.tppDeviation.has_value()) {
          // As with booth types, make sure that these vote types exist in the sums, even if just zero
          seatTppDeviationWeightedSums[booth.voteType] += 0.0f;
          seatTppWeightSums[booth.voteType] += 0.0f;
          continue;
        }
        
        float const weight = booth.node.totalTcpVotesCurrent()
          * booth.node.relevanceModifier;
        seatTppDeviationWeightedSums[booth.voteType] +=
          booth.node.tppDeviation.value() * weight;
        seatTppWeightSums[booth.voteType] += weight;
      }

      if (!seatTppDeviationWeightedSums.contains(Results2::VoteType::Ordinary)) continue;
      // zero weight sum can occur if a seat's count is being realigned and therefore no TCP results
      if (!seatTppWeightSums.contains(Results2::VoteType::Ordinary) || seatTppWeightSums.at(Results2::VoteType::Ordinary) == 0) continue;
      float normalDeviation = seatTppDeviationWeightedSums.at(Results2::VoteType::Ordinary) / seatTppWeightSums.at(Results2::VoteType::Ordinary);
      for (auto const& [voteType, deviationWeightedSum] : seatTppDeviationWeightedSums) {
        if (voteType == Results2::VoteType::Ordinary) continue;
        if (seatTppWeightSums.at(voteType) == 0.0f) {
          // avoid division by zero
          tppBiasWeightedSums[voteType] += 0.0f;
          tppWeightSums[voteType] += 0.0f;
          continue;
        }
        float deviation = deviationWeightedSum / seatTppWeightSums.at(voteType);
        float voteTypeBias = deviation - normalDeviation;
        float weight = seatTppWeightSums.at(voteType);
        tppBiasWeightedSums[voteType] += voteTypeBias * weight;
        tppWeightSums[voteType] += weight;
        ++tppSourceCount[voteType];
      }
    }
    for (auto const& [voteType, bias] : tppBiasWeightedSums) {
      float votes = tppWeightSums.at(voteType);
      float overallTppBias = votes ? tppBiasWeightedSums.at(voteType) / votes : 0.0f;
      // placeholder formula, works for PPVCs/postals/absents but not smaller categories (but they don't usually have a significant impact)
      float obsProportion = std::pow(votes, 0.9f) / (20000.0f + std::pow(votes, 0.9f));
      // Declaration votes tend to move the tpp back toward the baseline
      float baseline = -0.6f * node.specificTppDeviation.value_or(0.0f);
      voteTypeTppBiases[voteType] = overallTppBias * obsProportion + baseline * (1.0f - obsProportion);
      float stdDev = 7.0f * std::exp(-std::pow(votes + 1000.0f, 0.25f) * 0.06f);
      voteTypeTppBiasStdDev[voteType] = stdDev;
      voteTypeTppBiasesRaw[voteType] = overallTppBias;
      voteTypeTppSourceCount[voteType] = tppSourceCount[voteType];
      voteTypeTppVoteCount[voteType] = tppWeightSums[voteType];
    }
  }
  PA_LOG_VAR(boothTypeTppBiases);
  PA_LOG_VAR(boothTypeTppBiasStdDev);
  PA_LOG_VAR(voteTypeTppBiases);
  PA_LOG_VAR(voteTypeTppBiasStdDev);
}

void Election::aggregate() {
  for (auto& seat : seats) {
    aggregateToSeat(seat);
  }
  for (auto& largeRegion : largeRegions) {
    aggregateToLargeRegion(largeRegion);
  }
  aggregateToElection();
}

// Template method for aggregation
template<typename T, typename U>
void Election::aggregateCollection(T& parent, const std::vector<int>& childIndices, 
                                  const std::vector<U>& childNodes) const {
  std::vector<Node const*> nodesToAggregate;
  for (auto const& childIndex : childIndices) {
    nodesToAggregate.push_back(&childNodes.at(childIndex).node);
  }
  parent.node = aggregateFromChildren(nodesToAggregate, &parent.node);
}

void Election::aggregateToSeat(Seat& seat) {
  aggregateCollection(seat, seat.booths, booths);
}

void Election::aggregateToLargeRegion(LargeRegion& largeRegion) {
  aggregateCollection(largeRegion, largeRegion.seats, seats);
}

void Election::aggregateToElection() {
  std::vector<int> indices(largeRegions.size());
  std::iota(indices.begin(), indices.end(), 0);
  aggregateCollection(*this, indices, largeRegions);
}

Node Election::aggregateFromChildren(
  const std::vector<Node const*>& nodesToAggregate,
  Node const* parentNode) const {
  // Aggregation takes lower-level deviations and calculates a weighted average
  // for the parent. Decomposition later separates that average into specific
  // election, region, seat and booth components.
  // Raw tallies are also summed for weighting and downstream reporting; raw
  // shares and swings remain local because they are not directly composable.

  Node aggregatedNode = parentNode ? *parentNode : Node();

  // Aggregate previous election vote totals (used for weighting only)
  aggregatedNode.fpVotesPrevious = std::map<int, int>();
  for (auto const& thisNode : nodesToAggregate) {
    for (auto const& [partyId, votes] : thisNode->fpVotesPrevious) {
      aggregatedNode.fpVotesPrevious[partyId] += votes;
    }
  }

  std::map<int, float> fpDeviationWeightedSum; // weighted by number of votes
  std::map<int, float> fpWeightSum; // sum of weights
  float fpConfidenceSum = 0.0f; // confidence weighted by relevant evidence
  float fpConfidenceWeightSum = 0.0f; // all expected votes, including nodes without evidence
  float fpCompletionSum = 0.0f;
  float fpCompletionWeightSum = 0.0f; // all expected votes

  for (auto const& thisNode : nodesToAggregate) {
    float const expectedVotes = expectedVotesForAggregation(*thisNode);
    float const evidenceWeight = expectedVotes * thisNode->relevanceModifier;
    float const weight = evidenceWeight * thisNode->fpConfidence;
    // TODO: FP confidence is shared by every party in a Node. A party-specific
    // confidence model would better represent booths where only some parties
    // have comparable historical or baseline evidence.
    bool confidenceAdded = false;
    for (auto const& [partyId, swing] : thisNode->fpDeviations) {
      fpDeviationWeightedSum[partyId] += swing * weight;
      fpWeightSum[partyId] += weight;
      // Only count confidence when the deviation actually contributes to the calculations
      if (!confidenceAdded) {
        fpConfidenceSum += thisNode->fpConfidence * evidenceWeight;
        confidenceAdded = true;
      }
    }
    // Completion uses all expected votes, while confidence discounts booths
    // whose historical comparison belongs to a different seat.
    fpConfidenceWeightSum += expectedVotes;
    fpCompletionSum += thisNode->fpCompletion * expectedVotes;
    fpCompletionWeightSum += expectedVotes;
  }

  // Node created for each new seat/region/election: should be <200 times
  // per election, so not major bottleneck
  for (auto const& [partyId, swing] : fpDeviationWeightedSum) {
    if (fpWeightSum[partyId] > 0) { // ignore parties with no votes
      aggregatedNode.fpDeviations[partyId] = swing / fpWeightSum[partyId];
    }
  }
  if (fpCompletionWeightSum > 0) {
    aggregatedNode.fpCompletion = fpCompletionSum / fpCompletionWeightSum;
  }
  if (fpConfidenceWeightSum > 0) {
    aggregatedNode.fpConfidence = fpConfidenceSum / fpConfidenceWeightSum;
  }

  // Aggregate current election vote totals (used for weighting only)
  aggregatedNode.fpVotesCurrent = std::map<int, int>();
  aggregatedNode.tcpVotesCurrent = std::map<int, int>();
  for (auto const& thisNode : nodesToAggregate) {
    for (auto const& [partyId, votes] : thisNode->fpVotesCurrent) {
      aggregatedNode.fpVotesCurrent[partyId] += votes;
    }
    for (auto const& [partyId, votes] : thisNode->tcpVotesCurrent) {
      aggregatedNode.tcpVotesCurrent[partyId] += votes;
    }
  }

  // Note: for now we don't aggregate tcp swings because they cannot be
  // consistently extrapolated beyond the seat level
  // (except when they are equivalent to tpp swings, which are already covered
  // by the tpp swing calculation below)
  // Will eventually add some code for the seat-level tcp swings, but not a priority
  // for WA election

  // Aggregate tpp swing
  float tppDeviationWeightedSum = 0.0f;
  float tppWeightSum = 0.0f;
  float tppConfidenceSum = 0.0f;
  float tppConfidenceWeightSum = 0.0f;
  float tppCompletionSum = 0.0f;
  float tppCompletionWeightSum = 0.0f; // all expected votes
  for (auto const& thisNode : nodesToAggregate) {
    float const expectedVotes = expectedVotesForAggregation(*thisNode);
    float const evidenceWeight = expectedVotes * thisNode->relevanceModifier;
    float const weight = evidenceWeight * thisNode->tppConfidence;
    if (thisNode->tppDeviation) {
      tppDeviationWeightedSum += thisNode->tppDeviation.value() * weight;
      tppWeightSum += weight;
      // Only count confidence when the deviation actually contributes to the calculations
      tppConfidenceSum += thisNode->tppConfidence * evidenceWeight;
    }
    tppConfidenceWeightSum += expectedVotes;
    // Always count completion
    tppCompletionSum += thisNode->tppCompletion * expectedVotes;
    tppCompletionWeightSum += expectedVotes;
  }
  if (tppWeightSum > 0) {
    aggregatedNode.tppDeviation = tppDeviationWeightedSum / tppWeightSum;
  }
  if (tppConfidenceWeightSum > 0) {
    // will eventually have a more sophisticated nonlinear confidence calculation
    aggregatedNode.tppConfidence = tppConfidenceSum / tppConfidenceWeightSum;
  }
  if (tppCompletionWeightSum > 0) {
    aggregatedNode.tppCompletion = tppCompletionSum / tppCompletionWeightSum;
  }

  // Aggregate preference flow deviation
  float preferenceFlowDeviationWeightedSum = 0.0f;
  float preferenceFlowWeightSum = 0.0f;
  float preferenceFlowConfidenceSum = 0.0f;
  float preferenceFlowConfidenceWeightSum = 0.0f;
  for (auto const& thisNode : nodesToAggregate) {
    float const expectedVotes = expectedVotesForAggregation(*thisNode);
    float const evidenceWeight = expectedVotes * thisNode->relevanceModifier;
    float const weight = evidenceWeight * thisNode->preferenceFlowConfidence;
    if (thisNode->preferenceFlowDeviation) {
      preferenceFlowDeviationWeightedSum += thisNode->preferenceFlowDeviation.value() * weight;
      preferenceFlowWeightSum += weight;
      // Only count confidence when the deviation actually contributes to the calculations
      preferenceFlowConfidenceSum +=
        thisNode->preferenceFlowConfidence * evidenceWeight;
    }
    preferenceFlowConfidenceWeightSum += expectedVotes;
  }
  if (preferenceFlowWeightSum > 0) {
    aggregatedNode.preferenceFlowDeviation = preferenceFlowDeviationWeightedSum / preferenceFlowWeightSum;
  }
  if (preferenceFlowConfidenceWeightSum > 0) {
    aggregatedNode.preferenceFlowConfidence = preferenceFlowConfidenceSum / preferenceFlowConfidenceWeightSum;
  }

  return aggregatedNode;
}

void Election::determineSpecificDeviations() {
  // Work from broadest to narrowest. Each child removes the already-shrunk
  // parent components before its residual is shrunk by the child's confidence.
  // This is intentional rather than an exact algebraic decomposition: a raw
  // seat or booth deviation is additional local evidence that the broad swing
  // applies there, so it can restore some movement rejected by cautious parent
  // shrinkage. A child without its own evidence contributes nothing and simply
  // inherits the accepted parent components.
  determineElectionSpecificDeviations();
  determineLargeRegionSpecificDeviations();
  determineSeatSpecificDeviations();
  determineBoothSpecificDeviations();
}

void Election::determineElectionSpecificDeviations() {
  // Since elections are aggregating a greater total volume of data, a lower total proportion is
  // needed to reach the same level of true confidence. So we use a higher strength than for
  // other areas. In v3 of the model, we should treat this more as a sampling issue (base the
  // weight on sample size adjusted for representativeness, not % of total) but this will do for now
  float fpObsWeight = obsWeight(node.fpConfidence, VoteObsWeightStrength * 2.5f);
  for (auto const& [partyId, deviation] : node.fpDeviations) {
    node.specificFpDeviations[partyId] = deviation * fpObsWeight;
  }
  float tppObsWeight = obsWeight(node.tppConfidence, VoteObsWeightStrength * 2.5f);
  node.specificTppDeviation = node.tppDeviation.value_or(0.0f) * tppObsWeight;
}

void Election::determineLargeRegionSpecificDeviations() {
  for (auto& largeRegion : largeRegions) {
    float fpObsWeight = obsWeight(largeRegion.node.fpConfidence);
    for (auto const& [partyId, deviation] : largeRegion.node.fpDeviations) {
      float parentFpDeviation = node.specificFpDeviations.contains(partyId) ? node.specificFpDeviations.at(partyId) : 0.0f;
      float excludingParents = deviation - parentFpDeviation;
      largeRegion.node.specificFpDeviations[partyId] = excludingParents * fpObsWeight;
    }
    float tppObsWeight = obsWeight(largeRegion.node.tppConfidence);
    float parentTppDeviation = node.specificTppDeviation.value_or(0.0f);
    float excludingParents = largeRegion.node.tppDeviation ? largeRegion.node.tppDeviation.value() - parentTppDeviation : 0.0f;
    largeRegion.node.specificTppDeviation = excludingParents * tppObsWeight;
  }
}

void Election::determineSeatSpecificDeviations() {
  for (auto& largeRegion : largeRegions) {
    for (int seatIndex : largeRegion.seats) {
      auto& seat = seats.at(seatIndex);
      float fpObsWeight = obsWeight(seat.node.fpConfidence);
      for (auto const& [partyId, deviation] : seat.node.fpDeviations) {
        float parentsFpDeviation = largeRegion.node.specificFpDeviations.contains(partyId) ? largeRegion.node.specificFpDeviations.at(partyId) : 0.0f;
        parentsFpDeviation += node.specificFpDeviations.contains(partyId) ? node.specificFpDeviations.at(partyId) : 0.0f;
        float excludingParents = deviation - parentsFpDeviation;
        seat.node.specificFpDeviations[partyId] = excludingParents * fpObsWeight;
      }
      float tppObsWeight = obsWeight(seat.node.tppConfidence);
      float parentsTppDeviation = largeRegion.node.specificTppDeviation.value_or(0.0f);
      parentsTppDeviation += node.specificTppDeviation.value_or(0.0f);
      float excludingParents = seat.node.tppDeviation ? seat.node.tppDeviation.value() - parentsTppDeviation : 0.0f;
      seat.node.specificTppDeviation = excludingParents * tppObsWeight;
    }
  }
}

void Election::determineBoothSpecificDeviations() {
  for (auto& seat : seats) {
    auto const& largeRegion = largeRegions.at(seat.parentRegionIndex);
    for (int boothIndex : seat.booths) {
      auto& booth = booths.at(boothIndex);
      float fpObsWeight = obsWeight(booth.node.fpConfidence);
      for (auto const& [partyId, deviation] : booth.node.fpDeviations) {
        float parentsFpDeviation = seat.node.specificFpDeviations.contains(partyId) ? seat.node.specificFpDeviations.at(partyId) : 0.0f;
        parentsFpDeviation += largeRegion.node.specificFpDeviations.contains(partyId) ? largeRegion.node.specificFpDeviations.at(partyId) : 0.0f;
        parentsFpDeviation += node.specificFpDeviations.contains(partyId) ? node.specificFpDeviations.at(partyId) : 0.0f;
        float excludingParents = deviation - parentsFpDeviation;
        booth.node.specificFpDeviations[partyId] = excludingParents * fpObsWeight;
      }
      float tppObsWeight = obsWeight(booth.node.tppConfidence);
      float parentsTppDeviation = seat.node.specificTppDeviation.value_or(0.0f);
      parentsTppDeviation += largeRegion.node.specificTppDeviation.value_or(0.0f);
      parentsTppDeviation += node.specificTppDeviation.value_or(0.0f);
      float excludingParents = booth.node.tppDeviation ? booth.node.tppDeviation.value() - parentsTppDeviation : 0.0f;
      booth.node.specificTppDeviation = excludingParents * tppObsWeight;
    }
  }
}

void Election::calculateNationalsProportions() {
  for (auto& seat : seats) {
    float totalFp = static_cast<float>(seat.node.totalFpVotesCurrent());
    if (totalFp == 0.0f) {
      seat.nationalsProportion = std::nullopt;
      continue;
    }
    float nationalsVotes = static_cast<float>(seat.node.fpVotesCurrent.contains(natPartyIndex) ? seat.node.fpVotesCurrent.at(natPartyIndex) : 0);
    float partyOneVotes = static_cast<float>(seat.node.fpVotesCurrent.contains(1) ? seat.node.fpVotesCurrent.at(1) : 0);
    if (nationalsVotes + partyOneVotes == 0) {
      seat.nationalsProportion = std::nullopt;
      continue;
    }
    if (!nationalsVotes) {
      seat.nationalsProportion = 0;
      continue;
    }
    if (!partyOneVotes) {
      seat.nationalsProportion = 1;
      continue;
    }
    float nationalsShare = static_cast<float>(nationalsVotes) / totalFp;
    float partyOneShare = static_cast<float>(partyOneVotes) / totalFp;
    seat.nationalsProportion = nationalsShare / (nationalsShare + partyOneShare);
  }
}

void Election::calculateTcpPreferenceFlows() {
  for (auto& seat : seats) {
    if (seat.node.tcpVotesCurrent.size() != 2) continue;
    if (isTppSet(seat.node.tcpVotesCurrent, natPartyIndex)) continue;
    int partyOneIndex = seat.node.tcpVotesCurrent.begin()->first;
    int partyTwoIndex = std::next(seat.node.tcpVotesCurrent.begin())->first;
    float totalPrefs = 0.0f;
    float totalPartyOnePrefs = 0.0f;
    float focusPartyConfidence = 0.0f;
    for (auto& boothIndex : seat.booths) {
      auto& booth = booths.at(boothIndex);
      float totalFpVotes = float(booth.node.totalFpVotesCurrent());
      float totalTcpVotes = float(booth.node.totalTcpVotesCurrent());
      if (!booth.node.fpVotesCurrent.contains(partyOneIndex) || !booth.node.fpVotesCurrent.contains(partyTwoIndex)) continue;
      if (!booth.node.tcpVotesCurrent.contains(partyOneIndex) || !booth.node.tcpVotesCurrent.contains(partyTwoIndex)) continue;
      if (totalTcpVotes == 0 || totalFpVotes == 0) continue;

      if (run.regionCode == "sa") {
        // SA TCP votes are commonly different to FP votes due to savings provisions, so handle them differently
        // Small total differences are expected from savings provisions and
        // asynchronous publication. Larger differences are too ambiguous to
        // treat as measured preference-flow evidence.
        float const relativeTotalDifference =
          std::abs(totalTcpVotes - totalFpVotes) / totalFpVotes;
        if (relativeTotalDifference > MaxPreferenceVoteTotalDifference) continue;
        float fpTcpRatio = totalFpVotes / totalTcpVotes;
        float scaledTcpPartyOne = float(booth.node.tcpVotesCurrent.at(partyOneIndex)) * fpTcpRatio;
        float boothPrefs = totalFpVotes - booth.node.fpVotesCurrent.at(partyOneIndex) - booth.node.fpVotesCurrent.at(partyTwoIndex);
        if (!std::isfinite(boothPrefs) || boothPrefs <= 2.0f) continue;
        float boothPartyOnePrefs = std::clamp(scaledTcpPartyOne - float(booth.node.fpVotesCurrent.at(partyOneIndex)), 1.0f, boothPrefs - 1.0f);
        totalPartyOnePrefs += boothPartyOnePrefs;
        totalPrefs += boothPrefs;
        focusPartyConfidence += totalTcpVotes;
      }
      else if (totalFpVotes == totalTcpVotes) {
        float boothPrefs = totalFpVotes - float(booth.node.fpVotesCurrent.at(partyOneIndex)) - float(booth.node.fpVotesCurrent.at(partyTwoIndex));
        float boothPartyOnePrefs = float(booth.node.tcpVotesCurrent.at(partyOneIndex)) - float(booth.node.fpVotesCurrent.at(partyOneIndex));
        if (!std::isfinite(boothPrefs) || boothPrefs <= 2.0f) continue;
        boothPartyOnePrefs = std::clamp(
          boothPartyOnePrefs, 1.0f, boothPrefs - 1.0f);
        totalPartyOnePrefs += boothPartyOnePrefs;
        totalPrefs += boothPrefs;
        focusPartyConfidence += totalTcpVotes;
      }
    }
    float const confidenceDenominator = static_cast<float>(
      std::max(seat.node.totalVotesPrevious(), seat.node.totalFpVotesCurrent()));
    if (totalPrefs <= 0.0f || confidenceDenominator <= 0.0f) continue;
    seat.tcpFocusPartyIndex = partyOneIndex;
    seat.tcpFocusPartyPrefFlow = totalPartyOnePrefs / totalPrefs * 100.0f;
    seat.tcpFocusPartyConfidence = std::clamp(
      focusPartyConfidence / confidenceDenominator, 0.0f, 1.0f);
  }
}

void Election::recomposeVoteCounts() {
  // To avoid Simpson's paradox related issues, we need to recompose the vote counts
  // from the swing data. Possible future additions:
  // (This is probably the minimum level for release)
  // 6. If the estimated swing would result in a seat having too many votes compared to its enrolment, reduce the votes for incremental/uncounted booths
  // 7. Use externally known attendance data to adjust the expected size for the relevant booths.
  // 8. Extrapolate between booths to estimate the changes in vote counts to refine and complement the above
  //    (for example, if we see a drop in ordinary votes in reporting polling places, assume this will extrapolate to the unreported booths
  //    and perhaps also be compensated for by higher turnout in other booths, might need to look at both turnout and formality)

  if (!createRandomVariation) {
    refreshFpProgressForDeclarationEstimates();

    for (int boothIndex : std::ranges::views::iota(0, int(booths.size()))) {
      recomposeBoothFpVotes(false, boothIndex);
      recomposeBoothTppVotes(false, boothIndex);
    }

    for (int seatIndex : std::ranges::views::iota(0, int(seats.size()))) {
      recomposeSeatFpVotes(seatIndex);
      recomposeSeatTppVotes(seatIndex);
    }
    for (int largeRegionIndex : std::ranges::views::iota(0, int(largeRegions.size()))) {
      recomposeLargeRegionFpVotes(largeRegionIndex);
      recomposeLargeRegionTppVotes(largeRegionIndex);
    }
    recomposeElectionFpVotes();
    recomposeElectionTppVotes();
    determineElectionFinalFpDeviations(false);
    determineElectionFinalTppDeviation(false);
    for (int largeRegionIndex : std::ranges::views::iota(0, int(largeRegions.size()))) {
      determineLargeRegionFinalFpDeviations(false, largeRegionIndex);
      determineLargeRegionFinalTppDeviation(false, largeRegionIndex);
    }
    for (int seatIndex : std::ranges::views::iota(0, int(seats.size()))) {
      determineSeatFinalFpDeviations(false, seatIndex);
      determineSeatFinalTppDeviation(false, seatIndex);
    }
  }

  for (int boothIndex : std::ranges::views::iota(0, int(booths.size()))) {
    recomposeBoothFpVotes(true, boothIndex);
    recomposeBoothTppVotes(true, boothIndex);
    recomposeBoothTcpVotes(boothIndex);
  }

  // Need this to use actual election data, but don't want to recalculate it every
  // time we generate random variation
  if (!createRandomVariation) {
    calculateTppEstimateBias();
  }

  for (int seatIndex : std::ranges::views::iota(0, int(seats.size()))) {
    recomposeSeatFpVotes(seatIndex);
    recomposeSeatTppVotes(seatIndex);
    recomposeSeatTcpVotes(seatIndex);
  }
  for (int largeRegionIndex : std::ranges::views::iota(0, int(largeRegions.size()))) {
    recomposeLargeRegionFpVotes(largeRegionIndex);
    recomposeLargeRegionTppVotes(largeRegionIndex);
  }
  recomposeElectionFpVotes();
  recomposeElectionTppVotes();

  determineElectionFinalFpDeviations(true);
  determineElectionFinalTppDeviation(true);
  for (int largeRegionIndex : std::ranges::views::iota(0, int(largeRegions.size()))) {
    determineLargeRegionFinalFpDeviations(true, largeRegionIndex);
    determineLargeRegionFinalTppDeviation(true, largeRegionIndex);
  }

  for (int seatIndex : std::ranges::views::iota(0, int(seats.size()))) {
    determineSeatFinalFpDeviations(true, seatIndex);
    determineSeatFinalTppDeviation(true, seatIndex);
  }
}

// Temporary declaration-size model used to prevent partial counts becoming
// prematurely certain. These constants should eventually be calibrated from
// historical declaration-count progression, enrolment and turnout data rather
// than maintained as election-specific estimates.
int Election::generateDeclarationVoteExpectedSize(int boothIndex) {
  constexpr int MinimumDeclarationVoteExpectationBase = 30;
  auto const& booth = booths.at(boothIndex);
  int expectationBase = std::max(booth.node.totalVotesPrevious(), MinimumDeclarationVoteExpectationBase);
  // Rough allowance for population growth since the previous election.
  float baseExpectation = expectationBase * 1.05f;
  if (run.getTermCode() == "2026sa" && booth.voteType != Results2::VoteType::Postal) {
    baseExpectation *= 0.661f; // expected decline in non-postal declaration votes
  }
  if (run.getTermCode() == "2026sa") {
    // Temporary fix for 2026sa declaration votes being way off expectations, replace with a more robust system when there's more time
    if (booth.voteType == Results2::VoteType::Absent) baseExpectation = 1200.0f;
    if (booth.voteType == Results2::VoteType::PrePoll) baseExpectation = 1300.0f;
    // These are are early declaration votes. Clearly too many to be provisionals
    if (booth.voteType == Results2::VoteType::EarlyProvisional) baseExpectation = 50.0f;
    if (booth.voteType == Results2::VoteType::Provisional) baseExpectation = 50.0f;
    if (booth.voteType == Results2::VoteType::Postal) baseExpectation = 3000.0f;
    if (booth.voteType == Results2::VoteType::EVM) baseExpectation = 200.0f;
    if (booth.voteType == Results2::VoteType::TIO) baseExpectation = 30.0f;
  }
  if (createRandomVariation) {
    baseExpectation *= std::max(
      0.1f,
      variabilityNormal(
        1.0f, 0.12f, boothIndex, 0,
        uint32_t(VariabilityTag::DeclarationVoteSizeVariability)));
  }

  return static_cast<int>(baseExpectation);
}

void Election::refreshFpProgressForDeclarationEstimates() {
  // Declaration counts do not have a reliable completion marker in the feed.
  // Replace the previous-election denominator with the current expected final
  // size, then propagate only FP progress. Calling aggregate() here would also
  // recompute deviations and other already-derived hierarchy state.
  std::vector<float> boothExpectedVotes(booths.size(), 0.0f);
  for (int boothIndex : std::ranges::views::iota(0, int(booths.size()))) {
    auto& booth = booths[boothIndex];
    float expectedVotes = expectedVotesForAggregation(booth.node);
    if (booth.voteType != Results2::VoteType::Ordinary) {
      expectedVotes = std::max(
        float(generateDeclarationVoteExpectedSize(boothIndex)),
        float(booth.node.totalFpVotesCurrent()));
      if (booth.node.totalFpVotesCurrent() > 0) {
        float const progress = std::clamp(
          float(booth.node.totalFpVotesCurrent()) / expectedVotes,
          0.0f, 1.0f);
        booth.node.fpCompletion = progress;
        booth.node.fpConfidence = progress;
      }
    }
    if (expectedVotes <= 0.0f) {
      expectedVotes = booth.boothType == Results2::Booth::Type::Hospital
        ? HospitalBoothVotesGuess
        : PreviousTotalVotesGuess;
    }
    boothExpectedVotes[boothIndex] = expectedVotes;
  }

  auto refreshParentProgress = [](
    Node& parent,
    auto const& childIndices,
    auto const& children,
    std::vector<float> const& expectedVotes) {
    float completionSum = 0.0f;
    float confidenceSum = 0.0f;
    float totalExpectedVotes = 0.0f;
    for (int childIndex : childIndices) {
      auto const& child = children.at(childIndex).node;
      float const childExpectedVotes = expectedVotes.at(childIndex);
      completionSum += child.fpCompletion * childExpectedVotes;
      if (!child.fpDeviations.empty()) {
        confidenceSum += child.fpConfidence * childExpectedVotes
          * child.relevanceModifier;
      }
      totalExpectedVotes += childExpectedVotes;
    }
    parent.fpCompletion = totalExpectedVotes > 0.0f
      ? completionSum / totalExpectedVotes
      : 0.0f;
    parent.fpConfidence = totalExpectedVotes > 0.0f
      ? confidenceSum / totalExpectedVotes
      : 0.0f;
    return totalExpectedVotes;
  };

  std::vector<float> seatExpectedVotes(seats.size(), 0.0f);
  for (int seatIndex : std::ranges::views::iota(0, int(seats.size()))) {
    seatExpectedVotes[seatIndex] = refreshParentProgress(
      seats[seatIndex].node, seats[seatIndex].booths, booths,
      boothExpectedVotes);
  }

  std::vector<float> regionExpectedVotes(largeRegions.size(), 0.0f);
  for (int regionIndex : std::ranges::views::iota(0, int(largeRegions.size()))) {
    regionExpectedVotes[regionIndex] = refreshParentProgress(
      largeRegions[regionIndex].node, largeRegions[regionIndex].seats, seats,
      seatExpectedVotes);
  }

  std::vector<int> regionIndices(largeRegions.size());
  std::iota(regionIndices.begin(), regionIndices.end(), 0);
  refreshParentProgress(
    node, regionIndices, largeRegions, regionExpectedVotes);
}

void Election::recomposeBoothFpVotes(bool allowCurrentData, int boothIndex) {
  auto assignBlindOthers = [this](
    std::map<int, float>& votesProjected,
    std::set<int> const& blindOthers,
    int projectSeatIndex,
    float currentVotesEstimate,
    float othersAccountedFor) {
    float remainingExpectedVotes = 0.01f * blindOthers.size() * currentVotesEstimate;
    if (
      sim.getLiveBaselineReport().has_value()
      && sim.getLiveBaselineReport().value().seatFpProbabilityBand[projectSeatIndex].contains(OthersIndex)
    ) {
      auto const& probabilityBands = sim.getLiveBaselineReport().value()
        .seatFpProbabilityBand[projectSeatIndex].at(OthersIndex);
      if (probabilityBands.empty()) {
        throw std::runtime_error(
          "Baseline Others distribution is empty for seat "
          + project.seats().viewByIndex(projectSeatIndex).name + ".");
      }
      float median = probabilityBands[(probabilityBands.size() - 1) / 2];
      remainingExpectedVotes = std::max(remainingExpectedVotes, median * currentVotesEstimate * 0.01f - othersAccountedFor);
    }
    for (auto const& partyId : blindOthers) {
      votesProjected[partyId] = remainingExpectedVotes / float(blindOthers.size());
    }
  };

  auto& booth = booths.at(boothIndex);
  auto const& seat = seats.at(booth.parentSeatId);
  int projectSeatIndex = project.seats().indexByName(seats[booth.parentSeatId].name);
  if (allowCurrentData && booth.node.totalFpVotesCurrent()) {
    // map from party index to vote count (as a float)
    auto fpVotesProjected = std::map<int, float>();
    for (auto const& [partyId, votes] : booth.node.fpVotesCurrent) { 
      int effectivePartyId = partyId == seat.liveIndependentPartyIndex ? run.indPartyIndex : partyId;
      fpVotesProjected[effectivePartyId] = static_cast<float>(votes);
    }
    if (booth.voteType != Results2::VoteType::Ordinary) {
      float currentTotalVotesProjected = std::accumulate(fpVotesProjected.begin(), fpVotesProjected.end(), 0.0f,
        [](float sum, const auto& pair) { return sum + pair.second; });
      float expectedTotalVotes = std::max(
        float(generateDeclarationVoteExpectedSize(boothIndex)),
        currentTotalVotesProjected);
      float totalAdditionalVotes = std::max(0.0f, expectedTotalVotes - currentTotalVotesProjected);
      std::map<int, float> additionalVotes;
      float totalAddedVotes = 0.0f;
      for (auto const& [partyId, votes] : fpVotesProjected) {
        float stdDev = 7.0f + 12.0f * std::exp(-static_cast<float>(booth.node.totalVotesPrevious()) * 0.0001f);
        float random = variabilityNormal(0.0f, stdDev, boothIndex, partyId, uint32_t(VariabilityTag::NonOrdinaryFp));
        float projectionDifference = createRandomVariation ? random : 0.0f;
        if (booth.voteType == Results2::VoteType::Postal) {
          const float adjustment = 2.0f; // late-arriving postals are generally less friendly to Coalition than early postals
          if (partyId == 1 || partyId == natPartyIndex) projectionDifference -= adjustment;
        }
        float transformedOriginal = transformVoteShare(float(fpVotesProjected.at(partyId)) / currentTotalVotesProjected * 100.0f);
        float transformedProjection = transformedOriginal + projectionDifference;
        float additionalPartyVotes = detransformVoteShare(transformedProjection) * totalAdditionalVotes * 0.01f;
        additionalVotes[partyId] = additionalPartyVotes;
        totalAddedVotes += additionalPartyVotes;
      }
      // Independent transformed-share adjustments do not necessarily allocate
      // exactly 100% of the remaining votes. Normalise only those additions so
      // already-counted votes are never reduced or otherwise rewritten.
      if (totalAdditionalVotes > 0.0f) {
        if (!std::isfinite(totalAddedVotes) || totalAddedVotes <= 0.0f) {
          throw std::runtime_error(
            "Could not allocate remaining declaration FP votes for "
            + booth.name + ".");
        }
        float const additionalVoteAdjustment =
          totalAdditionalVotes / totalAddedVotes;
        for (auto& [partyId, votes] : fpVotesProjected) {
          votes += additionalVotes.at(partyId) * additionalVoteAdjustment;
        }
      }

      booth.node.fpCompletion = std::clamp(currentTotalVotesProjected / expectedTotalVotes, 0.0f, 1.0f);
      booth.node.fpConfidence = std::clamp(currentTotalVotesProjected / expectedTotalVotes, 0.0f, 1.0f);
    }
    if (createRandomVariation) {
      booth.node.tempFpVotesProjected = fpVotesProjected;
    } else {
      booth.node.fpVotesProjected = fpVotesProjected;
    }
  }
  else {
    const float votesGuess = booth.boothType == Results2::Booth::Type::Hospital ? HospitalBoothVotesGuess : PreviousTotalVotesGuess;
    float previousTotalVotes = booth.node.totalVotesPrevious() ? booth.node.totalVotesPrevious() : votesGuess;
    const float fpTotalCurrent = allowCurrentData
      ? float(booth.node.totalFpVotesCurrent())
      : 0.0f;
    float currentVotesEstimate = previousTotalVotes;
    if (booth.voteType != Results2::VoteType::Ordinary) {
      currentVotesEstimate = float(generateDeclarationVoteExpectedSize(boothIndex));
    }
    else if (run.getTermCode() == "2026sa") {
      if (booth.boothType == Results2::Booth::Type::Ppvc) {
        currentVotesEstimate *= 2.19f; // account for large increase in PPVC votes in SA
      }
      else {
        // Postals are known to be flat, so the general decline is shares across other booths
        //currentVotesEstimate *= 0.661f;
      }
    }
    float currentVoteTarget = fpTotalCurrent ? fpTotalCurrent : currentVotesEstimate;
    std::map<int, float> tempFpVotesProjected;
    float othersAccountedFor = 0.0f;
    std::set<int> blindOthers;
    for (auto const& partyId : booth.node.runningParties) {
      float deviation = 0.0f;
      int indIndex = allowCurrentData ? seat.liveIndependentPartyIndex : seat.independentPartyIndex;
      int effectivePartyId = partyId == indIndex ? run.indPartyIndex : partyId;
      if (allowCurrentData && booth.node.totalVotesPrevious()) {
        auto const& thisAndParents = getThisAndParents(booth);
        for (auto const& parent : thisAndParents) {
          if (parent->specificFpDeviations.contains(effectivePartyId)) {
            deviation += parent->specificFpDeviations.at(effectivePartyId);
          }
        }
      }
      float randomFactor = 0.0f;
      if (createRandomVariation) {
        // placeholder formula, a little on the conservative side but will do for a prototype
        // until I get around to properly calibrating the variance
        float stdDev = 6.0f + 10.0f * std::exp(-static_cast<float>(booth.node.totalVotesPrevious()) * 0.0001f);
        randomFactor = variabilityNormal(0.0f, stdDev, boothIndex, effectivePartyId, uint32_t(VariabilityTag::BoothProjectionFp));
      }
      // Apply biases for this booth classification.
      if (allowCurrentData && boothTypeFpBiases.contains(effectivePartyId) && boothTypeFpBiases.at(effectivePartyId).contains(booth.boothType)) {
        deviation += boothTypeFpBiases.at(effectivePartyId).at(booth.boothType);
      }
      // Apply biases for declaration/ordinary vote type.
      if (allowCurrentData && voteTypeFpBiases.contains(effectivePartyId) && voteTypeFpBiases.at(effectivePartyId).contains(booth.voteType)) {
        deviation += voteTypeFpBiases.at(effectivePartyId).at(booth.voteType);
      }
      std::optional<float> baselineShareUntransformed;
      if (sim.getLiveBaselineReport().has_value()) {
        if (sim.getLiveBaselineReport().value().seatFpProbabilityBand[projectSeatIndex].contains(effectivePartyId)) {  
          auto const& fpProbabilityBands = sim.getLiveBaselineReport().value().seatFpProbabilityBand[projectSeatIndex].at(effectivePartyId);
          if (fpProbabilityBands.empty()) {
            throw std::runtime_error(
              "Baseline FP distribution is empty for party "
              + std::to_string(effectivePartyId) + " in seat "
              + project.seats().viewByIndex(projectSeatIndex).name + ".");
          }
          baselineShareUntransformed = fpProbabilityBands[(fpProbabilityBands.size() - 1) / 2];
        }
      }
      if (booth.node.fpVotesPrevious.contains(effectivePartyId) && booth.node.totalVotesPrevious()) {
        // We have previous election data and some sort of a deviation estimate (even if it's just zero)
        float prevVotes = booth.node.fpVotesPrevious.at(effectivePartyId);
        float prevShare = transformVoteShare(std::clamp(float(prevVotes) / float(previousTotalVotes) * 100.0f, 0.5f, 99.5f));
        float baselineSwing = seat.node.fpSwingsBaseline.contains(effectivePartyId) ? seat.node.fpSwingsBaseline.at(effectivePartyId) : 0.0f;
        float newShare = prevShare + baselineSwing + deviation + randomFactor;
        float newVotes = detransformVoteShare(newShare) * currentVoteTarget * 0.01f;
        tempFpVotesProjected[effectivePartyId] = newVotes;
        if (!baselineShareUntransformed.has_value()) {
          othersAccountedFor += newVotes;
        }
      } else if (
        allowCurrentData &&
        seats[booth.parentSeatId].node.totalFpVotesCurrent() &&
        seats[booth.parentSeatId].node.fpVotesCurrent.contains(partyId)
       ) {
        // We don't have previous data, but we have seat share data from some other booths
        // If there is a baseline, use it and modify from there; otherwise, use a low initial expectation
        // Either way, use the seat share data to modify the initial expectation
        float baselineShare = transformVoteShare(baselineShareUntransformed.value_or(1.0f));
        float weight = obsWeight(seats[booth.parentSeatId].node.fpConfidence, VoteObsWeightStrength);
        float currentPartyVotes = float(seats[booth.parentSeatId].node.fpVotesCurrent.at(partyId));
        float currentTotalVotes = float(seats[booth.parentSeatId].node.totalFpVotesCurrent());
        float seatShare = transformVoteShare(std::clamp(currentPartyVotes / currentTotalVotes * 100.0f, 0.1f, 99.9f));
        float newShare = seatShare * weight + baselineShare * (1.0f - weight);
        float newVotes = detransformVoteShare(newShare + randomFactor) * currentVoteTarget * 0.01f;
        tempFpVotesProjected[effectivePartyId] = newVotes;
        if (!baselineShareUntransformed.has_value()) {
          othersAccountedFor += newVotes;
        }
      } else if (baselineShareUntransformed.has_value()) {
        // No previous data and no current data, but we have a baseline
        // (Typically occurs for independents, early on the night when no useful data at all has been recorded for this party yet)
        float transformedBaselineShare = transformVoteShare(std::clamp(baselineShareUntransformed.value(), 0.1f, 99.9f));
        if (booth.voteType == Results2::VoteType::Postal) {
          deviation -= 5.0f; // Rough approximation to account for first-time third party candidates generally doing worse on postal votes
          // TODO: Applied like this will bias the results to the negative, some sort of compensation should be applied to other votes
        }
        float adjustedShare = transformedBaselineShare + deviation + randomFactor;
        float detransformedShare = detransformVoteShare(adjustedShare);
        float newVotes = detransformedShare * currentVoteTarget * 0.01f;
        tempFpVotesProjected[effectivePartyId] = newVotes;
      } else {
        // No previous data, no current data, no baseline
        // Blind others are parties that we have no information about
        // other than that they are part of the "Others" category
        // record this for later so that the "Others" category can be shared
        // between them.
        // Typically only the case early on the night when no data is recorded for the seat
        // and no pre-election expectations exist yet
        blindOthers.insert(effectivePartyId);
      }
    }

    assignBlindOthers(tempFpVotesProjected, blindOthers, projectSeatIndex, currentVoteTarget, othersAccountedFor);

    // normalize so that the sum of the votes is the same as the previous election (or other estimate)
    float totalVotes = 0.0f;
    for (auto const& [partyId, votes] : tempFpVotesProjected) {
      if (!std::isfinite(votes) || votes < 0.0f) {
        throw std::runtime_error(
          "Invalid FP projection for booth " + booth.name + ".");
      }
      totalVotes += votes;
    }
    if (!std::isfinite(totalVotes) || totalVotes <= 0.0f) {
      throw std::runtime_error(
        "No positive FP projection was produced for booth " + booth.name + ".");
    }
    for (auto& [partyId, votes] : tempFpVotesProjected) {
      votes *= currentVoteTarget / totalVotes;
    }
    if (createRandomVariation) {
      booth.node.tempFpVotesProjected = tempFpVotesProjected;
    }
    else {
      booth.node.fpVotesProjected = tempFpVotesProjected;
      for (auto const& [partyId, _] : booth.node.fpVotesProjected) {
        if (allowCurrentData && booth.boothType != Results2::Booth::Type::Normal && booth.boothType != Results2::Booth::Type::Invalid && booth.boothType != Results2::Booth::Type::Other) {
          seats[booth.parentSeatId].fpBoothTypeSensitivity[partyId][booth.boothType] += currentVoteTarget;
        }
        else if (allowCurrentData && booth.voteType != Results2::VoteType::Ordinary && booth.voteType != Results2::VoteType::Invalid) {
          seats[booth.parentSeatId].fpVoteTypeSensitivity[partyId][booth.voteType] += currentVoteTarget;
        }
      }
    }
  }
}

void Election::recomposeBoothTcpVotes(int boothIndex) {
  auto& booth = booths.at(boothIndex);
  auto const& seat = seats.at(booth.parentSeatId);
  // Not the expected TCP vote structure
  if (seat.node.tcpVotesCurrent.size() != 2) return;
  // This is for non-classic TCPs, if we have a classic TPP for the seat then we don't need to project anything
  if (isTppSet(seat.node.tcpVotesCurrent, natPartyIndex)) return;

  auto convertPartyId = [this, &seat](int partyId) {
    return partyId == seat.liveIndependentPartyIndex ? run.indPartyIndex : partyId;
  };

  auto it = seat.node.tcpVotesCurrent.begin();
  int firstPartyId = it->first;
  int secondPartyId = std::next(it)->first;
  if (isMajorParty(firstPartyId, natPartyIndex)) {
    std::swap(firstPartyId, secondPartyId);
  }
  bool fullComparisonAvailable = booth.node.totalTcpVotesPrevious() > 0
    && booth.node.tcpVotesPrevious.contains(firstPartyId)
    && booth.node.tcpVotesPrevious.contains(secondPartyId);
  int sharedPreviousPartyId = InvalidPartyIndex;
  if (!fullComparisonAvailable
    && booth.node.tcpVotesPrevious.size() == 2
    && booth.node.totalTcpVotesPrevious() > 0) {
    bool const firstPartyShared =
      booth.node.tcpVotesPrevious.contains(firstPartyId);
    bool const secondPartyShared =
      booth.node.tcpVotesPrevious.contains(secondPartyId);
    if (firstPartyShared != secondPartyShared) {
      sharedPreviousPartyId = firstPartyShared ? firstPartyId : secondPartyId;
    }
  }
  bool const partialComparisonAvailable =
    sharedPreviousPartyId != InvalidPartyIndex;

  bool cantProject = false;
  if (booth.node.totalTcpVotesCurrent()) {
    // map from party index to vote count (as a float)
    auto tcpVotesProjected = std::map<int, float>();
    for (auto const& [partyId, votes] : booth.node.tcpVotesCurrent) { 
      int effectivePartyId = convertPartyId(partyId);
      tcpVotesProjected[effectivePartyId] += static_cast<float>(votes);
    }
    int const effectiveFirstPartyId = convertPartyId(firstPartyId);
    int const effectiveSecondPartyId = convertPartyId(secondPartyId);
    if (effectiveFirstPartyId == effectiveSecondPartyId
      || !tcpVotesProjected.contains(effectiveFirstPartyId)
      || !tcpVotesProjected.contains(effectiveSecondPartyId)) {
      throw std::runtime_error(
        "Current TCP does not contain two distinct mapped candidates for booth "
        + booth.name + ".");
    }
    if (booth.voteType != Results2::VoteType::Ordinary) {
      float currentTotalVotesProjected = std::accumulate(tcpVotesProjected.begin(), tcpVotesProjected.end(), 0.0f,
        [](float sum, const auto& pair) { return sum + pair.second; });
      float expectedTotalVotes = std::max(
        float(generateDeclarationVoteExpectedSize(boothIndex)),
        currentTotalVotesProjected);
      // This assumes the final TCP total is close to the expected FP total.
      // Savings provisions can make it slightly lower; retain this simple
      // approximation unless a concrete election shows a material effect.
      float totalAdditionalVotes = std::max(0.0f, expectedTotalVotes - currentTotalVotesProjected);
      float stdDev = 7.0f + 12.0f * std::exp(-static_cast<float>(currentTotalVotesProjected) * 0.0001f);

      float random = variabilityNormal(0.0f, stdDev, boothIndex, 0 /* no party id required */, uint32_t(VariabilityTag::NonOrdinaryTcp));
      float projectionDifference = createRandomVariation ? random : 0.0f;
      if (firstPartyId == seat.liveIndependentPartyIndex) firstPartyId = run.indPartyIndex;
      if (secondPartyId == seat.liveIndependentPartyIndex) secondPartyId = run.indPartyIndex;
      if (booth.voteType == Results2::VoteType::Postal) {
        float adjustment = 2.0f; // late-arriving postals are generally less friendly to Coalition than early postals
        if (firstPartyId == natPartyIndex || firstPartyId == 1) {
          // But only if other party is less right than the Coalition themselves
          if (secondPartyId > 0 && project.parties().viewByIndex(secondPartyId).ideology < 3) {
            projectionDifference -= adjustment;
          }
        } else if (secondPartyId == natPartyIndex || secondPartyId == 1) {
          if (firstPartyId > 0 && project.parties().viewByIndex(firstPartyId).ideology < 3) {
            projectionDifference += adjustment;
          }
        }
      }
      float transformedOriginal = transformVoteShare(std::clamp(
        float(tcpVotesProjected.at(firstPartyId)) /
          currentTotalVotesProjected * 100.0f,
        0.1f, 99.9f));
      float transformedProjection = transformedOriginal + projectionDifference;
      float additionalPartyOneVotes = detransformVoteShare(transformedProjection) * totalAdditionalVotes * 0.01f;
      tcpVotesProjected[firstPartyId] += additionalPartyOneVotes;
      tcpVotesProjected[secondPartyId] += totalAdditionalVotes - additionalPartyOneVotes;
      booth.node.tcpCompletion = std::clamp(currentTotalVotesProjected / expectedTotalVotes, 0.0f, 1.0f);
      booth.node.tcpConfidence = booth.node.tcpCompletion;
    }
    else {
      booth.node.tcpConfidence = std::max(fullComparisonAvailable ? 1.0f : 0.2f, booth.node.tcpConfidence);
    }
    if (createRandomVariation) {
      booth.node.tempTcpVotesProjected = tcpVotesProjected;
    } else {
      booth.node.tcpVotesProjected = tcpVotesProjected;
    }
  }
  else if (seat.node.totalTcpVotesCurrent()) {
    // Note this present code may have an issue if the results output
    // does not record TCP candidates for booths that don't have a TCP count yet
    // Not an issue for AEC, but needs to be checked for other elections
    // Make sure the major party is always in 2nd position
    // Do all methods of estimating the TCP and select the one with most confidence
    constexpr int NumMethods = 4;
    std::array<float, NumMethods> methodConfidence = { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, NumMethods> tcpFirst = { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, NumMethods> tcpSecond = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float fpTotalCurrent = booth.node.totalFpVotesCurrent();
    const float fpTotalProjected = booth.node.totalFpVotesProjected();
    float currentVotesEstimate = float(booth.node.totalVotesPrevious());
    if (booth.voteType != Results2::VoteType::Ordinary) {
      currentVotesEstimate = float(generateDeclarationVoteExpectedSize(boothIndex));
    }
    else if (run.getTermCode() == "2026sa") {
      if (booth.boothType == Results2::Booth::Type::Ppvc) {
        currentVotesEstimate *= 2.19f; // account for large increase in PPVC votes in SA
      }
      else {
        // Postals are known to be flat, so the general decline is shares across other booths
        // currentVotesEstimate *= 0.661f;
      }
    }
    float currentVoteTarget = fpTotalProjected > 0.0f
      ? fpTotalProjected
      : currentVotesEstimate;
    bool const currentFpPairAvailable = fpTotalCurrent > 0.0f
      && booth.node.fpVotesCurrent.contains(firstPartyId)
      && booth.node.fpVotesCurrent.contains(secondPartyId);
    bool const projectedFpPairAvailable = fpTotalProjected > 0.0f
      && booth.node.fpVotesProjected.contains(convertPartyId(firstPartyId))
      && booth.node.fpVotesProjected.contains(convertPartyId(secondPartyId));
    if (
      seat.tcpFocusPartyIndex.has_value() && seat.tcpFocusPartyPrefFlow.has_value()
      && seat.tcpFocusPartyConfidence.value_or(0.0f) > 0.0f
      && (currentFpPairAvailable || projectedFpPairAvailable)
    ) {
      // We have a preference flow estimate for the seat, use that to project
      // from the known first preference votes or (with less confidence) projected first preference votes
      int focusParty = seat.tcpFocusPartyIndex.value() == firstPartyId ? firstPartyId : secondPartyId;
      int otherParty = focusParty == firstPartyId ? secondPartyId : firstPartyId;
      bool const useCurrentFp = currentFpPairAvailable;
      float boothPrefs = useCurrentFp ?
        fpTotalCurrent - booth.node.fpVotesCurrent.at(focusParty) - booth.node.fpVotesCurrent.at(otherParty) :
        fpTotalProjected - booth.node.fpVotesProjected.at(convertPartyId(focusParty)) - booth.node.fpVotesProjected.at(convertPartyId(otherParty));
      float preferenceFlow = seat.tcpFocusPartyPrefFlow.value();
      if (createRandomVariation) {
        // placeholder formula, a little on the conservative side but will do for a prototype
        // until I get around to properly calibrating the variance
        float stdDev = 4.0f + 10.0f * std::min(std::exp(-static_cast<float>(booth.node.totalVotesPrevious()) * 0.002f), 2.0f) + 1.5f * std::clamp(1.0f / std::sqrt(seat.tcpFocusPartyConfidence.value()), 1.0f, 20.0f);
        float random = variabilityNormal(0.0f, stdDev, boothIndex, RandomGenerator::combinePartyIds(firstPartyId, secondPartyId), uint32_t(VariabilityTag::PreferenceFlow));
        preferenceFlow = basicTransformedSwing(preferenceFlow, random);
      }
      float boothFocusPartyPrefs = boothPrefs * preferenceFlow * 0.01f;
      float boothFocusPartyFpVotes = useCurrentFp ?
        static_cast<float>(booth.node.fpVotesCurrent.at(focusParty)) :
        booth.node.fpVotesProjected.at(convertPartyId(focusParty));
      float boothFocusPartyTcpVotes = boothFocusPartyFpVotes + boothFocusPartyPrefs;
      float boothOtherPartyTcpVotes = useCurrentFp ?
        fpTotalCurrent - boothFocusPartyTcpVotes : fpTotalProjected - boothFocusPartyTcpVotes;
      float firstTcp = focusParty == firstPartyId ? boothFocusPartyTcpVotes : boothOtherPartyTcpVotes;
      float secondTcp = focusParty == firstPartyId ? boothOtherPartyTcpVotes : boothFocusPartyTcpVotes;
      float const fpTotalUsed = useCurrentFp ? fpTotalCurrent : fpTotalProjected;
      float targetRatio = currentVoteTarget / fpTotalUsed;
      methodConfidence[0] = std::sqrt(seat.tcpFocusPartyConfidence.value())
        * (useCurrentFp ? 0.5f : 0.2f);
      tcpFirst[0] = firstTcp * targetRatio;
      tcpSecond[0] = secondTcp * targetRatio;

      // TODO: As above, we need to account for incomplete returns in non-ordinary booths
      // (FPs seem to arrive with TCPs for now but can't rely on that always being the case)
    }
    if (fullComparisonAvailable) {
      // We either don't have first preference data for this booth yet, or we don't have a preference flow estimate for the seat
      // We  *do* have a match between the current TCP count for the seat and the previous TCP count for the booth
      // So we can calculate the existing swing across the seat and project to this booth
      // TODO: need to add extra checks if the booth was previously in a different seat
      // (won't affect independents since they're counted separately at this stage but could affect e.g. Wills)
      float weightedSwing = 0.0f;
      float summedWeight = 0.0f;
      bool prefFlowAvailable = seat.tcpFocusPartyIndex.has_value() && seat.tcpFocusPartyPrefFlow.has_value()
        && seat.tcpFocusPartyConfidence.value_or(0.0f) > 0.0f;
      for (auto const& otherBoothIndex : seat.booths) {
        // Need to find the existing TCP swing for the seat, average swings across the seat
        // and then project to the booth
        // TODO: for booths with an fp recorded, estimate a tcp from that instead based on preference flows
        // in confirmed booths and weight according to the confidence in each metric
        auto& otherBooth = booths.at(otherBoothIndex);
        if (otherBooth.node.tcpSwings.contains(firstPartyId)
          && otherBooth.node.tcpSwings.contains(secondPartyId)
          && otherBooth.node.totalTcpVotesCurrent() > 0
        ) {
          // This booth is a valid comparison
          float swing = otherBooth.node.tcpSwings.at(firstPartyId);
          float weight = static_cast<float>(otherBooth.node.totalFpVotesCurrent());
          weightedSwing += swing * weight;
          summedWeight += weight;
        }
        else if (otherBooth.node.fpSwings.size() > 0 && prefFlowAvailable) {
          /* calculate estimate for tcp swing and add it at a lower weight */
          int focusParty = seat.tcpFocusPartyIndex.value();
          if (otherBooth.node.fpSwings.contains(firstPartyId) && otherBooth.node.fpSwings.contains(secondPartyId)) {
            float fpSwingFirst = otherBooth.node.fpSwings.at(firstPartyId);
            float fpSwingSecond = otherBooth.node.fpSwings.at(secondPartyId);
            float fpSwingPrefs = -fpSwingFirst - fpSwingSecond;
            float preferenceFlow = firstPartyId == focusParty ? seat.tcpFocusPartyPrefFlow.value() : 100.0f - seat.tcpFocusPartyPrefFlow.value();
            float estimatedTcpSwing = (fpSwingFirst - fpSwingSecond) * 0.5f + fpSwingPrefs * (preferenceFlow * 0.01f - 0.5f);
            float topTwoProportion = 0.01f * ( otherBooth.node.fpSharesPercent().at(firstPartyId) + otherBooth.node.fpSharesPercent().at(secondPartyId));
            float weight = static_cast<float>(otherBooth.node.totalFpVotesCurrent()) * topTwoProportion * topTwoProportion;
            weightedSwing += estimatedTcpSwing * weight;
            summedWeight += weight;
          }
        }
      }

      if (summedWeight > 0.0f) {
        float averageSwing = weightedSwing / summedWeight;
        if (createRandomVariation) {
          // placeholder formula, a little on the conservative side but will do for a prototype
          // until I get around to properly calibrating the variance
          float stdDev = 5.0f + 9.0f * std::exp(-static_cast<float>(booth.node.totalVotesPrevious()) * 0.0001f);
          float random = variabilityNormal(0.0f, stdDev, boothIndex, RandomGenerator::combinePartyIds(firstPartyId, secondPartyId), uint32_t(VariabilityTag::TcpSwing));
          averageSwing += random;
        }
        float tcpTransformedSharePrevious = transformVoteShare(std::clamp(
          float(booth.node.tcpVotesPrevious.at(firstPartyId)) /
            float(booth.node.totalTcpVotesPrevious()) * 100.0f,
          0.01f, 99.99f));
        float tcpTransformedEstimateCurrent = tcpTransformedSharePrevious + averageSwing;
        float tcpEstimateCurrent = detransformVoteShare(tcpTransformedEstimateCurrent) * currentVoteTarget * 0.01f;
        methodConfidence[1] = 0.01f;
        tcpFirst[1] = tcpEstimateCurrent;
        tcpSecond[1] = currentVoteTarget - tcpEstimateCurrent;
      } else {
        // As a last resort use project the vote shares directly (without any kind of swing)
        // Can happen if the only booths reporting TCP data had a different TCP last time or are new.
        // Example case: 2025 Grayndler at 18:50 on election night
        for (auto const& [partyId, votes] : seat.node.tcpVotesCurrent) {
          float tcpProportionCurrent = float(votes) / float(seat.node.totalTcpVotesCurrent());
          float tcpEstimateCurrent = tcpProportionCurrent * currentVoteTarget;
          methodConfidence[1] = 0.001f;
          if (partyId == firstPartyId) tcpFirst[1] = tcpEstimateCurrent;
          else if (partyId == secondPartyId) tcpSecond[1] = tcpEstimateCurrent;
        }
      }
    }
    if (partialComparisonAvailable && !fpTotalCurrent) {
      // We have some TCP data for the seat but (a) no FP data for this booth yet and 
      // (b) exactly one current TCP party was also in this booth's previous TCP.
      // Anchor the comparison on that shared party; it need not be a major party.
      auto previousOpponent = std::find_if(
        booth.node.tcpVotesPrevious.begin(),
        booth.node.tcpVotesPrevious.end(),
        [sharedPreviousPartyId](auto const& partyVotes) {
          return partyVotes.first != sharedPreviousPartyId;
        });
      if (previousOpponent == booth.node.tcpVotesPrevious.end()) {
        throw std::runtime_error(
          "Could not identify the previous TCP opponent for booth "
          + booth.name + ".");
      }
      int const previousOpponentId = previousOpponent->first;
      float weightedSwing = 0.0f;
      float summedWeight = 0.0f;
      float totalExpected = 0.0f;

      for (auto const& otherBoothIndex : seat.booths) {
        // Compare the shared finalist against its previous opponent.
        auto& otherBooth = booths.at(otherBoothIndex);
        if (otherBooth.node.tcpVotesCurrent.contains(firstPartyId)
          && otherBooth.node.tcpVotesCurrent.contains(secondPartyId)
          && otherBooth.node.tcpVotesPrevious.contains(sharedPreviousPartyId)
          && otherBooth.node.tcpVotesPrevious.contains(previousOpponentId)
          && otherBooth.node.totalTcpVotesCurrent() > 0
          && otherBooth.node.totalTcpVotesPrevious() > 0
        ) {
          // This booth is a valid comparison
          float previousSharedShare = transformVoteShare(std::clamp(
            float(otherBooth.node.tcpVotesPrevious.at(sharedPreviousPartyId)) /
              float(otherBooth.node.totalTcpVotesPrevious()) * 100.0f,
            0.01f, 99.99f));
          float currentSharedShare = transformVoteShare(std::clamp(
            float(otherBooth.node.tcpVotesCurrent.at(sharedPreviousPartyId)) /
              float(otherBooth.node.totalTcpVotesCurrent()) * 100.0f,
            0.01f, 99.99f));
          float swing = currentSharedShare - previousSharedShare;
          float weight = static_cast<float>(otherBooth.node.totalVotesPrevious()) * otherBooth.node.relevanceModifier;
          weightedSwing += swing * weight;
          summedWeight += weight;
          totalExpected += otherBooth.node.totalFpVotesProjected();
        }
      }
      if (summedWeight > 0.0f) {
        float averageSwing = weightedSwing / summedWeight;
        if (createRandomVariation) {
          // placeholder formula, a little on the conservative side but will do for a prototype
          // until I get around to properly calibrating the variance
          float stdDev = 7.0f + 12.0f * std::exp(-static_cast<float>(booth.node.totalVotesPrevious()) * 0.0001f);
          float random = variabilityNormal(0.0f, stdDev, boothIndex, RandomGenerator::combinePartyIds(firstPartyId, secondPartyId), uint32_t(VariabilityTag::TcpSwingDifferentCandidate));
          averageSwing += random;
        }
        if (booth.voteType == Results2::VoteType::Postal
          && isMajorParty(sharedPreviousPartyId, natPartyIndex)) {
          // Rough approximation for a new non-major opponent generally doing
          // worse on postals. Do not impose it on two non-major finalists.
          averageSwing -= 5.0f;
        }
        float tcpTransformedSharePrevious = transformVoteShare(std::clamp(
          float(booth.node.tcpVotesPrevious.at(sharedPreviousPartyId)) /
            float(booth.node.totalTcpVotesPrevious()) * 100.0f,
          0.01f, 99.99f));
        float tcpTransformedEstimateCurrent = tcpTransformedSharePrevious + averageSwing;
        float tcpEstimateCurrent = detransformVoteShare(tcpTransformedEstimateCurrent) * currentVoteTarget * 0.01f;
        // Lower confidence because one of the parties has changed, so patterns are less reliable
        methodConfidence[2] = totalExpected > 0.0f
          ? std::sqrt(std::clamp(summedWeight / totalExpected, 0.0f, 1.0f)) * 0.1f
          : 0.0f;
        if (sharedPreviousPartyId == firstPartyId) {
          tcpFirst[2] = tcpEstimateCurrent;
          tcpSecond[2] = currentVoteTarget - tcpEstimateCurrent;
        }
        else {
          tcpFirst[2] = currentVoteTarget - tcpEstimateCurrent;
          tcpSecond[2] = tcpEstimateCurrent;
        }
      }
    }
    if (currentVoteTarget > 0.0f
      && (currentFpPairAvailable || projectedFpPairAvailable)) {
      // As a last resort, if we have FP data, use a 50% pref flow by default
      // Maybe refine this using preference guesses later
      // But for now it should prevent booth TCPs from being completely unassigned
      bool const useCurrentFp = currentFpPairAvailable;
      float boothPrefs = useCurrentFp ?
        fpTotalCurrent - booth.node.fpVotesCurrent.at(firstPartyId) - booth.node.fpVotesCurrent.at(secondPartyId) :
        fpTotalProjected - booth.node.fpVotesProjected.at(convertPartyId(firstPartyId)) - booth.node.fpVotesProjected.at(convertPartyId(secondPartyId));
      float preferenceFlow = 50.0f;
      if (createRandomVariation) {
        // placeholder formula, a little on the conservative side but will do for a prototype
        // until I get around to properly calibrating the variance
        float stdDev = 32.0f
          + 10.0f * std::exp(
            -static_cast<float>(booth.node.totalVotesPrevious()) * 0.0001f);
        float random = variabilityNormal(0.0f, stdDev, boothIndex, RandomGenerator::combinePartyIds(firstPartyId, secondPartyId), uint32_t(VariabilityTag::PreferenceFlow));
        preferenceFlow = basicTransformedSwing(preferenceFlow, random);
      }
      float boothFirstPartyPrefs = boothPrefs * preferenceFlow * 0.01f;
      float boothFirstPartyFpVotes = useCurrentFp ?
        static_cast<float>(booth.node.fpVotesCurrent.at(firstPartyId)) :
        booth.node.fpVotesProjected.at(convertPartyId(firstPartyId));
      float boothFirstPartyTcpVotes = boothFirstPartyFpVotes + boothFirstPartyPrefs;
      float boothSecondPartyTcpVotes = useCurrentFp ?
        fpTotalCurrent - boothFirstPartyTcpVotes : fpTotalProjected - boothFirstPartyTcpVotes;
      float targetRatio = currentVoteTarget / (useCurrentFp ? fpTotalCurrent : fpTotalProjected);
      methodConfidence[3] = 0.0001f;
      tcpFirst[3] = boothFirstPartyTcpVotes * targetRatio;
      tcpSecond[3] = boothSecondPartyTcpVotes * targetRatio;
    }

    float firstPartyWeighted = 0.0f;
    float secondPartyWeighted = 0.0f;
    float totalWeight = 0.0f;
    float highestConfidence = 0.0f;
    // The weighted blend is intentional: disagreement can support an
    // intermediate estimate rather than automatically reducing confidence.
    // Revisit how confidence should reflect disagreement when this path is
    // calibrated against a wider set of non-classic contests.
    for (int method = 0; method < NumMethods; ++method) {
      firstPartyWeighted += tcpFirst[method] * methodConfidence[method];
      secondPartyWeighted += tcpSecond[method] * methodConfidence[method];
      totalWeight += methodConfidence[method];
      highestConfidence = std::max(highestConfidence, methodConfidence[method]);
    }

    if (totalWeight > 0.0f && highestConfidence > 0.0f) {
      float estimatedFirstParty = firstPartyWeighted / totalWeight;
      float estimatedSecondParty = secondPartyWeighted / totalWeight;
      if (!std::isfinite(estimatedFirstParty)
        || !std::isfinite(estimatedSecondParty)
        || estimatedFirstParty < 0.0f || estimatedSecondParty < 0.0f) {
        throw std::runtime_error(
          "Invalid TCP projection for booth " + booth.name + ".");
      }
      if (createRandomVariation) {
        booth.node.tempTcpVotesProjected[convertPartyId(firstPartyId)] = estimatedFirstParty;
        booth.node.tempTcpVotesProjected[convertPartyId(secondPartyId)] = estimatedSecondParty;
      }
      else {
        booth.node.tcpVotesProjected[convertPartyId(firstPartyId)] = estimatedFirstParty;
        booth.node.tcpVotesProjected[convertPartyId(secondPartyId)] = estimatedSecondParty;
        booth.node.tcpConfidence = highestConfidence;
      }

    }
    else {
      cantProject = true;
    }
  }
  else {
    // If we don't have any TCP data in the seat at all, don't project anything,
    // the simulation will default to fp-based estimates in any case.
  }

  if (cantProject) {
    // TODO: If we couldn't project anything because there were no matches,
    // then just use the TCP shares directly and assign low confidence
  }
}

void Election::recomposeBoothTppVotes(bool allowCurrentData, int boothIndex) {
  auto& booth = booths.at(boothIndex);
  bool dataExists = allowCurrentData && booth.node.totalTcpVotesCurrent() && isTppSet(booth.node.tcpVotesCurrent, natPartyIndex);
  if (dataExists) {
    // map from party index to vote count (as a float)
    auto tppVotesProjected = std::map<int, float>();
    for (auto const& [partyId, votes] : booth.node.tcpVotesCurrent) {
      tppVotesProjected[partyId == natPartyIndex ? 1 : partyId] += static_cast<float>(votes);
    }
    if (tppVotesProjected.size() != 2
      || !tppVotesProjected.contains(0)
      || !tppVotesProjected.contains(1)) {
      throw std::runtime_error(
        "Current TPP does not contain exactly one Labor and one Coalition count for booth "
        + booth.name + ".");
    }
    if (booth.voteType != Results2::VoteType::Ordinary) {
      float currentTotalVotesProjected = std::accumulate(tppVotesProjected.begin(), tppVotesProjected.end(), 0.0f,
        [](float sum, const auto& pair) { return sum + pair.second; });
      float expectedTotalVotes = std::max(
        float(generateDeclarationVoteExpectedSize(boothIndex)),
        currentTotalVotesProjected);
      // This assumes the final TPP count is close to the expected FP total.
      // Savings provisions can make it slightly lower; retain this simple
      // approximation unless a concrete election shows a material effect.
      float totalAdditionalVotes = expectedTotalVotes - currentTotalVotesProjected;
      float stdDev = 4.5f + 7.0f * std::exp(-static_cast<float>(booth.node.totalVotesPrevious()) * 0.0001f);
      float random = variabilityNormal(0.0f, stdDev, boothIndex, 0, uint32_t(VariabilityTag::NonOrdinaryTpp));
      float projectionDifference = createRandomVariation ? random : 0.0f;
      if (booth.voteType == Results2::VoteType::Postal) {
        projectionDifference += 2.0f; // late-arriving postals are generally more friendly to ALP than early postals
      }
      float transformedOriginal = transformVoteShare(std::clamp(
        float(tppVotesProjected.at(0)) /
          currentTotalVotesProjected * 100.0f,
        0.1f, 99.9f));
      float transformedProjection = transformedOriginal + projectionDifference;
      float additionalPartyOneVotes = detransformVoteShare(transformedProjection) * totalAdditionalVotes * 0.01f;
      tppVotesProjected[0] += additionalPartyOneVotes;
      tppVotesProjected[1] += totalAdditionalVotes - additionalPartyOneVotes;
      float const progress = std::clamp(
        currentTotalVotesProjected / expectedTotalVotes, 0.0f, 1.0f);
      booth.node.tppCompletion = progress;
      booth.node.tppConfidence = progress;
    }
    if (createRandomVariation) {
      booth.node.tempTppVotesProjected = tppVotesProjected;
    } else {
      booth.node.tppVotesProjected = tppVotesProjected;
    }
  }
  // Do this last section even if there is a known TPP count, so that we can compare and calculate a bias
  // If we don't have an actual TCP count, just make an estimate based on observed deviations
  // Even if the seat doesn't end up using TPP, this will still be used to estimate the fp votes
  // and the actual TCP will come from fp-based estimates in the simulation.
  const float votesGuess = booth.boothType == Results2::Booth::Type::Hospital ? HospitalBoothVotesGuess : PreviousTotalVotesGuess;
  float previousTotalVotes = booth.node.totalVotesPrevious() ? booth.node.totalVotesPrevious() : votesGuess;
  const float fpTotalCurrent = booth.node.totalFpVotesCurrent();
  const float fpTotalProjected = booth.node.totalFpVotesProjected();
  float currentVotesEstimate = previousTotalVotes;
  if (booth.voteType != Results2::VoteType::Ordinary) {
    currentVotesEstimate = float(generateDeclarationVoteExpectedSize(boothIndex));
  }
  else if (run.getTermCode() == "2026sa") {
    if (booth.boothType == Results2::Booth::Type::Ppvc) {
      currentVotesEstimate *= 2.19f; // account for large increase in PPVC votes in SA
    }
    else {
      // Postals are known to be flat, so the general decline is shares across other booths
      //currentVotesEstimate *= 0.661f;
    }
  }
  float currentVoteTarget = fpTotalProjected > 0.0f
    ? fpTotalProjected
    : std::max(fpTotalCurrent, currentVotesEstimate);
  std::map<int, float> tempTppVotesProjected;
  std::optional<float> prevAlpShare;
  float const previousTcpTotal = float(booth.node.totalTcpVotesPrevious());
  if (previousTcpTotal > 0.0f
    && isTppSet(booth.node.tcpVotesPrevious, natPartyIndex)) {
    // TPP is a share of the TCP count. The mapped FP total may differ when
    // multiple historical candidates collapse to one internal party, so using
    // it here would make recomposition inconsistent with calculateTppSwing().
    prevAlpShare = transformVoteShare(
      float(booth.node.tcpVotesPrevious.at(0)) / previousTcpTotal * 100.0f);
  }
  std::optional<float> seatBaselineAlpShare;
  if (seats[booth.parentSeatId].node.tppShareBaseline.has_value()) {
    seatBaselineAlpShare = seats[booth.parentSeatId].node.tppShareBaseline.value();
  }
  std::optional<float> baselineAlpShare =
    (prevAlpShare.has_value()
      ? prevAlpShare.value() + booth.node.tppSwingBaseline.value_or(0.0f)
      : seatBaselineAlpShare.value_or(0.0f) // this already has the baseline swing factored in
    );
  float deviation = 0.0f;
  if (allowCurrentData) {
    if (dataExists) {
      // When we have a non-classic TCP seat, there isn't a booth or seat TPP deviation
      // so when we want to calibrate them to the known-TPP data, we shouldn't use that either,
      // only the regional and election-level TPP
      auto const& seat = seats[booth.parentSeatId];
      auto const& region = largeRegions[seat.parentRegionIndex];
      auto const& thisAndParents = getThisAndParents(region);
      for (auto const& parent : thisAndParents) {
        deviation += parent->specificTppDeviation.value_or(0.0f);
      }
    }
    else {
      auto const& thisAndParents = getThisAndParents(booth);
      for (auto const& parent : thisAndParents) {
        deviation += parent->specificTppDeviation.value_or(0.0f);
      }
    }
  }
  float existingAlpShare = baselineAlpShare.value_or(
    previousTcpTotal > 0.0f
      && isTppSet(booth.node.tcpVotesPrevious, natPartyIndex)
      ? transformVoteShare(
        float(booth.node.tcpVotesPrevious.at(0)) / previousTcpTotal * 100.0f)
      : 0.0f
    );
  if (createRandomVariation) {
    // placeholder formula, a little on the conservative side but will do for a prototype
    // until I get around to properly calibrating the variance
    float stdDev = 4.5f + 7.0f * std::exp(-static_cast<float>(booth.node.totalVotesPrevious()) * 0.0001f);
    float random = variabilityNormal(0.0f, stdDev, boothIndex, 0, uint32_t(VariabilityTag::BoothProjectionTpp));
    deviation += random;
  }
  // Apply biases for this booth type and vote type
  if (allowCurrentData && boothTypeTppBiases.contains(booth.boothType)) {
    deviation += boothTypeTppBiases.at(booth.boothType);
  }
  if (allowCurrentData && voteTypeTppBiases.contains(booth.voteType)) {
    deviation += voteTypeTppBiases.at(booth.voteType);
  }
  float newAlpShare = existingAlpShare + deviation;
  // If we know how many FP votes there are, and we know that the booth is ~complete, then that's a better estimate
  // of eventual TPP votes than the previous election, so use that instead
  float totalEstimate = allowCurrentData && booth.node.totalFpVotesCurrent() > 0 && booth.node.fpCompletion > 0.99f ?
    booth.node.totalFpVotesCurrent() : currentVoteTarget;
  // Even if booth isn't complete, allow increased vote count if there are more fp votes than previous
  if (allowCurrentData && booth.node.totalFpVotesCurrent() > totalEstimate) totalEstimate = booth.node.totalFpVotesCurrent();
  float newAlpVotes = detransformVoteShare(newAlpShare) * totalEstimate * 0.01f;
  tempTppVotesProjected[0] = newAlpVotes;
  int coalitionPartyId = 1;
  tempTppVotesProjected[coalitionPartyId] = totalEstimate - newAlpVotes;

  for (auto const& [partyId, votes] : tempTppVotesProjected) {
    if (!std::isfinite(votes) || votes < 0.0f) {
      throw std::runtime_error(
        "Invalid TPP projection for booth " + booth.name + ".");
    }
  }
  if (createRandomVariation) {
    if (!dataExists) booth.node.tempTppVotesProjected = tempTppVotesProjected;
  } else {
    if (!dataExists) {
      booth.node.tppVotesProjected = tempTppVotesProjected;
      if (allowCurrentData && booth.boothType != Results2::Booth::Type::Normal && booth.boothType != Results2::Booth::Type::Invalid && booth.boothType != Results2::Booth::Type::Other) {
        seats[booth.parentSeatId].tppBoothTypeSensitivity[booth.boothType] += currentVoteTarget;
      }
      else if (allowCurrentData && booth.voteType != Results2::VoteType::Ordinary && booth.voteType != Results2::VoteType::Invalid) {
        seats[booth.parentSeatId].tppVoteTypeSensitivity[booth.voteType] += currentVoteTarget;
      }
    }
    else { // dataExists
      booth.tppVotesEstimated = tempTppVotesProjected;
    }
  }
}

void Election::recomposeSeatFpVotes(int seatIndex) {
  seats[seatIndex].node.fpVotesProjected = std::map<int, float>();
  for (auto const& boothIndex : seats[seatIndex].booths) {
    for (auto const& [partyId, votes] : booths[boothIndex].node.fpVotesProjected) {
      seats[seatIndex].node.fpVotesProjected[partyId] += votes;
    }
  }

  // Map the strongest candidate-specific independent to the simulation's
  // generic independent. This avoids a sharp discontinuity when that candidate
  // first reports; other independents remain separate and contribute through
  // the seat's generic Others projection.
  if (!seats[seatIndex].node.fpVotesProjected.contains(run.indPartyIndex)) {
    int bestIndependentId = InvalidPartyIndex;
    for (auto const& [partyId, votes] : seats[seatIndex].node.fpVotesProjected) {
      if (partyId >= IndependentPartyIdOffset) {
        if (bestIndependentId == InvalidPartyIndex || votes > seats[seatIndex].node.fpVotesProjected.at(bestIndependentId)) {
          bestIndependentId = partyId;
        }
      }
    }
    if (bestIndependentId != InvalidPartyIndex) {
      seats[seatIndex].node.fpVotesProjected[run.indPartyIndex] = seats[seatIndex].node.fpVotesProjected.at(bestIndependentId);
      seats[seatIndex].node.fpVotesProjected.erase(bestIndependentId);
    }
  }
}

void Election::recomposeSeatTcpVotes(int seatIndex) {
  seats[seatIndex].node.tcpVotesProjected = std::map<int, float>();
  auto& seatNode = seats[seatIndex].node;
  seatNode.tcpShares.clear();
  seatNode.tcpCompletion = 0.0f;
  seatNode.tcpConfidence = 0.0f;
  float weightedCompletion = 0.0f;
  float weightedConfidence = 0.0f;
  float totalWeight = 0.0f;
  for (auto const& boothIndex : seats[seatIndex].booths) {
    auto const& boothNode = booths[boothIndex].node;
    float const boothWeight = boothNode.totalFpVotesProjected();
    if (!std::isfinite(boothWeight) || boothWeight < 0.0f) {
      throw std::runtime_error(
        "Invalid TCP aggregation weight in seat " + seats[seatIndex].name + ".");
    }
    // Every booth contributes to the denominator. A booth without a usable TCP
    // projection therefore lowers seat completion/confidence rather than being
    // silently omitted from those metrics.
    weightedCompletion += boothNode.tcpCompletion * boothWeight;
    weightedConfidence += boothNode.tcpConfidence * boothWeight;
    totalWeight += boothWeight;
    if (boothNode.tcpVotesProjected.empty()) continue;
    for (auto const& [partyId, votes] : boothNode.tcpVotesProjected) {
      if (!std::isfinite(votes) || votes < 0.0f) {
        throw std::runtime_error(
          "Invalid TCP projection in seat " + seats[seatIndex].name + ".");
      }
      seatNode.tcpVotesProjected[partyId] += votes;
    }
  }
  if (seatNode.totalTcpVotesProjected() == 0) return;
  if (totalWeight <= 0.0f) {
    throw std::runtime_error(
      "No positive TCP aggregation weight in seat " + seats[seatIndex].name + ".");
  }
  if (seatNode.totalTcpVotesProjected() < seatNode.totalFpVotesProjected() * 0.95f) {
    // In some cases (e.g. mismatched declaration vote counts and no ordinary vote counts,
    // such as Griffith 2025 at 2025-05-04 14:02), TCP projections are only done for some
    // booths. Since this can be very unrepresentative, we can't assign any confidence to such
    // cases, but should still record that the votes exist.
    // This fixed comparison assumes compulsory preferential voting. Under OPV,
    // legitimate exhaustion must be separated from genuinely missing booth TCP
    // projections before assigning confidence.
    seatNode.tcpCompletion = weightedCompletion / totalWeight;
    seatNode.tcpConfidence = 0.0f;
    return;
  }
  for (auto const& [partyId, votes] : seatNode.tcpVotesProjected) {
    float const share = votes / seatNode.totalTcpVotesProjected() * 100.0f;
    seatNode.tcpShares[partyId] = transformVoteShare(
      std::clamp(share, 0.1f, 99.9f));
  }
  seatNode.tcpCompletion = weightedCompletion / totalWeight;
  seatNode.tcpConfidence = weightedConfidence / totalWeight;
}

void Election::recomposeSeatTppVotes(int seatIndex) {
  // Just collect all the tpp votes from the booths in the seat
  // then convert them in to shares

  seats[seatIndex].node.tppVotesProjected = std::map<int, float>();
  for (auto const& boothIndex : seats[seatIndex].booths) {
    for (auto const& [partyId, votes] : booths[boothIndex].node.tppVotesProjected) {
      seats[seatIndex].node.tppVotesProjected[partyId] += votes;
    }
  }
}

void Election::recomposeLargeRegionFpVotes(int largeRegionIndex) {
  largeRegions[largeRegionIndex].node.fpVotesProjected = std::map<int, float>();
  for (auto const& seatIndex : largeRegions[largeRegionIndex].seats) {
    for (auto const& [partyId, votes] : seats[seatIndex].node.fpVotesProjected) {
      largeRegions[largeRegionIndex].node.fpVotesProjected[partyId] += votes;
    }
  }
}

void Election::recomposeLargeRegionTppVotes(int largeRegionIndex) {
  largeRegions[largeRegionIndex].node.tppVotesProjected = std::map<int, float>();
  for (auto const& seatIndex : largeRegions[largeRegionIndex].seats) {
    for (auto const& [partyId, votes] : seats[seatIndex].node.tppVotesProjected) {
      largeRegions[largeRegionIndex].node.tppVotesProjected[partyId] += votes;
    }
  }
}

void Election::recomposeElectionFpVotes() {
  node.fpVotesProjected = std::map<int, float>();
  for (auto const& largeRegion : largeRegions) {
    for (auto const& [partyId, votes] : largeRegion.node.fpVotesProjected) {
      node.fpVotesProjected[partyId] += votes;
    }
  }
}

void Election::recomposeElectionTppVotes() {
  node.tppVotesProjected = std::map<int, float>();
  for (auto const& largeRegion : largeRegions) {
    for (auto const& [partyId, votes] : largeRegion.node.tppVotesProjected) {
      node.tppVotesProjected[partyId] += votes;
    }
  }
}

static float offsetWeight(float completion, float confidence) {
  // This cubic fade is currently heuristic. It should ideally be calibrated
  // against historical counts once enough suitable snapshots are available.
  if (!std::isfinite(completion) || !std::isfinite(confidence)) {
    throw std::runtime_error("Cannot calculate an offset weight from non-finite evidence.");
  }
  float const evidence = std::clamp(
    std::max(completion, confidence), 0.0f, 1.0f);
  return std::pow(1.0f - evidence, 3.0f);
}

static float offsetWeightFp(LiveV2::Node const& node) {
  return offsetWeight(node.fpCompletion, node.fpConfidence);
}

static float offsetWeightTpp(LiveV2::Node const& node) {
  return offsetWeight(node.tppCompletion, node.tppConfidence);
}

void Election::determineElectionFinalFpDeviations(bool allowCurrentData) {
  auto const shares = projectedVoteShares(
    node.fpVotesProjected, "Election");
  // now convert to deviations
  for (auto const& [partyId, share] : shares) {
    // TODO: this should be sourced from the simulation report
    float finalDeviation = node.fpSharesBaseline.contains(partyId)
      ? transformVoteShare(std::clamp(share * 100.0f, 0.1f, 99.9f))
        - node.fpSharesBaseline.at(partyId)
      : 0.0f;

    if (allowCurrentData) {
      float offset = offsetSpecificFpDeviations.contains(partyId)
        ? offsetSpecificFpDeviations.at(partyId) * offsetWeightFp(node)
        : 0.0f;
      finalSpecificFpDeviations[partyId] = finalDeviation - offset;
    } else {
      offsetSpecificFpDeviations[partyId] = finalDeviation;
    }
  }
}

void Election::determineElectionFinalTppDeviation(bool allowCurrentData) {
  auto const alpShare = projectedTppAlpShare(
    node.tppVotesProjected, "Election");
  if (!alpShare) {
    // No tpp estimations, so don't modify anything at the seat level
    if (allowCurrentData) {
      finalSpecificTppDeviation = 0.0f;
    } else {
      offsetSpecificTppDeviation = 0.0f;
    }
    return;
  }
  float const finalDeviation = transformVoteShare(
    std::clamp(*alpShare * 100.0f, 0.1f, 99.9f))
    - node.tppShareBaseline.value_or(0.0f);
  if (allowCurrentData) {
    // This will eventually be replaced with something a lot more sophisticated,
    // but for now this is a simple way to account for factors that may bias
    // the area's TPP share away from the baseline, such as redistributions, new
    // booths, or expected shifts between vote types.
    // use completion rather than confidence to make sure that complete TPP results
    // in no offset, even if confidence is low due to inability to measure swings
    float offset = offsetSpecificTppDeviation.value_or(0.0f) * offsetWeightTpp(node);
    finalSpecificTppDeviation = finalDeviation - offset;
  } else {
    offsetSpecificTppDeviation = finalDeviation;
  }
}

void Election::determineLargeRegionFinalFpDeviations(bool allowCurrentData, int largeRegionIndex) {
  auto const shares = projectedVoteShares(
    largeRegions[largeRegionIndex].node.fpVotesProjected,
    "Region " + largeRegions[largeRegionIndex].name);
  // now convert to deviations
  for (auto const& [partyId, share] : shares) {
    float inheritedDeviation = 0.0f;
    if (allowCurrentData) {
      if (finalSpecificFpDeviations.contains(partyId)) {
        inheritedDeviation += finalSpecificFpDeviations.at(partyId);
      }
    }
    float finalDeviation = largeRegions[largeRegionIndex].node.fpSharesBaseline.contains(partyId)
      ? transformVoteShare(std::clamp(share * 100.0f, 0.1f, 99.9f))
        - largeRegions[largeRegionIndex].node.fpSharesBaseline.at(partyId)
        - inheritedDeviation
      : 0.0f;
    if (allowCurrentData) {
      float offset = largeRegions[largeRegionIndex].offsetSpecificFpDeviations.contains(partyId)
        ? largeRegions[largeRegionIndex].offsetSpecificFpDeviations.at(partyId) * offsetWeightFp(largeRegions[largeRegionIndex].node)
        : 0.0f;
      largeRegions[largeRegionIndex].finalSpecificFpDeviations[partyId] =
        finalDeviation - offset;
    } else {
      largeRegions[largeRegionIndex].offsetSpecificFpDeviations[partyId] =
        finalDeviation;
    }
  }
}

void Election::determineLargeRegionFinalTppDeviation(bool allowCurrentData, int largeRegionIndex) {
  auto& region = largeRegions[largeRegionIndex];
  auto const alpShare = projectedTppAlpShare(
    region.node.tppVotesProjected, "Region " + region.name);
  if (!alpShare) {
    // No tpp estimations, so don't modify anything at the seat level
    if (allowCurrentData) {
      largeRegions[largeRegionIndex].finalSpecificTppDeviation = 0.0f;
    } else {
      largeRegions[largeRegionIndex].offsetSpecificTppDeviation = 0.0f;
    }
    return;
  }
  float deviation = 0.0f;
  if (allowCurrentData) {
    deviation += finalSpecificTppDeviation.value_or(0.0f);
  }
  float const finalDeviation = transformVoteShare(
    std::clamp(*alpShare * 100.0f, 0.1f, 99.9f))
    - region.node.tppShareBaseline.value_or(0.0f) - deviation;
  if (allowCurrentData) {
    // This will eventually be replaced with something a lot more sophisticated,
    // but for now this is a simple way to account for factors that may bias
    // the seat's TPP share away from the baseline, such as redistributions, new
    // booths, or expected shifts between vote types.
    // use completion rather than confidence to make sure that complete TPP results
    // in no offset, even if confidence is low due to inability to measure swings
    float offset = largeRegions[largeRegionIndex].offsetSpecificTppDeviation.value_or(0.0f) * offsetWeightTpp(largeRegions[largeRegionIndex].node);
    largeRegions[largeRegionIndex].finalSpecificTppDeviation = finalDeviation - offset;
  } else {
    largeRegions[largeRegionIndex].offsetSpecificTppDeviation = finalDeviation;
  }
}

void Election::determineSeatFinalFpDeviations(bool allowCurrentData, int seatIndex) {
  auto const shares = projectedVoteShares(
    seats[seatIndex].node.fpVotesProjected,
    "Seat " + seats[seatIndex].name);
  // now convert to deviations
  for (auto const& [partyId, share] : shares) {
    float inheritedDeviation = 0.0f;
    if (allowCurrentData) {
      auto const& parentRegion = largeRegions[seats[seatIndex].parentRegionIndex];
      if (parentRegion.finalSpecificFpDeviations.contains(partyId)) {
        inheritedDeviation += parentRegion.finalSpecificFpDeviations.at(partyId);
      }
      if (finalSpecificFpDeviations.contains(partyId)) {
        inheritedDeviation += finalSpecificFpDeviations.at(partyId);
      }
    }
    // Parties absent from the baseline currently receive no live deviation and
    // are filtered from seat information. This prevents unsupported activation,
    // but also means genuinely emerging parties need a separate future pathway.
    float finalDeviation = seats[seatIndex].node.fpSharesBaseline.contains(partyId)
      ? transformVoteShare(std::clamp(share * 100.0f, 0.1f, 99.9f))
        - seats[seatIndex].node.fpSharesBaseline.at(partyId)
        - inheritedDeviation
      : 0.0f;
    if (allowCurrentData) {
      float offset = seats[seatIndex].offsetSpecificFpDeviations.contains(partyId)
        ? seats[seatIndex].offsetSpecificFpDeviations.at(partyId) * offsetWeightFp(seats[seatIndex].node)
        : 0.0f;
      seats[seatIndex].finalSpecificFpDeviations[partyId] =
        finalDeviation - offset;
    } else {
      seats[seatIndex].offsetSpecificFpDeviations[partyId] = finalDeviation;
    }
  }
}

void Election::determineSeatFinalTppDeviation(bool allowCurrentData, int seatIndex) {
  auto& seat = seats[seatIndex];
  auto const alpShare = projectedTppAlpShare(
    seat.node.tppVotesProjected, "Seat " + seat.name);
  if (!alpShare) {
    // No tpp estimations, so don't modify anything at the seat level
    if (allowCurrentData) {
      seats[seatIndex].finalSpecificTppDeviation = 0.0f;
    } else {
      seats[seatIndex].offsetSpecificTppDeviation = 0.0f;
    }
    return;
  }
  float inheritedDeviation = 0.0f;
  if (allowCurrentData) {
    auto const& parentRegion = largeRegions[seats[seatIndex].parentRegionIndex];
    inheritedDeviation += parentRegion.finalSpecificTppDeviation.value_or(0.0f);
    inheritedDeviation += finalSpecificTppDeviation.value_or(0.0f);
  }
  float const finalDeviation = transformVoteShare(
    std::clamp(*alpShare * 100.0f, 0.1f, 99.9f))
    - seat.node.tppShareBaseline.value_or(0.0f) - inheritedDeviation;
  if (allowCurrentData) {
    // This will eventually be replaced with something a lot more sophisticated,
    // but for now this is a simple way to account for factors that may bias
    // the seat's TPP share away from the baseline, such as redistributions, new
    // booths, or expected shifts between vote types.
    // use completion rather than confidence to make sure that complete TPP results
    // in no offset, even if confidence is low due to inability to measure swings
    float offset = seats[seatIndex].offsetSpecificTppDeviation.value_or(0.0f) * offsetWeightTpp(seats[seatIndex].node);
    seats[seatIndex].finalSpecificTppDeviation = finalDeviation - offset;
  } else {
    seats[seatIndex].offsetSpecificTppDeviation = finalDeviation;
  }
}

void LiveV2::Election::calculateTppEstimateBias()
{
  nonClassicTppBiasPercentagePoints = 0.0f;
  nonClassicTppBiasConfidence = 0.0f;

  std::map<int, float> recordedVotes;
  std::map<int, float> estimatedVotes;
  float totalProjectedTppVotes = 0.0f;
  float completedClassicTppVotes = 0.0f;
  float completedClassicTppVotesSquared = 0.0f;
  for (auto const& booth : booths) {
    for (auto const& [partyId, votes] : booth.node.tppVotesProjected) {
      if (!std::isfinite(votes) || votes < 0.0f) {
        throw std::runtime_error(
          "Invalid projected TPP votes while estimating non-classic bias for booth "
          + booth.name + ".");
      }
      totalProjectedTppVotes += votes;
    }

    if (booth.node.tppCompletion < 0.9f
      || !isTppSet(booth.node.tcpVotesCurrent, natPartyIndex)) {
      continue;
    }
    if (!booth.tppVotesEstimated.contains(0)
      || !booth.tppVotesEstimated.contains(1)) {
      throw std::runtime_error(
        "Completed classic TPP booth lacks a comparable TPP estimate: "
        + booth.name + ".");
    }

    float boothRecordedVotes = 0.0f;
    for (auto const& [partyId, votes] : booth.node.tcpVotesCurrent) {
      int const mappedPartyId = partyId == natPartyIndex ? 1 : partyId;
      recordedVotes[mappedPartyId] += votes;
      if (mappedPartyId == 0 || mappedPartyId == 1) {
        boothRecordedVotes += votes;
      }
    }
    for (auto const& [partyId, votes] : booth.tppVotesEstimated) {
      estimatedVotes[partyId == natPartyIndex ? 1 : partyId] += votes;
    }
    completedClassicTppVotes += boothRecordedVotes;
    completedClassicTppVotesSquared += boothRecordedVotes * boothRecordedVotes;
  }
  PA_LOG_VAR(recordedVotes);
  PA_LOG_VAR(estimatedVotes);
  float recordedDenominator = recordedVotes[0] + recordedVotes[1];
  float estimatedDenominator = estimatedVotes[0] + estimatedVotes[1];
  if (recordedDenominator > 0 && estimatedDenominator > 0) {
    float recordedAlpShare = recordedVotes[0] / recordedDenominator;
    float estimatedAlpShare = estimatedVotes[0] / estimatedDenominator;
    nonClassicTppBiasPercentagePoints =
      (estimatedAlpShare - recordedAlpShare) * 100.0f;

    float const voteCoverage = totalProjectedTppVotes > 0.0f
      ? std::clamp(completedClassicTppVotes / totalProjectedTppVotes, 0.0f, 1.0f)
      : 0.0f;
    float const effectiveBoothCount = completedClassicTppVotesSquared > 0.0f
      ? completedClassicTppVotes * completedClassicTppVotes
        / completedClassicTppVotesSquared
      : 0.0f;
    // Pragmatic first pass: require both broad vote coverage and several
    // independently completed booths. This curve and its scale should
    // eventually be calibrated, ideally using booth-level bias dispersion.
    float const boothBreadth = 1.0f - std::exp(
      -effectiveBoothCount / NonClassicBiasEffectiveBoothScale);
    nonClassicTppBiasConfidence = voteCoverage * boothBreadth;

    PA_LOG_VAR(recordedAlpShare);
    PA_LOG_VAR(estimatedAlpShare);
    PA_LOG_VAR(nonClassicTppBiasPercentagePoints);
    PA_LOG_VAR(nonClassicTppBiasConfidence);
  }

}

void Election::calculateLivePreferenceFlowDeviations() {
  // Retained as diagnostic/preparatory output. SimulationIteration does not
  // currently consume this value because recalculating major-party FPs from it
  // remains disabled pending validation of that interaction.
  for (auto& seat : seats) {
    // Basic idea: calculate preference flow based on "expected" (pre-election) flows
    // Then compare this to "actual" flows (i.e. what the projections say, which might not be quite the same as directly measured from booths)

    float expectedPartyOnePrefs = 0.0f;
    float totalMinorPartyPrefs = 0.0f;
    // Calculate estimate of party one's share of the TPP based on the FP votes
    float totalFpVotes = std::accumulate(
      seat.node.fpVotesProjected.begin(),
      seat.node.fpVotesProjected.end(),
      0.0f,
      [](float sum, const auto& pair) { return sum + pair.second; }
    );
    if (!std::isfinite(totalFpVotes) || totalFpVotes <= 0.0f
      || !seat.node.fpVotesProjected.contains(0)) {
      seat.livePreferenceFlowDeviation = 0.0f;
      continue;
    }
    float alpPrimaryPlusCoalitionLeakage = seat.node.fpVotesProjected.at(0) / totalFpVotes;
    // Use the final FP projection consistently. Current counts can temporarily
    // favour the wrong Coalition candidate early in counting.
    int preferredCoalitionParty = 1;
    if (seat.node.fpVotesProjected.contains(natPartyIndex)
      && (!seat.node.fpVotesProjected.contains(1)
        || seat.node.fpVotesProjected.at(natPartyIndex)
          > seat.node.fpVotesProjected.at(1))) {
      preferredCoalitionParty = natPartyIndex;
    }
    for (auto const& [partyId, votes] : seat.node.fpVotesProjected) {
      float const share = votes / totalFpVotes;

      // now allocate preferences for non-major parties
      if (partyId == preferredCoalitionParty || partyId == 0) {
        // Major parties, don't include them in preference flow calculations
        continue;
      }
      else if (partyId == natPartyIndex || partyId == 1) {
        // Other coalition party's votes, assume some leakage to Labor
        alpPrimaryPlusCoalitionLeakage += share * CoalitionLeakagePercent * 0.01f;
      }
      else if (preferenceFlowMap.contains(partyId)) {
        expectedPartyOnePrefs += share * preferenceFlowMap.at(partyId) * 0.01f;
        totalMinorPartyPrefs += share;
      }
      else {
        expectedPartyOnePrefs += share
          * preferenceFlowMap.at(PartyCollection::InvalidIndex) * 0.01f;
        totalMinorPartyPrefs += share;
      }
    }
    // With no projected non-major vote there is no preference-flow evidence.
    // Keep the neutral prior rather than producing a non-finite transformed share.
    if (totalMinorPartyPrefs <= 0.0f) {
      seat.livePreferenceFlowDeviation = 0.0f;
      continue;
    }
    float expectedPreferenceFlow = transformVoteShare(std::clamp(
      expectedPartyOnePrefs / totalMinorPartyPrefs * 100.0f,
      0.1f, 99.9f));

    if (!seat.node.tppVotesProjected.contains(0)
      || !seat.node.tppVotesProjected.contains(1)) {
      seat.livePreferenceFlowDeviation = 0.0f;
      continue;
    }
    float const totalTppVotes = seat.node.tppVotesProjected.at(0)
      + seat.node.tppVotesProjected.at(1);
    if (!std::isfinite(totalTppVotes) || totalTppVotes <= 0.0f) {
      seat.livePreferenceFlowDeviation = 0.0f;
      continue;
    }
    float actualTpp = seat.node.tppVotesProjected.at(0) / totalTppVotes;
    float actualPartyOnePrefs = actualTpp - alpPrimaryPlusCoalitionLeakage;
    float actualPreferenceFlow = transformVoteShare(std::clamp(
      actualPartyOnePrefs / totalMinorPartyPrefs * 100.0f,
      0.1f, 99.9f));

    float deviation = actualPreferenceFlow - expectedPreferenceFlow;
    seat.livePreferenceFlowDeviation = deviation;
  }
}

void Election::prepareVariability() {
  // Because simulating variation per-booth is very time-consuming, we instead
  // do a smaller sample to establish the distribution at the seat level, then
  // use that for inter-simulation variation

  createRandomVariation = true;
  std::map<int, std::map<int, std::vector<float>>> seatFpResults; // by seat, then by party
  std::map<int, std::vector<float>> seatTppResults;
  std::map<int, std::vector<float>> seatTcpResults;
  // For testing purposes, keep number of iterations high (live should be more like 288 rather than 2400)
  int IterationsTarget = 288;
  int threadCount = 24;
  for (int iteration = 0; iteration < IterationsTarget / threadCount; ++iteration) {
    std::mutex seatResultsMutex;
    std::vector<std::thread> threads;

    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
      threads.emplace_back([&, threadIndex]() {
        auto sampleElection = *this;
        sampleElection.variabilitySampleIndex = iteration * threadCount + threadIndex;
        std::map<int, std::map<int, std::vector<float>>> tempSeatFpResults; // by seat, then by party
        std::map<int, std::vector<float>> tempSeatTppResults;
        std::map<int, std::vector<float>> tempSeatTcpResults;
        for (int seatIndex : std::ranges::views::iota(0, int(sampleElection.seats.size()))) {
          auto& seat = sampleElection.seats[seatIndex];
          // TODO: Something here to deal with the fact that existing booths won't necessarily be representative of the
          // remaining areas, so we need to implement some kind of systematic biases across the remaining booths
          // Need to know the base votes (the "best guess" without random variation) in order to determine the variation
          auto baseFpVoteCounts = std::map<int, float>();
          auto variedFpVoteCounts = std::map<int, float>();
          auto baseTppVoteCounts = std::map<int, float>();
          auto variedTppVoteCounts = std::map<int, float>();
          auto baseTcpVoteCounts = std::map<int, float>();
          auto variedTcpVoteCounts = std::map<int, float>();
          bool useTcp = seat.node.totalTcpVotesProjected() > 0.0f;
          for (int boothIndex : seat.booths) {
            sampleElection.recomposeBoothFpVotes(true, boothIndex);
            sampleElection.recomposeBoothTppVotes(true, boothIndex);
            sampleElection.recomposeBoothTcpVotes(boothIndex);
            auto const& booth = sampleElection.booths[boothIndex];
            for (auto const& [partyId, votes] : booth.node.fpVotesProjected) {
              if (std::isnan(votes)) continue;
              baseFpVoteCounts[partyId] += votes;
            }
            for (auto const& [partyId, votes] : booth.node.tempFpVotesProjected) {
              if (std::isnan(votes)) continue;
              variedFpVoteCounts[partyId] += votes;
            }
            for (auto const& [partyId, votes] : booth.node.tppVotesProjected) {
              if (std::isnan(votes)) continue;
              baseTppVoteCounts[partyId] += votes;
            }
            for (auto const& [partyId, votes] : booth.node.tempTppVotesProjected) {
              if (std::isnan(votes)) continue;
              variedTppVoteCounts[partyId] += votes;
            }
            if (useTcp) {
              for (auto const& [partyId, votes] : booth.node.tcpVotesProjected) {
                if (std::isnan(votes)) continue;
                baseTcpVoteCounts[partyId] += votes;
              }
              for (auto const& [partyId, votes] : booth.node.tempTcpVotesProjected) {
                if (std::isnan(votes)) continue;
                variedTcpVoteCounts[partyId] += votes;
              }
            }
          }
          auto totalBaseFpVotes = std::accumulate(baseFpVoteCounts.begin(), baseFpVoteCounts.end(), 0.0f, [](float sum, const auto& partyId) { return sum + partyId.second; });
          auto totalVariedFpVotes = std::accumulate(variedFpVoteCounts.begin(), variedFpVoteCounts.end(), 0.0f, [](float sum, const auto& partyId) { return sum + partyId.second; });
          for (auto const& [partyId, baseVoteCount] : baseFpVoteCounts) {
            float variedVoteCount = variedFpVoteCounts.at(partyId);
            if (baseVoteCount == 0.0f || variedVoteCount == 0.0f) continue; // if there are no votes for this party, then we can't calculate the variation
            float baseTransformedShare = transformVoteShare(baseVoteCount / totalBaseFpVotes * 100.0f);
            float variedTransformedShare = transformVoteShare(variedVoteCount / totalVariedFpVotes * 100.0f);
            tempSeatFpResults[seatIndex][partyId].push_back(variedTransformedShare - baseTransformedShare);
          }
          float totalBaseTppVotes = std::accumulate(baseTppVoteCounts.begin(), baseTppVoteCounts.end(), 0.0f, [](float sum, const auto& partyId) { return sum + partyId.second; });
          float totalVariedTppVotes = std::accumulate(variedTppVoteCounts.begin(), variedTppVoteCounts.end(), 0.0f, [](float sum, const auto& partyId) { return sum + partyId.second; });
          float baseTppTransformedShare = transformVoteShare(baseTppVoteCounts[0] / totalBaseTppVotes * 100.0f);
          float variedTppTransformedShare = transformVoteShare(variedTppVoteCounts[0] / totalVariedTppVotes * 100.0f);
          tempSeatTppResults[seatIndex].push_back(variedTppTransformedShare - baseTppTransformedShare);
          if (useTcp && baseTcpVoteCounts.size() >= 2) { // realignments may cause a TCP to be projected without any booths actually recording a tcp
            float totalBaseTcpVotes = std::accumulate(baseTcpVoteCounts.begin(), baseTcpVoteCounts.end(), 0.0f, [](float sum, const auto& partyId) { return sum + partyId.second; });
            float totalVariedTcpVotes = std::accumulate(variedTcpVoteCounts.begin(), variedTcpVoteCounts.end(), 0.0f, [](float sum, const auto& partyId) { return sum + partyId.second; });
            int arbitraryPartyId = baseTcpVoteCounts.begin()->first;
            float baseTcpTransformedShare = transformVoteShare(baseTcpVoteCounts[arbitraryPartyId] / totalBaseTcpVotes * 100.0f);
            float variedTcpTransformedShare = transformVoteShare(variedTcpVoteCounts[arbitraryPartyId] / totalVariedTcpVotes * 100.0f);
            tempSeatTcpResults[seatIndex].push_back(variedTcpTransformedShare - baseTcpTransformedShare);
          }
        }
        {
          std::lock_guard<std::mutex> lock(seatResultsMutex);
          for (auto const& [seatIndex, seatResults] : tempSeatFpResults) {
            for (auto const& [partyId, results] : seatResults) {
              seatFpResults[seatIndex][partyId].insert(seatFpResults[seatIndex][partyId].end(), results.begin(), results.end());
            }
          }
          for (auto const& [seatIndex, seatResults] : tempSeatTppResults) {
            seatTppResults[seatIndex].insert(seatTppResults[seatIndex].end(), seatResults.begin(), seatResults.end());
          }
          for (auto const& [seatIndex, seatResults] : tempSeatTcpResults) {
            seatTcpResults[seatIndex].insert(seatTcpResults[seatIndex].end(), seatResults.begin(), seatResults.end());
          }
        }
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }
  }
  
  for (auto const& [seatIndex, seatResults] : seatFpResults) {
    for (auto const& [partyId, results] : seatResults) {
      if (results.size()) {
        // assume mean is 0, any non-zero mean is due to rng
        float stdDev = std::sqrt(std::accumulate(
          results.begin(), results.end(), 0.0f, [](float sum, float result) { return sum + result * result; }
        ) / results.size());
        seats[seatIndex].fpAllBoothsStdDev[partyId] = stdDev;
      }
    }
    if (seatTppResults[seatIndex].size()) {
      float tppStdDev = std::sqrt(std::accumulate(
        seatTppResults[seatIndex].begin(), seatTppResults[seatIndex].end(), 0.0f, [](float sum, float result) { return sum + result * result; }
      ) / seatTppResults[seatIndex].size());
      seats[seatIndex].tppAllBoothsStdDev = tppStdDev;
    }
    if (seatTcpResults.contains(seatIndex) && seatTcpResults[seatIndex].size()) {
      float tcpStdDev = std::sqrt(std::accumulate(
        seatTcpResults[seatIndex].begin(), seatTcpResults[seatIndex].end(), 0.0f, [](float sum, float result) { return sum + result * result; }
      ) / seatTcpResults[seatIndex].size());
      seats[seatIndex].tcpAllBoothsStdDev = tcpStdDev;
    }
  }
  createRandomVariation = false;
}

void Election::generateVariability(int iterationIndex) {
  // generate seat-level variability using the parameters previously prepared
  // this simulates the variability caused by random booth results without
  // requiring a full recalculation of every booth

  variabilitySampleIndex = iterationIndex;

  for (auto [boothType, variation] : boothTypeTppBiasStdDev) {
    boothTypeTppIterationVariation[boothType] = variabilityNormal(
      0.0f, variation, int(boothType), 0, uint32_t(VariabilityTag::GenerateBoothTypeTppVariability)
    );
    boothTypeTppIterationVariation[boothType] *= 1 - std::exp(-node.fpCompletion * 50.0f);
  }
  for (auto [voteType, variation] : voteTypeTppBiasStdDev) {
    voteTypeTppIterationVariation[voteType] = variabilityNormal(
      0.0f, variation, int(voteType), 0, uint32_t(VariabilityTag::GenerateVoteTypeTppVariability)
    );
    voteTypeTppIterationVariation[voteType] *= 1 - std::exp(-node.fpCompletion * 50.0f);
  }
  for (auto [partyId, variations] : boothTypeFpBiasStdDev) {
    for (auto [boothType, variation] : variations) {
      boothTypeFpIterationVariation[partyId][boothType] = variabilityNormal(
        0.0f, variation, int(boothType), partyId, uint32_t(VariabilityTag::GenerateBoothTypeFpVariability)
      );
      boothTypeFpIterationVariation[partyId][boothType] *= 1 - std::exp(-node.fpCompletion * 50.0f);
    }
  }
  for (auto [partyId, variations] : voteTypeFpBiasStdDev) {
    for (auto [voteType, variation] : variations) {
      voteTypeFpIterationVariation[partyId][voteType] = variabilityNormal(
        0.0f, variation, int(voteType), partyId, uint32_t(VariabilityTag::GenerateVoteTypeFpVariability)
      );
      voteTypeFpIterationVariation[partyId][voteType] *= 1 - std::exp(-node.fpCompletion * 50.0f);
    }
  }
  nonClassicTppIterationVariation = variabilityNormal(
    0.0f, NonClassicTppVariabilityStdDev, 0, 0, uint32_t(VariabilityTag::GenerateNonClassicTppVariability)
  );

  for (int seatIndex = 0; seatIndex < int(seats.size()); ++seatIndex) {
    auto& seat = seats[seatIndex];
    // fp votes
    std::map<int, float> newFpVotesProjected;
    float totalFpProjectedVotes = std::accumulate(seat.node.fpVotesProjected.begin(), seat.node.fpVotesProjected.end(), 0.0f, [](float sum, const auto& pair) { return sum + pair.second; });
    for (auto const& [partyId, stdDev] : seat.fpAllBoothsStdDev) {
      float randomVariation = variabilityNormal(
        0.0f, stdDev, seatIndex, partyId, uint32_t(VariabilityTag::GenerateFpVariability)
      );
      // Random variability can be not-quite-symmetric due to the complexity of the simulation,
      // so this (and similar lines elsewhere) ensures that the variation is zero when completion is zero,
      // but rapidly transitions to full variability as completion increases
      randomVariation *= 1 - std::exp(-seat.node.fpCompletion * 50.0f);
      // If the party isn't found in the existing projection, that means it was converted
      // to the main independent, so use the independent party index instead
      int usePartyId = seat.node.fpVotesProjected.contains(partyId) ? partyId : run.indPartyIndex;
      float currentFpProjection = seat.node.fpVotesProjected.at(usePartyId);
      float transformedCurrentFpProjection = transformVoteShare(currentFpProjection / totalFpProjectedVotes * 100.0f);
      float transformedNewFpProjection = transformedCurrentFpProjection + randomVariation;
      float withBoothTypeBias = transformedNewFpProjection;
      if (boothTypeFpIterationVariation.contains(partyId)) {
        for (auto [boothType, variation] : boothTypeFpIterationVariation.at(partyId)) {
          if (!seat.fpBoothTypeSensitivity.contains(partyId) || !seat.fpBoothTypeSensitivity[partyId].contains(boothType)) continue;
          float sensitivity = seat.fpBoothTypeSensitivity[partyId][boothType] / seat.node.totalVotesPrevious();
          // See comments in the TPP section for explanation of the sensitivity scaling
          sensitivity *= obsWeight(node.fpConfidence);
          withBoothTypeBias += variation * sensitivity;
        }
      }
      float withVoteTypeBias = withBoothTypeBias;
      if (voteTypeFpIterationVariation.contains(partyId)) {
        for (auto [voteType, variation] : voteTypeFpIterationVariation.at(partyId)) {
          if (!seat.fpVoteTypeSensitivity.contains(partyId) || !seat.fpVoteTypeSensitivity[partyId].contains(voteType)) continue;
          float sensitivity = seat.fpVoteTypeSensitivity[partyId][voteType] / seat.node.totalVotesPrevious();
          // See comments in the TPP section for explanation of the sensitivity scaling
          sensitivity *= obsWeight(node.fpConfidence);
          withVoteTypeBias += variation * sensitivity;
        }
      }
      float newFpProjection = detransformVoteShare(withVoteTypeBias) * 0.01f * totalFpProjectedVotes;
      newFpVotesProjected[usePartyId] = newFpProjection;
    }
    float totalNewFpProjectedVotes = std::accumulate(newFpVotesProjected.begin(), newFpVotesProjected.end(), 0.0f, [](float sum, const auto& pair) { return sum + pair.second; });
    float normalisationFactor = totalFpProjectedVotes / totalNewFpProjectedVotes; 
    for (auto const& [partyId, newFpProjection] : newFpVotesProjected) {
      seat.node.fpVotesProjected[partyId] = newFpProjection * normalisationFactor;
    }

    float totalTppProjectedVotes = std::accumulate(seat.node.tppVotesProjected.begin(), seat.node.tppVotesProjected.end(), 0.0f, [](float sum, const auto& pair) { return sum + pair.second; });
    float randomTppVariation = variabilityNormal(
      0.0f, seat.tppAllBoothsStdDev, seatIndex, 0, uint32_t(VariabilityTag::GenerateTppVariability)
    );
    randomTppVariation *= 1 - std::exp(-seat.node.tppCompletion * 50.0f);
    float currentTppProjection = seat.node.tppVotesProjected.at(0);
    float transformedCurrentTppProjection = transformVoteShare(currentTppProjection / totalTppProjectedVotes * 100.0f);
    float transformedNewTppProjection = transformedCurrentTppProjection + randomTppVariation;
    float withBoothTypeBias = transformedNewTppProjection;
    for (auto [boothType, variation] : boothTypeTppIterationVariation) {
      float sensitivity = seat.tppBoothTypeSensitivity[boothType] / seat.node.totalVotesPrevious();
      // Sensitivity is scaled by the election's confidence. We need scaling because the
      // variation is applied on top of the existing projection, so if confidence is zero/low
      // this should not change the forecast much from the baseline.
      // For now, use election-wide confidence as the booth sensitivity is an election-level measure
      // but could revisit this later
      sensitivity *= obsWeight(node.tppConfidence);
      withBoothTypeBias += variation * sensitivity;
    }
    float withVoteTypeBias = withBoothTypeBias;
    for (auto [voteType, variation] : voteTypeTppIterationVariation) {
      float sensitivity = seat.tppVoteTypeSensitivity[voteType] / seat.node.totalVotesPrevious();
      sensitivity *= obsWeight(node.tppConfidence);
      withVoteTypeBias += variation * sensitivity;
    }
    // This must eventually check for feed-derived TPP results for non-classic seats once those are included in the simulation
    bool const usesEstimatedClassicTpp = !isTppSet(seat.node.tcpVotesCurrent, natPartyIndex);
    float withNonClassicVariability = withVoteTypeBias;
    if (usesEstimatedClassicTpp) {
      // General TPP variability and the measured non-classic bias have
      // different evidence bases, so phase them in independently.
      float const variabilityWeight = obsWeight(node.tppConfidence);
      float const biasWeight = obsWeight(nonClassicTppBiasConfidence);
      withNonClassicVariability += nonClassicTppIterationVariation * variabilityWeight;
      // Ideally the bias for non-classic seats should be applied at a booth level, not here
      // but it will do as a quick stop-gap
      // The 0.8f reflects that non-classic seats may not follow the same patterns as classic seats (calibrated to 2025fed)
      float const biasAdjustedShare = predictorCorrectorTransformedSwing(
        detransformVoteShare(withNonClassicVariability),
        -nonClassicTppBiasPercentagePoints * biasWeight * 0.8f);
      withNonClassicVariability = transformVoteShare(biasAdjustedShare);
    }
    float newTppProjection = detransformVoteShare(withNonClassicVariability) * 0.01f * totalTppProjectedVotes;
    seat.node.tppVotesProjected[0] = newTppProjection;
    for (auto const& [partyId, votes] : seat.node.tppVotesProjected) {
      if (partyId != 0)  seat.node.tppVotesProjected[partyId] = totalTppProjectedVotes - newTppProjection;
    }

    if (seat.tcpAllBoothsStdDev.has_value()) {
      float totalTcpProjectedVotes = std::accumulate(seat.node.tcpVotesProjected.begin(), seat.node.tcpVotesProjected.end(), 0.0f, [](float sum, const auto& pair) { return sum + pair.second; });
      float randomTcpVariation = variabilityNormal(
        0.0f, seat.tcpAllBoothsStdDev.value(), seatIndex, 0, uint32_t(VariabilityTag::GenerateTcpVariability)
      );
      randomTcpVariation *= 1 - std::exp(-seat.node.tcpCompletion * 50.0f);
      float arbitraryPartyId = seat.node.tcpVotesProjected.begin()->first;
      float currentTcpProjection = seat.node.tcpVotesProjected.at(arbitraryPartyId);
      float transformedCurrentTcpProjection = transformVoteShare(currentTcpProjection / totalTcpProjectedVotes * 100.0f);
      float transformedNewTcpProjection = transformedCurrentTcpProjection + randomTcpVariation;
      float newTcpProjection = detransformVoteShare(transformedNewTcpProjection) * 0.01f * totalTcpProjectedVotes;
      seat.node.tcpVotesProjected[arbitraryPartyId] = newTcpProjection;
      seat.node.tcpShares[arbitraryPartyId] = transformVoteShare(newTcpProjection / totalTcpProjectedVotes * 100.0f);
      for (auto const& [partyId, votes] : seat.node.tcpVotesProjected) {
        if (partyId == arbitraryPartyId) continue;
        seat.node.tcpVotesProjected[partyId] = totalTcpProjectedVotes - newTcpProjection;
        seat.node.tcpShares[partyId] = -seat.node.tcpShares[arbitraryPartyId];
      }
    }
  }

  // Recompose large-scale results
  for (int largeRegionIndex : std::ranges::views::iota(0, int(largeRegions.size()))) {
    recomposeLargeRegionFpVotes(largeRegionIndex);
    recomposeLargeRegionTppVotes(largeRegionIndex);
  }
  recomposeElectionFpVotes();
  recomposeElectionTppVotes();

  // Determine final deviations so that the simulation can actually use the varied data
  determineElectionFinalFpDeviations(true);
  determineElectionFinalTppDeviation(true);
  for (int largeRegionIndex : std::ranges::views::iota(0, int(largeRegions.size()))) {
    determineLargeRegionFinalFpDeviations(true, largeRegionIndex);
    determineLargeRegionFinalTppDeviation(true, largeRegionIndex);
  }

  for (int seatIndex : std::ranges::views::iota(0, int(seats.size()))) {
    determineSeatFinalFpDeviations(true, seatIndex);
    determineSeatFinalTppDeviation(true, seatIndex);
  }
}

int Election::mapPartyId(
  int ecCandidateId,
  bool isPrevious,
  Results2::Election const& previousElection,
  Results2::Election const& currentElection) {
  // Note: only used when initially loading data from EC files
  // so not a bottleneck

  // Helper function to get next available ID
  auto getNextId = [this]() {
    // Dynamic live-only parties must not collide with configured project
    // parties that happen to be absent from both election result files.
    int maxId = project.parties().count() - 1;
    for (auto const& [_, mappedId] : ecPartyToInternalParty) {
      maxId = std::max(maxId, mappedId);
    }
    if (maxId + 1 >= IndependentPartyIdOffset) {
      throw std::runtime_error(
        "No internal party IDs remain below the independent-candidate range.");
    }
    return maxId + 1;
  };

  // Helper function to process a party from any election
  auto processParty = [this, &getNextId]
    (const Results2::Party& party, int ecPartyId) -> std::optional<int> {
    // Check if we've seen this abbreviation before
    auto abbrevIt = ecAbbreviationToInternalParty.find(party.shortCode);
    if (abbrevIt != ecAbbreviationToInternalParty.end()) {
      // Reuse the same internal ID for parties with same short code
      ecPartyToInternalParty[ecPartyId] = abbrevIt->second;
      return abbrevIt->second;
    }
    
    // New party abbreviation - assign a new internal ID
    int newId = getNextId();
    ecPartyToInternalParty[ecPartyId] = newId;
    ecAbbreviationToInternalParty[party.shortCode] = newId;
    return newId;
  };

  int ecPartyId = ecCandidateId;
  // Since a candidate can change party from one election to the next,
  // and we shouldn't be comparing those votes directly, we need to
  // choose the correct election to look in
  auto candidateIt = currentElection.candidates.find(ecCandidateId);
  if (isPrevious) {
      candidateIt = previousElection.candidates.find(ecCandidateId);
  }
  if (!isPrevious && candidateIt == currentElection.candidates.end()) {
    candidateIt = previousElection.candidates.find(ecCandidateId);
    if (candidateIt == previousElection.candidates.end()) {
      logger << "Warning: Candidate ID " << ecCandidateId << " not found in either current or previous election data\n";
      return -1;
    }
  } else if (isPrevious && candidateIt == previousElection.candidates.end()) {
    candidateIt = currentElection.candidates.find(ecCandidateId);
    if (candidateIt == currentElection.candidates.end()) {
      logger << "Warning: Candidate ID " << ecCandidateId << " not found in either current or previous election data\n";
      return -1;
    }
  }
  ecPartyId = candidateIt->second.party;

  // Independent candidates are given a new ID so they don't clash with real parties
  // Don't add this to ecPartyToNetParty as other candidates should never be mapped to this ID
  // (doing so would also break the generation of new party IDs)
  if (ecPartyId == Results2::Candidate::Independent) {
    return ecCandidateId + IndependentPartyIdOffset;
  }

  // Check if we've already mapped this EC party ID
  auto it = ecPartyToInternalParty.find(ecPartyId);
  if (it != ecPartyToInternalParty.end()) {
    return it->second;
  }
  
  // Check if party is in the current election data
  if (currentElection.parties.contains(ecPartyId)) {
    return processParty(currentElection.parties.at(ecPartyId), ecPartyId).value();
  }
  
  // Check if party is in the previous election data
  if (previousElection.parties.contains(ecPartyId)) {
    return processParty(previousElection.parties.at(ecPartyId), ecPartyId).value();
  }
  
  // 3. Failsafe: Party not found in either election
  logger << "Warning: Party ID " << ecPartyId << " not found in either current or previous election data\n";
  int newId = getNextId();
  ecPartyToInternalParty[ecPartyId] = newId;
  return newId;
}

float Election::variabilityNormal(float mean, float sd, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const {
  std::uint64_t key = variabilityBaseSeed;
  key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(variabilitySampleIndex));
  key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(itemIndex));
  key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(partyId));
  key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(tag));
  return RandomGenerator::normal_from_key(key, mean, sd);
}

void Election::log(bool includeLargeRegions, bool includeSeats, bool includeBooths) const {
  logger << "\nElection:\n";
  logger << "Projected 2PP: " << node.tppVotesProjected.at(0) / (node.tppVotesProjected.at(0) + node.tppVotesProjected.at(1)) * 100.0f << "\n";
  PA_LOG_VAR(finalSpecificFpDeviations);
  PA_LOG_VAR(finalSpecificTppDeviation);
  PA_LOG_VAR(offsetSpecificFpDeviations);
  PA_LOG_VAR(offsetSpecificTppDeviation);
  node.log();
  if (includeLargeRegions) {
    for (auto const& largeRegion : largeRegions) {
      largeRegion.log(*this, includeSeats, includeBooths);
    }
  }
}
