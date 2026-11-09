#include "LiveV2.h"

#include "General.h"
#include "PollingProject.h"
#include "RandomGenerator.h"
#include "SpecialPartyCodes.h"
#include "LiveTurnoutMath.h"
#include "LivePartyMapping.h"
#include "LiveTppSensitivity.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <mutex>
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

constexpr double VoteObsWeightStrength = 250.0;
constexpr double PreferenceFlowObsWeightStrength = 200.0;
// Operator notices concern materially limited historical evidence. A tiny
// genuine unanimous pool can be weak information without being a data error.
// This vote-scale threshold controls notices only, not the reliability fade.
constexpr double HistoricalPreferenceWarningVotes = 10;
constexpr double CoalitionLeakagePercent = 20.0;
constexpr double PreviousTotalVotesGuess = 500.0;
constexpr double HospitalBoothVotesGuess = 50.0;
constexpr double DifferentSeatRelevanceModifier = 0.1;
// Effective breadth scale for the retained inferred-TPP bias diagnostic.
constexpr double NonClassicBiasEffectiveBoothScale = 5.0;
constexpr double MaxPreferenceVoteTotalDifference = double(LivePartialCount::ComparableTotalFraction);
constexpr int VariabilityPreparationSamples = 288;
constexpr int VariabilityPreparationThreads = 24;

// Keep LiveV2 draws independent from other deterministic RNG users whose tag
// enums start at the same values and otherwise produce identical keys.
constexpr std::uint64_t LiveVariabilityDomain = 0x4c69766556320001ULL;

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

double expectedVotesForAggregation(Node const& node) {
  double const projectedVotes = node.totalFpVotesProjected();
  if (projectedVotes > 0.0) return projectedVotes;

  int const previousVotes = node.totalVotesPrevious();
  if (previousVotes > 0) return double(previousVotes);

  // Newly created booths have no historical size. Their current count is the
  // best available lower-bound estimate and must not be silently weighted zero.
  return double(node.totalFpVotesCurrent());
}

std::map<int, double> projectedVoteShares(
  std::map<int, double> const& projectedVotes,
  std::string_view scope) {
  double totalVotes = 0.0;
  for (auto const& [partyId, votes] : projectedVotes) {
    if (!std::isfinite(votes) || votes < 0.0) {
      throw std::runtime_error(
        std::string(scope) + " has invalid projected FP votes for party "
        + std::to_string(partyId) + ".");
    }
    totalVotes += votes;
  }
  if (!std::isfinite(totalVotes) || totalVotes <= 0.0) {
    throw std::runtime_error(
      std::string(scope) + " has no positive projected FP vote total.");
  }

  std::map<int, double> shares;
  for (auto const& [partyId, votes] : projectedVotes) {
    shares[partyId] = votes / totalVotes;
  }
  return shares;
}

