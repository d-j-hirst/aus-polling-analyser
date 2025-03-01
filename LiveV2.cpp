#include "LiveV2.h"

#include "ElectionData.h"
#include "General.h"
#include "Log.h"
#include "PollingProject.h"
#include "SpecialPartyCodes.h"

#include <numeric>

using namespace Live;

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
  PA_LOG_VAR(tppShare);
  PA_LOG_VAR(tppSwing);
  PA_LOG_VAR(fpSharesPercent());
  PA_LOG_VAR(tcpSharesPercent());
}

int Node::totalVotesCurrent() const {
  return std::accumulate(fpVotesCurrent.begin(), fpVotesCurrent.end(), 0,
    [](int sum, const auto& pair) { return sum + pair.second; });
}

int Node::totalVotesPrevious() const {
  return std::accumulate(fpVotesPrevious.begin(), fpVotesPrevious.end(), 0,
    [](int sum, const auto& pair) { return sum + pair.second; });
}

Booth::Booth(
  Results2::Booth const& currentBooth,
  std::optional<Results2::Booth const*> previousBooth,
  std::function<int(int)> partyMapper,
  int parentSeatId,
  int natPartyIndex
)
  : name(currentBooth.name), parentSeatId(parentSeatId)
{
  // Helper function to process votes, calculate shares and swings
  auto processVotes = [this, &partyMapper](
      const auto& currentVotes, const auto& previousVotes, 
      auto& currentMap, auto& previousMap, auto& sharesMap, auto& swingsMap) {
    
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

    // Calculate total votes for percentages
    float totalCurrentVotes = static_cast<float>(node.totalVotesCurrent());
    float totalPreviousVotes = static_cast<float>(node.totalVotesPrevious());

    // Calculate shares and swings
    if (totalCurrentVotes > 0) {
      for (auto const& [partyId, votes] : currentMap) {
        if (votes == 0) {
          continue;
        }
        float currentTransformed = transformVoteShare(static_cast<float>(votes) / totalCurrentVotes * 100.0f);
        sharesMap[partyId] = currentTransformed;
        if (previousMap.contains(partyId) && totalPreviousVotes > 0) {
          if (previousMap.at(partyId) == 0) {
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
  processVotes(
    currentBooth.tcpVotesCandidate, 
    previousBooth ? std::optional(previousBooth.value()->tcpVotesCandidate) : std::nullopt,
    node.tcpVotesCurrent, node.tcpVotesPrevious, node.tcpShares, node.tcpSwings
  );

  // Determine tpp share and swing, if available
  if (
    node.totalVotesCurrent() > 0 &&
    node.tcpShares.contains(0) &&
    (node.tcpShares.contains(1) || node.tcpShares.contains(natPartyIndex))
  ) {
    node.tppShare = node.tcpShares.at(0);
    if (
      node.totalVotesPrevious() > 0 &&
      node.tcpVotesPrevious.contains(0) &&
      (node.tcpVotesPrevious.contains(1) || node.tcpVotesPrevious.contains(natPartyIndex))
    ) {
      float previousShare = transformVoteShare(
        static_cast<float>(node.tcpVotesPrevious.at(0)) / node.totalVotesPrevious() * 100.0f
      );
      node.tppSwing = node.tppShare.value() - previousShare;
    }
  }
}

void Booth::log() const
{
  logger << "Booth: " << name << "\n";
  node.log();
}

Live::Seat::Seat(Results2::Seat const& seat)
  : name(seat.name)
{
}

void Live::Seat::log(bool includeBooths) const
{
  logger << "\nSeat: " << name << "\n";
  node.log();
  if (includeBooths) {
    for (auto const& booth : booths) {
      booth.log();
    }
  }
}

Election::Election(Results2::Election const& previousElection, Results2::Election const& currentElection, PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), sim(sim), run(run), previousElection(previousElection), currentElection(currentElection)
{
  getNatPartyIndex();
  initializePartyMappings();
  createBoothsFromElectionData();
}

void Election::getNatPartyIndex() {
	natPartyIndex = project.parties().indexByShortCode("NAT");
	if (natPartyIndex == -1) natPartyIndex = InvalidPartyIndex;
}

void Election::initializePartyMappings() {
  for (auto const& [id, party] : currentElection.parties) {
    int simIndex = project.parties().indexByShortCode(party.shortCode);
    if (simIndex != -1) {
      ecPartyToNetParty[id] = simIndex;
      ecAbbreviationToNetParty[party.shortCode] = simIndex;
    }
  }

  for (auto const& [id, party] : previousElection.parties) {
    int simIndex = project.parties().indexByShortCode(party.shortCode);
    if (simIndex != -1) {
      ecPartyToNetParty[id] = simIndex;
      ecAbbreviationToNetParty[party.shortCode] = simIndex;
    }
  }
}

void Election::createBoothsFromElectionData() {
  for (auto const& [id, seat] : currentElection.seats) {
    int seatIndex = seats.size();
    seats.push_back(seat);
    for (auto const& boothId : seat.booths) {
      if (!currentElection.booths.contains(boothId)) {
        continue;
      }
      auto const& currentBooth = currentElection.booths.at(boothId);

      // Find matching booth in previous election if it exists
      std::optional<Results2::Booth const*> previousBoothPtr = std::nullopt;
      for (auto const& [prevBoothId, prevBooth] : previousElection.booths) {
          if (prevBooth.id == currentBooth.id) {
              previousBoothPtr = &prevBooth;
              break;
          }
      }

      // Create new booth with mapping function
      booths.push_back(Booth(
          currentBooth, 
          previousBoothPtr,
          [this](int partyId) { return this->mapPartyId(partyId); },
          seatIndex,
          natPartyIndex
      ));
      seats.at(seatIndex).booths.push_back(booths.back());
    }
  }
}

void Election::aggregate() {
  for (auto& seat : seats) {
    aggregateToSeat(seat);
    seat.log(true);
  }
}

void Election::aggregateToSeat(Seat& seat) {
  std::vector<Node const*> boothsToAggregate;

  for (auto const& booth : seat.booths) {
    boothsToAggregate.push_back(&booth.node);
  }
  seat.node = aggregateFromChildren(boothsToAggregate);
}

Node Election::aggregateFromChildren(std::vector<Node const*>& nodesToAggregate) {

  std::map<int, float> fpSwingWeightedSum; // weighted by number of votes
  std::map<int, float> fpWeightSum; // sum of weights

  for (auto const& node : nodesToAggregate) {
    for (auto const& [partyId, swing] : node->fpSwings) {
      fpSwingWeightedSum[partyId] += swing * node->totalVotesCurrent();
      fpWeightSum[partyId] += node->totalVotesCurrent();
    }
  }
  Node aggregatedNode;
  for (auto const& [partyId, swing] : fpSwingWeightedSum) {
    if (fpWeightSum[partyId] > 0) { // ignore parties with no votes
      aggregatedNode.fpSwings[partyId] = swing / fpWeightSum[partyId];
    }
  }

  // Calculate tpp swing
  float tppSwingWeightedSum = 0.0f;
  float tppWeightSum = 0.0f;
  for (auto const& node : nodesToAggregate) {
    if (node->tppSwing) {
      tppSwingWeightedSum += node->tppSwing.value() * node->totalVotesCurrent();
      tppWeightSum += node->totalVotesCurrent();
    }
  }
  if (tppWeightSum > 0) {
    aggregatedNode.tppSwing = tppSwingWeightedSum / tppWeightSum;
  }

  return aggregatedNode;
}

int Election::mapPartyId(int ecCandidateId) {
  // Helper function to get next available ID
  auto getNextId = [this]() {
    int maxId = -1;
    for (auto const& [_, mappedId] : ecPartyToNetParty) {
      maxId = std::max(maxId, mappedId);
    }
    return maxId + 1;
  };

  // Helper function to process a party from any election
  auto processParty = [&](const Results2::Party& party, int ecPartyId) -> std::optional<int> {
    // Check if we've seen this abbreviation before
    auto abbrevIt = ecAbbreviationToNetParty.find(party.shortCode);
    if (abbrevIt != ecAbbreviationToNetParty.end()) {
      // Reuse the same internal ID for parties with same short code
      ecPartyToNetParty[ecPartyId] = abbrevIt->second;
      return abbrevIt->second;
    }
    
    // New party abbreviation - assign a new internal ID
    int newId = getNextId();
    ecPartyToNetParty[ecPartyId] = newId;
    ecAbbreviationToNetParty[party.shortCode] = newId;
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
  if (ecPartyId == Results2::Candidate::Independent) {
    return ecCandidateId + 100000;
  }

  // Check if we've already mapped this EC party ID
  auto it = ecPartyToNetParty.find(ecPartyId);
  if (it != ecPartyToNetParty.end()) {
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
  ecPartyToNetParty[ecPartyId] = newId;
  return newId;
}
