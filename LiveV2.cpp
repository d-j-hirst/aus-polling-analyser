#include "LiveV2.h"

#include "ElectionData.h"
#include "General.h"
#include "PollingProject.h"
#include "SpecialPartyCodes.h"

#include <numeric>
#include <ranges>

using namespace LiveV2;

constexpr float VoteObsWeightStrength = 80.0f;
constexpr float PreferenceFlowObsWeightStrength = 200.0f;
constexpr float CoalitionLeakagePercent = 20.0f;

// Arbitrary offset to ensure independent candidates don't clash with real party IDs
// Candidate IDs are 5-digit (or shorter) numbers, so this offset makes it easy to spot
// what the original EC ID was if necessary.
constexpr int IndependentPartyIdOffset = 100000;

const float obsWeight(float confidence, float strength = VoteObsWeightStrength) {
  return std::min(1.0f, 1.025f - 1.025f / (1.0f + std::pow(confidence, 1.6f) * strength));
}

const std::map<Results2::VoteType, std::string> VoteTypeNames = {
  {Results2::VoteType::Ordinary, "Ordinary"},
  {Results2::VoteType::Absent, "Absent"},
  {Results2::VoteType::Provisional, "Provisional"},
  {Results2::VoteType::PrePoll, "PrePoll"},
  {Results2::VoteType::Postal, "Postal"},
  {Results2::VoteType::Early, "Early"},
  {Results2::VoteType::IVote, "iVote"},
  {Results2::VoteType::SIR, "SIR"},
  {Results2::VoteType::Invalid, "Invalid"}
};

Node::Node()
{
}

void Node::log() const
{
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
}

int Node::totalFpVotesCurrent() const {
  return std::accumulate(fpVotesCurrent.begin(), fpVotesCurrent.end(), 0,
    [](int sum, const auto& pair) { return sum + pair.second; });
}

int Node::totalTcpVotesCurrent() const {
  return std::accumulate(tcpVotesCurrent.begin(), tcpVotesCurrent.end(), 0,
    [](int sum, const auto& pair) { return sum + pair.second; });
}

int Node::totalVotesPrevious() const {
  return std::accumulate(fpVotesPrevious.begin(), fpVotesPrevious.end(), 0,
    [](int sum, const auto& pair) { return sum + pair.second; });
}

// Helper function to determine if a given tcp set is a valid tpp set
auto isTppSet = [](const auto& shares, int natPartyIndex) {
  return shares.contains(0) && (shares.contains(1) || shares.contains(natPartyIndex));
};