std::optional<double> projectedTppAlpShare(
  std::map<int, double> const& projectedVotes,
  std::string_view scope) {
  double totalVotes = 0.0;
  for (auto const& [partyId, votes] : projectedVotes) {
    if (!std::isfinite(votes) || votes < 0.0) {
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
  if (totalVotes == 0.0) return std::nullopt;
  if (!projectedVotes.contains(0) || !projectedVotes.contains(1)) {
    throw std::runtime_error(
      std::string(scope) + " does not contain both major-party TPP votes.");
  }
  return projectedVotes.at(0) / totalVotes;
}


}

double obsWeight(double confidence, double strength = VoteObsWeightStrength) {
  // TODO: Calibrate this confidence-to-weight curve against historical live
  // counts. Its shape and the level-specific strengths are currently heuristic.
  return LiveSimulationMath::observationWeight(confidence, strength);
}

double variabilityProgressWeight(double completion) {
  if (!std::isfinite(completion)) {
    throw std::runtime_error(
      "Cannot weight variability using non-finite completion.");
  }
  // This fast phase-in preserves exact zero-results behaviour. Its shape is a
  // heuristic and should eventually be calibrated from historical snapshots.
  return 1.0 - std::exp(-std::clamp(completion, 0.0, 1.0) * 50.0);
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
  GenerateFpVariability = 11,
  GenerateTcpVariability = 13,
  GenerateBoothTypeFpVariability = 16,
  GenerateVoteTypeFpVariability = 17,
  GenerateFpEvidenceEndpoint = 18,
  RevisedTcpTransfers = 73,
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

double Node::totalFpVotesProjected() const {
  return std::accumulate(fpVotesProjected.begin(), fpVotesProjected.end(), 0.0,
    [](double sum, const auto& pair) { return sum + pair.second; });
}

double Node::totalTcpVotesProjected() const {
  return std::accumulate(tcpVotesProjected.begin(), tcpVotesProjected.end(), 0.0,
    [](double sum, const auto& pair) { return sum + pair.second; });
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
    // Usually each candidate maps to a different modelling party. Configured
    // aliases can combine parties, so add their counts rather than allowing
    // the last candidate to replace votes already assigned to that group.
    for (auto const& [id, votes] : currentVotes) {
      int mappedPartyId = partyMapper(id, false);
      currentMap[mappedPartyId] += votes;
    }

    // Extract votes from previous booth if available
    if (previousVotes) {
      for (auto const& [id, votes] : *previousVotes) {
        int mappedPartyId = partyMapper(id, true);
        previousMap[mappedPartyId] += votes;
      }
    }

    if (isTcp) {
      rejectTcpWithoutFpCandidates(
        previousMap, node.fpVotesPrevious, name, "previous-election");
    }

    for (auto const& [partyId, votes] : currentMap) {
      node.runningParties.insert(partyId);
    }

    // Current records were validated and recovered before party mapping.
    // Keep historical comparability checks above, but do not silently alter
    // this shared effective account or omit its operator warning here.
    int totalCurrentVotesCounted = std::accumulate(currentMap.begin(), currentMap.end(), 0,
      [](int sum, const auto& pair) { return sum + pair.second; });

    // FP and TCP shares must each use the total from their own count. In
    // particular, savings provisions can make the two totals differ.
    double totalCurrentVotes = static_cast<double>(totalCurrentVotesCounted);
    double totalPreviousVotes = static_cast<double>(std::accumulate(
      previousMap.begin(), previousMap.end(), 0,
      [](int sum, const auto& pair) { return sum + pair.second; }));

    // Calculate shares and swings
    if (totalCurrentVotes > 0) {
      for (auto const& [partyId, votes] : currentMap) {
        // Smooth only analytical FP proportions. TCP and integer observations
        // retain their recorded values and their own count denominator.
        double currentShare = isTcp ? static_cast<double>(votes) / totalCurrentVotes * 100.0 :
          double(100*LiveNewVotes::smoothedShare(votes,totalCurrentVotes,currentMap.size()));
        double currentTransformed = isTcp ? transformVoteShare(std::clamp(currentShare, 0.01, 99.99)) :
          double(25*LivePartialCount::logOdds(currentShare*.01));
        sharesMap[partyId] = currentTransformed;
        if (previousMap.contains(partyId) && totalPreviousVotes > 0) {
          double previousShare = static_cast<double>(previousMap.at(partyId)) / totalPreviousVotes * 100.0;
          double previousTransformed = transformVoteShare(std::clamp(previousShare, 0.01, 99.99));
          double change = currentTransformed - previousTransformed;
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

  if (currentBooth.incrementalFp) {
    // Before the turnout account is prepared, historical size supplies only a
    // provisional progress denominator for mixed early/day places. A first day
    // batch must not enter the initial pooled swings as a completed early place.
    // refreshFpBoothProgress replaces this denominator with the common turnout
    // target once available; the published candidate counts are unchanged.
    double expected = double(std::max(node.totalVotesPrevious(),node.totalFpVotesCurrent()));
    double fpProgress = expected > 0 ? double(node.totalFpVotesCurrent())/expected : 0;
    double tcpProgress = expected > 0 ? std::min(1.0,double(node.totalTcpVotesCurrent())/expected) : 0;
    node.fpConfidence *= fpProgress; node.fpCompletion *= fpProgress;
    node.tcpConfidence *= tcpProgress; node.tcpCompletion *= tcpProgress;
    node.tppConfidence *= tcpProgress; node.tppCompletion *= tcpProgress;
  }

  // For booths previously different seat, we reduce their relevance for projections
  // as they are less likely to be indicative of the remaining votes in the seat
  // than booths from the same seat
  node.relevanceModifier = sameSeat ? 1.0 : DifferentSeatRelevanceModifier;
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
  sameSeat(sameSeat)
{
  auto processVotes = [this, &partyMapper, voteType](
    Results2::Seat::VotesByType const& currentVotes,
    std::optional<Results2::Seat::VotesByType const*> previousVotes,
    auto& currentMap, auto& previousMap, auto& sharesMap, auto& swingsMap, bool isTcp = false)
    {
      // Incremental categories use the same grouping as ordinary booths:
      // preserve all candidate votes when configured aliases share a party.
      for (auto const& [partyId, votes] : currentVotes) {
        auto const voteIt = votes.find(voteType);
        if (voteIt == votes.end()) continue;
        int mappedPartyId = partyMapper(partyId, false);
        currentMap[mappedPartyId] += voteIt->second;
      }
      // Extract votes from previous booth if available
      if (previousVotes) {
        for (auto const& [partyId, votes] : *(previousVotes.value())) {
          if (!votes.contains(voteType)) continue;
          int mappedPartyId = partyMapper(partyId, true);
          previousMap[mappedPartyId] += votes.at(voteType);
        }
      }

      if (isTcp) {
        rejectTcpWithoutFpCandidates(
          previousMap, node.fpVotesPrevious, name, "previous-election");
      }

      for (auto const& [partyId, votes] : currentMap) {
        node.runningParties.insert(partyId);
      }

      // Partial declaration preferences remain valid measured counts. Current
      // contradictions have already been handled by the shared input boundary.
      // Calculate total votes for percentages
      // Need to actually calculate this because the fp and tcp can be legitimately different for incremental booths
      double totalCurrentVotes = static_cast<double>(std::accumulate(currentMap.begin(), currentMap.end(), 0,
        [](int sum, const auto& pair) { return sum + pair.second; }));
      double totalPreviousVotes = static_cast<double>(std::accumulate(previousMap.begin(), previousMap.end(), 0,
        [](int sum, const auto& pair) { return sum + pair.second; }));
      // Calculate shares and swings
      if (totalCurrentVotes > 0) {
        for (auto const& [partyId, votes] : currentMap) {
          if (isTcp && (votes == 0 || votes >= totalCurrentVotes)) {
            continue;
          }
          // A tiny first declaration batch must provide finite, weak FP
          // evidence even when a candidate happens to receive no votes yet.
          double share = isTcp ? static_cast<double>(votes) / totalCurrentVotes * 100.0 :
            double(100*LiveNewVotes::smoothedShare(votes,totalCurrentVotes,currentMap.size()));
          double currentTransformed = isTcp ? transformVoteShare(share) :
            double(25*LivePartialCount::logOdds(share*.01));
          sharesMap[partyId] = currentTransformed;
          if (previousMap.contains(partyId) && totalPreviousVotes > 0) {
            if (previousMap.at(partyId) == 0 || previousMap.at(partyId) >= totalPreviousVotes) {
              continue;
            }
            double previousTransformed = transformVoteShare(static_cast<double>(previousMap.at(partyId)) / totalPreviousVotes * 100.0);
            double change = currentTransformed - previousTransformed;
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
  double const previousVoteTotal = double(node.totalVotesPrevious());
  auto progressFromPrevious = [previousVoteTotal](int currentVotes) {
    if (previousVoteTotal <= 0.0) return 0.0;
    return std::min(double(currentVotes) / previousVoteTotal, 0.95);
  };
  node.fpConfidence = progressFromPrevious(node.totalFpVotesCurrent());
  // If we can't compare tcp swings due to difference matchup, keep tcp confidence to zero 
  node.tcpConfidence = node.tcpSwings.size()
    ? progressFromPrevious(node.totalTcpVotesCurrent())
    : 0.0;
  node.tppConfidence = std::min(
    std::max(
      // TCP vote progress should only be counted as TPP confidence if it's actually a TPP matchup
      isTppSet(node.tcpVotesCurrent, natPartyIndex)
        ? progressFromPrevious(node.totalTcpVotesCurrent())
        : 0.0,
      progressFromPrevious(node.totalFpVotesCurrent()) * 0.5
    ),
    0.95
  );

  node.fpCompletion = node.fpConfidence;
  node.tcpCompletion = node.tcpConfidence;
  // The same as tppConfidence but without the approximation from FPs
  node.tppCompletion = isTppSet(node.tcpVotesCurrent, natPartyIndex)
    ? progressFromPrevious(node.totalTcpVotesCurrent())
    : 0.0;

  // For booths previously different seat, we reduce their relevance for projections
  // as they are less likely to be indicative of the remaining votes in the seat
  // than booths from the same seat
  node.relevanceModifier = sameSeat ? 1.0 : DifferentSeatRelevanceModifier;
}

std::optional<double> Election::rawPendingFirstVotes(Booth const& booth) const {
  auto const& n = booth.node;
  double const tcpVotes = n.totalTcpVotesCurrent();
  double const pending = n.totalFpVotesCurrent()-tcpVotes;
  if (tcpVotes <= 0 || pending <= 0 || !isTppSet(n.tcpVotesCurrent,natPartyIndex)) return {};
  // Match projectCountedTpp/projectDeclarationTpp's received-vote accounting.
  // Counted preferences stay fixed; only FP awaiting preferences is inferred,
  // using the same historical/pooled flows and retained matching FP/TCP cohort.
  // This deliberately excludes the forecast's not-yet-received FP voters.
  if (optionalPreferential) return pending*n.tcpVotesCurrent.at(0)/tcpVotes;
  int const coalition = n.tcpVotesCurrent.contains(natPartyIndex) ? natPartyIndex : 1;
  return LivePartialCount::pendingFirst(n.fpVotesCurrent,n.tcpVotesCurrent,
    classicPreferenceFlows(booth,coalition),preferencePriorOffset(booth),
    booth.preferenceEvidence,0,coalition);
}

void Election::prepareTcpBases(Results2::Election const& previousElection,
  Results2::Election const& currentElection, LiveInputRecovery::PreferenceHistories const& history) {
  // Map original counted accounts before any preference-flow training. Keep
  // candidate identity until this boundary, including independent identities.
  auto mapCounts = [&](auto const& counts) {
    LivePartialCount::Counts mapped;
    for (auto const& [candidate,votes] : counts)
      mapped[mapPartyId(candidate,false,previousElection,currentElection)] += votes;
    return mapped;
  };
  for (auto& booth : preparationBooths()) {
    auto role = booth.voteType != Results2::VoteType::Ordinary ? "declaration" :
      booth.boothType == Results2::Booth::Type::Ppvc ? "ppvc" : "ordinary";
    auto found = history.find(seats.at(booth.parentSeatId).name+"/"+role+"/"+booth.name);
    if (found == history.end()) continue;
    auto const& source = found->second.tcpBasis ? found->second.tcpBasis : found->second.matchingCurrentTcp;
    if (!source) continue;
    LivePartialCount::TcpBasis basis{mapCounts(source->fp),mapCounts(source->tcp),{},source->recheckedReconciledPair};
    for (auto const& [candidate,counts] : source->transfers) {
      auto& target = basis.transfers[mapPartyId(candidate,false,previousElection,currentElection)];
      for (auto const& [finalist,votes] : mapCounts(counts)) target[finalist] += votes;
    }
    if (LivePartialCount::validBasis(basis) && basis.tcp == booth.node.tcpVotesCurrent)
      booth.tcpBasis = std::move(basis);
  }
}

std::optional<LivePartialCount::RevisedTcp> Election::revisedBoothTcp(Booth const& booth,
  int first, int second, double variation) const {
  if (optionalPreferential || !booth.hasTcpRevision()) return {};
  auto flows = first == 0 && (second == 1 || second == natPartyIndex)
    ? classicPreferenceFlows(booth,second) : pairPreferenceFlows(booth,first,second);
  auto const& basis = *booth.tcpBasis;
  // With no per-candidate transfers, calibrate the existing flow assumptions
  // against the original pair. Use the same assumptions on both sides of the
  // revision, so unchanged ballots cancel rather than acquiring a new mean.
  auto evidence = LivePartialCount::measure(basis.fp,basis.tcp,flows,first,second);
  double prior = first == 0 && (second == 1 || second == natPartyIndex) ? preferencePriorOffset(booth) : 0;
  double offset = LivePartialCount::evidenceOffset(prior,evidence,first,second);
  return LivePartialCount::reviseTcp(booth.node.fpVotesCurrent,booth.node.tcpVotesCurrent,
    basis,flows,offset,first,second,variation);
}

std::optional<LiveTppIntegration::ReceivedTpp> Election::receivedBoothTpp(Booth const& booth) const {
  auto const& n = booth.node;
  double tcp = isTppSet(n.tcpVotesCurrent,natPartyIndex) ? n.totalTcpVotesCurrent() : 0;
  if (tcp > 0) if (auto revised = revisedBoothTcp(booth,0,coalitionFinalist(booth))) {
    double first = revised->first, total = revised->votes;
    double uncertain = revised->uncertainVotes();
    // The whole booth has not become inferred. Only the feasible revised
    // transfer interval is unresolved; known finalist corrections stay exact.
    // A batch-scale allowance prevents disproportionate corrections acquiring
    // binomial precision merely because many ballots were revised together.
    double jacobian = 25*(1/(first+.25)+1/(total-first+.25));
    return LiveTppIntegration::ReceivedTpp{total,25*std::log((first+.25)/(total-first+.25)),
      uncertain/total,LiveTppIntegration::samplingVariance(total,first)+std::pow(jacobian*uncertain/2,2),
      first-revised->lower};
  }
  return LiveTppIntegration::receivedComparison(n.totalFpVotesCurrent(),tcp,tcp > 0 ? n.tcpVotesCurrent.at(0) : 0,
    n.tppShare,rawPendingFirstVotes(booth),optionalPreferential);
}

double Election::getSeatRawTppSwing(std::string const& seatName) const {
  auto seat = std::find_if(seats.begin(),seats.end(),[&](auto const& s) { return s.name == seatName; });
  if (seat == seats.end()) return std::numeric_limits<double>::quiet_NaN();
  LiveData::RawTppSwing swing;
  auto percent = [](std::optional<double> share) -> std::optional<double> {
    return share ? std::optional<double>(detransformVoteShare(*share)) : std::nullopt;
  };
  for (int index : seat->booths) {
    auto const& booth = readBooths().at(index);
    auto const& n = booth.node;
    // The raw booth shares are established from received TCP or FP before
    // turnout recomposition and forecast blending. Using projected vote totals
    // here would reintroduce the prior-heavy seat estimate into the raw column.
    if (auto revised = revisedBoothTcp(booth,0,coalitionFinalist(booth))) {
      swing.add({},n.tcpVotesPrevious,natPartyIndex,revised->votes,
        100*revised->first/revised->votes,percent(n.tppSharePrevious));
      continue;
    }
    swing.add(n.tcpVotesCurrent,n.tcpVotesPrevious,natPartyIndex,
      n.totalFpVotesCurrent(),percent(n.tppShare),percent(n.tppSharePrevious),
      rawPendingFirstVotes(booth));
  }
  return swing.value().value_or(std::numeric_limits<double>::quiet_NaN());
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
    node.tppSharePrevious = transformVoteShare(std::clamp(static_cast<double>(node.tcpVotesPrevious.at(0)) / static_cast<double>(node.totalTcpVotesPrevious()) * 100.0, 1.0, 99.0));
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
  if (coords) {
    logger << "Coordinates: (" << coords->first << ", " << coords->second << ")\n";
  }
  else {
    logger << "Coordinates: (not available)\n";
  }
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
  PA_LOG_VAR(estimatedTppInfluence);
  PA_LOG_VAR(tcpAllBoothsStdDev);
  PA_LOG_VAR(independentPartyIndex);
  PA_LOG_VAR(tcpFocusPartyIndex);
  PA_LOG_VAR(tcpFocusPartyPrefFlow);
  PA_LOG_VAR(tcpFocusPartyConfidence);
  PA_LOG_VAR(nationalsProportion);

  node.log();
  if (includeBooths) {
    for (auto const& booth : booths) {
      election.readBooths().at(booth).log();
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

LiveV2::Election LiveV2::Election::copyForPreparation() const {
  boothStorage.requirePreparing();
  return Election(*this);
}

std::unique_ptr<LiveV2::Election> LiveV2::Election::copyForScenario() const {
  boothStorage.requireFrozen();
  // The private copy constructor preserves every field and all indices. Its
  // frozen booth owner is shared; mutable seat/region projections and draw
  // maps are ordinary value copies, just as before this optimisation.
  return std::unique_ptr<Election>(new Election(*this));
}

std::unique_ptr<LiveData::Provider> LiveV2::Election::generateScenario(
  int iterationIndex) const {
  auto newElection = copyForScenario();

  newElection->drawTurnoutCounts(iterationIndex);
  newElection->generateVariability(iterationIndex);

  return newElection;
}

int LiveV2::Election::findSeatIndex(std::string const& seatName) const {
  // Completed models already share turnout's checked name-to-index map. An
  // absent/inactive district returns the same end index as the original scan.
  // Keep the scan available during construction, before party composition has
  // attached that index; this fallback stores no partial or scenario values.
  if (turnout && turnout->hasComposition()) {
    auto index = turnout->findSeatIndex(seatName);
    return index ? int(*index) : int(seats.size());
  }
  return int(std::find_if(seats.begin(), seats.end(),
    [&seatName](Seat const& s) { return s.name == seatName; }) - seats.begin());
}

LiveV2::Election::FloatInformation LiveV2::Election::getSeatOthersInformation(std::string const& seatName, std::map<int, double> const& representedParties) const {
  int seatIndex = findSeatIndex(seatName);
  if (seatIndex != int(seats.size())) {
    double othersVotes = 0.0;
    double totalVotes = seats[seatIndex].node.totalFpVotesProjected();
    if (totalVotes <= 0.0) {
      return {0.0, seats[seatIndex].node.fpCompletion,
        seats[seatIndex].node.fpConfidence};
    }
    for (auto [partyIndex, votes] : seats[seatIndex].node.fpVotesProjected) {
      bool isRepresented = representedParties.contains(partyIndex);
      // This is a partition of the current scenario, not a decision about
      // which independent was large enough to model before results arrived.
      // An explicitly represented candidate cannot also be inside Others.
      // Unrepresented candidates belong there even when their share is large.
      bool isOthers = !isRepresented;
      if (isOthers && votes > 0.0) {
        othersVotes += votes;
      }
    }
    double othersShare = othersVotes / totalVotes * 100.0;
    return {othersShare, seats[seatIndex].node.fpCompletion, seats[seatIndex].node.fpConfidence};
  }
  return {0.0, 0.0, 0.0};
}

std::vector<LiveV2::Election::BoothSnapshot> LiveV2::Election::getBoothSnapshots() const {
  std::vector<BoothSnapshot> snapshots;
  snapshots.reserve(readBooths().size());
  for (auto const& booth : readBooths()) {
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

LiveV2::Election::Election(Results2::Election const& previousElection, Results2::Election const& currentElection, PollingProject& project, Simulation& sim, SimulationRun& run, TurnoutModelIO::Artifact turnoutInput,
  LiveInputRecovery::PreferenceHistories preferenceHistory)
	: optionalPreferential(turnoutInput.optionalPreferential), project(project), sim(sim), run(run)
{
  configureInactiveTurnoutContests(turnoutInput);
  getNatPartyIndex();
  loadEstimatedPreferenceFlows();
  initializePartyMappings(previousElection, currentElection);
  // Both complete historical categories and matched booths need the same
  // previous-election preference information. Read it once before constructing
  // either account; this local preparation input is not retained by scenarios.
  auto partyFlowHistory = readPartyConversionHistory(previousElection,currentElection);
  auto notionalHistory = readNotionalPreferenceHistory(previousElection,currentElection);
  createNodesFromElectionData(previousElection, currentElection, partyFlowHistory, notionalHistory);
  prepareTcpBases(previousElection,currentElection,preferenceHistory);
  preparePartyConversionPriors(partyFlowHistory, previousElection, currentElection);
  prepareNotionalPreferenceOffsets(partyFlowHistory);
  calculateTppEstimates(true); // This is done now so that we can observe deviations from preference flows
  aggregate(); // preliminary, for calculating preference flow and seat fp totals
  calculatePreferenceFlowDeviations();
  prepareUnreportedPreferenceCohorts(previousElection, currentElection, preferenceHistory);
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
  preparePreferenceCohorts(previousElection, currentElection, preferenceHistory);
  prepareTurnout(currentElection, std::move(turnoutInput));
  reportLimitedHistoricalPreferenceEvidence(currentElection.sourceTime);
  prepareNewVoteContexts();
  prepareFinalResults(previousElection,currentElection);
  recomposeVoteCounts();
  prepareFpEvidence();
  refreshFpEvidenceProjections();
  prepareTppProjectionSensitivities();
  prepareEstimatedTppEvidence();
  calculateLivePreferenceFlowDeviations();
  preparePreferenceCorrections(currentElection.liveCountStatus);
  prepareVariability();
  prepareTurnoutComposition();
  prepareFpIntegration();
  prepareTppIntegration();
  freezePreparedBooths();
  logPreparedSummary();
}

void Election::getNatPartyIndex() {
	natPartyIndex = project.parties().separateNationalsIndex();
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
		double const thisPreferenceFlow = std::stof(line[3]);
		if (!std::isfinite(thisPreferenceFlow)
			|| thisPreferenceFlow <= 0.0 || thisPreferenceFlow >= 100.0) {
			throw std::runtime_error(
				"Preference flow for " + party + " in " + run.getTermCode()
				+ " must be finite and strictly between 0 and 100.");
		}
		preferenceFlowMap[partyIndex] = thisPreferenceFlow;
		if (line.size() >= 5 && !line[4].empty() && line[4][0] != '#') {
			double const thisExhaustRate = std::stof(line[4]);
			if (!std::isfinite(thisExhaustRate)
				|| thisExhaustRate < 0.0 || thisExhaustRate >= 100.0) {
				throw std::runtime_error(
					"Exhaust rate for " + party + " in " + run.getTermCode()
					+ " must be finite and in [0, 100).");
			}
			preferenceExhaustMap[partyIndex] = thisExhaustRate;
		}
		else {
			preferenceExhaustMap[partyIndex] = 0.0;
		}
		// A changed preference relationship can require a different estimate
		// when interpreting the previous election's FP counts. Keep that input
		// alongside the current estimate rather than branching on election codes.
		if (line.size() >= 6 && !line[5].empty() && line[5][0] != '#') {
			double const previousFlow = std::stof(line[5]);
			if (!std::isfinite(previousFlow) || previousFlow <= 0 || previousFlow >= 100) {
				throw std::runtime_error("Previous preference flow for " + party + " in "
					+ run.getTermCode() + " must be finite and strictly between 0 and 100.");
			}
			prevPreferenceOverrides[partyIndex] = previousFlow;
		}
	}

	preferenceFlowMap[0] = 100.0;
	preferenceFlowMap[1] = 0.0;
	preferenceExhaustMap[0] = 0.0;
	preferenceExhaustMap[1] = 0.0;
	if (!preferenceFlowMap.contains(PartyCollection::InvalidIndex)) {
		throw std::runtime_error(
			"No generic Others preference estimate was supplied for "
			+ run.getTermCode() + ".");
	}
}

void Election::initializePartyMappings(
  Results2::Election const& previousElection,
  Results2::Election const& currentElection) {
  // Configured official codes retain the project's party/group choices. Keep
  // numeric feed IDs separate by election; their numbers do not identify the
  // same party across independently prepared current and historical inputs.
  // Abbreviations remain shared so genuine aliases still join those inputs.
  for (auto const& [id, party] : currentElection.parties) {
    if (party.shortCode.empty()) continue;
    int simIndex = project.parties().indexByShortCode(party.shortCode);
    if (simIndex != -1) {
      ecPartyToInternalParty[{LivePartyMapping::SourceElection::Current, id}] = simIndex;
      ecAbbreviationToInternalParty[party.shortCode] = simIndex;
    }
  }

  for (auto const& [id, party] : previousElection.parties) {
    if (party.shortCode.empty()) continue;
    int simIndex = project.parties().indexByShortCode(party.shortCode);
    if (simIndex != -1) {
      ecPartyToInternalParty[{LivePartyMapping::SourceElection::Previous, id}] = simIndex;
      ecAbbreviationToInternalParty[party.shortCode] = simIndex;
    }
  }
}

void Election::createNodesFromElectionData(
  Results2::Election const& previousElection,
  Results2::Election const& currentElection,
  LivePartyConversion::History const& history,
  LiveHistoricalPreferences::History const& notionalHistory) {
  for (auto const& [regionId, region] : project.regions()) {
    largeRegions.push_back(LargeRegion(region));
  }
  for (auto const& [id, seat] : currentElection.seats) {
    if (inactiveContests.contains(seat.name)) continue;
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
          previousSeatName = previousBooth.comparisonSeat.empty()
            ? previousElection.seats.at(previousBooth.parentSeat).name : previousBooth.comparisonSeat;
        }
      }
      bool const sameSeat = previousBoothPtr.has_value() && (
        previousSeatName == seat.name
        || (!projectSeat.previousName.empty()
          && previousSeatName == projectSeat.previousName));

      // Create new booth with mapping function
      preparationBooths().emplace_back(Booth(
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
      preparationBooths().back().previousPreferenceSeat = previousSeatName;
      preparationBooths().back().syntheticPreviousCategory = previousBoothPtr && (*previousBoothPtr)->syntheticBaseline;
      preparationBooths().back().incrementalFp = currentBooth.incrementalFp;
      if (previousBoothPtr && !(*previousBoothPtr)->syntheticBaseline) {
        // Match commission candidate counts BEFORE modelling-party grouping.
        // Equal totals alone cannot distinguish a revised or reconstructed FP
        // account. Do not replace the old TCP: it still describes its real pair.
        auto const& old = **previousBoothPtr;
        LivePartialCount::Counts raw(old.fpVotes.begin(),old.fpVotes.end());
        auto record = notionalHistory.find(previousSeatName,"booth",old.id,raw);
        if (record) preparationBooths().back().notionalTppPrevious = LiveHistoricalPreferences::mappedTpp(
          *record,preparationBooths().back().node.fpVotesPrevious,natPartyIndex);
      }
      // A reconstructed composition is useful but is not a new observed
      // historical sample. Quarter weight is a conservative working safeguard
      // for swing pooling, not a fitted WA coefficient. Absolute current votes
      // remain fully counted; the joint TPP path also retains its baseline SD.
      if (run.regionCode == "wa" && preparationBooths().back().syntheticPreviousCategory)
        preparationBooths().back().node.relevanceModifier *= .25;
      liveSeat.booths.push_back(int(readBooths().size()) - 1);
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
    // The WA reporting bridge has already allocated old category accounts to
    // current districts. Prefer that exact prepared account: Oakford's fallback
    // name Jandakot must not select Jandakot's separate share of the old total.
    if (currentElection.termCode == "2025wa") {
      auto prepared = previousElection.seats.find(seat.id);
      if (prepared != previousElection.seats.end()) previousSeatPtr = &prepared->second;
    }
    for (auto const& [prevSeatId, prevSeat] : previousElection.seats) {
      if (previousSeatPtr) break;
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
    if (previousSeatPtr)
      prepareCategoryHistory(seatIndex, **previousSeatPtr, previousElection, currentElection, history, notionalHistory);
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

      preparationBooths().emplace_back(
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
      if (previousSeatPtr) {
        preparationBooths().back().previousPreferenceSeat = (*previousSeatPtr)->name;
        preparationBooths().back().syntheticPreviousCategory = (*previousSeatPtr)->syntheticBaselineTypes.contains(voteType);
        if (!preparationBooths().back().syntheticPreviousCategory) {
          LivePartialCount::Counts raw;
          for (auto const& [candidate,types] : (*previousSeatPtr)->fpVotes) {
            auto found = types.find(voteType);
            if (found != types.end()) raw[candidate] = found->second;
          }
          auto record = notionalHistory.find((*previousSeatPtr)->name,"declaration",int(voteType),raw);
          if (record) preparationBooths().back().notionalTppPrevious = LiveHistoricalPreferences::mappedTpp(
            *record,preparationBooths().back().node.fpVotesPrevious,natPartyIndex);
        }
        if (run.regionCode == "wa" && preparationBooths().back().syntheticPreviousCategory)
          preparationBooths().back().node.relevanceModifier *= .25;
      }
      liveSeat.booths.push_back(int(readBooths().size()) - 1);
    }
  }
}

template<typename T>
std::vector<Node const*> Election::getThisAndParents(T& child) const {
  // Read-only and mutable children use the same hierarchy. Const qualification
  // must not silently discard the containing seat, region or election evidence.
  std::vector<Node const*> parents;
  parents.push_back(&child.node);
  if constexpr (std::is_same_v<std::remove_cv_t<T>, Booth>) {
    parents.push_back(&seats.at(child.parentSeatId).node);
    parents.push_back(&largeRegions.at(seats.at(child.parentSeatId).parentRegionIndex).node);
    parents.push_back(&node);
  }
  else if constexpr (std::is_same_v<std::remove_cv_t<T>, Seat>) {
    parents.push_back(&largeRegions.at(child.parentRegionIndex).node);
    parents.push_back(&node);
  }
  else if constexpr (std::is_same_v<std::remove_cv_t<T>, LargeRegion>) {
    parents.push_back(&node);
  }
  return parents;
}

LiveHistoricalPreferences::History Election::readNotionalPreferenceHistory(
  Results2::Election const& previousElection, Results2::Election const& currentElection) const {
  if (optionalPreferential) return {}; // Continuing-vote/exhaustion populations need a separate treatment.
  std::ifstream input("analysis/Data/historical-notional-tpp.csv");
  if (!input) return {}; // Optional additional coverage; ordinary historical TCP remains available.
  return LiveHistoricalPreferences::readHistory(input,previousElection.termCode,currentElection.termCode);
}

LivePartialCount::Counts const& Election::historicalTppCounts(Booth const& booth) const {
  // The existing real TCP comparison wins. Independent notional TPP fills
  // only its missing Labor/Coalition account; it never changes that old TCP's
  // finalist identities or becomes evidence of a current-election count.
  if (booth.notionalPreferenceOffset) return booth.notionalTppPrevious;
  return booth.node.tcpVotesPrevious;
}

void Election::prepareNotionalPreferenceOffsets(LivePartyConversion::History const& history) {
  // Historical party priors have already been attached. Fit only their residual
  // against the additional count, once per matched reporting unit. The usual
  // historical TCP keeps its pooled arithmetic and takes precedence below.
  for (auto& booth : preparationBooths()) {
    booth.notionalPreferenceOffset.reset();
    if (booth.notionalTppPrevious.empty()) continue;
    if (preferenceRateOffset(booth,false)) {
      // The established comparison is already usable. Do not add another
      // correction, or change its arithmetic, merely because a duplicate old
      // count happens to be available through the additional source.
      booth.notionalTppPrevious.clear();
      continue;
    }
    int coalition = booth.notionalTppPrevious.contains(natPartyIndex) ? natPartyIndex : 1;
    auto rates = LivePartyConversion::notionalCalibrationFlows(
      classicPreferenceFlows(booth,coalition,true),history,booth.previousPreferenceSeat,run.grnPartyIndex);
    booth.notionalPreferenceOffset = LiveHistoricalPreferences::fitOffset(
      booth.node.fpVotesPrevious,booth.notionalTppPrevious,rates,0,coalition);
    if (!booth.notionalPreferenceOffset) booth.notionalTppPrevious.clear();
  }
}

LivePartyConversion::History Election::readPartyConversionHistory(Results2::Election const& previousElection,
  Results2::Election const& currentElection) const {
  // Historical candidate destinations are available for some compulsory-
  // preferential elections. Unsupported systems keep the existing conversion;
  // optional exhaustion needs its own denominator and is not fitted here.
  if (optionalPreferential) return {};
  std::ifstream input("analysis/Data/historical-party-tpp-flows.csv");
  if (!input) throw std::runtime_error("Cannot open historical party TPP flows.");
  return LivePartyConversion::readHistory(input, previousElection.termCode, currentElection.termCode);
}

void Election::preparePartyConversionPriors(LivePartyConversion::History const& history,
  Results2::Election const& previousElection, Results2::Election const& currentElection) {
  if (history.records.empty()) return;
  prepareHistoricalBoothFlows(history, previousElection);
  prepareDistrictPartyFlows(history, currentElection);
}

LivePartialCount::Flows Election::historicalPartyPreferenceFlows(LivePartyConversion::History const& history,
  std::string const& previousSeatName, LivePartialCount::Counts const& primaries,
  Results2::Election const& previousElection) const {
  // Select only the historical party-specific rates actually available. Both
  // matched locations and aggregate categories use the source district's name,
  // so renamed/transferred booths do not acquire another district's history.
  // Independent IDs are decoded through the historical candidate roster; the
  // complete normalised name must match before that rate can be used.
  LivePartialCount::Flows flows;
  auto green = LivePartyConversion::historicalFlow(history,previousSeatName,"GRN");
  if (green) flows[run.grnPartyIndex] = *green;
  for (auto const& [party, votes] : primaries) {
    if (party < IndependentPartyIdOffset) continue;
    auto candidate = previousElection.candidates.find(party-IndependentPartyIdOffset);
    if (candidate == previousElection.candidates.end()) continue;
    auto flow = LivePartyConversion::historicalFlow(history,previousSeatName,"IND",
      LivePartyConversion::candidateKey(candidate->second.name));
    if (flow) flows[party] = *flow;
  }
  return flows;
}

void Election::prepareHistoricalBoothFlows(LivePartyConversion::History const& history,
  Results2::Election const& previousElection) {
  // Re-express old booth residuals against the shared historical party rule,
  // avoiding a second addition of the Greens district effect. A measured old
  // TPP remains unchanged; these priors estimate only missing old preferences.
  for (auto& booth : preparationBooths())
    booth.previousPartyPreferenceFlows = historicalPartyPreferenceFlows(history,
      booth.previousPreferenceSeat,booth.node.fpVotesPrevious,previousElection);
}

double Election::districtGreensStrength(Seat const& seat, int party) const {
  double total = 0, green = 0;
  for (int index : seat.booths) {
    total += readBooths()[index].node.totalFpVotesCurrent();
    green += LivePartialCount::value(readBooths()[index].node.fpVotesCurrent, party);
  }
  // Read the district baseline directly because party priors precede the
  // observed-swing hierarchy. No TPP estimate feeds back into this strength.
  double prior = .1;
  if (sim.getLiveBaselineReport()) {
    auto const& report = *sim.getLiveBaselineReport();
    auto found = std::find(report.seatName.begin(), report.seatName.end(), seat.name);
    if (found != report.seatName.end()) {
      auto const& bands = report.seatFpProbabilityBand.at(found-report.seatName.begin());
      if (bands.contains(party) && !bands.at(party).empty())
        prior = bands.at(party).at((bands.at(party).size()-1)/2)*.01;
    }
  }
  return LivePartyConversion::phasedStrength(prior, green, total);
}

double Election::districtGreensResidual(Seat const& seat, LivePartyConversion::History const& history) const {
  // Redistribution carries each matched booth's source-district offset into
  // the new district. Missing/new booth history adds no invented offset.
  // A campaign-specific residual can be discounted for forward reuse without
  // changing the historical booth flows used to reconstruct the older account.
  // Keep its votes in the denominator: exclusion must not amplify other sources.
  double weighted = 0, total = 0;
  for (int index : seat.booths) {
    auto const& booth = readBooths()[index];
    double votes = booth.node.totalVotesPrevious();
    total += votes;
    if (auto row = history.find(booth.previousPreferenceSeat, "GRN"))
      weighted += votes*history.forwardGreensResidual(*row);
  }
  return total > 0 ? weighted/total : 0;
}

void Election::prepareDistrictPartyFlows(LivePartyConversion::History const& history,
  Results2::Election const& currentElection) {
  for (auto& seat : seats) {
    if (history.hasGreens && preferenceFlowMap.contains(run.grnPartyIndex))
      seat.partyPreferenceFlows[run.grnPartyIndex] = LivePartyConversion::greensFlow(
        preferenceFlowMap.at(run.grnPartyIndex)*.01, districtGreensStrength(seat, run.grnPartyIndex),
        districtGreensResidual(seat, history), history);
    prepareReturningIndependentFlows(seat, history, currentElection);
  }
}

void Election::prepareReturningIndependentFlows(Seat& seat, LivePartyConversion::History const& history,
  Results2::Election const& currentElection) {
  // Returning independents retain their own electorate. New candidates do
  // not inherit a predecessor's preferences, even in the same district.
  std::set<std::string> sources{seat.name};
  for (int index : seat.booths) sources.insert(readBooths()[index].previousPreferenceSeat);
  auto currentSeat = std::find_if(currentElection.seats.begin(), currentElection.seats.end(),
    [&](auto const& entry) { return entry.second.name == seat.name; });
  if (currentSeat == currentElection.seats.end()) return;
  for (auto const& [id, votes] : currentSeat->second.fpVotes) {
    auto candidate = currentElection.candidates.find(id);
    if (candidate == currentElection.candidates.end() || candidate->second.party != Results2::Candidate::Independent) continue;
    LivePartyConversion::Record const* matched = nullptr;
    bool ambiguous = false;
    auto key = LivePartyConversion::candidateKey(candidate->second.name);
    for (auto const& source : sources) if (auto row = history.find(source, "IND", key)) {
      if (matched) ambiguous = true;
      matched = row;
    }
    if (!matched || ambiguous) continue;
    double base = preferenceFlowMap.at(PartyCollection::InvalidIndex)*.01;
    double previous = prevPreferenceOverrides.contains(PartyCollection::InvalidIndex)
      ? prevPreferenceOverrides.at(PartyCollection::InvalidIndex)*.01 : base;
    seat.partyPreferenceFlows[id+IndependentPartyIdOffset] =
      LivePartyConversion::returningIndependentFlow(*matched, base, previous);
  }
}

LivePartialCount::Flows Election::classicPreferenceFlows(Booth const& booth,
  int coalitionParty, bool previous) const {
  LivePartialCount::Flows flows;
  auto const& counts = previous ? booth.node.fpVotesPrevious : booth.node.fpVotesCurrent;
  // The losing Coalition candidate transfers like another eliminated party;
  // retain that distinction when Liberals and Nationals both contest a seat.
  std::set<int> candidates;
  for (auto const& [party,votes] : counts) candidates.insert(party);
  if (!previous) candidates.insert(booth.node.runningParties.begin(),booth.node.runningParties.end());
  if (!previous) for (auto const& [party, share] : booth.node.fpShares) candidates.insert(party);
  for (int party : candidates) {
    double rate = 0;
    if (party == 0) rate = 1;
    else if (party == coalitionParty) rate = 0;
    else if (party == 1 || party == natPartyIndex) rate = CoalitionLeakagePercent*.01;
    else if (previous && booth.previousPartyPreferenceFlows.contains(party))
      rate = booth.previousPartyPreferenceFlows.at(party);
    else if (!previous && seats.at(booth.parentSeatId).partyPreferenceFlows.contains(party))
      rate = seats.at(booth.parentSeatId).partyPreferenceFlows.at(party);
    else if (previous && prevPreferenceOverrides.contains(party)) rate = prevPreferenceOverrides.at(party)*.01;
    else if (preferenceFlowMap.contains(party)) rate = preferenceFlowMap.at(party)*.01;
    else rate = preferenceFlowMap.at(PartyCollection::InvalidIndex)*.01;
    flows.emplace(party,rate);
  }
  return flows;
}

std::optional<double> Election::preferenceRateOffset(Booth const& booth, bool current) const {
  // An unchanged TCP can describe an older primary batch. Only that batch may
  // train preference flows; the corrected estimate is never fresh evidence.
  auto const& fp = current ? (booth.hasTcpRevision() ? booth.tcpBasis->fp : booth.node.fpVotesCurrent)
    : booth.node.fpVotesPrevious;
  auto const& tcp = current ? booth.node.tcpVotesCurrent : booth.node.tcpVotesPrevious;
  if (!isTppSet(tcp,natPartyIndex)) {
    if (!current && booth.notionalPreferenceOffset) return *booth.notionalPreferenceOffset*25;
    return {};
  }
  int coalition = tcp.contains(natPartyIndex) ? natPartyIndex : 1;
  if (current && booth.notionalPreferenceOffset) {
    // Movement must compare like calculations. Against a newly fitted exact
    // historical mixture, use that same mixture calculation for its current
    // measured counterpart. Subtracting the old exact fit from today's pooled
    // approximation could otherwise manufacture movement even from identical
    // FP/TPP counts. Existing historical TCP comparisons retain their old path.
    auto offset = LiveHistoricalPreferences::fitOffset(fp,tcp,classicPreferenceFlows(booth,coalition),0,coalition);
    return offset ? std::optional<double>(*offset*25) : std::nullopt;
  }
  auto evidence = LivePartialCount::measure(fp,tcp,classicPreferenceFlows(booth,coalition,!current),0,coalition);
  if (evidence) return double(evidence->offset*25);
  if (!current && booth.node.totalVotesPrevious() > 0 &&
    LivePartialCount::total(fp)-LivePartialCount::value(fp,0)-LivePartialCount::value(fp,coalition) == 0)
    return 0;
  if (!current && booth.notionalPreferenceOffset) return *booth.notionalPreferenceOffset*25;
  return {};
}

std::optional<LivePartialCount::PreferenceTransfer> Election::historicalPreferenceTransfer(Booth const& booth) const {
  auto const& tcp = historicalTppCounts(booth);
  if (!isTppSet(tcp,natPartyIndex)) return {};
  int coalition = tcp.contains(natPartyIndex) ? natPartyIndex : 1;
  return LivePartialCount::measureTransfer(booth.node.fpVotesPrevious,tcp,0,coalition);
}

void Election::calculatePreferenceFlowEvidence(Booth& booth) {
  auto current = preferenceRateOffset(booth,true);
  auto previous = preferenceRateOffset(booth,false);
  if (!current || !previous) return;
  if (!std::isfinite(*current) || !std::isfinite(*previous)) {
    logger << "Warning: Non-finite preference rate offset for booth " << booth.name << "\n";
    return;
  }
  // Usually both elections supply ordinary, comparable preference flows.
  // Retain their measured difference and the existing coverage rule. Only
  // an extreme historical rate discounts how much that difference trains
  // local/seat/regional extrapolation; current counted votes remain fixed.
  auto historical = historicalPreferenceTransfer(booth);
  double reliability = historical ? LivePartialCount::historicalFlowReliability(historical->share()) : 1;
  booth.node.preferenceFlowDeviation = *current-*previous;
  booth.node.preferenceFlowConfidence = double(reliability*std::min(
    booth.node.fpConfidence,booth.node.tppCompletion));
}

double Election::limitedHistoricalPreferenceImpact(Booth const& booth,
  LivePartialCount::PreferenceTransfer const& historical, double reliability) const {
  auto const& fp = booth.node.fpVotesCurrent;
  auto const& tcp = booth.node.tcpVotesCurrent;
  if (!isTppSet(tcp,natPartyIndex)) return 0;
  int coalition = tcp.contains(natPartyIndex) ? natPartyIndex : 1;
  auto current = LivePartialCount::measureTransfer(fp,tcp,0,coalition);
  auto previousOffset = preferenceRateOffset(booth,false);
  if (!current || !previousOffset) return 0;
  auto flows = classicPreferenceFlows(booth,coalition);
  double expected = (LivePartialCount::expectedFirst(fp,flows,0)-LivePartialCount::value(fp,0))/current->votes;
  double historicalPrediction = LivePartialCount::inverseLogOdds(
    LivePartialCount::logOdds(expected)+*previousOffset/25);
  // Rank notices by the smaller measured preference pool times the flow
  // disagreement and lost reliability. This is a vote-scale severity proxy,
  // not a claim that the forecast changed by this many votes. Party mix is
  // already accounted for in the current expected flow used here.
  return std::min(historical.votes,current->votes)*(1-reliability)*
    std::abs(historicalPrediction-current->share())*booth.node.relevanceModifier;
}

std::string Election::describePreferenceCounts(std::map<int,int> const& counts) const {
  std::ostringstream text;
  for (auto const& [party,votes] : counts) {
    if (text.tellp() > 0) text << ", ";
    if (party >= 0 && party < project.parties().count())
      text << project.parties().viewByIndex(party).abbreviation;
    else text << "historical party " << party;
    text << " " << votes;
  }
  return text.str();
}

void Election::recordLimitedHistoricalPreferenceIssue(Booth const& booth,
  LivePartialCount::PreferenceTransfer const& historical, double reliability,
  double impact, std::string const& sourceTime) {
  auto const& seat = seats.at(booth.parentSeatId);
  std::string role = booth.voteType != Results2::VoteType::Ordinary ? "declaration" :
    booth.boothType == Results2::Booth::Type::Ppvc ? "ppvc" : "ordinary";
  std::ostringstream explanation;
  explanation << std::fixed << std::setprecision(2)
    << "The previous count implies " << historical.share()*100
    << "% of non-finalist preferences reaching Labor after normalizing the FP/TCP totals. "
    << "Near-unanimous flows are sensitive to small count discrepancies; retaining "
    << reliability*100 << "% of this comparison's extrapolation weight.\n"
    << "Previous FP: " << describePreferenceCounts(booth.node.fpVotesPrevious)
    << " (total " << booth.node.totalVotesPrevious() << ").\n"
    << "Previous Labor/Coalition count: " << describePreferenceCounts(historicalTppCounts(booth))
    << " (total " << LivePartialCount::total(historicalTppCounts(booth)) << ").";
  run.inputIssues.add({"historical_preference_limited",seat.name+"/"+role+"/"+booth.name,
    seat.name,booth.name,explanation.str(),
    "Check the historical FP/TCP counts if available. Their extrapolation evidence has been discounted; counted TCP is retained.",
    sourceTime,impact,false,{},{}});
}

void Election::reportLimitedHistoricalPreferenceEvidence(std::string const& sourceTime) {
  // Preparation reports this once through the existing grouped registry.
  // Scenario generation never adds notices, and tiny unanimous pools remain
  // quiet even though their extrapolation weight is correctly reduced.
  for (auto const& booth : readBooths()) {
    if (!booth.node.preferenceFlowDeviation) continue;
    auto historical = historicalPreferenceTransfer(booth);
    if (!historical) continue;
    double reliability = LivePartialCount::historicalFlowReliability(historical->share());
    if (reliability == 1) continue;
    double impact = limitedHistoricalPreferenceImpact(booth,*historical,reliability);
    if (impact >= HistoricalPreferenceWarningVotes)
      recordLimitedHistoricalPreferenceIssue(booth,*historical,reliability,impact,sourceTime);
  }
}

LivePartialCount::Flows Election::pairPreferenceFlows(Booth const& booth, int first, int second) const {
  if (first == 0 && (second == 1 || second == natPartyIndex))
    return classicPreferenceFlows(booth,second);
  // Non-classic contests retain the existing seat-wide preference estimate.
  // The same cohort matching can still identify which primaries supplied its
  // counted pair, without imposing Labor/Coalition party flows on that contest.
  auto const& seat = seats.at(booth.parentSeatId);
  double rate = seat.tcpFocusPartyPrefFlow.value_or(50)*.01;
  if (seat.tcpFocusPartyIndex && *seat.tcpFocusPartyIndex != first) rate = 1-rate;
  LivePartialCount::Flows flows;
  for (int party : booth.node.runningParties)
    flows[party] = party == first ? 1 : party == second ? 0 : rate;
  return flows;
}

double Election::preferencePriorOffset(Booth const& booth, bool includeLocal) const {
  // Retain the normal historical and pooled preference assumptions when this
  // service has no suitable same-election cohort. Local cohort evidence then
  // displaces that prior in proportion to its non-finalist sample size.
  double offset = preferenceRateOffset(booth,false).value_or(0);
  for (auto parent : getThisAndParents(booth)) {
    // Pending preferences retain the normal local prior. For new voters, the
    // same measured category flow enters once through the cohort weight below.
    if (!includeLocal && parent == &booth.node) continue;
    offset += parent->specificPreferenceFlowDeviation.value_or(0);
  }
  return offset/25;
}

int Election::coalitionFinalist(Booth const& booth) const {
  auto const& tcp = booth.node.tcpVotesCurrent;
  if (isTppSet(tcp,natPartyIndex)) return tcp.contains(natPartyIndex) ? natPartyIndex : 1;
  auto const& seat = seats.at(booth.parentSeatId);
  if (isTppSet(seat.node.tcpVotesCurrent,natPartyIndex))
    return seat.node.tcpVotesCurrent.contains(natPartyIndex) ? natPartyIndex : 1;
  // Before any confirmed classic pair, retain the ordinary ballot-based
  // assumption. Separate Liberal/National ballots preserve the losing party's
  // configured leakage rather than treating both parties as the finalist.
  auto const& fp = booth.node.fpVotesCurrent;
  if (fp.contains(natPartyIndex) && (!fp.contains(1) || fp.at(natPartyIndex) > fp.at(1))) return natPartyIndex;
  return 1;
}

double Election::futurePreferenceOffset(Booth const& booth, int first, int second) const {
  return LivePartialCount::evidenceOffset(preferencePriorOffset(booth,false),booth.preferenceEvidence,first,second);
}

std::map<int,double> Election::projectDeclarationTpp(int boothIndex) {
  auto& booth = preparationBooths().at(boothIndex);
  auto const& fp = booth.node.fpVotesCurrent;
  auto const& tcp = booth.node.tcpVotesCurrent;
  int coalition = coalitionFinalist(booth);
  auto flows = classicPreferenceFlows(booth,coalition);
  double n = booth.node.totalFpVotesCurrent(), nt = booth.node.totalTcpVotesCurrent();
  bool countedPair = nt > 0 && isTppSet(tcp,natPartyIndex);
  // In a non-classic contest this is a hypothetical Labor/Coalition account;
  // its observed non-classic TCP cannot supply counted TPP or TPP lower bounds.
  double pairTarget = countedPair ? turnoutPairTarget(boothIndex) : turnoutFpTarget(boothIndex);
  double offset = futurePreferenceOffset(booth,0,coalition);
  double variation = 0;
  if (createRandomVariation) {
    auto tag = countedPair ? VariabilityTag::NonOrdinaryTpp : VariabilityTag::BoothProjectionTpp;
    variation = variabilityNormal(0,4.5+7*std::exp(-double(booth.node.totalVotesPrevious())*.0001),
      boothIndex,0,uint32_t(tag))/25;
  }
  // The future FP mixture already contains the postal arrival adjustment.
  // Applying an independent postal TPP shift here would count it a second time.
  double futureShare = LiveNewVotes::firstShare(booth.futureFpShares,flows,offset+variation);
  double estimatedKnown = LivePartialCount::expectedFirst(fp,flows,offset+variation);
  double known = countedPair ? tcp.at(0)+LivePartialCount::pendingFirst(fp,tcp,flows,
    preferencePriorOffset(booth),booth.preferenceEvidence,0,coalition,variation) : estimatedKnown;
  auto revised = countedPair ? revisedBoothTcp(booth,0,coalition,
    createRandomVariation ? variabilityNormal(0,1,boothIndex,0,uint32_t(VariabilityTag::RevisedTcpTransfers)) : 0) : std::nullopt;
  if (revised) known = revised->first;
  double newVotes = std::max(0.0,pairTarget-(countedPair && !revised ? std::max(n,nt) : n));
  double first = known+newVotes*futureShare;
  if (!createRandomVariation) {
    double measured = revised ? revised->votes-revised->uncertainVotes() : nt;
    booth.node.tppCompletion = countedPair ? double(std::clamp(measured/pairTarget,0.0,1.0)) : 0;
    booth.node.tppConfidence = countedPair ? booth.node.tppCompletion : booth.node.fpConfidence*.5;
    // Retain an FP-derived comparison for the existing non-classic bias audit.
    // Its denominator equals this projection's turnout/pair account.
    double estimate = estimatedKnown+std::max(0.0,pairTarget-n)*futureShare;
    booth.tppVotesEstimated = {{0,double(estimate)},{1,double(pairTarget-estimate)}};
  }
  return {{0,double(first)},{1,double(pairTarget-first)}};
}

void Election::preparePreferenceCohorts(Results2::Election const& previousElection,
  Results2::Election const& currentElection, LiveInputRecovery::PreferenceHistories const& history) {
  // Identity matching and chronological disk access have already happened at
  // input preparation. Map the two selected measured accounts to model parties
  // once; scenario generation only uses the resulting compact flow evidence.
  for (auto& booth : preparationBooths()) {
    if (booth.node.tcpVotesCurrent.size() != 2 || booth.node.totalTcpVotesCurrent() <= 0) continue;
    auto const& seat = seats.at(booth.parentSeatId);
    std::string role = booth.voteType != Results2::VoteType::Ordinary ? "declaration" :
      booth.boothType == Results2::Booth::Type::Ppvc ? "ppvc" : "ordinary";
    auto found = history.find(seat.name+"/"+role+"/"+booth.name);
    if (found == history.end()) continue;
    preparePreferenceCohort(booth,previousElection,currentElection,found->second);
  }
}

void Election::prepareUnreportedPreferenceCohorts(Results2::Election const& previousElection,
  Results2::Election const& currentElection, LiveInputRecovery::PreferenceHistories const& history) {
  // Normally FP-only booths use historical and pooled flows. After recovery
  // removes incompatible TCP, an earlier compatible classic pair can inform
  // the FP estimate without supplying counted TPP or fresh progress. Other
  // pairings wait for their seat-level preparation later in the normal flow.
  for (auto& booth : preparationBooths()) {
    if (booth.node.totalTcpVotesCurrent() > 0) continue;
    auto const& seat = seats.at(booth.parentSeatId);
    std::string role = booth.voteType != Results2::VoteType::Ordinary ? "declaration" :
      booth.boothType == Results2::Booth::Type::Ppvc ? "ppvc" : "ordinary";
    auto found = history.find(seat.name+"/"+role+"/"+booth.name);
    if (found == history.end() || !found->second.previousAligned) continue;
    preparePreferenceCohort(booth,previousElection,currentElection,found->second);
    if (booth.preferenceEvidence && (booth.preferenceEvidence->first != 0 ||
        (booth.preferenceEvidence->second != 1 && booth.preferenceEvidence->second != natPartyIndex)))
      booth.preferenceEvidence.reset();
  }
}

void Election::preparePreferenceCohort(Booth& booth, Results2::Election const& previousElection,
  Results2::Election const& currentElection, LiveInputRecovery::PreferenceHistory const& history) {
  auto const& selected = history.matchingCurrentTcp ? history.matchingCurrentTcp : history.previousAligned;
  if (!selected) return;
  auto mapCounts = [&](auto const& raw) {
    LivePartialCount::Counts mapped;
    for (auto const& [candidate,votes] : raw)
      mapped[mapPartyId(candidate,false,previousElection,currentElection)] += votes;
    return mapped;
  };
  auto fp = mapCounts(selected->fp), tcp = mapCounts(selected->tcp);
  if (tcp.size() != 2) return;
  int first = tcp.begin()->first, second = std::next(tcp.begin())->first;
  if (isMajorParty(first,natPartyIndex) && !isTppSet(tcp,natPartyIndex)) std::swap(first,second);
  auto flows = pairPreferenceFlows(booth,first,second);
  if (std::any_of(fp.begin(),fp.end(),[&](auto const& row) { return !flows.contains(row.first); })) return;
  booth.preferenceEvidence = LivePartialCount::measure(fp,tcp,flows,first,second,history.matchingCurrentTcp.has_value());
  if (booth.notionalPreferenceOffset && booth.preferenceEvidence && first == 0
    && (second == 1 || second == natPartyIndex)) {
    // On the added historical path an aligned current cohort must use the same
    // mixture calculation as current preference movement. Otherwise a precise
    // cohort could pull the estimate back towards the pooled approximation.
    // Other pairings and existing historical-TCP paths keep their usual rule.
    auto exact = LiveHistoricalPreferences::fitOffset(fp,tcp,flows,first,second);
    if (exact) booth.preferenceEvidence->offset = *exact;
  }
  if (booth.preferenceEvidence) {
    booth.preferenceEvidence->fpSourceTime = selected->fpTime;
    booth.preferenceEvidence->tcpSourceTime = selected->tcpTime;
  }
  if (auto revised = revisedBoothTcp(booth,first,second)) {
    // Non-classic TCP extrapolation reads these comparison shares as well as
    // projected votes. It must see the same revised account; published count
    // fields and the matched preference evidence above remain unchanged.
    for (int party : {first,second}) {
      double votes = party == first ? revised->first : revised->votes-revised->first;
      booth.node.tcpShares[party] = double(25*std::log((votes+.25)/(revised->votes-votes+.25)));
      auto const& previous = booth.node.tcpVotesPrevious;
      if (previous.contains(party) && booth.node.totalTcpVotesPrevious() > 0) {
        double before = previous.at(party), total = booth.node.totalTcpVotesPrevious();
        booth.node.tcpSwings[party] = booth.node.tcpShares[party]-double(25*std::log((before+.25)/(total-before+.25)));
      }
    }
  }
}

std::map<int,double> Election::projectCountedTpp(int boothIndex, double contextShare) {
  auto& booth = preparationBooths().at(boothIndex);
  auto const& fp = booth.node.fpVotesCurrent;
  auto const& tcp = booth.node.tcpVotesCurrent;
  int coalition = tcp.contains(natPartyIndex) ? natPartyIndex : 1;
  double countedFpVotes = booth.node.totalFpVotesCurrent();
  double countedTcpVotes = booth.node.totalTcpVotesCurrent();
  double expectedPairVotes = turnoutPairTarget(boothIndex);
  double pendingPreferences = std::max(0.0,countedFpVotes-countedTcpVotes);
  double newFpVotes = std::max(0.0,expectedPairVotes-std::max(countedFpVotes,countedTcpVotes));
  double countedFirst = tcp.at(0);
  double deviation = createRandomVariation ? variabilityNormal(0,
    4.5+7*std::exp(-double(booth.node.totalVotesPrevious())*.0001),
    boothIndex,0,uint32_t(VariabilityTag::NonOrdinaryTpp))/25 : 0;
  // Missing preferences use the known FP cohort. New ordinary voters use the
  // surrounding composition; the retained OPV path uses its continuing-vote
  // share. Postal arrival bias affects new voters, not already reported FP.
  double pendingFirst = optionalPreferential ? pendingPreferences*countedFirst/countedTcpVotes :
    LivePartialCount::pendingFirst(fp,tcp,classicPreferenceFlows(booth,coalition),
      preferencePriorOffset(booth),booth.preferenceEvidence,0,coalition,deviation);
  // Complete ordinary booths normally have no new FP voters. The retained OPV
  // path uses its continuing-vote share; compulsory declaration services use
  // projectDeclarationTpp and the shared future-primary mixture instead.
  double futureShare = optionalPreferential ? countedFirst/countedTcpVotes : contextShare;
  double arrivalBias = optionalPreferential && booth.voteType == Results2::VoteType::Postal ? 2.0/25 : 0;
  futureShare = LivePartialCount::inverseLogOdds(LivePartialCount::logOdds(futureShare)+deviation+arrivalBias);
  double first = countedFirst+pendingFirst+newFpVotes*futureShare;
  if (auto revised = revisedBoothTcp(booth,0,coalition,
      createRandomVariation ? variabilityNormal(0,1,boothIndex,0,uint32_t(VariabilityTag::RevisedTcpTransfers)) : 0)) {
    first = revised->first+std::max(0.0,expectedPairVotes-revised->votes)*futureShare;
    countedTcpVotes = revised->votes-revised->uncertainVotes();
  }
  booth.node.tppCompletion = double(std::clamp(countedTcpVotes/expectedPairVotes,0.0,1.0));
  booth.node.tppConfidence = booth.node.tppCompletion;
  return {{0,double(first)},{1,double(expectedPairVotes-first)}};
}

double Election::estimateBoothTpp(Booth const& booth) const {
  // Use one set of party priors for observed-offset measurement, inferred TPP,
  // pending preferences and future declaration votes. Measured TCP is never
  // replaced by this estimate. The established OPV limitations are unchanged.
  int coalition = coalitionFinalist(booth);
  auto flows = classicPreferenceFlows(booth, coalition);
  double offset = booth.node.totalTcpVotesCurrent() == 0 && booth.preferenceEvidence ?
    futurePreferenceOffset(booth,0,coalition) : preferencePriorOffset(booth);
  double first = 0;
  for (auto const& [party, share] : booth.node.fpShares) {
    double flow = flows.at(party);
    if (flow > 0 && flow < 1)
      flow = LivePartialCount::inverseLogOdds(LivePartialCount::logOdds(flow)+offset);
    first += detransformVoteShare(share)*flow;
  }
  return first;
}

void Election::calculatePreviousTpp(Booth& booth) const {
  auto const& n = booth.node;
  auto const& previous = historicalTppCounts(booth);
  double previousTotal = LivePartialCount::total(previous);
  if (previousTotal > 0 && isTppSet(previous, natPartyIndex)) {
    // Use the same real old account for swing/reference comparisons and for
    // preference calibration. Keeping an estimated historical share here after
    // calibrating against actual TPP would make reference centering compensate
    // for that old preference error a second time.
    booth.node.tppSharePrevious = transformVoteShare(std::clamp(
      100.0*previous.at(0)/previousTotal, 1.0, 99.0));
  } else if (n.totalVotesPrevious() > 0) {
    int coalition = n.fpVotesPrevious.contains(natPartyIndex) &&
      LivePartialCount::value(n.fpVotesPrevious, natPartyIndex) > LivePartialCount::value(n.fpVotesPrevious, 1)
      ? natPartyIndex : 1;
    double first = LivePartialCount::expectedFirst(n.fpVotesPrevious,
      classicPreferenceFlows(booth, coalition, true), 0);
    booth.node.tppSharePrevious = transformVoteShare(double(std::clamp(
      100*first/n.totalVotesPrevious(), 1.0, 99.0)));
  }
}

void Election::calculateTppEstimates(bool withTpp) {
  for (auto& booth : preparationBooths()) {
    // Pass one handles only observed TPP booths and records preference-flow
    // evidence. After that evidence is aggregated, pass two handles only booths
    // without observed TPP and estimates their TPP from FP votes.
    if (booth.node.tppShare.has_value() != withTpp) continue;

    double partyOneShare = estimateBoothTpp(booth);
    calculatePreviousTpp(booth);

    if (partyOneShare > 0.0 && partyOneShare < 100.0) {
      calculatePreferenceFlowEvidence(booth);
      if (!withTpp) {
        booth.node.tppShare = transformVoteShare(partyOneShare);
        // An FP-derived TPP estimate is less informative than a direct count,
        // while still respecting how much of an incremental category exists.
        booth.node.tppConfidence = booth.node.fpConfidence * 0.5;
      }
    }
    // A TPP comparison needs current and previous shares, independently of
    // whether this booth can also train the preference-flow estimate. Normally
    // both are directly counted; a historical FP estimate can supply the older
    // share when its TCP is absent. Incomplete or differing FP/TCP accounts can
    // prevent preference-rate estimation without invalidating the TPP swing.
    booth.calculateTppSwing(natPartyIndex);
    if (auto revised = revisedBoothTcp(booth,0,coalitionFinalist(booth))) {
      // Keep published TCP in the raw count fields. Its comparison share now
      // describes the revised received FP account, with only changed transfers
      // estimated. Recompute the swing after the usual counted-share shortcut.
      booth.node.tppShare = double(25*std::log((revised->first+.25)/(revised->votes-revised->first+.25)));
      if (booth.node.tppSharePrevious)
        booth.node.tppSwing = *booth.node.tppShare-*booth.node.tppSharePrevious;
    }
    if (withTpp && booth.node.tppSwing && booth.voteType == Results2::VoteType::Ordinary) {
      // Ordinary TCP is a completed booth count. If its earlier TPP comparison
      // was supplied above from historical FP, it now has the same count
      // coverage as a booth whose historical TCP was available at creation.
      booth.node.tppConfidence = 1.0;
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
  node.specificPreferenceFlowDeviation = node.preferenceFlowDeviation.value_or(0.0) * obsWeight(node.preferenceFlowConfidence, PreferenceFlowObsWeightStrength);
}

void Election::determineLargeRegionPreferenceFlowDeviations() {
  for (auto& largeRegion : largeRegions) {
    double preferenceFlowObsWeight = obsWeight(largeRegion.node.preferenceFlowConfidence, PreferenceFlowObsWeightStrength);
    double parentPreferenceFlowDeviation = node.specificPreferenceFlowDeviation.value_or(0.0);
    double withoutElectionSpecific = largeRegion.node.preferenceFlowDeviation ? largeRegion.node.preferenceFlowDeviation.value() - parentPreferenceFlowDeviation : 0.0;
    largeRegion.node.specificPreferenceFlowDeviation = withoutElectionSpecific * preferenceFlowObsWeight;
  }
}

void Election::determineSeatPreferenceFlowDeviations() {
  for (auto& largeRegion : largeRegions) {
    for (int seatIndex : largeRegion.seats) {
      auto& seat = seats.at(seatIndex);
      double preferenceFlowObsWeight = obsWeight(seat.node.preferenceFlowConfidence, PreferenceFlowObsWeightStrength);
      double parentPreferenceFlowDeviation = largeRegion.node.specificPreferenceFlowDeviation.value_or(0.0);
      parentPreferenceFlowDeviation += node.specificPreferenceFlowDeviation.value_or(0.0);
      double withoutElectionSpecific = seat.node.preferenceFlowDeviation ? seat.node.preferenceFlowDeviation.value() - parentPreferenceFlowDeviation : 0.0;
      seat.node.specificPreferenceFlowDeviation = withoutElectionSpecific * preferenceFlowObsWeight;
    }
  }
}

void Election::determineBoothPreferenceFlowDeviations() {
  for (auto& seat : seats) {
    for (int boothIndex : seat.booths) {
      auto& booth = preparationBooths().at(boothIndex);
      double preferenceFlowObsWeight = obsWeight(booth.node.preferenceFlowConfidence, PreferenceFlowObsWeightStrength);
      double parentPreferenceFlowDeviation = seat.node.specificPreferenceFlowDeviation.value_or(0.0);
      auto const& largeRegion = largeRegions.at(seat.parentRegionIndex);
      parentPreferenceFlowDeviation += largeRegion.node.specificPreferenceFlowDeviation.value_or(0.0);
      parentPreferenceFlowDeviation += node.specificPreferenceFlowDeviation.value_or(0.0);
      double withoutElectionSpecific = booth.node.preferenceFlowDeviation ? booth.node.preferenceFlowDeviation.value() - parentPreferenceFlowDeviation : 0.0;
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
      if (inactiveContests.contains(name)) continue;
      logger << "Warning: Seat " << name << " from baseline report not found in current election results\n";
      continue;
    }
    auto& seat = *seatIt;
    for (auto const& [partyId, probabilityBands] : baseline.seatFpProbabilityBand.at(i)) {
      double median = probabilityBands.at((probabilityBands.size() - 1) / 2);
      if (partyId == run.indPartyIndex) seat.independentPoolingBaselineShare = median*.01;
      // Some categories like emerging-ind will likely have a median of 0,
      // but we want to include them, so assign them a small baseline share
      // This won't affect significant parties or independents as their median will
      // be above 1% even in their worst seats.
      seat.node.fpSharesBaseline[partyId] = transformVoteShare(std::clamp(median, 1.0, 99.0));
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
    double bestIndependentShare = 0.0;
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
        double thisVotesPrevious = 0.0;
        if (seat.node.fpVotesPrevious.contains(partyId)) {
          thisVotesPrevious = seat.node.fpVotesPrevious.at(partyId);
        }
        else if (partyId == run.indPartyIndex && seat.node.fpVotesPrevious.contains(seat.independentPartyIndex)) {
          thisVotesPrevious = seat.node.fpVotesPrevious.at(seat.independentPartyIndex);
        }
        if (thisVotesPrevious > 0.0) {
          seat.node.fpSwingsBaseline[partyId] = seat.node.fpSharesBaseline[partyId] - 
            transformVoteShare(double(thisVotesPrevious) / double(seat.node.totalVotesPrevious()) * 100.0);
          // this ignores redistributions, might have to tweak it to handle some cases better
        }
      }
    }
    
    double tppMedian = baseline.seatTppProbabilityBand.at(i).at((baseline.seatTppProbabilityBand.at(i).size() - 1) / 2);
    if (tppMedian > 0 && tppMedian < 100) {
      seat.node.tppShareBaseline = transformVoteShare(tppMedian);
    }
    int projectSeatIndex = project.seats().indexByName(name);
    ::Seat const& projectSeat = project.seats().viewByIndex(projectSeatIndex);
    double tppExistingShare = transformVoteShare(projectSeat.tppMargin + 50.0);
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
      double median = probabilityBands.at((probabilityBands.size() - 1) / 2);
      if (median > 0 && median < 100) {
        region.node.fpSharesBaseline[partyId] = transformVoteShare(median);
      }
    }
    double tppMedian = baseline.regionTppProbabilityBand.at(i).at((baseline.regionTppProbabilityBand.at(i).size() - 1) / 2);
    if (tppMedian > 0 && tppMedian < 100) {
      region.node.tppShareBaseline = transformVoteShare(tppMedian);
    }
  }
}

void Election::includeElectionBaselineResults() {
  auto const& baseline = sim.getLiveBaselineReport().value();
  for (auto const& [partyId, probabilityBands] : baseline.electionFpProbabilityBand) {
    double median = probabilityBands.at((probabilityBands.size() - 1) / 2);
    if (median > 0 && median < 100) {
      node.fpSharesBaseline[partyId] = transformVoteShare(median);
    }
  }
  double tppMedian = baseline.electionTppProbabilityBand.at((baseline.electionTppProbabilityBand.size() - 1) / 2);
  if (tppMedian > 0 && tppMedian < 100) {
    node.tppShareBaseline = transformVoteShare(tppMedian);
  }
}

void Election::extrapolateBaselineSwings() {
  for (auto& seat : seats) {
    for (int boothIndex : seat.booths) {
      auto& booth = preparationBooths().at(boothIndex);
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
      auto& booth = preparationBooths().at(boothIndex);
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
    std::map<int, std::map<Results2::Booth::Type, double>> fpBiasWeightedSums;
    std::map<int, std::map<Results2::Booth::Type, double>> fpWeightSums;
    std::map<int, std::map<Results2::Booth::Type, double>> fpSourceCount;
    for (auto& seat : seats) {
      std::map<int, std::map<Results2::Booth::Type, double>> seatFpDeviationWeightedSums;
      std::map<int, std::map<Results2::Booth::Type, double>> seatFpWeightSums;
      for (auto const& [partyId, _] : seat.node.fpVotesCurrent) {
        for (int boothIndex : seat.booths) {
          auto const& booth = readBooths().at(boothIndex);
          // Handle "other" booths (postals/absent/etc.) separately and ignore "invalid" booths (we don't know what they are)
          if (booth.boothType == Results2::Booth::Type::Other || booth.boothType == Results2::Booth::Type::Invalid) {
            continue;
          }
          if (!booth.node.totalFpVotesCurrent() || !booth.node.fpDeviations.contains(partyId)) {
            // Keep known booth types represented for consistent downstream
            // handling, even when this party has no usable evidence yet.
            seatFpDeviationWeightedSums[partyId][booth.boothType] += 0.0;
            seatFpWeightSums[partyId][booth.boothType] += 0.0;
            continue;
          }

          double const weight = booth.node.totalFpVotesCurrent()
            * booth.node.relevanceModifier;
          seatFpDeviationWeightedSums[partyId][booth.boothType] +=
            booth.node.fpDeviations.at(partyId) * weight;
          seatFpWeightSums[partyId][booth.boothType] += weight;
        }
      }
      for (auto const& [partyId, deviationWeightedSums] : seatFpDeviationWeightedSums) {
        if (!deviationWeightedSums.contains(Results2::Booth::Type::Normal)) continue;
        double const normalWeight = seatFpWeightSums[partyId].at(
          Results2::Booth::Type::Normal);
        // A category bias is a within-seat comparison. Do not treat a missing
        // ordinary-booth reference as an observed deviation of zero.
        if (normalWeight <= 0.0) continue;
        double const normalDeviation = deviationWeightedSums.at(
          Results2::Booth::Type::Normal) / normalWeight;
        for (auto const& [boothType, deviationWeightedSum] : deviationWeightedSums) {
          if (boothType == Results2::Booth::Type::Normal) continue;
          if (seatFpWeightSums[partyId].at(boothType) == 0.0) {
            // avoid division by zero
            fpBiasWeightedSums[partyId][boothType] += 0.0;
            fpWeightSums[partyId][boothType] += 0.0;
            continue;
          }
          double deviation = deviationWeightedSum / seatFpWeightSums[partyId].at(boothType);
          double boothTypeBias = deviation - normalDeviation;
          double weight = seatFpWeightSums[partyId].at(boothType);
          fpBiasWeightedSums[partyId][boothType] += boothTypeBias * weight;
          fpWeightSums[partyId][boothType] += weight;
          ++fpSourceCount[partyId][boothType];
        }
      }
    }

    for (auto const& [partyId, biasWeightedSums] : fpBiasWeightedSums) {
      for (auto const& [boothType, bias] : biasWeightedSums) {
        double votes = fpWeightSums[partyId].at(boothType);
        double overallFpBias = votes ? bias / votes : 0.0;
        // placeholder formula, works for PPVCs/postals/absents but not smaller categories (but they don't usually have a significant impact)
        double obsProportion = std::pow(votes, 0.9) / (20000.0 + std::pow(votes, 0.9));
        double baseline = 0.0;
        boothTypeFpBiases[partyId][boothType] = overallFpBias * obsProportion + baseline * (1.0 - obsProportion);
        double stdDev = 7.0 * std::exp(-std::pow(votes + 1000.0, 0.25) * 0.06);
        boothTypeFpBiasStdDev[partyId][boothType] = stdDev;
        boothTypeFpBiasesRaw[partyId][boothType] = overallFpBias;
        boothTypeFpSourceCount[partyId][boothType] = fpSourceCount[partyId][boothType];
        boothTypeFpVoteCount[partyId][boothType] = fpWeightSums[partyId][boothType];
      }
    }
  }

  { // biases for declaration votes
    std::map<int, std::map<Results2::VoteType, double>> fpBiasWeightedSums;
    std::map<int, std::map<Results2::VoteType, double>> fpWeightSums;
    std::map<int, std::map<Results2::VoteType, double>> fpSourceCount;
    for (auto& seat : seats) {
      std::map<int, std::map<Results2::VoteType, double>> seatFpDeviationWeightedSums;
      std::map<int, std::map<Results2::VoteType, double>> seatFpWeightSums;
      for (auto const& [partyId, _] : seat.node.fpVotesCurrent) {
        for (int boothIndex : seat.booths) {
          auto const& booth = readBooths().at(boothIndex);
          // make sure that this booth type "exists" (for output/consistency purposes)
          if (booth.voteType != Results2::VoteType::Ordinary) {
            fpBiasWeightedSums[partyId][booth.voteType] += 0.0;
            fpWeightSums[partyId][booth.voteType] += 0.0;
          }
          // ignore "invalid" booths (we don't know what they are)
          if (booth.voteType == Results2::VoteType::Invalid) {
            continue;
          }
          if (booth.node.totalFpVotesCurrent() == 0 || !booth.node.fpDeviations.contains(partyId)) {
            // As with booth types, retain known vote types without treating
            // missing deviations as observations.
            seatFpDeviationWeightedSums[partyId][booth.voteType] += 0.0;
            seatFpWeightSums[partyId][booth.voteType] += 0.0;
            continue;
          }

          double const weight = booth.node.totalFpVotesCurrent()
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
        double normalDeviation = seatFpDeviationWeightedSums[partyId].at(Results2::VoteType::Ordinary) / seatFpWeightSums[partyId].at(Results2::VoteType::Ordinary);
        for (auto const& [voteType, deviationWeightedSum] : deviationWeightedSums) {
          if (voteType == Results2::VoteType::Ordinary) continue;
          if (seatFpWeightSums[partyId].at(voteType) == 0.0) {
            // avoid division by zero
            fpBiasWeightedSums[partyId][voteType] += 0.0;
            fpWeightSums[partyId][voteType] += 0.0;
            continue;
          }
          double deviation = deviationWeightedSum / seatFpWeightSums[partyId].at(voteType);
          double voteTypeBias = deviation - normalDeviation;
          double weight = seatFpWeightSums[partyId].at(voteType);
          fpBiasWeightedSums[partyId][voteType] += voteTypeBias * weight;
          fpWeightSums[partyId][voteType] += weight;
          ++fpSourceCount[partyId][voteType];
        }
      }
    }
    for (auto const& [partyId, biasWeightedSums] : fpBiasWeightedSums) {
      for (auto const& [voteType, bias] : biasWeightedSums) {
        double votes = fpWeightSums[partyId].at(voteType);
        double overallFpBias = votes ? bias / votes : 0.0;
        // placeholder formula, works for PPVCs/postals/absents but not smaller categories (but they don't usually have a significant impact)
        double obsProportion = std::pow(votes, 0.9) / (20000.0 + std::pow(votes, 0.9));
        double baseline = 0.0;
        voteTypeFpBiases[partyId][voteType] = overallFpBias * obsProportion + baseline * (1.0 - obsProportion);
        double stdDev = 7.0 * std::exp(-std::pow(votes + 1000.0, 0.25) * 0.06);
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
    std::map<Results2::Booth::Type, double> tppBiasWeightedSums;
    std::map<Results2::Booth::Type, double> tppWeightSums;
    std::map<Results2::Booth::Type, double> tppSourceCount;
    for (auto& seat : seats) {
      std::map<Results2::Booth::Type, double> seatTppDeviationWeightedSums;
      std::map<Results2::Booth::Type, double> seatTppWeightSums;
      for (int boothIndex : seat.booths) {
        auto const& booth = readBooths().at(boothIndex);
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
          seatTppDeviationWeightedSums[booth.boothType] += 0.0;
          seatTppWeightSums[booth.boothType] += 0.0;
          continue;
        }

        
        double const weight = booth.node.totalTcpVotesCurrent()
          * booth.node.relevanceModifier;
        seatTppDeviationWeightedSums[booth.boothType] +=
          booth.node.tppDeviation.value() * weight;
        seatTppWeightSums[booth.boothType] += weight;
      }
      if (!seatTppDeviationWeightedSums.contains(Results2::Booth::Type::Normal)) continue;
      double const normalWeight = seatTppWeightSums.at(
        Results2::Booth::Type::Normal);
      // A category bias is a within-seat comparison. Do not treat a missing
      // ordinary-booth reference as an observed deviation of zero.
      if (normalWeight <= 0.0) continue;
      double const normalDeviation = seatTppDeviationWeightedSums.at(
        Results2::Booth::Type::Normal) / normalWeight;
      for (auto const& [boothType, deviationWeightedSum] : seatTppDeviationWeightedSums) {
        if (boothType == Results2::Booth::Type::Normal) continue;
        if (seatTppWeightSums.at(boothType) == 0.0) {
          // avoid division by zero
          tppBiasWeightedSums[boothType] += 0.0;
          tppWeightSums[boothType] += 0.0;
          continue;
        }
        double deviation = deviationWeightedSum / seatTppWeightSums.at(boothType);
        double boothTypeBias = deviation - normalDeviation;
        double weight = seatTppWeightSums.at(boothType);
        tppBiasWeightedSums[boothType] += boothTypeBias * weight;
        tppWeightSums[boothType] += weight;
        ++tppSourceCount[boothType];
      }
    }

    for (auto const& [boothType, bias] : tppBiasWeightedSums) {
      double votes = tppWeightSums.at(boothType);
      double overallTppBias = votes ? tppBiasWeightedSums.at(boothType) / votes : 0.0;
      // placeholder formula, works for PPVCs/postals/absents but not smaller categories (but they don't usually have a significant impact)
      double obsProportion = std::pow(votes, 0.9) / (20000.0 + std::pow(votes, 0.9));
      double baseline = 0.0;
      // pre-polls tend to move the tpp back towards the baseline
      if (boothType == Results2::Booth::Type::Ppvc) {
        baseline = -0.6 * node.specificTppDeviation.value_or(0.0);
      }
      boothTypeTppBiases[boothType] = overallTppBias * obsProportion + baseline * (1.0 - obsProportion);
      double stdDev = 7.0 * std::exp(-std::pow(votes + 1000.0, 0.25) * 0.06);
      boothTypeTppBiasStdDev[boothType] = stdDev;
      boothTypeTppBiasesRaw[boothType] = overallTppBias;
      boothTypeTppSourceCount[boothType] = tppSourceCount[boothType];
      boothTypeTppVoteCount[boothType] = tppWeightSums[boothType];
    }
  }

  { // biases for declaration votes
    std::map<Results2::VoteType, double> tppBiasWeightedSums;
    std::map<Results2::VoteType, double> tppWeightSums;
    std::map<Results2::VoteType, double> tppSourceCount;
    for (auto& seat : seats) {
      std::map<Results2::VoteType, double> seatTppDeviationWeightedSums;
      std::map<Results2::VoteType, double> seatTppWeightSums;
      for (int boothIndex : seat.booths) {
        auto const& booth = readBooths().at(boothIndex);
        // make sure that this booth type "exists" (for output/consistency purposes)
        if (booth.voteType != Results2::VoteType::Ordinary) {
          tppBiasWeightedSums[booth.voteType] += 0.0;
          tppWeightSums[booth.voteType] += 0.0;
        }
        // ignore "invalid" booths (we don't know what they are)
        if (booth.voteType == Results2::VoteType::Invalid) {
          continue;
        }
        if (booth.node.totalTcpVotesCurrent() == 0
          || !booth.node.tppDeviation.has_value()) {
          // As with booth types, make sure that these vote types exist in the sums, even if just zero
          seatTppDeviationWeightedSums[booth.voteType] += 0.0;
          seatTppWeightSums[booth.voteType] += 0.0;
          continue;
        }
        
        double const weight = booth.node.totalTcpVotesCurrent()
          * booth.node.relevanceModifier;
        seatTppDeviationWeightedSums[booth.voteType] +=
          booth.node.tppDeviation.value() * weight;
        seatTppWeightSums[booth.voteType] += weight;
      }

      if (!seatTppDeviationWeightedSums.contains(Results2::VoteType::Ordinary)) continue;
      // zero weight sum can occur if a seat's count is being realigned and therefore no TCP results
      if (!seatTppWeightSums.contains(Results2::VoteType::Ordinary) || seatTppWeightSums.at(Results2::VoteType::Ordinary) == 0) continue;
      double normalDeviation = seatTppDeviationWeightedSums.at(Results2::VoteType::Ordinary) / seatTppWeightSums.at(Results2::VoteType::Ordinary);
      for (auto const& [voteType, deviationWeightedSum] : seatTppDeviationWeightedSums) {
        if (voteType == Results2::VoteType::Ordinary) continue;
        if (seatTppWeightSums.at(voteType) == 0.0) {
          // avoid division by zero
          tppBiasWeightedSums[voteType] += 0.0;
          tppWeightSums[voteType] += 0.0;
          continue;
        }
        double deviation = deviationWeightedSum / seatTppWeightSums.at(voteType);
        double voteTypeBias = deviation - normalDeviation;
        double weight = seatTppWeightSums.at(voteType);
        tppBiasWeightedSums[voteType] += voteTypeBias * weight;
        tppWeightSums[voteType] += weight;
        ++tppSourceCount[voteType];
      }
    }
    for (auto const& [voteType, bias] : tppBiasWeightedSums) {
      double votes = tppWeightSums.at(voteType);
      double overallTppBias = votes ? tppBiasWeightedSums.at(voteType) / votes : 0.0;
      // placeholder formula, works for PPVCs/postals/absents but not smaller categories (but they don't usually have a significant impact)
      double obsProportion = std::pow(votes, 0.9) / (20000.0 + std::pow(votes, 0.9));
      // Declaration votes tend to move the tpp back toward the baseline
      double baseline = -0.6 * node.specificTppDeviation.value_or(0.0);
      voteTypeTppBiases[voteType] = overallTppBias * obsProportion + baseline * (1.0 - obsProportion);
      double stdDev = 7.0 * std::exp(-std::pow(votes + 1000.0, 0.25) * 0.06);
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
  aggregateCollection(seat, seat.booths, readBooths());
  prepareSeatIndependentPooling(seat);
}

void Election::aggregateToLargeRegion(LargeRegion& largeRegion) {
  aggregateCollection(largeRegion, largeRegion.seats, seats);
}

void Election::aggregateToElection() {
  std::vector<int> indices(largeRegions.size());
  std::iota(indices.begin(), indices.end(), 0);
  aggregateCollection(*this, indices, largeRegions);
}

LiveNewVotes::FpEvidence Election::primaryEvidence(Node const& source) const {
  return {expectedVotesForAggregation(source),double(source.totalFpVotesCurrent()),
    source.fpConfidence,source.relevanceModifier,source.fpDeviations,{},source.fpPoolingVotes};
}

void Election::prepareIndependentPooling(LiveNewVotes::FpEvidence& evidence, Seat const& seat) const {
  // Most parties represent the same organisation in every district. The
  // independent slot instead represents a different principal candidate in
  // each seat. Use its baseline size to assess cross-seat comparability;
  // current counts still determine that candidate's own local estimate.
  // Only the separate principal-campaign baseline establishes source size.
  // Mapping a hypothetical emerging candidate into the local principal slot
  // later does not establish that this is a continuing/comparable campaign.
  // Usually the baseline and current principal identify the same candidate.
  // If a different independent overtakes that candidate locally, its local
  // votes remain usable but the older campaign's baseline is not its source
  // size for movement shared with other districts.
  double share = seat.independentPartyIndex != InvalidPartyIndex
    && seat.liveIndependentPartyIndex == seat.independentPartyIndex
    ? seat.independentPoolingBaselineShare : 0;
  LiveNewVotes::prepareIndependentPooling(evidence,run.indPartyIndex,share);
}

void Election::prepareSeatIndependentPooling(Seat& seat) const {
  auto evidence = primaryEvidence(seat.node);
  prepareIndependentPooling(evidence,seat);
  seat.node.fpPoolingVotes = std::move(evidence.poolingVotes);
}

double Election::fpPoolingObservationWeight(Node const& source, int party, double strength) const {
  // Region and election shrinkage must use the same discounted candidate
  // evidence as their means. Using all-party confidence here would give a
  // region with one weak independent comparison disproportionate influence.
  double confidence = source.fpPoolingVotes.contains(party)
    ? double(LiveNewVotes::poolingConfidence(primaryEvidence(source),party)) : source.fpConfidence;
  return obsWeight(confidence,strength);
}

std::map<int,double> Election::fpPoolingObservationWeights(
  LiveNewVotes::FpEvidence const& evidence, double strength) const {
  std::map<int,double> weights;
  for (auto const& [party,votes] : evidence.poolingVotes)
    weights[party] = obsWeight(double(LiveNewVotes::poolingConfidence(evidence,party)),strength);
  return weights;
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

  // Evidence and completion are separate accounts. Cap primary-composition
  // evidence at actual received votes, but retain the expected-size denominator
  // for completion and confidence even for booths with no usable comparison.
  std::vector<LiveNewVotes::FpEvidence> childrenPrimaryEvidence;
  double completedVotes = 0, primaryExpectedVotes = 0;
  for (auto child : nodesToAggregate) {
    double size = expectedVotesForAggregation(*child);
    childrenPrimaryEvidence.push_back(primaryEvidence(*child));
    completedVotes += child->fpCompletion*size;
    primaryExpectedVotes += size;
  }
  auto primary = LiveNewVotes::aggregate(childrenPrimaryEvidence,primaryExpectedVotes);
  aggregatedNode.fpDeviations = std::move(primary.deviations);
  aggregatedNode.fpConfidence = double(primary.confidence);
  aggregatedNode.fpPoolingVotes = std::move(primary.poolingVotes);
  if (primaryExpectedVotes > 0) aggregatedNode.fpCompletion = double(completedVotes/primaryExpectedVotes);

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
  double tppDeviationWeightedSum = 0.0;
  double tppWeightSum = 0.0;
  double tppConfidenceSum = 0.0;
  double tppConfidenceWeightSum = 0.0;
  double tppCompletionSum = 0.0;
  double tppCompletionWeightSum = 0.0; // all expected votes
  for (auto const& thisNode : nodesToAggregate) {
    double const expectedVotes = expectedVotesForAggregation(*thisNode);
    double const evidenceWeight = expectedVotes * thisNode->relevanceModifier;
    double const weight = evidenceWeight * thisNode->tppConfidence;
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
  double preferenceFlowDeviationWeightedSum = 0.0;
  double preferenceFlowWeightSum = 0.0;
  double preferenceFlowConfidenceSum = 0.0;
  double preferenceFlowConfidenceWeightSum = 0.0;
  for (auto const& thisNode : nodesToAggregate) {
    double const expectedVotes = expectedVotesForAggregation(*thisNode);
    double const evidenceWeight = expectedVotes * thisNode->relevanceModifier;
    double const weight = evidenceWeight * thisNode->preferenceFlowConfidence;
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
  for (auto const& [partyId, deviation] : node.fpDeviations) {
    node.specificFpDeviations[partyId] = deviation * fpPoolingObservationWeight(node,partyId,VoteObsWeightStrength*2.5);
  }
  double tppObsWeight = obsWeight(node.tppConfidence, VoteObsWeightStrength * 2.5);
  node.specificTppDeviation = node.tppDeviation.value_or(0.0) * tppObsWeight;
}

void Election::determineLargeRegionSpecificDeviations() {
  for (auto& largeRegion : largeRegions) {
    for (auto const& [partyId, deviation] : largeRegion.node.fpDeviations) {
      double parentFpDeviation = node.specificFpDeviations.contains(partyId) ? node.specificFpDeviations.at(partyId) : 0.0;
      double excludingParents = deviation - parentFpDeviation;
      largeRegion.node.specificFpDeviations[partyId] = excludingParents
        * fpPoolingObservationWeight(largeRegion.node,partyId,VoteObsWeightStrength);
    }
    double tppObsWeight = obsWeight(largeRegion.node.tppConfidence);
    double parentTppDeviation = node.specificTppDeviation.value_or(0.0);
    double excludingParents = largeRegion.node.tppDeviation ? largeRegion.node.tppDeviation.value() - parentTppDeviation : 0.0;
    largeRegion.node.specificTppDeviation = excludingParents * tppObsWeight;
  }
}

void Election::determineSeatSpecificDeviations() {
  for (auto& largeRegion : largeRegions) {
    for (int seatIndex : largeRegion.seats) {
      auto& seat = seats.at(seatIndex);
      double fpObsWeight = obsWeight(seat.node.fpConfidence);
      for (auto const& [partyId, deviation] : seat.node.fpDeviations) {
        double parentsFpDeviation = largeRegion.node.specificFpDeviations.contains(partyId) ? largeRegion.node.specificFpDeviations.at(partyId) : 0.0;
        parentsFpDeviation += node.specificFpDeviations.contains(partyId) ? node.specificFpDeviations.at(partyId) : 0.0;
        double excludingParents = deviation - parentsFpDeviation;
        seat.node.specificFpDeviations[partyId] = excludingParents * fpObsWeight;
      }
      double tppObsWeight = obsWeight(seat.node.tppConfidence);
      double parentsTppDeviation = largeRegion.node.specificTppDeviation.value_or(0.0);
      parentsTppDeviation += node.specificTppDeviation.value_or(0.0);
      double excludingParents = seat.node.tppDeviation ? seat.node.tppDeviation.value() - parentsTppDeviation : 0.0;
      seat.node.specificTppDeviation = excludingParents * tppObsWeight;
    }
  }
}

void Election::determineBoothSpecificDeviations() {
  for (auto& seat : seats) {
    auto const& largeRegion = largeRegions.at(seat.parentRegionIndex);
    for (int boothIndex : seat.booths) {
      auto& booth = preparationBooths().at(boothIndex);
      double fpObsWeight = obsWeight(booth.node.fpConfidence);
      for (auto const& [partyId, deviation] : booth.node.fpDeviations) {
        double parentsFpDeviation = seat.node.specificFpDeviations.contains(partyId) ? seat.node.specificFpDeviations.at(partyId) : 0.0;
        parentsFpDeviation += largeRegion.node.specificFpDeviations.contains(partyId) ? largeRegion.node.specificFpDeviations.at(partyId) : 0.0;
        parentsFpDeviation += node.specificFpDeviations.contains(partyId) ? node.specificFpDeviations.at(partyId) : 0.0;
        double excludingParents = deviation - parentsFpDeviation;
        booth.node.specificFpDeviations[partyId] = excludingParents * fpObsWeight;
      }
      double tppObsWeight = obsWeight(booth.node.tppConfidence);
      double parentsTppDeviation = seat.node.specificTppDeviation.value_or(0.0);
      parentsTppDeviation += largeRegion.node.specificTppDeviation.value_or(0.0);
      parentsTppDeviation += node.specificTppDeviation.value_or(0.0);
      double excludingParents = booth.node.tppDeviation ? booth.node.tppDeviation.value() - parentsTppDeviation : 0.0;
      booth.node.specificTppDeviation = excludingParents * tppObsWeight;
    }
  }
}

void Election::calculateNationalsProportions() {
  for (auto& seat : seats) {
    double totalFp = static_cast<double>(seat.node.totalFpVotesCurrent());
    if (totalFp == 0.0) {
      seat.nationalsProportion = std::nullopt;
      continue;
    }
    double nationalsVotes = static_cast<double>(seat.node.fpVotesCurrent.contains(natPartyIndex) ? seat.node.fpVotesCurrent.at(natPartyIndex) : 0);
    double partyOneVotes = static_cast<double>(seat.node.fpVotesCurrent.contains(1) ? seat.node.fpVotesCurrent.at(1) : 0);
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
    double nationalsShare = static_cast<double>(nationalsVotes) / totalFp;
    double partyOneShare = static_cast<double>(partyOneVotes) / totalFp;
    seat.nationalsProportion = nationalsShare / (nationalsShare + partyOneShare);
  }
}

void Election::calculateTcpPreferenceFlows() {
  for (auto& seat : seats) {
    if (seat.node.tcpVotesCurrent.size() != 2) continue;
    if (isTppSet(seat.node.tcpVotesCurrent, natPartyIndex)) continue;
    int partyOneIndex = seat.node.tcpVotesCurrent.begin()->first;
    int partyTwoIndex = std::next(seat.node.tcpVotesCurrent.begin())->first;
    double totalPrefs = 0.0;
    double totalPartyOnePrefs = 0.0;
    double focusPartyConfidence = 0.0;
    for (auto& boothIndex : seat.booths) {
      auto const& booth = readBooths().at(boothIndex);
      auto const& fp = booth.hasTcpRevision() ? booth.tcpBasis->fp : booth.node.fpVotesCurrent;
      double totalFpVotes = double(LivePartialCount::total(fp));
      double totalTcpVotes = double(booth.node.totalTcpVotesCurrent());
      if (!fp.contains(partyOneIndex) || !fp.contains(partyTwoIndex)) continue;
      if (!booth.node.tcpVotesCurrent.contains(partyOneIndex) || !booth.node.tcpVotesCurrent.contains(partyTwoIndex)) continue;
      if (totalTcpVotes == 0 || totalFpVotes == 0) continue;

      if (run.regionCode == "sa") {
        // SA TCP votes are commonly different to FP votes due to savings provisions, so handle them differently
        // Small total differences are expected from savings provisions and
        // asynchronous publication. Larger differences are too ambiguous to
        // treat as measured preference-flow evidence.
        double const relativeTotalDifference =
          std::abs(totalTcpVotes - totalFpVotes) / totalFpVotes;
        if (relativeTotalDifference > MaxPreferenceVoteTotalDifference) continue;
        double fpTcpRatio = totalFpVotes / totalTcpVotes;
        double scaledTcpPartyOne = double(booth.node.tcpVotesCurrent.at(partyOneIndex)) * fpTcpRatio;
        double boothPrefs = totalFpVotes - fp.at(partyOneIndex) - fp.at(partyTwoIndex);
        if (!std::isfinite(boothPrefs) || boothPrefs <= 2.0) continue;
        double boothPartyOnePrefs = std::clamp(scaledTcpPartyOne - double(fp.at(partyOneIndex)), 1.0, boothPrefs - 1.0);
        totalPartyOnePrefs += boothPartyOnePrefs;
        totalPrefs += boothPrefs;
        focusPartyConfidence += totalTcpVotes;
      }
      else if (totalFpVotes == totalTcpVotes) {
        double boothPrefs = totalFpVotes - double(fp.at(partyOneIndex)) - double(fp.at(partyTwoIndex));
        double boothPartyOnePrefs = double(booth.node.tcpVotesCurrent.at(partyOneIndex)) - double(fp.at(partyOneIndex));
        if (!std::isfinite(boothPrefs) || boothPrefs <= 2.0) continue;
        boothPartyOnePrefs = std::clamp(
          boothPartyOnePrefs, 1.0, boothPrefs - 1.0);
        totalPartyOnePrefs += boothPartyOnePrefs;
        totalPrefs += boothPrefs;
        focusPartyConfidence += totalTcpVotes;
      }
    }
    double const confidenceDenominator = static_cast<double>(
      std::max(seat.node.totalVotesPrevious(), seat.node.totalFpVotesCurrent()));
    if (totalPrefs <= 0.0 || confidenceDenominator <= 0.0) continue;
    seat.tcpFocusPartyIndex = partyOneIndex;
    seat.tcpFocusPartyPrefFlow = totalPartyOnePrefs / totalPrefs * 100.0;
    seat.tcpFocusPartyConfidence = std::clamp(
      focusPartyConfidence / confidenceDenominator, 0.0, 1.0);
  }
}

void Election::recomposeVoteCounts() {
  // Rebuild party composition at the turnout model's count means. Blind and
  // current passes share those sizes, isolating observed share changes from
  // differences in the mix of reporting units.

  if (!createRandomVariation) {
    // FP sensitivities are collected by this composition pass. TPP weights
    // are prepared separately after all FP-evidence projection refreshes.
    for (auto& seat : seats) {
      seat.fpBoothTypeSensitivity.clear();
      seat.fpVoteTypeSensitivity.clear();
    }
    refreshTurnoutFpProgress();

    for (int boothIndex : std::ranges::views::iota(0, int(readBooths().size()))) {
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

  for (int boothIndex : std::ranges::views::iota(0, int(readBooths().size()))) {
    recomposeBoothFpVotes(true, boothIndex);
    recomposeBoothTppVotes(true, boothIndex);
    recomposeBoothTcpVotes(boothIndex);
  }

  // Need this to use actual election data, but don't want to recalculate it every
  // time we generate random variation
  if (!createRandomVariation) {
    refreshTurnoutPairProgress();
    calculateTppEstimateBias();
  }

  refreshProjectedVoteAggregates();
}

void Election::refreshProjectedVoteAggregates() {
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

void Election::recomposeBoothFpVotes(bool allowCurrentData, int boothIndex) {
  auto& booth = preparationBooths().at(boothIndex);
  if (turnoutFpTarget(boothIndex) == 0) {
    clearFpProjection(boothIndex);
  } else if (allowCurrentData && booth.receivesFpBatches()) {
    projectDeclarationFp(boothIndex);
  } else if (allowCurrentData && booth.node.totalFpVotesCurrent() > 0) {
    // Reported ordinary booths normally have no new FP pool. Retain their
    // accepted candidate counts; partial preferences are handled independently.
    projectCountedOrdinaryFp(boothIndex);
  } else if (!allowCurrentData && booth.receivesFpBatches()) {
    // The reference account must use the same counts -> composition -> counts
    // arithmetic as the declaration calculation. Otherwise the extra division
    // and multiplication on the live side manufacture a tiny apparent swing,
    // even when no evidence changes the composition. Here only the reference
    // information is supplied; the live branch above still incorporates every
    // actual count and sampled change in the usual way.
    auto context = declarationFpContext(false,boothIndex);
    auto projected = LiveNewVotes::project({},context,turnoutFpTarget(boothIndex));
    storeFpProjection(boothIndex,mapFpProjection(boothIndex,projected,false));
  } else {
    storeFpProjection(boothIndex,projectUnreportedFp(allowCurrentData,boothIndex));
    if (allowCurrentData) recordFpProjectionSensitivity(boothIndex,turnoutFpTarget(boothIndex));
  }
  if (allowCurrentData) applyFpEvidence(boothIndex);
}

void Election::clearFpProjection(int boothIndex) {
  preparationBooths().at(boothIndex).futureFpShares.clear();
  storeFpProjection(boothIndex,{});
}

std::map<int,double> Election::mapFpProjection(int boothIndex, LiveNewVotes::Shares const& raw,
  bool allowCurrentData) const {
  // Raw candidate identities remain available for FP/TCP cohort comparisons.
  // Only the simulator-facing projection maps the prominent Independent onto
  // its configured modelling party; other Independents remain separate.
  auto const& seat = seats.at(readBooths().at(boothIndex).parentSeatId);
  int independent = allowCurrentData ? seat.liveIndependentPartyIndex : seat.independentPartyIndex;
  std::map<int,double> mapped;
  for (auto const& [candidate,votes] : raw)
    mapped[candidate == independent ? run.indPartyIndex : candidate] += votes;
  return mapped;
}

void Election::projectCountedOrdinaryFp(int boothIndex) {
  auto const& fp = readBooths().at(boothIndex).node.fpVotesCurrent;
  auto observed = LiveNewVotes::project(fp,{},LivePartialCount::total(fp));
  storeFpProjection(boothIndex,mapFpProjection(boothIndex,observed));
}

void Election::storeFpProjection(int boothIndex, std::map<int,double> projection) {
  auto& boothNode = preparationBooths().at(boothIndex).node;
  if (createRandomVariation) boothNode.tempFpVotesProjected = std::move(projection);
  else boothNode.fpVotesProjected = std::move(projection);
}

void Election::recordFpProjectionSensitivity(int boothIndex, double additions) {
  if (createRandomVariation) return;
  auto const& booth = readBooths().at(boothIndex);
  auto& seat = seats.at(booth.parentSeatId);
  for (auto const& [party,votes] : booth.node.fpVotesProjected) {
    if (booth.boothType != Results2::Booth::Type::Normal && booth.boothType != Results2::Booth::Type::Invalid
      && booth.boothType != Results2::Booth::Type::Other)
      seat.fpBoothTypeSensitivity[party][booth.boothType] += additions;
    else if (booth.voteType != Results2::VoteType::Ordinary && booth.voteType != Results2::VoteType::Invalid)
      seat.fpVoteTypeSensitivity[party][booth.voteType] += additions;
  }
}

Election::OrdinaryFpHierarchy Election::collectOrdinaryFpEvidence() const {
  OrdinaryFpHierarchy hierarchy;
  hierarchy.seats.reserve(seats.size());
  hierarchy.regions.resize(largeRegions.size());
  for (auto const& seat : seats) {
    std::vector<LiveNewVotes::FpEvidence> children;
    children.reserve(seat.booths.size());
    for (int index : seat.booths) {
      auto const& booth = readBooths().at(index);
      auto const& n = booth.node;
      // Keep all expected category sizes in the confidence denominator. Only
      // ordinary polling locations train this starting primary-movement prior;
      // each declaration service's current batch is blended in separately.
      children.push_back({expectedVotesForAggregation(n),double(n.totalFpVotesCurrent()),
        booth.voteType == Results2::VoteType::Ordinary ? n.fpConfidence : 0,
        n.relevanceModifier,n.fpDeviations,{}, {}});
    }
    auto evidence = LiveNewVotes::aggregate(children,expectedVotesForAggregation(seat.node));
    prepareIndependentPooling(evidence,seat);
    hierarchy.seats.push_back(std::move(evidence));
  }
  return hierarchy;
}

void Election::aggregateOrdinaryFpEvidence(OrdinaryFpHierarchy& hierarchy) const {
  for (std::size_t i = 0; i < largeRegions.size(); ++i) {
    std::vector<LiveNewVotes::FpEvidence> children;
    for (int seat : largeRegions[i].seats) children.push_back(hierarchy.seats.at(seat));
    hierarchy.regions[i] = LiveNewVotes::aggregate(children,expectedVotesForAggregation(largeRegions[i].node));
  }
  hierarchy.election = LiveNewVotes::aggregate(hierarchy.regions,expectedVotesForAggregation(node));
}

void Election::conditionOrdinaryFpEvidence(OrdinaryFpHierarchy& hierarchy) const {
  LiveNewVotes::condition(hierarchy.election,{},obsWeight(double(hierarchy.election.confidence),VoteObsWeightStrength*2.5),
    fpPoolingObservationWeights(hierarchy.election,VoteObsWeightStrength*2.5));
  for (auto& region : hierarchy.regions)
    LiveNewVotes::condition(region,{&hierarchy.election},obsWeight(double(region.confidence)),
      fpPoolingObservationWeights(region,VoteObsWeightStrength));
  for (std::size_t i = 0; i < seats.size(); ++i) {
    auto& evidence = hierarchy.seats[i];
    LiveNewVotes::condition(evidence,{&hierarchy.election,&hierarchy.regions.at(seats[i].parentRegionIndex)},
      obsWeight(double(evidence.confidence)));
  }
}

void Election::prepareNewVoteContexts() {
  auto hierarchy = collectOrdinaryFpEvidence();
  aggregateOrdinaryFpEvidence(hierarchy);
  conditionOrdinaryFpEvidence(hierarchy);
  newVoteContexts = std::make_shared<std::vector<LiveNewVotes::Shares> const>(declarationFpContexts(hierarchy));
}

std::vector<LiveNewVotes::Shares> Election::declarationFpContexts(OrdinaryFpHierarchy const& hierarchy) const {
  // Prepare once at mean turnout. Variability samples and simulation draws
  // reuse these small, immutable ballot-specific contexts without disk reads
  // or copying an extra election hierarchy into every scenario.
  std::vector<LiveNewVotes::Shares> contexts(readBooths().size());
  for (std::size_t i = 0; i < readBooths().size(); ++i) {
    auto const& booth = readBooths()[i];
    if (!booth.receivesFpBatches() || turnoutFpTarget(int(i)) == 0) continue;
    contexts[i] = declarationFpContext(true,int(i),&hierarchy);
  }
  return contexts;
}

// Express the projection for unreported voters as fractions for the actual
// candidates on this ballot. The reference and live accounts share this
// conversion and addition order. Only their available information differs:
// the live context can include measured ordinary-booth movement, while the
// reference uses historical composition and the pre-election baseline alone.
LiveNewVotes::Shares Election::declarationFpContext(bool allowCurrentData, int boothIndex,
  OrdinaryFpHierarchy const* hierarchy) const {
  auto const& booth = readBooths().at(boothIndex);
  auto const& seat = seats.at(booth.parentSeatId);
  auto projected = projectUnreportedFp(allowCurrentData,boothIndex,hierarchy);
  int independent = allowCurrentData ? seat.liveIndependentPartyIndex : seat.independentPartyIndex;
  LiveNewVotes::Shares context;
  for (int candidate : booth.node.runningParties) {
    int effective = candidate == independent ? run.indPartyIndex : candidate;
    context[candidate] = projected.at(effective);
  }
  return LiveNewVotes::normalize(std::move(context));
}

void Election::projectDeclarationFp(int boothIndex) {
  auto& booth = preparationBooths().at(boothIndex);
  booth.futureFpShares = composeDeclarationFp(boothIndex);
  auto projected = LiveNewVotes::project(booth.node.fpVotesCurrent,booth.futureFpShares,turnoutFpTarget(boothIndex));
  storeFpProjection(boothIndex,mapFpProjection(boothIndex,projected));
  if (!createRandomVariation) updateDeclarationFpProgress(boothIndex);
}

LiveNewVotes::Shares Election::composeDeclarationFp(int boothIndex) const {
  auto const& booth = readBooths().at(boothIndex);
  auto const& seat = seats.at(booth.parentSeatId);
  auto category = booth.incrementalFp || booth.voteType == Results2::VoteType::Early || booth.voteType == Results2::VoteType::PrePoll
    ? LiveNewVotes::Category::AggregatedEarly : LiveNewVotes::Category::General;
  double target = turnoutFpTarget(boothIndex);
  auto const& context = newVoteContexts->at(boothIndex);
  LiveNewVotes::Shares shifts,variation;
  for (auto const& [candidate,share] : context) {
    int effective = candidate == seat.liveIndependentPartyIndex ? run.indPartyIndex : candidate;
    if (booth.voteType == Results2::VoteType::Postal && (candidate == 1 || candidate == natPartyIndex))
      shifts[candidate] = LiveNewVotes::PostalCoalitionLogOddsShift;
    if (createRandomVariation) {
      bool reported = booth.node.totalFpVotesCurrent() > 0;
      double scale = std::exp(-double(booth.node.totalVotesPrevious())*.0001);
      double stdDev = reported ? 7+12*scale : 6+10*scale;
      auto tag = reported ? VariabilityTag::NonOrdinaryFp : VariabilityTag::BoothProjectionFp;
      variation[candidate] = variabilityNormal(0,stdDev,boothIndex,effective,uint32_t(tag))/25;
    }
  }
  return LiveNewVotes::composition(booth.node.fpVotesCurrent,context,target,category,shifts,variation);
}

void Election::updateDeclarationFpProgress(int boothIndex) {
  auto& booth = preparationBooths().at(boothIndex);
  double target = turnoutFpTarget(boothIndex);
  auto category = booth.incrementalFp || booth.voteType == Results2::VoteType::Early || booth.voteType == Results2::VoteType::PrePoll
    ? LiveNewVotes::Category::AggregatedEarly : LiveNewVotes::Category::General;
  booth.node.fpCompletion = booth.node.fpConfidence = double(std::clamp(booth.node.totalFpVotesCurrent()/target,0.0,1.0));
  // The pooled category bias enters on the context side, whose influence
  // declines as this service's own primary composition becomes informative.
  double contextWeight = 1-LiveNewVotes::observedWeight(booth.node.totalFpVotesCurrent(),target,category);
  recordFpProjectionSensitivity(boothIndex,double(std::max(0.0,target-booth.node.totalFpVotesCurrent())*contextWeight));
}

void Election::assignBlindFpOthers(
    std::map<int, double>& votesProjected,
    std::set<int> const& blindOthers,
    int projectSeatIndex,
    double currentVotesEstimate,
    double othersAccountedFor) const {
    double remainingExpectedVotes = 0.01 * blindOthers.size() * currentVotesEstimate;
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
      double median = probabilityBands[(probabilityBands.size() - 1) / 2];
      remainingExpectedVotes = std::max(remainingExpectedVotes, median * currentVotesEstimate * 0.01 - othersAccountedFor);
    }
    for (auto const& partyId : blindOthers) {
      votesProjected[partyId] = remainingExpectedVotes / double(blindOthers.size());
    }
}


std::map<int,double> Election::projectUnreportedFp(bool allowCurrentData, int boothIndex,
  OrdinaryFpHierarchy const* hierarchy) const {
  // Historical composition, pooled category biases and ordinary movement form
  // the usual starting estimate. Existing new-candidate and missing-history
  // fallbacks still apply where that historical comparison is unavailable.
  auto const& booth = readBooths().at(boothIndex);
  auto const& seat = seats.at(booth.parentSeatId);
  int projectSeatIndex = project.seats().indexByName(seat.name);
  const double votesGuess = booth.boothType == Results2::Booth::Type::Hospital ? HospitalBoothVotesGuess : PreviousTotalVotesGuess;
  double previousTotalVotes = booth.node.totalVotesPrevious() ? booth.node.totalVotesPrevious() : votesGuess;
  double const currentVoteTarget = turnoutFpTarget(boothIndex);
  std::map<int, double> tempFpVotesProjected;
  double othersAccountedFor = 0.0;
  std::set<int> blindOthers;
  for (auto const& partyId : booth.node.runningParties) {
    double deviation = 0.0;
    int indIndex = allowCurrentData ? seat.liveIndependentPartyIndex : seat.independentPartyIndex;
    int effectivePartyId = partyId == indIndex ? run.indPartyIndex : partyId;
    if (allowCurrentData && booth.node.totalVotesPrevious()) {
      if (hierarchy) {
        // Declaration services inherit movement measured in ordinary booths.
        // Their own partial batch enters later, once, through the FP blend.
        for (auto evidence : {&hierarchy->seats.at(booth.parentSeatId),
          &hierarchy->regions.at(seat.parentRegionIndex),&hierarchy->election})
          if (evidence->specific.contains(effectivePartyId)) deviation += evidence->specific.at(effectivePartyId);
      } else {
        for (auto parent : getThisAndParents(booth))
          if (parent->specificFpDeviations.contains(effectivePartyId)) deviation += parent->specificFpDeviations.at(effectivePartyId);
      }
    }
    double randomFactor = 0.0;
    if (createRandomVariation) {
      // Use a conservative composition error for an unfinished booth.
      // This varies its party shares; the turnout component owns its size.
      double stdDev = 6.0 + 10.0 * std::exp(-static_cast<double>(booth.node.totalVotesPrevious()) * 0.0001);
      randomFactor = variabilityNormal(0.0, stdDev, boothIndex, effectivePartyId, uint32_t(VariabilityTag::BoothProjectionFp));
    }
    // Apply biases for this booth classification.
    if (allowCurrentData && boothTypeFpBiases.contains(effectivePartyId) && boothTypeFpBiases.at(effectivePartyId).contains(booth.boothType)) {
      deviation += boothTypeFpBiases.at(effectivePartyId).at(booth.boothType);
    }
    // Apply biases for declaration/ordinary vote type.
    if (allowCurrentData && voteTypeFpBiases.contains(effectivePartyId) && voteTypeFpBiases.at(effectivePartyId).contains(booth.voteType)) {
      deviation += voteTypeFpBiases.at(effectivePartyId).at(booth.voteType);
    }
    std::optional<double> baselineShareUntransformed;
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
      double prevVotes = booth.node.fpVotesPrevious.at(effectivePartyId);
      double prevShare = transformVoteShare(std::clamp(double(prevVotes) / double(previousTotalVotes) * 100.0, 0.5, 99.5));
      double baselineSwing = seat.node.fpSwingsBaseline.contains(effectivePartyId) ? seat.node.fpSwingsBaseline.at(effectivePartyId) : 0.0;
      double newShare = prevShare + baselineSwing + deviation + randomFactor;
      double newVotes = detransformVoteShare(newShare) * currentVoteTarget * 0.01;
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
      double baselineShare = transformVoteShare(baselineShareUntransformed.value_or(1.0));
      double weight = obsWeight(seats[booth.parentSeatId].node.fpConfidence, VoteObsWeightStrength);
      double currentPartyVotes = double(seats[booth.parentSeatId].node.fpVotesCurrent.at(partyId));
      double currentTotalVotes = double(seats[booth.parentSeatId].node.totalFpVotesCurrent());
      double seatShare = transformVoteShare(std::clamp(currentPartyVotes / currentTotalVotes * 100.0, 0.1, 99.9));
      double newShare = seatShare * weight + baselineShare * (1.0 - weight);
      double newVotes = detransformVoteShare(newShare + randomFactor) * currentVoteTarget * 0.01;
      tempFpVotesProjected[effectivePartyId] = newVotes;
      if (!baselineShareUntransformed.has_value()) {
        othersAccountedFor += newVotes;
      }
    } else if (baselineShareUntransformed.has_value()) {
      // No previous data and no current data, but we have a baseline
      // (Typically occurs for independents, early on the night when no useful data at all has been recorded for this party yet)
      double transformedBaselineShare = transformVoteShare(std::clamp(baselineShareUntransformed.value(), 0.1, 99.9));
      if (booth.voteType == Results2::VoteType::Postal) {
        deviation -= 5.0; // Rough approximation to account for first-time third party candidates generally doing worse on postal votes
        // TODO: Applied like this will bias the results to the negative, some sort of compensation should be applied to other votes
      }
      double adjustedShare = transformedBaselineShare + deviation + randomFactor;
      double detransformedShare = detransformVoteShare(adjustedShare);
      double newVotes = detransformedShare * currentVoteTarget * 0.01;
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

  assignBlindFpOthers(tempFpVotesProjected, blindOthers, projectSeatIndex, currentVoteTarget, othersAccountedFor);

  // normalize so that the sum of the votes is the same as the previous election (or other estimate)
  double totalVotes = 0.0;
  for (auto const& [partyId, votes] : tempFpVotesProjected) {
    if (!std::isfinite(votes) || votes < 0.0) {
      throw std::runtime_error(
        "Invalid FP projection for booth " + booth.name + ".");
    }
    totalVotes += votes;
  }
  if (!std::isfinite(totalVotes) || totalVotes <= 0.0) {
    throw std::runtime_error(
      "No positive FP projection was produced for booth " + booth.name + ".");
  }
  for (auto& [partyId, votes] : tempFpVotesProjected) {
    votes *= currentVoteTarget / totalVotes;
  }
  return tempFpVotesProjected;
}

void Election::recomposeBoothTcpVotes(int boothIndex) {
  auto& booth = preparationBooths().at(boothIndex);
  auto const& seat = seats.at(booth.parentSeatId);
  if (turnoutFpTarget(boothIndex) == 0) {
    if (createRandomVariation) booth.node.tempTcpVotesProjected.clear();
    else booth.node.tcpVotesProjected.clear();
    return;
  }
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
    auto tcpVotesProjected = std::map<int, double>();
    for (auto const& [partyId, votes] : booth.node.tcpVotesCurrent) { 
      int effectivePartyId = convertPartyId(partyId);
      tcpVotesProjected[effectivePartyId] += static_cast<double>(votes);
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
    if (auto revised = revisedBoothTcp(booth,firstPartyId,secondPartyId,
        createRandomVariation ? variabilityNormal(0,1,boothIndex,0,uint32_t(VariabilityTag::RevisedTcpTransfers)) : 0)) {
      // The same original-batch correction applies to independent/minor TCPs.
      // New FP voters remain a separate pool, using the existing future-primary
      // mixture for declaration services and the booth share for ordinary votes.
      double target = turnoutPairTarget(boothIndex);
      double futureVariation = createRandomVariation ? variabilityNormal(0,
        double(7+12*std::exp(-revised->votes*.0001)),boothIndex,0,uint32_t(VariabilityTag::NonOrdinaryTcp))/25 : 0;
      // Declaration arrivals use the same preference-only variation as an
      // unrevised account below. Even a one-vote FP correction must not change
      // the uncertainty model for all future voters. The revision's own
      // uncertain transfers are already included in revised->first.
      double futureShare = revised->first/revised->votes;
      if (booth.receivesFpBatches())
        futureShare = LiveNewVotes::nonClassicFirstShare(booth.futureFpShares,
          pairPreferenceFlows(booth,firstPartyId,secondPartyId),
          booth.preferenceEvidence,firstPartyId,secondPartyId,futureVariation);
      else if (createRandomVariation)
        futureShare = LivePartialCount::inverseLogOdds(
          LivePartialCount::logOdds(futureShare)+futureVariation);
      double first = revised->first+std::max(0.0,target-revised->votes)*futureShare;
      tcpVotesProjected = {{effectiveFirstPartyId,double(first)},{effectiveSecondPartyId,double(target-first)}};
      booth.node.tcpCompletion = double(std::clamp((revised->votes-revised->uncertainVotes())/target,0.0,1.0));
      booth.node.tcpConfidence = booth.node.tcpCompletion;
      if (createRandomVariation) booth.node.tempTcpVotesProjected = std::move(tcpVotesProjected);
      else booth.node.tcpVotesProjected = std::move(tcpVotesProjected);
      return;
    }
    // Ordinary results are usually complete, but an accepted partial TCP still
    // needs preferences on the FP votes already reported. Complete ordinary
    // pairs keep the existing cheap path during reduced preparation.
    if (booth.receivesFpBatches()
      || (booth.node.totalTcpVotesCurrent() < booth.node.totalFpVotesCurrent())) {
      double const confidenceScale = booth.voteType == Results2::VoteType::Ordinary
        ? std::max(fullComparisonAvailable ? 1.0 : 0.2, booth.node.tcpConfidence) : 1.0;
      double currentTotalVotesProjected = std::accumulate(tcpVotesProjected.begin(), tcpVotesProjected.end(), 0.0,
        [](double sum, const auto& pair) { return sum + pair.second; });
      double const expectedTotalVotes = turnoutPairTarget(boothIndex);
      // The turnout target includes missing preferences on counted FP as well
      // as genuinely new FP votes. Allocate their estimates without changing
      // either candidate's published TCP count.
      double totalAdditionalVotes = std::max(0.0, expectedTotalVotes - currentTotalVotesProjected);
      double stdDev = 7.0 + 12.0 * std::exp(-static_cast<double>(currentTotalVotesProjected) * 0.0001);

      double random = variabilityNormal(0.0, stdDev, boothIndex, 0 /* no party id required */, uint32_t(VariabilityTag::NonOrdinaryTcp));
      double projectionDifference = createRandomVariation ? random : 0.0;
      int const rawFirst = firstPartyId, rawSecond = secondPartyId;
      if (firstPartyId == seat.liveIndependentPartyIndex) firstPartyId = run.indPartyIndex;
      if (secondPartyId == seat.liveIndependentPartyIndex) secondPartyId = run.indPartyIndex;
      if (optionalPreferential && booth.voteType == Results2::VoteType::Postal) {
        // The legacy continuing-vote extrapolation still needs this adjustment.
        // Compulsory declaration pairs receive it through future FP instead.
        double adjustment = 2.0; // late-arriving postals are generally less friendly to Coalition than early postals
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
      // Normally the pair and FP report together. If preferences lag, keep
      // their already counted candidates fixed and infer only the unfinished
      // FP cohort using the seat's non-classic flow and local batch evidence.
      // Compulsory declaration arrivals use the shared future FP mixture;
      // continuing-vote extrapolation retains the existing OPV behaviour.
      double pending = std::max(0.0,double(booth.node.totalFpVotesCurrent())-currentTotalVotesProjected);
      double countedShare = tcpVotesProjected.at(firstPartyId)/currentTotalVotesProjected;
      double unfinishedFirst = optionalPreferential ? pending*countedShare :
        LivePartialCount::pendingFirst(booth.node.fpVotesCurrent,booth.node.tcpVotesCurrent,
          pairPreferenceFlows(booth,rawFirst,rawSecond),0,booth.preferenceEvidence,rawFirst,rawSecond,
          createRandomVariation ? random/25 : 0);
      double futureShare = LivePartialCount::inverseLogOdds(
        LivePartialCount::logOdds(countedShare)+projectionDifference/25);
      if (!optionalPreferential && booth.receivesFpBatches()) {
        // Non-classic finalists keep their existing seat preference prior,
        // but their new voters share the very same projected primary mix.
        // The retained cohort adjusts that prior once, with the standard small-
        // sample weight. Its pending-preference allocation above stays separate.
        futureShare = LiveNewVotes::nonClassicFirstShare(booth.futureFpShares,
          pairPreferenceFlows(booth,rawFirst,rawSecond),booth.preferenceEvidence,
          rawFirst,rawSecond,createRandomVariation ? random/25 : 0);
      }
      double additionalPartyOneVotes = double(unfinishedFirst+(totalAdditionalVotes-pending)*futureShare);
      tcpVotesProjected[firstPartyId] += additionalPartyOneVotes;
      tcpVotesProjected[secondPartyId] += totalAdditionalVotes - additionalPartyOneVotes;
      booth.node.tcpCompletion = std::clamp(currentTotalVotesProjected / expectedTotalVotes, 0.0, 1.0);
      booth.node.tcpConfidence = booth.node.tcpCompletion * confidenceScale;
    }
    else {
      booth.node.tcpConfidence = std::max(fullComparisonAvailable ? 1.0 : 0.2, booth.node.tcpConfidence);
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
    std::array<double, NumMethods> methodConfidence = { 0.0, 0.0, 0.0, 0.0 };
    std::array<double, NumMethods> tcpFirst = { 0.0, 0.0, 0.0, 0.0 };
    std::array<double, NumMethods> tcpSecond = { 0.0, 0.0, 0.0, 0.0 };
    const double fpTotalCurrent = booth.node.totalFpVotesCurrent();
    auto const& projectedFp = createRandomVariation ? booth.node.tempFpVotesProjected : booth.node.fpVotesProjected;
    double const fpTotalProjected = std::accumulate(projectedFp.begin(),projectedFp.end(),0.0,
      [](double sum, auto const& row) { return sum+row.second; });
    double const currentVoteTarget = turnoutFpTarget(boothIndex);
    bool const currentFpPairAvailable = fpTotalCurrent > 0.0
      && booth.node.fpVotesCurrent.contains(firstPartyId)
      && booth.node.fpVotesCurrent.contains(secondPartyId);
    bool const projectedFpPairAvailable = fpTotalProjected > 0.0
      && projectedFp.contains(convertPartyId(firstPartyId))
      && projectedFp.contains(convertPartyId(secondPartyId));
    if (
      seat.tcpFocusPartyIndex.has_value() && seat.tcpFocusPartyPrefFlow.has_value()
      && seat.tcpFocusPartyConfidence.value_or(0.0) > 0.0
      && (projectedFpPairAvailable || (booth.voteType == Results2::VoteType::Ordinary && currentFpPairAvailable))
    ) {
      // We have a preference flow estimate for the seat, use that to project
      // from the known first preference votes or (with less confidence) projected first preference votes
      int focusParty = seat.tcpFocusPartyIndex.value() == firstPartyId ? firstPartyId : secondPartyId;
      int otherParty = focusParty == firstPartyId ? secondPartyId : firstPartyId;
      // A declaration service's projected FP includes its separately blended
      // additions. Using current FP and scaling it to turnout would reintroduce
      // immediate extrapolation of the first tiny batch.
      bool const useCurrentFp = currentFpPairAvailable && booth.voteType == Results2::VoteType::Ordinary;
      double boothPrefs = useCurrentFp ?
        fpTotalCurrent - booth.node.fpVotesCurrent.at(focusParty) - booth.node.fpVotesCurrent.at(otherParty) :
        fpTotalProjected - projectedFp.at(convertPartyId(focusParty)) - projectedFp.at(convertPartyId(otherParty));
      double preferenceFlow = seat.tcpFocusPartyPrefFlow.value();
      if (createRandomVariation) {
        // Use a conservative composition error for an unfinished booth.
        // This varies its party shares; the turnout component owns its size.
        double stdDev = 4.0 + 10.0 * std::min(std::exp(-static_cast<double>(booth.node.totalVotesPrevious()) * 0.002), 2.0) + 1.5 * std::clamp(1.0 / std::sqrt(seat.tcpFocusPartyConfidence.value()), 1.0, 20.0);
        double random = variabilityNormal(0.0, stdDev, boothIndex, RandomGenerator::combinePartyIds(firstPartyId, secondPartyId), uint32_t(VariabilityTag::PreferenceFlow));
        preferenceFlow = basicTransformedSwing(preferenceFlow, random);
      }
      double boothFocusPartyPrefs = boothPrefs * preferenceFlow * 0.01;
      double boothFocusPartyFpVotes = useCurrentFp ?
        static_cast<double>(booth.node.fpVotesCurrent.at(focusParty)) :
        projectedFp.at(convertPartyId(focusParty));
      double boothFocusPartyTcpVotes = boothFocusPartyFpVotes + boothFocusPartyPrefs;
      double boothOtherPartyTcpVotes = useCurrentFp ?
        fpTotalCurrent - boothFocusPartyTcpVotes : fpTotalProjected - boothFocusPartyTcpVotes;
      double firstTcp = focusParty == firstPartyId ? boothFocusPartyTcpVotes : boothOtherPartyTcpVotes;
      double secondTcp = focusParty == firstPartyId ? boothOtherPartyTcpVotes : boothFocusPartyTcpVotes;
      double const fpTotalUsed = useCurrentFp ? fpTotalCurrent : fpTotalProjected;
      double targetRatio = currentVoteTarget / fpTotalUsed;
      methodConfidence[0] = std::sqrt(seat.tcpFocusPartyConfidence.value())
        * (useCurrentFp ? 0.5 : 0.2);
      tcpFirst[0] = firstTcp * targetRatio;
      tcpSecond[0] = secondTcp * targetRatio;

    }
    if (fullComparisonAvailable) {
      // We either don't have first preference data for this booth yet, or we don't have a preference flow estimate for the seat
      // We  *do* have a match between the current TCP count for the seat and the previous TCP count for the booth
      // So we can calculate the existing swing across the seat and project to this booth
      // TODO: need to add extra checks if the booth was previously in a different seat
      // (won't affect independents since they're counted separately at this stage but could affect e.g. Wills)
      double weightedSwing = 0.0;
      double summedWeight = 0.0;
      bool prefFlowAvailable = seat.tcpFocusPartyIndex.has_value() && seat.tcpFocusPartyPrefFlow.has_value()
        && seat.tcpFocusPartyConfidence.value_or(0.0) > 0.0;
      for (auto const& otherBoothIndex : seat.booths) {
        // Need to find the existing TCP swing for the seat, average swings across the seat
        // and then project to the booth
        // TODO: for booths with an fp recorded, estimate a tcp from that instead based on preference flows
        // in confirmed booths and weight according to the confidence in each metric
        auto const& otherBooth = readBooths().at(otherBoothIndex);
        if (otherBooth.node.tcpSwings.contains(firstPartyId)
          && otherBooth.node.tcpSwings.contains(secondPartyId)
          && otherBooth.node.totalTcpVotesCurrent() > 0
        ) {
          // This booth is a valid comparison
          double swing = otherBooth.node.tcpSwings.at(firstPartyId);
          double weight = static_cast<double>(otherBooth.node.totalFpVotesCurrent());
          weightedSwing += swing * weight;
          summedWeight += weight;
        }
        else if (otherBooth.node.fpSwings.size() > 0 && prefFlowAvailable) {
          /* calculate estimate for tcp swing and add it at a lower weight */
          int focusParty = seat.tcpFocusPartyIndex.value();
          if (otherBooth.node.fpSwings.contains(firstPartyId) && otherBooth.node.fpSwings.contains(secondPartyId)) {
            double fpSwingFirst = otherBooth.node.fpSwings.at(firstPartyId);
            double fpSwingSecond = otherBooth.node.fpSwings.at(secondPartyId);
            double fpSwingPrefs = -fpSwingFirst - fpSwingSecond;
            double preferenceFlow = firstPartyId == focusParty ? seat.tcpFocusPartyPrefFlow.value() : 100.0 - seat.tcpFocusPartyPrefFlow.value();
            double estimatedTcpSwing = (fpSwingFirst - fpSwingSecond) * 0.5 + fpSwingPrefs * (preferenceFlow * 0.01 - 0.5);
            double topTwoProportion = 0.01 * ( otherBooth.node.fpSharesPercent().at(firstPartyId) + otherBooth.node.fpSharesPercent().at(secondPartyId));
            double weight = static_cast<double>(otherBooth.node.totalFpVotesCurrent()) * topTwoProportion * topTwoProportion;
            weightedSwing += estimatedTcpSwing * weight;
            summedWeight += weight;
          }
        }
      }

      if (summedWeight > 0.0) {
        double averageSwing = weightedSwing / summedWeight;
        if (createRandomVariation) {
          // Remaining preference composition is less certain when the
          // supporting booth is small. Its total stays in the turnout account.
          double stdDev = 5.0 + 9.0 * std::exp(-static_cast<double>(booth.node.totalVotesPrevious()) * 0.0001);
          double random = variabilityNormal(0.0, stdDev, boothIndex, RandomGenerator::combinePartyIds(firstPartyId, secondPartyId), uint32_t(VariabilityTag::TcpSwing));
          averageSwing += random;
        }
        double tcpTransformedSharePrevious = transformVoteShare(std::clamp(
          double(booth.node.tcpVotesPrevious.at(firstPartyId)) /
            double(booth.node.totalTcpVotesPrevious()) * 100.0,
          0.01, 99.99));
        double tcpTransformedEstimateCurrent = tcpTransformedSharePrevious + averageSwing;
        double tcpEstimateCurrent = detransformVoteShare(tcpTransformedEstimateCurrent) * currentVoteTarget * 0.01;
        methodConfidence[1] = 0.01;
        tcpFirst[1] = tcpEstimateCurrent;
        tcpSecond[1] = currentVoteTarget - tcpEstimateCurrent;
      } else {
        // As a last resort use project the vote shares directly (without any kind of swing)
        // Can happen if the only booths reporting TCP data had a different TCP last time or are new.
        // Example case: 2025 Grayndler at 18:50 on election night
        for (auto const& [partyId, votes] : seat.node.tcpVotesCurrent) {
          double tcpProportionCurrent = double(votes) / double(seat.node.totalTcpVotesCurrent());
          double tcpEstimateCurrent = tcpProportionCurrent * currentVoteTarget;
          methodConfidence[1] = 0.001;
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
      LivePartialCount::SharedFinalistTcpEvidence comparison;

      for (auto const& otherBoothIndex : seat.booths) {
        // Compare the shared finalist against its previous opponent.
        auto const& otherBooth = readBooths().at(otherBoothIndex);
        double swing = 0.0;
        double weight = 0.0;
        if (otherBooth.node.tcpVotesCurrent.contains(firstPartyId)
          && otherBooth.node.tcpVotesCurrent.contains(secondPartyId)
          && otherBooth.node.tcpVotesPrevious.contains(sharedPreviousPartyId)
          && otherBooth.node.tcpVotesPrevious.contains(previousOpponentId)
          && otherBooth.node.totalTcpVotesCurrent() > 0
          && otherBooth.node.totalTcpVotesPrevious() > 0
        ) {
          // This booth is a valid comparison
          double previousSharedShare = transformVoteShare(std::clamp(
            double(otherBooth.node.tcpVotesPrevious.at(sharedPreviousPartyId)) /
              double(otherBooth.node.totalTcpVotesPrevious()) * 100.0,
            0.01, 99.99));
          double currentSharedShare = transformVoteShare(std::clamp(
            double(otherBooth.node.tcpVotesCurrent.at(sharedPreviousPartyId)) /
              double(otherBooth.node.totalTcpVotesCurrent()) * 100.0,
            0.01, 99.99));
          swing = currentSharedShare - previousSharedShare;
          weight = static_cast<double>(otherBooth.node.totalVotesPrevious()) * otherBooth.node.relevanceModifier;
        }
        // Include every location's expected size even when its TCP has not
        // arrived or its old pair cannot be compared. Restricting this total
        // to reporting booths makes one small result look like full coverage.
        comparison.addBooth(otherBooth.node.totalFpVotesProjected(), swing, weight);
      }
      if (comparison.available()) {
        double averageSwing = comparison.meanSwing();
        if (createRandomVariation) {
          // Remaining preference composition is less certain when the
          // supporting booth is small. Its total stays in the turnout account.
          double stdDev = 7.0 + 12.0 * std::exp(-static_cast<double>(booth.node.totalVotesPrevious()) * 0.0001);
          double random = variabilityNormal(0.0, stdDev, boothIndex, RandomGenerator::combinePartyIds(firstPartyId, secondPartyId), uint32_t(VariabilityTag::TcpSwingDifferentCandidate));
          averageSwing += random;
        }
        if (booth.voteType == Results2::VoteType::Postal
          && isMajorParty(sharedPreviousPartyId, natPartyIndex)) {
          // Rough approximation for a new non-major opponent generally doing
          // worse on postals. Do not impose it on two non-major finalists.
          averageSwing -= 5.0;
        }
        double tcpTransformedSharePrevious = transformVoteShare(std::clamp(
          double(booth.node.tcpVotesPrevious.at(sharedPreviousPartyId)) /
            double(booth.node.totalTcpVotesPrevious()) * 100.0,
          0.01, 99.99));
        double tcpTransformedEstimateCurrent = tcpTransformedSharePrevious + averageSwing;
        double tcpEstimateCurrent = detransformVoteShare(tcpTransformedEstimateCurrent) * currentVoteTarget * 0.01;
        methodConfidence[2] = comparison.confidence();
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
    if (currentVoteTarget > 0.0
      && (currentFpPairAvailable || projectedFpPairAvailable)) {
      // As a last resort, if we have FP data, use a 50% pref flow by default
      // Maybe refine this using preference guesses later
      // But for now it should prevent booth TCPs from being completely unassigned
      bool const useCurrentFp = currentFpPairAvailable;
      double boothPrefs = useCurrentFp ?
        fpTotalCurrent - booth.node.fpVotesCurrent.at(firstPartyId) - booth.node.fpVotesCurrent.at(secondPartyId) :
        fpTotalProjected - projectedFp.at(convertPartyId(firstPartyId)) - projectedFp.at(convertPartyId(secondPartyId));
      double preferenceFlow = 50.0;
      if (createRandomVariation) {
        // Use a conservative composition error for an unfinished booth.
        // This varies its party shares; the turnout component owns its size.
        double stdDev = 32.0
          + 10.0 * std::exp(
            -static_cast<double>(booth.node.totalVotesPrevious()) * 0.0001);
        double random = variabilityNormal(0.0, stdDev, boothIndex, RandomGenerator::combinePartyIds(firstPartyId, secondPartyId), uint32_t(VariabilityTag::PreferenceFlow));
        preferenceFlow = basicTransformedSwing(preferenceFlow, random);
      }
      double boothFirstPartyPrefs = boothPrefs * preferenceFlow * 0.01;
      double boothFirstPartyFpVotes = useCurrentFp ?
        static_cast<double>(booth.node.fpVotesCurrent.at(firstPartyId)) :
        projectedFp.at(convertPartyId(firstPartyId));
      double boothFirstPartyTcpVotes = boothFirstPartyFpVotes + boothFirstPartyPrefs;
      double boothSecondPartyTcpVotes = useCurrentFp ?
        fpTotalCurrent - boothFirstPartyTcpVotes : fpTotalProjected - boothFirstPartyTcpVotes;
      double targetRatio = currentVoteTarget / (useCurrentFp ? fpTotalCurrent : fpTotalProjected);
      methodConfidence[3] = 0.0001;
      tcpFirst[3] = boothFirstPartyTcpVotes * targetRatio;
      tcpSecond[3] = boothSecondPartyTcpVotes * targetRatio;
    }

    double firstPartyWeighted = 0.0;
    double secondPartyWeighted = 0.0;
    double totalWeight = 0.0;
    double highestConfidence = 0.0;
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

    if (totalWeight > 0.0 && highestConfidence > 0.0) {
      double estimatedFirstParty = firstPartyWeighted / totalWeight;
      double estimatedSecondParty = secondPartyWeighted / totalWeight;
      if (!std::isfinite(estimatedFirstParty)
        || !std::isfinite(estimatedSecondParty)
        || estimatedFirstParty < 0.0 || estimatedSecondParty < 0.0) {
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

bool Election::usesUnmatchedFpTpp(Booth const& booth) const {
  // Historical booths retain their existing swing-based projection. A new
  // physical booth (including PPVC) with neither measured nor inferred previous
  // TPP instead supplies an absolute estimate from its received primaries.
  // Declarations already use their separate received/future vote account;
  // actual classic TCP and OPV exhaustion keep their established paths.
  return !optionalPreferential && booth.voteType == Results2::VoteType::Ordinary
    && !booth.node.tppSharePrevious && booth.node.totalFpVotesCurrent() > 0
    && LiveEstimatedTpp::observedTppVotes(booth.node.tcpVotesCurrent,natPartyIndex) == 0;
}

double Election::projectUnmatchedBoothTpp(int boothIndex, double futureShare,
  double preferenceVariation) const {
  auto const& booth = readBooths().at(boothIndex);
  int coalition = coalitionFinalist(booth);
  // Use the same flow corrections as estimateBoothTpp, but allocate the exact
  // FP counts rather than smoothed primary shares so major-party votes remain
  // hard lower bounds. Variation is a log-odds shift of preference rates only.
  double offset = booth.node.totalTcpVotesCurrent() == 0 && booth.preferenceEvidence
    ? futurePreferenceOffset(booth,0,coalition) : preferencePriorOffset(booth);
  return LivePartialCount::projectFpOnly(booth.node.fpVotesCurrent,
    classicPreferenceFlows(booth,coalition),offset+preferenceVariation,
    turnoutFpTarget(boothIndex),futureShare);
}

void Election::recomposeBoothTppVotes(bool allowCurrentData, int boothIndex) {
  auto& booth = preparationBooths().at(boothIndex);
  if (turnoutFpTarget(boothIndex) == 0) {
    if (createRandomVariation) booth.node.tempTppVotesProjected = {{0, 0.0}, {1, 0.0}};
    else {
      booth.node.tppVotesProjected = {{0, 0.0}, {1, 0.0}};
      booth.tppVotesEstimated = {{0, 0.0}, {1, 0.0}};
    }
    return;
  }
  if (allowCurrentData && booth.receivesFpBatches() && !optionalPreferential) {
    auto projected = projectDeclarationTpp(boothIndex);
    if (createRandomVariation) booth.node.tempTppVotesProjected = std::move(projected);
    else booth.node.tppVotesProjected = std::move(projected);
    return;
  }
  bool dataExists = allowCurrentData && booth.node.totalTcpVotesCurrent() && isTppSet(booth.node.tcpVotesCurrent, natPartyIndex);
  // Do this last section even if there is a known TPP count, so that we can compare and calculate a bias
  // If we don't have an actual TCP count, just make an estimate based on observed deviations
  // Even if the seat doesn't end up using TPP, this will still be used to estimate the fp votes
  // and the actual TCP will come from fp-based estimates in the simulation.
  double const currentVoteTarget = turnoutFpTarget(boothIndex);
  std::map<int, double> tempTppVotesProjected;
  std::optional<double> prevAlpShare;
  double const previousTcpTotal = double(booth.node.totalTcpVotesPrevious());
  if (previousTcpTotal > 0.0
    && isTppSet(booth.node.tcpVotesPrevious, natPartyIndex)) {
    // TPP is a share of the TCP count. The mapped FP total may differ when
    // multiple historical candidates collapse to one internal party, so using
    // it here would make recomposition inconsistent with calculateTppSwing().
    prevAlpShare = transformVoteShare(
      double(booth.node.tcpVotesPrevious.at(0)) / previousTcpTotal * 100.0);
  }
  std::optional<double> seatBaselineAlpShare;
  if (seats[booth.parentSeatId].node.tppShareBaseline.has_value()) {
    seatBaselineAlpShare = seats[booth.parentSeatId].node.tppShareBaseline.value();
  }
  std::optional<double> baselineAlpShare =
    (prevAlpShare.has_value()
      ? prevAlpShare.value() + booth.node.tppSwingBaseline.value_or(0.0)
      : seatBaselineAlpShare.value_or(0.0) // this already has the baseline swing factored in
    );
  double deviation = 0.0;
  if (allowCurrentData) {
    if (dataExists) {
      // When we have a non-classic TCP seat, there isn't a booth or seat TPP deviation
      // so when we want to calibrate them to the known-TPP data, we shouldn't use that either,
      // only the regional and election-level TPP
      auto const& seat = seats[booth.parentSeatId];
      auto const& region = largeRegions[seat.parentRegionIndex];
      auto const& thisAndParents = getThisAndParents(region);
      for (auto const& parent : thisAndParents) {
        deviation += parent->specificTppDeviation.value_or(0.0);
      }
    }
    else {
      auto const& thisAndParents = getThisAndParents(booth);
      for (auto const& parent : thisAndParents) {
        deviation += parent->specificTppDeviation.value_or(0.0);
      }
    }
  }
  double existingAlpShare = baselineAlpShare.value_or(
    previousTcpTotal > 0.0
      && isTppSet(booth.node.tcpVotesPrevious, natPartyIndex)
      ? transformVoteShare(
        double(booth.node.tcpVotesPrevious.at(0)) / previousTcpTotal * 100.0)
      : 0.0
    );
  double variation = 0.0;
  if (createRandomVariation) {
    // Allow greater composition uncertainty for smaller unfinished booths;
    // this response leaves their turnout targets unchanged.
    double stdDev = 4.5 + 7.0 * std::exp(-static_cast<double>(booth.node.totalVotesPrevious()) * 0.0001);
    variation = variabilityNormal(0.0, stdDev, boothIndex, 0, uint32_t(VariabilityTag::BoothProjectionTpp));
    deviation += variation;
  }
  // Apply biases for this booth type and vote type
  if (allowCurrentData && boothTypeTppBiases.contains(booth.boothType)) {
    deviation += boothTypeTppBiases.at(booth.boothType);
  }
  if (allowCurrentData && voteTypeTppBiases.contains(booth.voteType)) {
    deviation += voteTypeTppBiases.at(booth.voteType);
  }
  double newAlpShare = existingAlpShare + deviation;
  // The turnout account varies continuously; reaching an arbitrary completion
  // percentage must not discard its remaining-vote allowance.
  double totalEstimate = currentVoteTarget;
  // Ordinary shares allocate the turnout target proportionally. A tiny
  // historical category can legitimately have all its votes for one party,
  // giving an exact 0% or 100% transformed-share endpoint. Convert the share
  // to a fraction first so rounding cannot allocate more than the target and
  // leave a negative Coalition remainder. This changes no preference or turnout
  // assumption; the validation below still rejects genuinely invalid counts.
  double newAlpVotes = votesFromPercentage(detransformVoteShare(newAlpShare),totalEstimate);
  if (allowCurrentData && usesUnmatchedFpTpp(booth)) {
    // The baseline/deviation share remains useful for genuinely unreported
    // voters. It must not replace the FP evidence from an unmatched booth:
    // once all its FP is in, the entire account comes from those primaries and
    // estimated preferences, even if that differs sharply from the seat prior.
    newAlpVotes = double(projectUnmatchedBoothTpp(boothIndex,
      newAlpVotes/totalEstimate,variation/25.0));
  }
  tempTppVotesProjected[0] = newAlpVotes;
  int coalitionPartyId = 1;
  tempTppVotesProjected[coalitionPartyId] = totalEstimate - newAlpVotes;

  for (auto const& [partyId, votes] : tempTppVotesProjected) {
    if (!std::isfinite(votes) || votes < 0.0) {
      std::ostringstream message;
      message << std::setprecision(17) << "Invalid TPP projection for "
        << seats.at(booth.parentSeatId).name << " / " << booth.name
        << ": party " << partyId << " has " << votes
        << " votes; expected total " << totalEstimate << ".";
      throw std::runtime_error(message.str());
    }
  }
  if (dataExists) {
    auto projected = projectCountedTpp(boothIndex,newAlpVotes/totalEstimate);
    if (createRandomVariation) booth.node.tempTppVotesProjected = std::move(projected);
    else booth.node.tppVotesProjected = std::move(projected);
  }
  if (createRandomVariation) {
    if (!dataExists) booth.node.tempTppVotesProjected = tempTppVotesProjected;
  } else {
    if (!dataExists) {
      booth.node.tppVotesProjected = tempTppVotesProjected;
    }
    else { // dataExists
      booth.tppVotesEstimated = tempTppVotesProjected;
    }
  }
}

void Election::prepareTppProjectionSensitivities() {
  // Composition can be refreshed more than once before variability is ready.
  // Collect category exposure once from the final accounts, outside that flow.
  for (int seatIndex = 0; seatIndex < int(seats.size()); ++seatIndex)
    prepareSeatTppProjectionSensitivities(seatIndex);
}

void Election::prepareSeatTppProjectionSensitivities(int seatIndex) {
  auto& seat = seats.at(seatIndex);
  std::vector<LiveTppSensitivity::Unit> units;
  units.reserve(seat.booths.size());
  for (int boothIndex : seat.booths) {
    auto const& booth = readBooths().at(boothIndex);
    bool const hasObservedTpp = LiveEstimatedTpp::observedTppVotes(
      booth.node.tcpVotesCurrent, natPartyIndex) > 0;
    // The unmatched booth's received FP estimate no longer moves with a PPVC
    // swing adjustment. Only any genuinely unreported votes retain that
    // category sensitivity; preference uncertainty remains separate.
    double exposedVotes = turnoutFpTarget(boothIndex);
    if (usesUnmatchedFpTpp(booth))
      exposedVotes = std::max(0.0,exposedVotes-double(booth.node.totalFpVotesCurrent()));
    units.push_back({booth.boothType, booth.voteType, exposedVotes, hasObservedTpp});
  }
  auto sensitivity = LiveTppSensitivity::prepare(units, optionalPreferential);
  seat.tppBoothTypeSensitivity = std::move(sensitivity.boothTypes);
  seat.tppVoteTypeSensitivity = std::move(sensitivity.voteTypes);
}

void Election::recomposeSeatFpVotes(int seatIndex) {
  if (auto const& final = seats[seatIndex].finalResult) {
    seats[seatIndex].node.fpVotesProjected = {final->fp.begin(),final->fp.end()};
    seats[seatIndex].node.fpCompletion = seats[seatIndex].node.fpConfidence = 1;
    return;
  }
  seats[seatIndex].node.fpVotesProjected = std::map<int, double>();
  for (auto const& boothIndex : seats[seatIndex].booths) {
    for (auto const& [partyId, votes] : readBooths()[boothIndex].node.fpVotesProjected) {
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
  if (auto const& final = seats[seatIndex].finalResult) {
    auto& n = seats[seatIndex].node;
    n.tcpVotesProjected = {final->tcp.begin(),final->tcp.end()};
    n.tcpShares.clear();
    // A quarter-vote equivalent keeps display log odds finite at a zero end
    // point; the final counted votes and reported percentages stay exact.
    for (auto const& [p,v] : n.tcpVotesProjected)
      n.tcpShares[p] = double(25*std::log((v+.25)/(n.totalTcpVotesProjected()-v+.25)));
    n.tcpCompletion = n.tcpConfidence = 1;
    return;
  }
  seats[seatIndex].node.tcpVotesProjected = std::map<int, double>();
  auto& seatNode = seats[seatIndex].node;
  seatNode.tcpShares.clear();
  seatNode.tcpCompletion = 0.0;
  seatNode.tcpConfidence = 0.0;
  double weightedCompletion = 0.0;
  double weightedConfidence = 0.0;
  double totalWeight = 0.0;
  for (auto const& boothIndex : seats[seatIndex].booths) {
    auto const& boothNode = readBooths()[boothIndex].node;
    double const boothWeight = boothNode.totalFpVotesProjected();
    if (!std::isfinite(boothWeight) || boothWeight < 0.0) {
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
      if (!std::isfinite(votes) || votes < 0.0) {
        throw std::runtime_error(
          "Invalid TCP projection in seat " + seats[seatIndex].name + ".");
      }
      seatNode.tcpVotesProjected[partyId] += votes;
    }
  }
  if (seatNode.totalTcpVotesProjected() == 0) return;
  if (totalWeight <= 0.0) {
    throw std::runtime_error(
      "No positive TCP aggregation weight in seat " + seats[seatIndex].name + ".");
  }
  if (seatNode.totalTcpVotesProjected() < seatNode.totalFpVotesProjected() * 0.95) {
    // In some cases (e.g. mismatched declaration vote counts and no ordinary vote counts,
    // such as Griffith 2025 at 2025-05-04 14:02), TCP projections are only done for some
    // booths. Since this can be very unrepresentative, we can't assign any confidence to such
    // cases, but should still record that the votes exist.
    // This fixed comparison assumes compulsory preferential voting. Under OPV,
    // legitimate exhaustion must be separated from genuinely missing booth TCP
    // projections before assigning confidence.
    seatNode.tcpCompletion = weightedCompletion / totalWeight;
    seatNode.tcpConfidence = 0.0;
    return;
  }
  for (auto const& [partyId, votes] : seatNode.tcpVotesProjected) {
    double const share = votes / seatNode.totalTcpVotesProjected() * 100.0;
    seatNode.tcpShares[partyId] = transformVoteShare(
      std::clamp(share, 0.1, 99.9));
  }
  seatNode.tcpCompletion = weightedCompletion / totalWeight;
  seatNode.tcpConfidence = weightedConfidence / totalWeight;
}

void Election::recomposeSeatTppVotes(int seatIndex) {
  if (auto const& final = seats[seatIndex].finalResult; final && !final->tpp.empty()) {
    seats[seatIndex].node.tppVotesProjected = {final->tpp.begin(),final->tpp.end()};
    return;
  }
  // Just collect all the tpp votes from the booths in the seat
  // then convert them in to shares

  seats[seatIndex].node.tppVotesProjected = std::map<int, double>();
  for (auto const& boothIndex : seats[seatIndex].booths) {
    for (auto const& [partyId, votes] : readBooths()[boothIndex].node.tppVotesProjected) {
      seats[seatIndex].node.tppVotesProjected[partyId] += votes;
    }
  }
}

void Election::recomposeLargeRegionFpVotes(int largeRegionIndex) {
  largeRegions[largeRegionIndex].node.fpVotesProjected = std::map<int, double>();
  for (auto const& seatIndex : largeRegions[largeRegionIndex].seats) {
    for (auto const& [partyId, votes] : seats[seatIndex].node.fpVotesProjected) {
      largeRegions[largeRegionIndex].node.fpVotesProjected[partyId] += votes;
    }
  }
}

void Election::recomposeLargeRegionTppVotes(int largeRegionIndex) {
  largeRegions[largeRegionIndex].node.tppVotesProjected = std::map<int, double>();
  for (auto const& seatIndex : largeRegions[largeRegionIndex].seats) {
    for (auto const& [partyId, votes] : seats[seatIndex].node.tppVotesProjected) {
      largeRegions[largeRegionIndex].node.tppVotesProjected[partyId] += votes;
    }
  }
}

void Election::recomposeElectionFpVotes() {
  node.fpVotesProjected = std::map<int, double>();
  for (auto const& largeRegion : largeRegions) {
    for (auto const& [partyId, votes] : largeRegion.node.fpVotesProjected) {
      node.fpVotesProjected[partyId] += votes;
    }
  }
}

void Election::recomposeElectionTppVotes() {
  node.tppVotesProjected = std::map<int, double>();
  for (auto const& largeRegion : largeRegions) {
    for (auto const& [partyId, votes] : largeRegion.node.tppVotesProjected) {
      node.tppVotesProjected[partyId] += votes;
    }
  }
}

static double offsetWeight(double completion, double confidence) {
  // This cubic fade is currently heuristic. It should ideally be calibrated
  // against historical counts once enough suitable snapshots are available.
  if (!std::isfinite(completion) || !std::isfinite(confidence)) {
    throw std::runtime_error("Cannot calculate an offset weight from non-finite evidence.");
  }
  double const evidence = std::clamp(
    std::max(completion, confidence), 0.0, 1.0);
  return std::pow(1.0 - evidence, 3.0);
}

static double offsetWeightFp(LiveV2::Node const& node) {
  return offsetWeight(node.fpCompletion, node.fpConfidence);
}

static double offsetWeightTpp(LiveV2::Node const& node) {
  return offsetWeight(node.tppCompletion, node.tppConfidence);
}

void Election::retainObservedIndependentPooling(Node const& source, std::map<int,double>& finalDeviations) const {
  // For ordinary parties the change in their total projected vote summarizes
  // the shared movement. Independent projections combine distinct campaigns,
  // including seats still inheriting the pooled estimate. Summing those
  // predictions would recycle that inheritance as fresh evidence and undo
  // candidate-specific source weights. Pass the accepted observed movement
  // instead; each seat's local account still uses all its actual counted votes.
  finalDeviations[run.indPartyIndex] = source.specificFpDeviations.contains(run.indPartyIndex)
    ? source.specificFpDeviations.at(run.indPartyIndex) : 0;
}

void Election::determineElectionFinalFpDeviations(bool allowCurrentData) {
  auto const shares = projectedVoteShares(
    node.fpVotesProjected, "Election");
  // now convert to deviations
  for (auto const& [partyId, share] : shares) {
    // TODO: this should be sourced from the simulation report
    double finalDeviation = node.fpSharesBaseline.contains(partyId)
      ? transformVoteShare(std::clamp(share * 100.0, 0.1, 99.9))
        - node.fpSharesBaseline.at(partyId)
      : 0.0;

    if (allowCurrentData) {
      double offset = offsetSpecificFpDeviations.contains(partyId)
        ? offsetSpecificFpDeviations.at(partyId) * offsetWeightFp(node)
        : 0.0;
      finalSpecificFpDeviations[partyId] = finalDeviation - offset;
    } else {
      offsetSpecificFpDeviations[partyId] = finalDeviation;
    }
  }
  if (allowCurrentData) retainObservedIndependentPooling(node,finalSpecificFpDeviations);
}

void Election::determineElectionFinalTppDeviation(bool allowCurrentData) {
  auto const alpShare = projectedTppAlpShare(
    node.tppVotesProjected, "Election");
  if (!alpShare) {
    // No tpp estimations, so don't modify anything at the seat level
    if (allowCurrentData) {
      finalSpecificTppDeviation = 0.0;
    } else {
      offsetSpecificTppDeviation = 0.0;
    }
    return;
  }
  double const finalDeviation = transformVoteShare(
    std::clamp(*alpShare * 100.0, 0.1, 99.9))
    - node.tppShareBaseline.value_or(0.0);
  if (allowCurrentData) {
    // This will eventually be replaced with something a lot more sophisticated,
    // but for now this is a simple way to account for factors that may bias
    // the area's TPP share away from the baseline, such as redistributions, new
    // booths, or expected shifts between vote types.
    // use completion rather than confidence to make sure that complete TPP results
    // in no offset, even if confidence is low due to inability to measure swings
    double offset = offsetSpecificTppDeviation.value_or(0.0) * offsetWeightTpp(node);
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
    double inheritedDeviation = 0.0;
    if (allowCurrentData) {
      if (finalSpecificFpDeviations.contains(partyId)) {
        inheritedDeviation += finalSpecificFpDeviations.at(partyId);
      }
    }
    double finalDeviation = largeRegions[largeRegionIndex].node.fpSharesBaseline.contains(partyId)
      ? transformVoteShare(std::clamp(share * 100.0, 0.1, 99.9))
        - largeRegions[largeRegionIndex].node.fpSharesBaseline.at(partyId)
        - inheritedDeviation
      : 0.0;
    if (allowCurrentData) {
      double offset = largeRegions[largeRegionIndex].offsetSpecificFpDeviations.contains(partyId)
        ? largeRegions[largeRegionIndex].offsetSpecificFpDeviations.at(partyId) * offsetWeightFp(largeRegions[largeRegionIndex].node)
        : 0.0;
      largeRegions[largeRegionIndex].finalSpecificFpDeviations[partyId] =
        finalDeviation - offset;
    } else {
      largeRegions[largeRegionIndex].offsetSpecificFpDeviations[partyId] =
        finalDeviation;
    }
  }
  if (allowCurrentData) retainObservedIndependentPooling(largeRegions[largeRegionIndex].node,
    largeRegions[largeRegionIndex].finalSpecificFpDeviations);
}

void Election::determineLargeRegionFinalTppDeviation(bool allowCurrentData, int largeRegionIndex) {
  auto& region = largeRegions[largeRegionIndex];
  auto const alpShare = projectedTppAlpShare(
    region.node.tppVotesProjected, "Region " + region.name);
  if (!alpShare) {
    // No tpp estimations, so don't modify anything at the seat level
    if (allowCurrentData) {
      largeRegions[largeRegionIndex].finalSpecificTppDeviation = 0.0;
    } else {
      largeRegions[largeRegionIndex].offsetSpecificTppDeviation = 0.0;
    }
    return;
  }
  double deviation = 0.0;
  if (allowCurrentData) {
    deviation += finalSpecificTppDeviation.value_or(0.0);
  }
  double const finalDeviation = transformVoteShare(
    std::clamp(*alpShare * 100.0, 0.1, 99.9))
    - region.node.tppShareBaseline.value_or(0.0) - deviation;
  if (allowCurrentData) {
    // This will eventually be replaced with something a lot more sophisticated,
    // but for now this is a simple way to account for factors that may bias
    // the seat's TPP share away from the baseline, such as redistributions, new
    // booths, or expected shifts between vote types.
    // use completion rather than confidence to make sure that complete TPP results
    // in no offset, even if confidence is low due to inability to measure swings
    double offset = largeRegions[largeRegionIndex].offsetSpecificTppDeviation.value_or(0.0) * offsetWeightTpp(largeRegions[largeRegionIndex].node);
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
    double inheritedDeviation = 0.0;
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
    double finalDeviation = seats[seatIndex].node.fpSharesBaseline.contains(partyId)
      ? transformVoteShare(std::clamp(share * 100.0, 0.1, 99.9))
        - seats[seatIndex].node.fpSharesBaseline.at(partyId)
        - inheritedDeviation
      : 0.0;
    if (allowCurrentData) {
      double offset = seats[seatIndex].offsetSpecificFpDeviations.contains(partyId)
        ? seats[seatIndex].offsetSpecificFpDeviations.at(partyId) * offsetWeightFp(seats[seatIndex].node)
        : 0.0;
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
      seats[seatIndex].finalSpecificTppDeviation = 0.0;
    } else {
      seats[seatIndex].offsetSpecificTppDeviation = 0.0;
    }
    return;
  }
  double inheritedDeviation = 0.0;
  if (allowCurrentData) {
    auto const& parentRegion = largeRegions[seats[seatIndex].parentRegionIndex];
    inheritedDeviation += parentRegion.finalSpecificTppDeviation.value_or(0.0);
    inheritedDeviation += finalSpecificTppDeviation.value_or(0.0);
  }
  double const finalDeviation = transformVoteShare(
    std::clamp(*alpShare * 100.0, 0.1, 99.9))
    - seat.node.tppShareBaseline.value_or(0.0) - inheritedDeviation;
  if (allowCurrentData) {
    // This will eventually be replaced with something a lot more sophisticated,
    // but for now this is a simple way to account for factors that may bias
    // the seat's TPP share away from the baseline, such as redistributions, new
    // booths, or expected shifts between vote types.
    // use completion rather than confidence to make sure that complete TPP results
    // in no offset, even if confidence is low due to inability to measure swings
    double offset = seats[seatIndex].offsetSpecificTppDeviation.value_or(0.0) * offsetWeightTpp(seats[seatIndex].node);
    seats[seatIndex].finalSpecificTppDeviation = finalDeviation - offset;
  } else {
    seats[seatIndex].offsetSpecificTppDeviation = finalDeviation;
  }
}

LiveEstimatedTpp::Unit Election::estimatedTppUnit(int boothIndex) const {
  auto const& booth = readBooths().at(boothIndex);
  auto const& n = booth.node;
  double measured = LiveEstimatedTpp::observedTppVotes(n.tcpVotesCurrent,natPartyIndex);
  if (auto revised = revisedBoothTcp(booth,0,coalitionFinalist(booth)))
    measured = revised->votes-revised->uncertainVotes();
  return {turnoutFpTarget(boothIndex), double(n.totalFpVotesCurrent()),
    measured,
    n.fpConfidence,n.tppCompletion,n.relevanceModifier,
    n.tppDeviation.has_value(),optionalPreferential};
}

void Election::prepareEstimatedTppEvidence() {
  // Freeze the received sources after turnout has supplied the common count
  // denominators. Scenarios vary their finite remaining pools, not whether a
  // feed declared the TCP candidates or which source counts supplied evidence.
  for (auto& seat : seats) {
    seat.estimatedTppEvidence = {};
    for (int booth : seat.booths) seat.estimatedTppEvidence.add(estimatedTppUnit(booth));
    seat.estimatedTppInfluence = double(seat.estimatedTppEvidence.influence());
  }
}

void LiveV2::Election::calculateTppEstimateBias()
{
  nonClassicTppBiasPercentagePoints = 0.0;
  nonClassicTppBiasConfidence = 0.0;

  std::map<int, double> recordedVotes;
  std::map<int, double> estimatedVotes;
  double totalProjectedTppVotes = 0.0;
  double completedClassicTppVotes = 0.0;
  double completedClassicTppVotesSquared = 0.0;
  for (auto const& booth : readBooths()) {
    for (auto const& [partyId, votes] : booth.node.tppVotesProjected) {
      if (!std::isfinite(votes) || votes < 0.0) {
        throw std::runtime_error(
          "Invalid projected TPP votes while estimating non-classic bias for booth "
          + booth.name + ".");
      }
      totalProjectedTppVotes += votes;
    }

    if (booth.hasTcpRevision() || booth.node.tppCompletion < 0.9
      || !isTppSet(booth.node.tcpVotesCurrent, natPartyIndex)) {
      continue;
    }
    if (!booth.tppVotesEstimated.contains(0)
      || !booth.tppVotesEstimated.contains(1)) {
      throw std::runtime_error(
        "Completed classic TPP booth lacks a comparable TPP estimate: "
        + booth.name + ".");
    }

    double boothRecordedVotes = 0.0;
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
  double recordedDenominator = recordedVotes[0] + recordedVotes[1];
  double estimatedDenominator = estimatedVotes[0] + estimatedVotes[1];
  if (recordedDenominator > 0 && estimatedDenominator > 0) {
    double recordedAlpShare = recordedVotes[0] / recordedDenominator;
    double estimatedAlpShare = estimatedVotes[0] / estimatedDenominator;
    nonClassicTppBiasPercentagePoints =
      (estimatedAlpShare - recordedAlpShare) * 100.0;

    double const voteCoverage = totalProjectedTppVotes > 0.0
      ? std::clamp(completedClassicTppVotes / totalProjectedTppVotes, 0.0, 1.0)
      : 0.0;
    double const effectiveBoothCount = completedClassicTppVotesSquared > 0.0
      ? completedClassicTppVotes * completedClassicTppVotes
        / completedClassicTppVotesSquared
      : 0.0;
    // Pragmatic first pass: require both broad vote coverage and several
    // independently completed booths. This curve and its scale should
    // eventually be calibrated, ideally using booth-level bias dispersion.
    double const boothBreadth = 1.0 - std::exp(
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

    double expectedPartyOnePrefs = 0.0;
    double totalMinorPartyPrefs = 0.0;
    // Calculate estimate of party one's share of the TPP based on the FP votes
    double totalFpVotes = std::accumulate(
      seat.node.fpVotesProjected.begin(),
      seat.node.fpVotesProjected.end(),
      0.0,
      [](double sum, const auto& pair) { return sum + pair.second; }
    );
    if (!std::isfinite(totalFpVotes) || totalFpVotes <= 0.0
      || !seat.node.fpVotesProjected.contains(0)) {
      seat.livePreferenceFlowDeviation = 0.0;
      continue;
    }
    double alpPrimaryPlusCoalitionLeakage = seat.node.fpVotesProjected.at(0) / totalFpVotes;
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
      double const share = votes / totalFpVotes;

      // now allocate preferences for non-major parties
      if (partyId == preferredCoalitionParty || partyId == 0) {
        // Major parties, don't include them in preference flow calculations
        continue;
      }
      else if (partyId == natPartyIndex || partyId == 1) {
        // Other coalition party's votes, assume some leakage to Labor
        alpPrimaryPlusCoalitionLeakage += share * CoalitionLeakagePercent * 0.01;
      }
      else if (preferenceFlowMap.contains(partyId)) {
        expectedPartyOnePrefs += share * preferenceFlowMap.at(partyId) * 0.01;
        totalMinorPartyPrefs += share;
      }
      else {
        expectedPartyOnePrefs += share
          * preferenceFlowMap.at(PartyCollection::InvalidIndex) * 0.01;
        totalMinorPartyPrefs += share;
      }
    }
    // With no projected non-major vote there is no preference-flow evidence.
    // Keep the neutral prior rather than producing a non-finite transformed share.
    if (totalMinorPartyPrefs <= 0.0) {
      seat.livePreferenceFlowDeviation = 0.0;
      continue;
    }
    double expectedPreferenceFlow = transformVoteShare(std::clamp(
      expectedPartyOnePrefs / totalMinorPartyPrefs * 100.0,
      0.1, 99.9));

    if (!seat.node.tppVotesProjected.contains(0)
      || !seat.node.tppVotesProjected.contains(1)) {
      seat.livePreferenceFlowDeviation = 0.0;
      continue;
    }
    double const totalTppVotes = seat.node.tppVotesProjected.at(0)
      + seat.node.tppVotesProjected.at(1);
    if (!std::isfinite(totalTppVotes) || totalTppVotes <= 0.0) {
      seat.livePreferenceFlowDeviation = 0.0;
      continue;
    }
    double actualTpp = seat.node.tppVotesProjected.at(0) / totalTppVotes;
    double actualPartyOnePrefs = actualTpp - alpPrimaryPlusCoalitionLeakage;
    double actualPreferenceFlow = transformVoteShare(std::clamp(
      actualPartyOnePrefs / totalMinorPartyPrefs * 100.0,
      0.1, 99.9));

    double deviation = actualPreferenceFlow - expectedPreferenceFlow;
    seat.livePreferenceFlowDeviation = deviation;
  }
}

void Election::prepareVariability() {
  // The active FP/TCP scales retain all 288 deterministic samples. The old
  // TPP RMS is optional inspection data; the joint update owns TPP uncertainty.
  auto const started = std::chrono::steady_clock::now();
  resetPreparedVariability();
  auto const baselines = prepareVariabilityBaselines(project.config().getLiveLegacyTppDiagnostic());
  auto const samples = runVariabilitySamples(baselines);
  storePreparedVariability(samples);
  logger << "Live uncertainty preparation: "
    << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
    << " seconds\n";
}

void Election::resetPreparedVariability() {
  for (auto& seat : seats) {
    seat.fpAllBoothsStdDev.clear();
    seat.tppAllBoothsStdDev.reset();
    seat.tcpAllBoothsStdDev.reset();
  }
}

std::vector<LiveVariability::Baseline> Election::prepareVariabilityBaselines(bool includeTpp) const {
  std::vector<LiveVariability::Baseline> baselines;
  baselines.reserve(seats.size());
  for (auto const& seat : seats) {
    LiveVariability::Counts fp, tcp;
    std::optional<LiveVariability::Counts> tpp;
    if (includeTpp) tpp.emplace();
    bool const useTcp = seat.node.totalTcpVotesProjected() > 0.0;
    std::string const context = "Variability baseline for seat " + seat.name;
    // Sum the original booth projections in exactly the existing order. Seat
    // projections can also include declaration adjustments, so substituting
    // their totals here would change the comparison and is not an optimisation.
    for (int boothIndex : seat.booths) {
      auto const& n = readBooths().at(boothIndex).node;
      LiveVariability::addProjectedVotes(fp, n.fpVotesProjected, context + " base FP projection");
      if (useTcp) LiveVariability::addProjectedVotes(tcp, n.tcpVotesProjected, context + " base TCP projection");
      if (tpp) LiveVariability::addProjectedVotes(*tpp, n.tppVotesProjected, context + " base TPP projection");
    }
    baselines.push_back(LiveVariability::prepareBaseline(std::move(fp), tcp, tpp, context));
  }
  return baselines;
}

void Election::resetVariabilitySample(Election& working, int sampleIndex) const {
  // Uncertainty preparation tries many possible allocations of the uncounted
  // votes to measure how uncertain each seat's projected FP/TCP shares are,
  // with an optional TPP diagnostic calculated alongside them.
  // sampleVariability calls this before each trial. working is a private copy
  // of this snapshot's prepared election, reused by one worker; sampleIndex
  // identifies the trial's random draws. Resetting it prevents one trial's
  // altered projections from becoming the next trial's starting evidence.
  //
  // Restore all writable booth fields before visiting any booth: a projection
  // can read another booth, so a partial reset during calculation would be unsafe.
  // Parent seat/region evidence and source counts remain fixed during sampling.
  auto& workingBooths = working.preparationBooths();
  auto const& preparedBooths = readBooths();
  for (std::size_t b = 0; b < preparedBooths.size(); ++b)
    LiveVariability::restoreBoothForSample(workingBooths.at(b), preparedBooths[b]);
  working.createRandomVariation = true;
  working.variabilitySampleIndex = sampleIndex;
}

LiveVariability::Sample Election::sampleVariability(int sampleIndex,
  std::vector<LiveVariability::Baseline> const& baselines, Election& sampleElection) const {
  // Perform one trial in runVariabilitySamples: vary booth projections, add
  // them to seat totals and compare their shares with the unvaried projections
  // in baselines. The returned differences are on the model's transformed
  // vote-share scale; storePreparedVariability combines trials to estimate the
  // local FP/TCP uncertainty later used when integrating counts with the
  // forecast. The optional TPP differences are for inspection; the separate
  // joint TPP calculation supplies the forecast's TPP uncertainty.
  //
  // sampleElection is private working storage, restored before each trial.
  // Reusing it saves copying and allocating memory; it must not alter which
  // seats or booths are included, the number of trials, or their random draws.
  // We also retain the order of vote additions and subsequent calculations:
  // floating-point rounding can depend on that order. These constraints let
  // the optimization preserve the previous calculation's numerical results.
  resetVariabilitySample(sampleElection, sampleIndex);
  LiveVariability::Sample result(seats.size());
  for (std::size_t seatIndex = 0; seatIndex < seats.size(); ++seatIndex) {
    auto const& seat = sampleElection.seats[seatIndex];
    auto const& baseline = baselines.at(seatIndex);
    LiveVariability::Counts fp, tcp, tpp;
    std::string const context = "Variability sample " + std::to_string(sampleIndex) + " for seat " + seat.name;
    for (int boothIndex : seat.booths) {
      sampleElection.recomposeBoothFpVotes(true, boothIndex);
      if (baseline.tpp) sampleElection.recomposeBoothTppVotes(true, boothIndex);
      sampleElection.recomposeBoothTcpVotes(boothIndex);
      auto const& n = sampleElection.readBooths().at(boothIndex).node;
      LiveVariability::addProjectedVotes(fp, n.tempFpVotesProjected, context + " varied FP projection");
      if (baseline.tcp) LiveVariability::addProjectedVotes(tcp, n.tempTcpVotesProjected, context + " varied TCP projection");
      if (baseline.tpp) LiveVariability::addProjectedVotes(tpp, n.tempTppVotesProjected, context + " varied TPP projection");
    }
    auto& output = result[seatIndex];
    output.fp = LiveVariability::fpDifferences(baseline, fp, context);
    if (baseline.tcp) output.tcp = LiveVariability::pairDifference(tcp,
      baseline.tcp->first, baseline.tcp->second, context + " varied TCP projection");
    if (baseline.tpp) output.tpp = LiveVariability::pairDifference(tpp,
      0, *baseline.tpp, context + " varied TPP projection");
  }
  return result;
}

std::vector<LiveVariability::Sample> Election::runVariabilitySamples(
  std::vector<LiveVariability::Baseline> const& baselines) const {
  run.reportProgress("Preparing live uncertainty: 0/"
    + std::to_string(VariabilityPreparationSamples) + " samples completed...");
  std::vector<LiveVariability::Sample> samples(VariabilityPreparationSamples);
  // Keep the same limit, sample identities and progress milestones. Each
  // worker writes one preallocated slot, so normal results need no mutex.
  int const preparationThreads = std::clamp(
    project.config().getSimulationThreads(), 1, VariabilityPreparationThreads);
  // Keep the existing batches and their join boundary. A slot is used by at
  // most one worker in a batch, and its next use starts only after joining all
  // workers. Lazily copy the prepared election on first use, then restore its
  // small writable portion for later samples. Immutable evidence stays shared;
  // source/booth data and temporary projections are private to each slot.
  // These copies die before preparation returns and never survive a snapshot.
  std::vector<std::unique_ptr<Election>> workingCopies(preparationThreads);
  int nextProgressSample = VariabilityPreparationSamples / 8;
  for (int batchStart = 0; batchStart < VariabilityPreparationSamples; batchStart += preparationThreads) {
    int const batchSize = std::min(preparationThreads, VariabilityPreparationSamples - batchStart);
    std::mutex failureMutex;
    std::exception_ptr failure;
    // jthread also joins any already-started workers if thread creation fails.
    // No worker can outlive its exception state or the snapshot's baselines.
    std::vector<std::jthread> threads;
    threads.reserve(batchSize);
    for (int index = 0; index < batchSize; ++index) {
      int const sampleIndex = batchStart + index;
      threads.emplace_back([&, sampleIndex, index]() {
        try {
          auto& working = workingCopies[index];
          if (!working) working.reset(new Election(copyForPreparation()));
          samples[sampleIndex] = sampleVariability(sampleIndex, baselines, *working);
        } catch (...) {
          std::lock_guard lock(failureMutex);
          if (!failure) failure = std::current_exception();
        }
      });
    }
    for (auto& thread : threads) thread.join();
    if (failure) std::rethrow_exception(failure);
    int const completed = batchStart + batchSize;
    if (completed >= nextProgressSample || completed == VariabilityPreparationSamples) {
      run.reportProgress("Preparing live uncertainty: " + std::to_string(completed)
        + "/" + std::to_string(VariabilityPreparationSamples) + " samples completed...");
      nextProgressSample = completed + VariabilityPreparationSamples / 8;
    }
  }
  return samples;
}

void Election::storePreparedVariability(std::vector<LiveVariability::Sample> const& samples) {
  for (std::size_t s = 0; s < seats.size(); ++s) {
    std::map<int, std::vector<double>> fp;
    std::vector<double> tcp, tpp;
    for (auto const& sample : samples) {
      for (auto const& [party, change] : sample.at(s).fp) fp[party].push_back(change);
      if (sample[s].tcp) tcp.push_back(*sample[s].tcp);
      if (sample[s].tpp) tpp.push_back(*sample[s].tpp);
    }
    // Preserve the existing zero-mean RMS and sorted float reduction. Skipping
    // the legacy TPP diagnostic changes neither active FP nor TCP samples.
    for (auto& [party, changes] : fp) seats[s].fpAllBoothsStdDev[party] =
      LiveVariability::deterministicRootMeanSquare(changes, "FP variability for seat " + seats[s].name);
    if (!tcp.empty()) seats[s].tcpAllBoothsStdDev =
      LiveVariability::deterministicRootMeanSquare(tcp, "TCP variability for seat " + seats[s].name);
    if (!tpp.empty()) seats[s].tppAllBoothsStdDev =
      LiveVariability::deterministicRootMeanSquare(tpp, "TPP variability for seat " + seats[s].name);
  }
}

void LiveV2::Election::generateSharedFpVariability() {
    // A category draw is shared between districts in the scenario. Its raw
    // uncertainty participates in the same precision calculation as local
    // uncertainty; applying a progress phase-in first would hide that width.
    boothTypeFpIterationVariation.clear();
    voteTypeFpIterationVariation.clear();
    for (auto const& [party,variations] : boothTypeFpBiasStdDev)
        for (auto const& [type,sd] : variations)
            boothTypeFpIterationVariation[party][type] = variabilityNormal(0.,sd,
                int(type),party,uint32_t(VariabilityTag::GenerateBoothTypeFpVariability));
    for (auto const& [party,variations] : voteTypeFpBiasStdDev)
        for (auto const& [type,sd] : variations)
            voteTypeFpIterationVariation[party][type] = variabilityNormal(0.,sd,
                int(type),party,uint32_t(VariabilityTag::GenerateVoteTypeFpVariability));
}

std::map<int,double> LiveV2::Election::drawSeatFpChanges(int seatIndex) const {
    auto const& seat = seats.at(seatIndex);
    // A final district has no remaining FP composition uncertainty. Its
    // authoritative totals can group minor candidates into Others even though
    // booth-derived variability still names them separately. Main scenarios
    // already skip final seats, but preparation also calls this helper while
    // measuring FP moments: do not draw or validate obsolete booth identities
    // before the final-count account has a chance to hold its values fixed.
    // Unfinished districts retain the normal candidate checks and draws below.
    if (seat.finalResult) return {};
    std::map<int,double> changes;
    auto sensitivity = [&](double votes) {
        if (!std::isfinite(votes) || votes < 0 || (votes > 0 && seat.node.totalVotesPrevious() <= 0))
            throw std::runtime_error("Invalid FP category sensitivity: "+seat.name);
        return votes > 0 ? votes/seat.node.totalVotesPrevious()*obsWeight(node.fpConfidence) : 0.;
    };
    for (auto const& [party,sd] : seat.fpAllBoothsStdDev) {
        int candidate = party;
        if (!seat.node.fpVotesProjected.contains(candidate)) {
            if (party < IndependentPartyIdOffset || !seat.node.fpVotesProjected.contains(run.indPartyIndex))
                throw std::runtime_error("FP variability references a missing candidate: "+seat.name);
            candidate = run.indPartyIndex;
        }
        if (changes.contains(candidate))
            throw std::runtime_error("Duplicate FP variability candidate: "+seat.name);
        double change = drawFpVariability(seatIndex,party,sd);
        if (boothTypeFpIterationVariation.contains(party) && seat.fpBoothTypeSensitivity.contains(party))
            for (auto const& [type,variation] : boothTypeFpIterationVariation.at(party))
                if (seat.fpBoothTypeSensitivity.at(party).contains(type))
                    change += variation*sensitivity(seat.fpBoothTypeSensitivity.at(party).at(type));
        if (voteTypeFpIterationVariation.contains(party) && seat.fpVoteTypeSensitivity.contains(party))
            for (auto const& [type,variation] : voteTypeFpIterationVariation.at(party))
                if (seat.fpVoteTypeSensitivity.at(party).contains(type))
                    change += variation*sensitivity(seat.fpVoteTypeSensitivity.at(party).at(type));
        changes[candidate] = change;
    }
    return changes;
}

void Election::generateVariability(int iterationIndex) {
  // generate seat-level variability using the parameters previously prepared
  // this simulates the variability caused by random booth results without
  // requiring a full recalculation of every booth

  if (iterationIndex < 0) {
    throw std::runtime_error(
      "Cannot generate live variability for a negative iteration index.");
  }
  variabilitySampleIndex = iterationIndex;
  boothTypeFpIterationVariation.clear();
  voteTypeFpIterationVariation.clear();

  // TPP composition errors are conditioned jointly at the simulator
  // boundary. Only FP and non-classic TCP retain their existing draws here.
  generateSharedFpVariability();

  for (int seatIndex = 0; seatIndex < int(seats.size()); ++seatIndex) {
    auto& seat = seats[seatIndex];
    std::string const seatContext = "Seat " + seat.name;
    // Official final district results have no remaining FP/TCP share draw.
    // Inferred TPP, if no final TPP was supplied, still uses its separate joint
    // uncertainty at the handoff; this loop does not generate TPP variation.
    if (seat.finalResult) continue;
    // Unphased FP draws are integrated by their measured precision at the
    // simulator boundary. Both preparation and scenarios use this same finite
    // outstanding-vote response; there is no second local progress weight.
    seat.node.fpVotesProjected = turnout->vary(LiveData::CountKind::Fp, seatIndex,
      seat.node.fpVotesProjected, drawSeatFpChanges(seatIndex));

    if (seat.tcpAllBoothsStdDev.has_value()) {
      if (seat.node.tcpVotesProjected.size() != 2) {
        throw std::runtime_error(
          seatContext + " has TCP variability without a two-party projection.");
      }
      double const totalTcpProjectedVotes = LiveVariability::positiveVoteTotal(
        seat.node.tcpVotesProjected, seatContext + " TCP projection");
      double randomTcpVariation = variabilityNormal(
        0.0, seat.tcpAllBoothsStdDev.value(), seatIndex, 0, uint32_t(VariabilityTag::GenerateTcpVariability)
      );
      randomTcpVariation *= variabilityProgressWeight(seat.node.tcpCompletion);
      if (randomTcpVariation == 0.0) continue;
      int const arbitraryPartyId = seat.node.tcpVotesProjected.begin()->first;
      seat.node.tcpVotesProjected = turnout->vary(LiveData::CountKind::Tcp, seatIndex,
        seat.node.tcpVotesProjected, {{arbitraryPartyId, randomTcpVariation}});
      for (auto const& [partyId, votes] : seat.node.tcpVotesProjected)
        seat.node.tcpShares[partyId] = double(25*std::log((double(votes)+.25)/(double(totalTcpProjectedVotes)-votes+.25)));
    }
  }

  // Rechecking transfers existing preferences, after the separate variations
  // in future-voter composition. It is not attenuated by votes remaining: a
  // completed count can still contain a substantial erroneous TCP allocation.
  applyPreferenceCorrections(iterationIndex);
  applyCountRevisions(iterationIndex);

  // Composition errors above use the mean preparation's evidence weights.
  // Now expose each sampled count's progress to the forecast handoff, so a
  // no-addition draw is complete and a large batch carries less confidence.
  refreshTurnoutScenarioProgress();

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
  // All vote accounts (FP, TCP and historical comparisons) use this translation.
  // Resolve candidate affiliation, party description and cached numeric ID in
  // the same source election. Matching numbers in the other election must not
  // redirect votes to another party or change their preference assumptions.
  auto const party = LivePartyMapping::candidateParty(ecCandidateId,
    isPrevious ? LivePartyMapping::SourceElection::Previous : LivePartyMapping::SourceElection::Current,
    previousElection, currentElection);
  if (!party) {
    logger << "Warning: Candidate ID " << ecCandidateId << " not found in either current or previous election data\n";
    return -1;
  }

  // Independent candidates keep their existing individual identities, outside
  // the registered-party range. They do not share the registered-party cache.
  if (party->partyId == Results2::Candidate::Independent) {
    return ecCandidateId + IndependentPartyIdOffset;
  }

  LivePartyMapping::CommissionPartyId const identity{party->source, party->partyId};
  if (!party->hasPartyRecord && !ecPartyToInternalParty.contains(identity)) {
    logger << "Warning: Party ID " << party->partyId << " not found in "
      << (party->source == LivePartyMapping::SourceElection::Previous ? "previous" : "current")
      << " election data\n";
  }
  return LivePartyMapping::mapParty(identity, party->abbreviation,
    project.parties().count(), IndependentPartyIdOffset,
    ecPartyToInternalParty, ecAbbreviationToInternalParty);
}

std::uint64_t Election::variabilityKey(int itemIndex, std::uint64_t partyId, std::uint32_t tag) const {
  // Separate keyed streams make each candidate's draw reproducible regardless
  // of thread scheduling. The endpoint choice must not share the normal draw's
  // stream, or selecting an estimate would also select a particular local error.
  std::uint64_t key = variabilityBaseSeed;
  key = RandomGenerator::mixKey(key, LiveVariabilityDomain);
  key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(variabilitySampleIndex));
  key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(itemIndex));
  key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(partyId));
  key = RandomGenerator::mixKey(key, static_cast<std::uint64_t>(tag));
  return key;
}

double Election::variabilityNormal(double mean, double sd, int itemIndex, std::uint64_t partyId, std::uint32_t tag) const {
  if (!std::isfinite(mean) || !std::isfinite(sd) || sd < 0.0) {
    throw std::runtime_error(
      "Cannot generate live variability from invalid normal parameters.");
  }
  return RandomGenerator::normal_from_key(variabilityKey(itemIndex,partyId,tag), mean, sd);
}

double Election::drawFpVariability(int seatIndex, int partyId, double localStdDev) const {
  // Both contributions use the same whole-seat transformed scale. Their raw
  // width determines precision at the simulator boundary; do not phase it in.
  // Counted candidate votes remain fixed when that sampler changes composition.
  double normal = variabilityNormal(0.0,fpEvidenceNormalStdDev(seatIndex,partyId,localStdDev),
    seatIndex,partyId,uint32_t(VariabilityTag::GenerateFpVariability));
  double quantile = RandomGenerator::uniform01_from_key(
    variabilityKey(seatIndex,partyId,uint32_t(VariabilityTag::GenerateFpEvidenceEndpoint)));
  return normal+fpEvidenceDisagreement(seatIndex,partyId,quantile);
}

void Election::logPreparedSummary() const {
  // Routine snapshots already export detailed analysis. Keep the text log
  // compact; full node dumps remain available for targeted debugging.
  logger << "Live preparation completed: " << seats.size() << " seats, "
    << readBooths().size() << " booths; legacy TPP RMS diagnostic "
    << (project.config().getLiveLegacyTppDiagnostic() ? "enabled" : "disabled") << "\n";
  if (project.config().getLiveDetailedLogging()) log(true, true, true);
}

void Election::log(bool includeLargeRegions, bool includeSeats, bool includeBooths) const {
  logger << "\nElection:\n";
  logger << "Projected 2PP: " << node.tppVotesProjected.at(0) / (node.tppVotesProjected.at(0) + node.tppVotesProjected.at(1)) * 100.0 << "\n";
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
