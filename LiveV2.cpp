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

  // Helper function to determine if a given tcp set is a valid tpp set
  auto isTppSet = [natPartyIndex](const auto& shares) {
    return shares.contains(0) && (shares.contains(1) || shares.contains(natPartyIndex));
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
  if (node.totalVotesCurrent() > 0 && isTppSet(node.tcpShares)) {
    node.tppShare = node.tcpShares.at(0);
    int previousTotal = node.totalVotesPrevious();
    if (
      previousTotal > 0
      && isTppSet(node.tcpVotesPrevious)
      && node.tcpVotesPrevious.at(0) > 0
      && node.tcpVotesPrevious.at(0) < previousTotal
    ) {
      float previousShare = transformVoteShare(
        static_cast<float>(node.tcpVotesPrevious.at(0)) / previousTotal * 100.0f
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

Live::Seat::Seat(Results2::Seat const& seat, int parentRegionId)
  : name(seat.name), parentRegionId(parentRegionId)
{
}

void Live::Seat::log(Election const& election, bool includeBooths) const
{
  logger << "\nSeat: " << name << "\n";
  node.log();
  if (includeBooths) {
    for (auto const& booth : booths) {
      election.booths.at(booth).log();
    }
  }
}

Live::LargeRegion::LargeRegion(Region const& region)
  : name(region.name)
{
}

void Live::LargeRegion::log(Election const& election, bool includeSeats, bool includeBooths) const
{
  logger << "\nLargeRegion: " << name << "\n";
  node.log();
  if (includeSeats) {
    for (auto const& seat : seats) {
      election.seats.at(seat).log(election, includeBooths);
    }
  }
}

Live::Election::Election(Results2::Election const& previousElection, Results2::Election const& currentElection, PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), sim(sim), run(run), previousElection(previousElection), currentElection(currentElection)
{
  getNatPartyIndex();
  initializePartyMappings();
  createNodesFromElectionData();
}

void Election::getNatPartyIndex() {
	natPartyIndex = project.parties().indexByShortCode("NAT");
	if (natPartyIndex == -1) natPartyIndex = InvalidPartyIndex;
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
      booths.push_back(Booth(
          currentBooth, 
          previousBoothPtr,
          [this](int partyId) { return this->mapPartyId(partyId); },
          seatIndex,
          natPartyIndex
      ));
      auto& liveSeat = seats.at(seatIndex);
      liveSeat.booths.push_back(int(booths.size()) - 1);
    }
  }
}

void Election::aggregate() {
  for (auto& seat : seats) {
    aggregateToSeat(seat);
  }
  for (auto& largeRegion : largeRegions) {
    aggregateToLargeRegion(largeRegion);
    largeRegion.log(*this, true, true);
  }
}

// Template method for aggregation
template<typename T, typename U>
void Election::aggregateCollection(T& parent, const std::vector<int>& childIndices, 
                                  const std::vector<U>& childNodes) const {
  std::vector<Node const*> nodesToAggregate;
  for (auto const& childIndex : childIndices) {
    nodesToAggregate.push_back(&childNodes.at(childIndex).node);
  }
  parent.node = aggregateFromChildren(nodesToAggregate);
}

void Election::aggregateToSeat(Seat& seat) {
  aggregateCollection(seat, seat.booths, booths);
}

void Election::aggregateToLargeRegion(LargeRegion& largeRegion) {
  aggregateCollection(largeRegion, largeRegion.seats, seats);
}

Node Election::aggregateFromChildren(const std::vector<Node const*>& nodesToAggregate) const {
  // Aggregate swings from previous election to current election
  // Aggregation takes small-scale results and calculates a weighted average
  // in a larger region. This can then be used (in a later step) to extrapolate
  // swings to other small-scale results.
  // Current raw vote tallies and vote shares are not aggregated
  // because they generally don't add information beyond what is already
  // available in the swings

  Node aggregatedNode;

  // Aggregate previous election vote totals (used for weighting only)
  for (auto const& node : nodesToAggregate) {
    for (auto const& [partyId, votes] : node->fpVotesPrevious) {
      aggregatedNode.fpVotesPrevious[partyId] += votes;
    }
  }

  std::map<int, float> fpSwingWeightedSum; // weighted by number of votes
  std::map<int, float> fpWeightSum; // sum of weights

  for (auto const& node : nodesToAggregate) {
    for (auto const& [partyId, swing] : node->fpSwings) {
      fpSwingWeightedSum[partyId] += swing * node->totalVotesPrevious();
      fpWeightSum[partyId] += node->totalVotesPrevious();
    }
  }
  // Node created for each new seat/region/election: should be <200 times
  // per election, so not major bottleneck
  for (auto const& [partyId, swing] : fpSwingWeightedSum) {
    if (fpWeightSum[partyId] > 0) { // ignore parties with no votes
      aggregatedNode.fpSwings[partyId] = swing / fpWeightSum[partyId];
    }
  }

  // Note: for now we don't aggregate tcp swings because they cannot be
  // consistently extrapolated beyond the seat level
  // (except when they are equivalent to tpp swings, which are already covered
  // by the tpp swing calculation below)

  // Calculate tpp swing
  float tppSwingWeightedSum = 0.0f;
  float tppWeightSum = 0.0f;
  for (auto const& node : nodesToAggregate) {
    if (node->tppSwing) {
      tppSwingWeightedSum += node->tppSwing.value() * node->totalVotesPrevious();
      tppWeightSum += node->totalVotesPrevious();
    }
  }
  if (tppWeightSum > 0) {
    aggregatedNode.tppSwing = tppSwingWeightedSum / tppWeightSum;
  }

  PA_LOG_VAR(aggregatedNode.fpSwings);
  PA_LOG_VAR(aggregatedNode.tppSwing);
  PA_LOG_VAR(tppSwingWeightedSum);
  PA_LOG_VAR(tppWeightSum);

  return aggregatedNode;
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
    // Arbitrary offset to ensure independent candidates don't clash with real party IDs
    // Candidate IDs are 5-digit (or shorter) numbers, so this offset makes it easy to spot
    // what the original EC ID was if necessary.
    constexpr int IndependentPartyIdOffset = 100000;
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