Booth::Booth(
  Results2::Booth const& currentBooth,
  std::optional<Results2::Booth const*> previousBooth,
  std::function<int(int)> partyMapper,
  int parentSeatId,
  int natPartyIndex
)
  : name(currentBooth.name), parentSeatId(parentSeatId), voteType(Results2::VoteType::Ordinary), boothType(currentBooth.type)
{

  // Helper function to process votes, calculate shares and swings
  auto processVotes = [this, &currentBooth, &partyMapper](
      const auto& currentVotes, const auto& previousVotes, 
      auto& currentMap, auto& previousMap, auto& sharesMap, auto& swingsMap, bool isTcp = false) {
    
    // Extract votes from current booth
    for (auto const& [id, votes] : currentVotes) {
      int mappedPartyId = partyMapper(id);
      currentMap[mappedPartyId] = votes;
    }
    
    // Extract votes from previous booth if available
    if (previousVotes) {
      for (auto const& [id, votes] : *previousVotes) {
        int mappedPartyId = partyMapper(id);
        previousMap[mappedPartyId] = votes;
      }
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
    float totalCurrentVotes = static_cast<float>(node.totalFpVotesCurrent());
    float totalPreviousVotes = static_cast<float>(node.totalVotesPrevious());

    int totalCurrentVotesCounted = std::accumulate(currentMap.begin(), currentMap.end(), 0,
      [](int sum, const auto& pair) { return sum + pair.second; });

    if (totalCurrentVotesCounted > 0 && float(totalCurrentVotesCounted) < float(totalCurrentVotes) * 0.95f) {
      logger << "Warning: Only " << float(totalCurrentVotesCounted) / float(totalCurrentVotes) * 100.0f << "% of current votes were counted in " << name << "\n";
      logger << "Resetting votes as they are not reliable\n";
      for (auto const& [partyId, votes] : currentMap) {
        currentMap[partyId] = 0;
      }
      return;
    }

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
    currentBooth.fpVotes, 
    previousBooth ? std::optional(previousBooth.value()->fpVotes) : std::nullopt,
    node.fpVotesCurrent, node.fpVotesPrevious, node.fpShares, node.fpSwings
  );

  // Process two-candidate-preferred votes
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
}

Booth::Booth(
  Results2::Seat::VotesByType const& currentFpVotes,
  Results2::Seat::VotesByType const& currentTcpVotes,
  std::optional<Results2::Seat::VotesByType const*> previousFpVotes,
  std::optional<Results2::Seat::VotesByType const*> previousTcpVotes,
  Results2::VoteType voteType,
  std::function<int(int)> partyMapper,
  int parentSeatId,
  int natPartyIndex
)
  : name(VoteTypeNames.at(voteType)), parentSeatId(parentSeatId), voteType(voteType), boothType(Results2::Booth::Type::Other)
{
  auto processVotes = [this, &partyMapper, voteType](
    Results2::Seat::VotesByType const& currentVotes,
    std::optional<Results2::Seat::VotesByType const*> previousVotes,
    auto& currentMap, auto& previousMap, auto& sharesMap, auto& swingsMap, bool isTcp = false)
  {
    // Extract votes from current booth
    for (auto const& [partyId, votes] : currentVotes) {
      int mappedPartyId = partyMapper(partyId);
      currentMap[mappedPartyId] = votes.at(voteType);
    }
    // Extract votes from previous booth if available
    if (previousVotes) {
      for (auto const& [partyId, votes] : *(previousVotes.value())) {
        int mappedPartyId = partyMapper(partyId);
        previousMap[mappedPartyId] = votes.at(voteType);
      }
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
    previousTcpVotes ? std::optional(previousTcpVotes.value()) : std::nullopt,
    node.tcpVotesCurrent, node.tcpVotesPrevious, node.tcpShares, node.tcpSwings, true
  );

  // Determine tpp share and swing, if available
  calculateTppSwing(natPartyIndex);

  // For incremental booths our confidence will increase as more votes are counted
  // but as we don't actually know how many votes are left to be counted
  // the confidence will plateau at 0.9
  node.fpConfidence = std::min(float(node.totalFpVotesCurrent()) / float(node.totalVotesPrevious()), 0.9f);
  node.tcpConfidence = std::min(float(node.totalTcpVotesCurrent()) / float(node.totalVotesPrevious()), 0.9f);
  node.tppConfidence = std::min({
    float(node.totalTcpVotesCurrent()) / float(node.totalVotesPrevious()),
    float(node.totalFpVotesCurrent()) / float(node.totalVotesPrevious()) * 0.5f,
    0.9f
  });
}

void Booth::calculateTppSwing(int natPartyIndex) {
  if (node.totalTcpVotesCurrent() > 0 && isTppSet(node.tcpVotesCurrent, natPartyIndex)) {
    node.tppShare = node.tcpShares.at(0);
  }
  if (!node.tppShare) {
    return;
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
  node.log();
}

LiveV2::Seat::Seat(Results2::Seat const& seat, int parentRegionId)
  : name(seat.name), parentRegionId(parentRegionId)
{
}

void LiveV2::Seat::log(Election const& election, bool includeBooths) const
{
  logger << "\nSeat: " << name << "\n";
  PA_LOG_VAR(finalSpecificFpDeviations);
  PA_LOG_VAR(finalSpecificTppDeviation);
  PA_LOG_VAR(offsetSpecificFpDeviations);
  PA_LOG_VAR(offsetSpecificTppDeviation);
  PA_LOG_VAR(independentPartyIndex);
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

LiveV2::Election::Election(Results2::Election const& previousElection, Results2::Election const& currentElection, PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), sim(sim), run(run), previousElection(previousElection), currentElection(currentElection)
{
  getNatPartyIndex();
  loadEstimatedPreferenceFlows();
  initializePartyMappings();
  createNodesFromElectionData();
  calculateTppEstimates(true); // This is done now so that we can observe deviations from preference flows
  aggregate(); // preliminary, for calculating preference flow and seat fp totals
  calculatePreferenceFlowDeviations();
  calculateTppEstimates(false); // Calculate again, this time using the observed deviations from preference flows
  includeBaselineResults();
  extrapolateBaselineSwings();
  calculateDeviationsFromBaseline();
  aggregate();
  determineSpecificDeviations();
  recomposeVoteCounts();
  log(true, true, true);
}

void Election::getNatPartyIndex() {
	natPartyIndex = project.parties().indexByShortCode("NAT");
	if (natPartyIndex == -1) natPartyIndex = InvalidPartyIndex;
}

void Election::loadEstimatedPreferenceFlows() {
  preferenceFlowMap.clear();
	preferenceExhaustMap.clear();
	auto lines = extractElectionDataFromFile("analysis/Data/preference-estimates.csv", run.getTermCode());
	for (auto const& line : lines) {
		std::string party = splitString(line[2], " ")[0];
    int partyIndex = project.parties().indexByShortCode(party);
		float thisPreferenceFlow = std::stof(line[3]);
		preferenceFlowMap[partyIndex] = thisPreferenceFlow;
		if (line.size() >= 5 && line[4][0] != '#') {
			float thisExhaustRate = std::stof(line[4]);
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
  if (!preferenceFlowMap.contains(-1)) {
    preferenceFlowMap[-1] = project.parties().getOthersPreferenceFlow();
  }
  if (!preferenceExhaustMap.contains(-1)) {
    preferenceExhaustMap[-1] = project.parties().getOthersExhaustRate();
  }
  if (run.getTermCode() == "2025fed") {
    prevPreferenceOverrides[7] = 35.7f; // One Nation preferences, expected to be lower in 2025
  }
}

void Election::initializePartyMappings() {
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

void Election::createNodesFromElectionData() {
  for (auto const [regionId, region] : project.regions()) {
    largeRegions.push_back(LargeRegion(region));
  }
  for (auto const& [id, seat] : currentElection.seats) {
    int seatIndex = seats.size();
    auto const& projectSeat = project.seats().accessByName(seat.name).second;
    int parentRegionId = projectSeat.region;
    seats.push_back(Seat(seat, parentRegionId));
    largeRegions.at(parentRegionId).seats.push_back(seatIndex);
    for (auto const& boothId : seat.booths) {
      if (!currentElection.booths.contains(boothId)) {
        continue;
      }
      auto const& currentBooth = currentElection.booths.at(boothId);

      // Find matching booth in previous election if it exists
      std::optional<Results2::Booth const*> previousBoothPtr = std::nullopt;
      for (auto const& [prevBoothId, prevBooth] : previousElection.booths) {
        // IDs are the only unique identifier for booths
        // as names can be changed from one election to the next
        // without changing the ID
        if (prevBooth.id == currentBooth.id) {
          previousBoothPtr = &prevBooth;
          break;
        }
      }

      // Create new booth with mapping function
      booths.emplace_back(Booth(
          currentBooth, 
          previousBoothPtr,
          [this](int partyId) { return this->mapPartyId(partyId); },
          seatIndex,
          natPartyIndex
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
    for (auto const& [voteType, votes] : seat.fpVotes.begin()->second) {
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
        [this](int partyId) { return this->mapPartyId(partyId); },
        seatIndex,
        natPartyIndex
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
    parents.push_back(&largeRegions.at(seats.at(child.parentSeatId).parentRegionId).node);
    parents.push_back(&node);
  }
  else if constexpr (std::is_same_v<T, Seat>) {
    parents.push_back(&largeRegions.at(child.parentRegionId).node);
    parents.push_back(&node);
  }
  else if constexpr (std::is_same_v<T, LargeRegion>) {
    parents.push_back(&node);
  }
  return parents;
}

void Election::calculateTppEstimates(bool withTpp) {
  for (auto& booth : booths) {
    // We're either doing this to help observe deviations from preference flows
    // or to calculate the final estimate of the TPP share for each booth
    if (booth.node.tppShare.has_value() != withTpp) continue;

    // The preference flows vary in each booth from the overall election flows, so we need to calculate an
    // offset value to properly compare each booth to the baseline
    // The offset should be applied to the new estimated tpp estimate before calculating
    // the estimated swing
    auto calculatePreferenceRateOffset = [this, &booth](bool current) -> std::optional<float> {
      std::optional<float> preferenceRateOffset = std::nullopt;
      auto const& tcpVotes = current ? booth.node.tcpVotesCurrent : booth.node.tcpVotesPrevious;
      if (tcpVotes.contains(0) && (tcpVotes.contains(1) || tcpVotes.contains(natPartyIndex))) {
        float partyOnePreferenceEstimatePercent = 0.0f;
        int preferredCoalitionParty = tcpVotes.contains(natPartyIndex) ? natPartyIndex : 1;
        float totalVotes = float(current ? booth.node.totalFpVotesCurrent() : booth.node.totalVotesPrevious());
        if (current && totalVotes != booth.node.totalTcpVotesCurrent()) {
          logger << "Warning: Total votes in current election (" << totalVotes << ") does not match total TCP votes (" << booth.node.totalTcpVotesCurrent() << ") for booth " << booth.name << "\n";
          logger << "Not calculating preference rate offset for booth " << booth.name << "\n";
          return std::nullopt;
        }
        auto const& fpVotes = current ? booth.node.fpVotesCurrent : booth.node.fpVotesPrevious;
        for (auto const& [partyId, votes] : fpVotes) {
          float partyPercent = float(votes) / totalVotes * 100.0f;
          if (partyId == preferredCoalitionParty || partyId == 0) {
            continue;
          }
          if (partyId == natPartyIndex || partyId == 1) {
            // Other coalition party's votes, assume some leakage to Labor
            partyOnePreferenceEstimatePercent += partyPercent * 0.2f;
          }
          else if (prevPreferenceOverrides.contains(partyId)) {
            // Override preference flow for this party
            partyOnePreferenceEstimatePercent += partyPercent * prevPreferenceOverrides.at(partyId) * 0.01f;
          }
          else if (preferenceFlowMap.contains(partyId)) {
            partyOnePreferenceEstimatePercent += partyPercent * preferenceFlowMap.at(partyId) * 0.01f;
          }
          else {
            partyOnePreferenceEstimatePercent += partyPercent * preferenceFlowMap.at(-1) * 0.01f;
          }
        }
        float nonMajorVotes = totalVotes - fpVotes.at(0) - fpVotes.at(preferredCoalitionParty);
        float nonMajorVotesPercent = nonMajorVotes / totalVotes * 100.0f;
        float partyOnePreferenceRateEstimate = partyOnePreferenceEstimatePercent / nonMajorVotesPercent * 100.0f;
        float partyOnePreferenceRateActual = (tcpVotes.at(0) - fpVotes.at(0)) / nonMajorVotes * 100.0f;
        preferenceRateOffset = transformVoteShare(partyOnePreferenceRateActual) - transformVoteShare(partyOnePreferenceRateEstimate);
      }
      return preferenceRateOffset;
    };
    
    std::optional<float> prevPreferenceRateOffset = calculatePreferenceRateOffset(false);

    // Calculate estimate of party one's share of the TPP based on the FP votes
    float partyOneShare = 0.0f;
    for (auto const& [partyId, share] : booth.node.fpShares) {

      auto applyOffset = [this, booth, prevPreferenceRateOffset](float flow) {
        if (flow == 0.0f || flow == 100.0f || !prevPreferenceRateOffset) return flow;
        auto const& thisAndParents = getThisAndParents(booth);
        float preferenceFlowDeviation = 0.0f;
        for (auto const& parent : thisAndParents) {
          preferenceFlowDeviation += parent->specificPreferenceFlowDeviation.value_or(0.0f);
        }
        return detransformVoteShare(transformVoteShare(flow) + *prevPreferenceRateOffset + preferenceFlowDeviation);
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
        partyOneShare += detransformVoteShare(share) * applyOffset(preferenceFlowMap.at(-1)) * 0.01f;
      }
    }

    int previousTotal = booth.node.totalVotesPrevious();
    if (
      previousTotal > 0
      && isTppSet(booth.node.tcpVotesPrevious, natPartyIndex)
      && booth.node.tcpVotesPrevious.at(0) > 0
      && booth.node.tcpVotesPrevious.at(0) < previousTotal
    ) {
      // TPP share is directly available for this booth, so record it
      float previousShare = transformVoteShare(
        static_cast<float>(booth.node.tcpVotesPrevious.at(0)) / previousTotal * 100.0f
      );
      booth.node.tppSharePrevious = previousShare;
    } else if (previousTotal > 0) {
      // work out which coalition party would have made the TPP here
      int preferredCoalitionParty = 1;
      if (booth.node.fpVotesPrevious.contains(natPartyIndex)) {
        if (!booth.node.fpVotesCurrent.contains(1)) {
          preferredCoalitionParty = natPartyIndex;
        }
        else if (booth.node.fpVotesCurrent.at(natPartyIndex) > booth.node.fpVotesCurrent.at(1)) {
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
          // Override preference flow for this party when it'd different from the current election
          partyOnePreferenceEstimatePercent += share * prevPreferenceOverrides.at(partyId) * 0.01f;
        }
        else if (preferenceFlowMap.contains(partyId)) {
          partyOnePreferenceEstimatePercent += share * preferenceFlowMap.at(partyId) * 0.01f;
        }
        else {
          partyOnePreferenceEstimatePercent += share * preferenceFlowMap.at(-1) * 0.01f;
        }
      }
      booth.node.tppSharePrevious = transformVoteShare(partyOnePreferenceEstimatePercent);
    }

    if (partyOneShare > 0.0f && partyOneShare < 100.0f) {
      std::optional<float> currentPreferenceRateOffset = calculatePreferenceRateOffset(true);
      if (currentPreferenceRateOffset && prevPreferenceRateOffset) {
        bool isBad = false;
        // Error inputs can result in NaN or Inf
        // Inf can also result from legitimate results where the
        // preference flow is 0% or 100%
        // For analysis purposes, it's better to just ignore these booths
        if (std::isnan(currentPreferenceRateOffset.value())) {
          logger << "Warning: NaN preference rate offset for booth " << booth.name << "\n";
          logger << "Current: " << currentPreferenceRateOffset.value() << "\n";
          isBad = true;
        }
        if (std::isinf(currentPreferenceRateOffset.value()) || std::isinf(prevPreferenceRateOffset.value())) {
          logger << "Warning: Infinite preference rate offset for booth " << booth.name << "\n";
          logger << "Current: " << currentPreferenceRateOffset.value() << "\n";
          logger << "Previous: " << prevPreferenceRateOffset.value() << "\n";
          isBad = true;
        }
        if (!isBad) {
          booth.node.preferenceFlowDeviation = currentPreferenceRateOffset.value() - prevPreferenceRateOffset.value();
          booth.node.preferenceFlowConfidence = 1.0f;
        }
      }
      if (!withTpp) {
        booth.node.tppShare = transformVoteShare(partyOneShare);
        booth.node.tppConfidence = 0.5f; // TODO: tune parameter
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
      auto const& largeRegion = largeRegions.at(seat.parentRegionId);
      parentPreferenceFlowDeviation += largeRegion.node.specificPreferenceFlowDeviation.value_or(0.0f);
      parentPreferenceFlowDeviation += node.specificPreferenceFlowDeviation.value_or(0.0f);
      float withoutElectionSpecific = booth.node.preferenceFlowDeviation ? booth.node.preferenceFlowDeviation.value() - parentPreferenceFlowDeviation : 0.0f;
      booth.node.specificPreferenceFlowDeviation = withoutElectionSpecific * preferenceFlowObsWeight;
    }
  }
}

void Election::includeBaselineResults() {
  if (!sim.getLiveBaselineReport()) {
    return;
  }
  includeSeatBaselineResults();
  includeLargeRegionBaselineResults();
  includeElectionBaselineResults();
}

void Election::includeSeatBaselineResults() {
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
      if (median > 0 && median < 100) {
        seat.node.fpSharesBaseline[partyId] = transformVoteShare(median);
      }
    }

    float bestIndependentShare = 0.0f;
    int bestIndependentId = InvalidPartyIndex;
    for (auto const& [prevPartyId, share] : seat.node.fpVotesCurrent) {
      if (prevPartyId < IndependentPartyIdOffset) continue;
      if (share > bestIndependentShare) {
        bestIndependentShare = share;
        bestIndependentId = prevPartyId;
      }
    }
    seat.independentPartyIndex = bestIndependentId;

    for (auto const& [partyId, swing] : seat.node.fpSharesBaseline) {
      if (seat.node.totalVotesPrevious() > 0) {
        float thisVotesPrevious = 0.0f;
        if (seat.node.fpVotesPrevious.contains(partyId)) {
          thisVotesPrevious = seat.node.fpVotesPrevious.at(partyId);
        }
        else if (partyId == run.indPartyIndex && seat.node.fpVotesPrevious.contains(seat.independentPartyIndex)) {
          // Independents can't be matched by party ID, so we need to find the best-performing independent
          thisVotesPrevious = seat.node.fpVotesPrevious.at(seat.independentPartyIndex);
        }
        if (thisVotesPrevious > 0.0f) {
          seat.node.fpSwingsBaseline[partyId] = seat.node.fpSharesBaseline[partyId] - 
            transformVoteShare(float(thisVotesPrevious) / float(seat.node.totalVotesPrevious()) * 100.0f);
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
        else if (partyId == run.indPartyIndex && booth.node.fpSwings.contains(seat.independentPartyIndex)) {
          if (seat.independentPartyIndex != InvalidPartyIndex) {
            booth.node.fpDeviations[partyId] = booth.node.fpSwings.at(seat.independentPartyIndex) - baselineSwing;
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

Node Election::aggregateFromChildren(const std::vector<Node const*>& nodesToAggregate, Node const* parentNode) const {
  // Aggregate swings from previous election to current election
  // Aggregation takes small-scale results and calculates a weighted average
  // in a larger region. This can then be used (in a later step) to extrapolate
  // swings to other small-scale results.
  // Current raw vote tallies and vote shares are not aggregated
  // because they generally don't add information beyond what is already
  // available in the swings

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
  float fpConfidenceSum = 0.0f; // sum of confidence, weighted by number of votes
  float fpConfidenceWeightSum = 0.0f; // sum of confidence weights (different from above as it includes even booths with no votes)

  for (auto const& thisNode : nodesToAggregate) {
    float weight = thisNode->totalVotesPrevious() * thisNode->fpConfidence;
    bool confidenceAdded = false;
    for (auto const& [partyId, swing] : thisNode->fpDeviations) {
      fpDeviationWeightedSum[partyId] += swing * weight;
      fpWeightSum[partyId] += weight;
      // Only count confidence when the deviation actually contributes to the calculations
      if (!confidenceAdded) {
        fpConfidenceSum += thisNode->fpConfidence * thisNode->totalVotesPrevious();
        confidenceAdded = true;
      }
    }

    fpConfidenceWeightSum += thisNode->totalVotesPrevious();
  }

  // Node created for each new seat/region/election: should be <200 times
  // per election, so not major bottleneck
  for (auto const& [partyId, swing] : fpDeviationWeightedSum) {
    if (fpWeightSum[partyId] > 0) { // ignore parties with no votes
      aggregatedNode.fpDeviations[partyId] = swing / fpWeightSum[partyId];
    }
  }
  if (fpConfidenceWeightSum > 0) {
    aggregatedNode.fpConfidence = fpConfidenceSum / fpConfidenceWeightSum; // will eventually have a more sophisticated nonlinear confidence calculation
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
  for (auto const& thisNode : nodesToAggregate) {
    float weight = thisNode->totalVotesPrevious() * thisNode->tppConfidence;
    if (thisNode->tppDeviation) {
      tppDeviationWeightedSum += thisNode->tppDeviation.value() * weight;
      tppWeightSum += weight;
      // Only count confidence when the deviation actually contributes to the calculations
      tppConfidenceSum += thisNode->tppConfidence * thisNode->totalVotesPrevious();
    }

    tppConfidenceWeightSum += thisNode->totalVotesPrevious();
  }
  if (tppWeightSum > 0) {
    aggregatedNode.tppDeviation = tppDeviationWeightedSum / tppWeightSum;
  }
  if (tppConfidenceWeightSum > 0) {
    aggregatedNode.tppConfidence = tppConfidenceSum / tppConfidenceWeightSum; // will eventually have a more sophisticated nonlinear confidence calculation
  }

    // Aggregate preference flow deviation
  float preferenceFlowDeviationWeightedSum = 0.0f;
  float preferenceFlowWeightSum = 0.0f;
  float preferenceFlowConfidenceSum = 0.0f;
  float preferenceFlowConfidenceWeightSum = 0.0f;
  for (auto const& thisNode : nodesToAggregate) {
    float weight = thisNode->totalVotesPrevious() * thisNode->preferenceFlowConfidence;
    if (thisNode->preferenceFlowDeviation) {
      preferenceFlowDeviationWeightedSum += thisNode->preferenceFlowDeviation.value() * weight;
      preferenceFlowWeightSum += weight;
      // Only count confidence when the deviation actually contributes to the calculations
      preferenceFlowConfidenceSum += thisNode->preferenceFlowConfidence * thisNode->totalVotesPrevious();
    }
    preferenceFlowConfidenceWeightSum += thisNode->totalVotesPrevious();
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
  determineElectionSpecificDeviations();
  determineLargeRegionSpecificDeviations();
  determineSeatSpecificDeviations();
  determineBoothSpecificDeviations();
}

void Election::determineElectionSpecificDeviations() {
  float fpObsWeight = obsWeight(node.fpConfidence);
  for (auto const& [partyId, deviation] : node.fpDeviations) {
    node.specificFpDeviations[partyId] = deviation * fpObsWeight;
  }
  float tppObsWeight = obsWeight(node.tppConfidence);
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
    auto const& largeRegion = largeRegions.at(seat.parentRegionId);
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

void Election::recomposeVoteCounts() {
  // To avoid Simpson's paradox related issues, we need to recompose the vote counts
  // from the swing data. Possible future additions:
  // (This is probably the minimum level for release)
  // 6. If the estimated swing would result in a seat having too many votes compared to its enrolment, reduce the votes for incremental/uncounted booths
  // 7. Use externally known attendance data to adjust the expected size for the relevant booths.
  // 8. Extrapolate between booths to estimate the changes in vote counts to refine and complement the above
  //    (for example, if we see a drop in ordinary votes in reporting polling places, assume this will extrapolate to the unreported booths
  //    and perhaps also be compensated for by higher turnout in other booths, might need to look at both turnout and formality)

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

  for (int boothIndex : std::ranges::views::iota(0, int(booths.size()))) {
    recomposeBoothFpVotes(true, boothIndex);
    recomposeBoothTppVotes(true, boothIndex);
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
  auto assignBlindOthers = [this](std::map<int, float>& votesProjected, std::set<int>& blindOthers, int projectSeatIndex, float totalVotesPrevious, float othersAccountedFor) {
    float remainingExpectedVotes = 0.01f * blindOthers.size() * totalVotesPrevious;
    if (
      sim.getLiveBaselineReport().has_value()
      && sim.getLiveBaselineReport().value().seatFpProbabilityBand[projectSeatIndex].contains(-1)
    ) {
      auto const& probabilityBands = sim.getLiveBaselineReport().value().seatFpProbabilityBand[projectSeatIndex].at(-1);
      float median = probabilityBands[(probabilityBands.size() - 1) / 2];
      remainingExpectedVotes = std::max(remainingExpectedVotes, median * totalVotesPrevious - othersAccountedFor);
    }
    for (auto const& partyId : blindOthers) {
      votesProjected[partyId] = remainingExpectedVotes / float(blindOthers.size());
    }
  };

  auto& booth = booths.at(boothIndex);
  auto const& seat = seats.at(booth.parentSeatId);
  int projectSeatIndex = project.seats().indexByName(seats[booth.parentSeatId].name);
  if (allowCurrentData && booth.node.totalFpVotesCurrent()) {
    // convert from int to float
    booth.node.fpVotesProjected = std::map<int, float>();
    for (auto const& [partyId, votes] : booth.node.fpVotesCurrent) {
      int effectivePartyId = partyId == seat.independentPartyIndex ? run.indPartyIndex : partyId;
      booth.node.fpVotesProjected[effectivePartyId] = static_cast<float>(votes);
    }
    if (booth.voteType != Results2::VoteType::Ordinary) {
      float previousTotalVotes = booth.node.totalVotesPrevious();
      float currentTotalVotesProjected = std::accumulate(booth.node.fpVotesProjected.begin(), booth.node.fpVotesProjected.end(), 0.0f,
        [](float sum, const auto& pair) { return sum + pair.second; });
      float ratio = previousTotalVotes / currentTotalVotesProjected;
      for (auto& [partyId, votes] : booth.node.fpVotesProjected) {
        votes *= std::max(1.0f, ratio);
      }
    }
  }
  else {
    constexpr float previousTotalVotesGuess = 500.0f;
    float previousTotalVotes = booth.node.totalVotesPrevious() ? booth.node.totalVotesPrevious() : previousTotalVotesGuess;
    std::map<int, float> tempFpVotesProjected;
    float othersAccountedFor = 0.0f;
    for (auto const& partyId : booth.node.runningParties) {
      float deviation = 0.0f;
      int effectivePartyId = partyId == seat.independentPartyIndex ? run.indPartyIndex : partyId;
      if (allowCurrentData && booth.node.totalVotesPrevious()) {
        auto const& thisAndParents = getThisAndParents(booth);
        for (auto const& parent : thisAndParents) {
          if (parent->specificFpDeviations.contains(effectivePartyId)) {
            deviation += parent->specificFpDeviations.at(effectivePartyId);
          }
        }
      }
      std::set<int> blindOthers;
      std::optional<float> baselineShareUntransformed;
      if (sim.getLiveBaselineReport().has_value()) {
        if (sim.getLiveBaselineReport().value().seatFpProbabilityBand[projectSeatIndex].contains(effectivePartyId)) {  
          auto const& fpProbabilityBands = sim.getLiveBaselineReport().value().seatFpProbabilityBand[projectSeatIndex].at(effectivePartyId);
          baselineShareUntransformed = fpProbabilityBands[(fpProbabilityBands.size() - 1) / 2];
        }
      }
      if (booth.node.fpVotesPrevious.contains(effectivePartyId)) {
        // We have previous election data and some sort of a deviation estimate (even if it's just zero)
        float prevVotes = booth.node.fpVotesPrevious.at(effectivePartyId);
        float baselineShare = transformVoteShare(float(prevVotes) / float(previousTotalVotes));
        // If we can use the baseline report (typically we can), use it to determine the baseline for the swing
        if (baselineShareUntransformed.has_value()) {
          baselineShare = transformVoteShare(baselineShareUntransformed.value());
        }
        float newShare = baselineShare + deviation;
        float newVotes = detransformVoteShare(newShare) * previousTotalVotes;
        tempFpVotesProjected[effectivePartyId] = newVotes;
        if (effectivePartyId > 2 && effectivePartyId != natPartyIndex) {
          othersAccountedFor += newVotes;
        }
      } else if (allowCurrentData && seats[booth.parentSeatId].node.fpShares.contains(effectivePartyId)) {
        // seat share data are available
        // If there is a baseline, use it and modify from there; otherwise, use a low initial expectation
        // Either way, use the seat share data to modify the initial expectation
        float baselineShare = transformVoteShare(baselineShareUntransformed.value_or(1.0f));
        float weight = obsWeight(seats[booth.parentSeatId].node.fpConfidence, VoteObsWeightStrength);
        float seatShare = seats[booth.parentSeatId].node.fpShares.at(effectivePartyId);
        float newShare = seatShare * weight + baselineShare * (1.0f - weight);
        float newVotes = detransformVoteShare(newShare) * previousTotalVotes;
        tempFpVotesProjected[effectivePartyId] = newVotes;
        if (effectivePartyId > 2 && effectivePartyId != natPartyIndex) {
          othersAccountedFor += newVotes;
        }
      } else if (baselineShareUntransformed.has_value()) {
        // No previous data and no current data, but we have a baseline
        // (Typically occurs early on the night when no useful data at all has been recorded for this party yet)
        // TODO: make sure that this correctly handles new prominent independents
        // (and also recontesting independents, though they should have the same party ID as previous election)
        float newVotes = baselineShareUntransformed.value() * previousTotalVotes;
        tempFpVotesProjected[effectivePartyId] = newVotes;
        if (effectivePartyId > 2 && effectivePartyId != natPartyIndex) {
          othersAccountedFor += newVotes;
        }
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

      assignBlindOthers(tempFpVotesProjected, blindOthers, projectSeatIndex, previousTotalVotes, othersAccountedFor);
    }
    // normalize so that the sum of the votes is the same as the previous election (or other estimate)
    float totalVotes = 0.0f;
    for (auto const& [partyId, votes] : tempFpVotesProjected) {
      totalVotes += votes;
    }
    for (auto& [partyId, votes] : tempFpVotesProjected) {
      votes *= previousTotalVotes / totalVotes;
    }
    booth.node.fpVotesProjected = tempFpVotesProjected;
  }
}

void Election::recomposeBoothTcpVotes(bool allowCurrentData, int boothIndex) {
  allowCurrentData; //placeholder 
  boothIndex; //placeholder 
}

void Election::recomposeBoothTppVotes(bool allowCurrentData, int boothIndex) {
  auto& booth = booths.at(boothIndex);
  if (allowCurrentData && booth.node.totalTcpVotesCurrent()) {
    if (isTppSet(booth.node.tcpVotesCurrent, natPartyIndex)) {
      // convert from int to float
      booth.node.tppVotesProjected = std::map<int, float>();
      for (auto const& [partyId, votes] : booth.node.tcpVotesCurrent) {
        booth.node.tppVotesProjected[partyId == natPartyIndex ? 1 : partyId] = static_cast<float>(votes);
      }
      if (booth.voteType != Results2::VoteType::Ordinary) {
        float previousTotalVotes = booth.node.totalVotesPrevious();
        float currentTotalVotesProjected = std::accumulate(booth.node.tppVotesProjected.begin(), booth.node.tppVotesProjected.end(), 0.0f,
          [](float sum, const auto& pair) { return sum + pair.second; });
        float ratio = previousTotalVotes / currentTotalVotesProjected;
        for (auto& [partyId, votes] : booth.node.tppVotesProjected) {
          votes *= std::max(1.0f, ratio);
        }
      }
    } else {
      // currently recording a non-classic TCP, ignore for now
      // the simulation will estimate off fp votes if necessary

      // Once we start projecting non-classic TCP votes, we need to also
      // revisit the conversion into TPP votes to make sure that it's
      // done correctly.
    }
  }
  else {
    // If we don't have an actual TCP count, just make an estimate based on observed deviations
    // Even if the seat doesn't end up using TPP, this will still be used to estimate the fp votes
    // and the actual TCP will come from fp-based estimates in the simulation.
    constexpr float previousTotalVotesGuess = 500.0f;
    float previousTotalVotes = booth.node.totalVotesPrevious() ? booth.node.totalVotesPrevious() : previousTotalVotesGuess;
    std::map<int, float> tempTppVotesProjected;
    std::optional<float> prevAlpShare;
    if (isTppSet(booth.node.tcpVotesPrevious, natPartyIndex)) {
      prevAlpShare = transformVoteShare(float(booth.node.tcpVotesPrevious.at(0)) / float(previousTotalVotes) * 100.0f);
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
      auto const& thisAndParents = getThisAndParents(booth);
      for (auto const& parent : thisAndParents) {
        deviation += parent->specificTppDeviation.value_or(0.0f);
      }
    }
    float existingAlpShare = baselineAlpShare.value_or(
      isTppSet(booth.node.tcpVotesPrevious, natPartyIndex)
        ? transformVoteShare(float(booth.node.tcpVotesPrevious.at(0)) / float(previousTotalVotes) * 100.0f)
        : 0.0f
      );
    float newAlpShare = existingAlpShare + deviation;
    float newAlpVotes = detransformVoteShare(newAlpShare) * previousTotalVotes * 0.01f;
    tempTppVotesProjected[0] = newAlpVotes;
    int coalitionPartyId = 1;
    tempTppVotesProjected[coalitionPartyId] = previousTotalVotes - newAlpVotes;

    // normalize so that the sum of the votes is the same as the previous election (or other estimate)
    float totalVotes = 0.0f;
    for (auto const& [partyId, votes] : tempTppVotesProjected) {
      totalVotes += votes;
    }
    for (auto& [partyId, votes] : tempTppVotesProjected) {
      votes *= previousTotalVotes / totalVotes;
    }
    booth.node.tppVotesProjected = tempTppVotesProjected;
  }
}

void Election::recomposeSeatFpVotes(int seatIndex) {
  seats[seatIndex].node.fpVotesProjected = std::map<int, float>();
  for (auto const& boothIndex : seats[seatIndex].booths) {
    for (auto const& [partyId, votes] : booths[boothIndex].node.fpVotesProjected) {
      seats[seatIndex].node.fpVotesProjected[partyId] += votes;
    }
  }
}

void Election::recomposeSeatTcpVotes(int seatIndex) {
  seatIndex ; //placeholder 
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

void Election::determineElectionFinalFpDeviations(bool allowCurrentData) {
  // now convert to shares
  float totalVotes = 0.0f;
  std::map<int, float> shares;
  for (auto const& [partyId, votes] : node.fpVotesProjected) {
    totalVotes += votes;
  }
  for (auto& [partyId, votes] : node.fpVotesProjected) {
    shares[partyId] = votes / totalVotes;
  }
  // now convert to deviations
  for (auto const& [partyId, share] : shares) {
    // TODO: this should be sourced from the simulation report
    float finalDeviation = node.fpSharesBaseline.contains(partyId)
      ? transformVoteShare(share * 100.0f) - node.fpSharesBaseline.at(partyId)
      : 0.0f;
    if (allowCurrentData) {
      float offset = offsetSpecificFpDeviations.contains(partyId)
        ? offsetSpecificFpDeviations.at(partyId) * (1.0f - node.fpConfidence)
        : 0.0f;
      finalSpecificFpDeviations[partyId] = finalDeviation - offset;
    } else {
      offsetSpecificFpDeviations[partyId] = finalDeviation;
    }
  }
}

void Election::determineElectionFinalTppDeviation(bool allowCurrentData) {
  // convert to shares
  float totalVotes = 0.0f;
  std::map<int, float> shares;
  for (auto const& [partyId, votes] : node.tppVotesProjected) {
    totalVotes += votes;
  }

  if (totalVotes == 0.0f) {
    // No tpp estimations, so don't modify anything at the seat level
    if (allowCurrentData) {
      finalSpecificTppDeviation = 0.0f;
    } else {
      offsetSpecificTppDeviation = 0.0f;
    }
    return;
  }
  float alpShare = node.tppVotesProjected.at(0) / totalVotes;
  float finalDeviation = transformVoteShare(alpShare * 100.0f) - node.tppShareBaseline.value_or(0.0f);
  if (allowCurrentData) {
    // This will eventually be replaced with something a lot more sophisticated,
    // but for now this is a simple way to account for factors that may bias
    // the seat's TPP share away from the baseline, such as redistributions, new
    // booths, or expected shifts between vote types.
    float offset = offsetSpecificTppDeviation.value_or(0.0f) * (1.0f - node.tppConfidence);
    finalSpecificTppDeviation = finalDeviation - offset;
  } else {
    offsetSpecificTppDeviation = finalDeviation;
  }
}

void Election::determineLargeRegionFinalFpDeviations(bool allowCurrentData, int largeRegionIndex) {
  // now convert to shares
  float totalVotes = 0.0f;
  std::map<int, float> shares;
  for (auto const& [partyId, votes] : largeRegions[largeRegionIndex].node.fpVotesProjected) {
    totalVotes += votes;
  }
  for (auto& [partyId, votes] : largeRegions[largeRegionIndex].node.fpVotesProjected) {
    shares[partyId] = votes / totalVotes;
  }
  // now convert to deviations
  for (auto const& [partyId, share] : shares) {
    float inheritedDeviation = 0.0f;
    if (allowCurrentData) {
      if (finalSpecificFpDeviations.contains(partyId)) {
        inheritedDeviation += finalSpecificFpDeviations.at(partyId);
      }
    }
    float finalDeviation = largeRegions[largeRegionIndex].node.fpSharesBaseline.contains(partyId)
      ? transformVoteShare(share * 100.0f) - largeRegions[largeRegionIndex].node.fpSharesBaseline.at(partyId) - inheritedDeviation
      : 0.0f;
    if (allowCurrentData) {
      float offset = largeRegions[largeRegionIndex].offsetSpecificFpDeviations.contains(partyId)
        ? largeRegions[largeRegionIndex].offsetSpecificFpDeviations.at(partyId) * (1.0f - largeRegions[largeRegionIndex].node.fpConfidence)
        : 0.0f;
      largeRegions[largeRegionIndex].finalSpecificFpDeviations[partyId] = finalDeviation - offset;
    } else {
      largeRegions[largeRegionIndex].offsetSpecificFpDeviations[partyId] = finalDeviation;
    }
  }
}

void Election::determineLargeRegionFinalTppDeviation(bool allowCurrentData, int largeRegionIndex) {
  // now convert to shares
  float totalVotes = 0.0f;
  std::map<int, float> shares;
  for (auto const& [partyId, votes] : largeRegions[largeRegionIndex].node.tppVotesProjected) {
    totalVotes += votes;
  }

  if (totalVotes == 0.0f) {
    // No tpp estimations, so don't modify anything at the seat level
    if (allowCurrentData) {
      largeRegions[largeRegionIndex].finalSpecificTppDeviation = 0.0f;
    } else {
      largeRegions[largeRegionIndex].offsetSpecificTppDeviation = 0.0f;
    }
    return;
  }
  float alpShare = largeRegions[largeRegionIndex].node.tppVotesProjected.at(0) / totalVotes;
  float deviation = 0.0f;
  if (allowCurrentData) {
    deviation += finalSpecificTppDeviation.value_or(0.0f);
  }
  float finalDeviation = transformVoteShare(alpShare * 100.0f) - largeRegions[largeRegionIndex].node.tppShareBaseline.value_or(0.0f) - deviation;
  if (allowCurrentData) {
    // This will eventually be replaced with something a lot more sophisticated,
    // but for now this is a simple way to account for factors that may bias
    // the seat's TPP share away from the baseline, such as redistributions, new
    // booths, or expected shifts between vote types.
    float offset = largeRegions[largeRegionIndex].offsetSpecificTppDeviation.value_or(0.0f) * (1.0f - largeRegions[largeRegionIndex].node.tppConfidence);
    largeRegions[largeRegionIndex].finalSpecificTppDeviation = finalDeviation - offset;
  } else {
    largeRegions[largeRegionIndex].offsetSpecificTppDeviation = finalDeviation;
  }
}

void Election::determineSeatFinalFpDeviations(bool allowCurrentData, int seatIndex) {
  // now convert to shares
  float totalVotes = 0.0f;
  std::map<int, float> shares;
  for (auto const& [partyId, votes] : seats[seatIndex].node.fpVotesProjected) {
    totalVotes += votes;
  }
  for (auto& [partyId, votes] : seats[seatIndex].node.fpVotesProjected) {
    shares[partyId] = votes / totalVotes;
  }
  // now convert to deviations
  for (auto const& [partyId, share] : shares) {
    float inheritedDeviation = 0.0f;
    if (allowCurrentData) {
      auto const& parentRegion = largeRegions[seats[seatIndex].parentRegionId];
      if (parentRegion.finalSpecificFpDeviations.contains(partyId)) {
        inheritedDeviation += parentRegion.finalSpecificFpDeviations.at(partyId);
      }
      if (finalSpecificFpDeviations.contains(partyId)) {
        inheritedDeviation += finalSpecificFpDeviations.at(partyId);
      }
    }
    float finalDeviation = seats[seatIndex].node.fpSharesBaseline.contains(partyId)
      ? transformVoteShare(share * 100.0f) - seats[seatIndex].node.fpSharesBaseline.at(partyId) - inheritedDeviation
      : 0.0f;
    if (allowCurrentData) {
      float offset = seats[seatIndex].offsetSpecificFpDeviations.contains(partyId)
        ? seats[seatIndex].offsetSpecificFpDeviations.at(partyId) * (1.0f - seats[seatIndex].node.fpConfidence)
        : 0.0f;
      seats[seatIndex].finalSpecificFpDeviations[partyId] = finalDeviation - offset;
    } else {
      seats[seatIndex].offsetSpecificFpDeviations[partyId] = finalDeviation;
    }
  }
}

void Election::determineSeatFinalTppDeviation(bool allowCurrentData, int seatIndex) {
  // now convert to shares
  float totalVotes = 0.0f;
  std::map<int, float> shares;
  for (auto const& [partyId, votes] : seats[seatIndex].node.tppVotesProjected) {
    totalVotes += votes;
  }

  if (totalVotes == 0.0f) {
    // No tpp estimations, so don't modify anything at the seat level
    if (allowCurrentData) {
      seats[seatIndex].finalSpecificTppDeviation = 0.0f;
    } else {
      seats[seatIndex].offsetSpecificTppDeviation = 0.0f;
    }
    return;
  }
  float alpShare = seats[seatIndex].node.tppVotesProjected.at(0) / totalVotes;
  float inheritedDeviation = 0.0f;
  if (allowCurrentData) {
    auto const& parentRegion = largeRegions[seats[seatIndex].parentRegionId];
    inheritedDeviation += parentRegion.finalSpecificTppDeviation.value_or(0.0f);
    inheritedDeviation += finalSpecificTppDeviation.value_or(0.0f);
  }
  float finalDeviation = transformVoteShare(alpShare * 100.0f) - seats[seatIndex].node.tppShareBaseline.value_or(0.0f) - inheritedDeviation;
  if (allowCurrentData) {
    // This will eventually be replaced with something a lot more sophisticated,
    // but for now this is a simple way to account for factors that may bias
    // the seat's TPP share away from the baseline, such as redistributions, new
    // booths, or expected shifts between vote types.
    float offset = seats[seatIndex].offsetSpecificTppDeviation.value_or(0.0f) * (1.0f - seats[seatIndex].node.tppConfidence);
    seats[seatIndex].finalSpecificTppDeviation = finalDeviation - offset;
  } else {
    seats[seatIndex].offsetSpecificTppDeviation = finalDeviation;
  }
}

int Election::mapPartyId(int ecCandidateId) {
  // Note: only used when initially loading data from EC files
  // so not a bottleneck

  // Helper function to get next available ID
  auto getNextId = [this]() {
    int maxId = -1;
    for (auto const& [_, mappedId] : ecPartyToInternalParty) {
      maxId = std::max(maxId, mappedId);
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
  auto candidateIt = currentElection.candidates.find(ecCandidateId);
  if (candidateIt == currentElection.candidates.end()) {
    candidateIt = previousElection.candidates.find(ecCandidateId);
    if (candidateIt == previousElection.candidates.end()) {
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
