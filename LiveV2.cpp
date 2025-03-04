#include "LiveV2.h"

#include "ElectionData.h"
#include "General.h"
#include "Log.h"
#include "PollingProject.h"
#include "SpecialPartyCodes.h"

#include <numeric>

using namespace LiveV2;

constexpr float VoteObsWeightStrength = 80.0f;
constexpr float PreferenceFlowObsWeightStrength = 200.0f;

const float obsWeight(float confidence, float strength = VoteObsWeightStrength) {
  return std::min(1.0f, 1.025f - 1.025f / (1.0f + std::pow(confidence, 1.6f) * strength));
}

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
    float totalCurrentVotes = static_cast<float>(node.totalFpVotesCurrent());
    float totalPreviousVotes = static_cast<float>(node.totalVotesPrevious());

    int totalCurrentVotesCounted = std::accumulate(currentMap.begin(), currentMap.end(), 0,
      [](int sum, const auto& pair) { return sum + pair.second; });

    if (totalCurrentVotesCounted > 0 && float(totalCurrentVotesCounted) < float(totalCurrentVotes) * 0.95f) {
      logger << "Warning: Only " << float(totalCurrentVotesCounted) / float(totalCurrentVotes) * 100.0f << "% of current votes were counted in " << name << "\n";
      logger << "Skipping votes as they are not reliable\n";
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
  processVotes(
    currentBooth.tcpVotesCandidate, 
    previousBooth ? std::optional(previousBooth.value()->tcpVotesCandidate) : std::nullopt,
    node.tcpVotesCurrent, node.tcpVotesPrevious, node.tcpShares, node.tcpSwings
  );

  // Determine tpp share and swing, if available
  if (node.totalFpVotesCurrent() > 0 && isTppSet(node.tcpShares, natPartyIndex)) {
    node.tppShare = node.tcpShares.at(0);
    int previousTotal = node.totalVotesPrevious();
    if (
      previousTotal > 0
      && isTppSet(node.tcpVotesPrevious, natPartyIndex)
      && node.tcpVotesPrevious.at(0) > 0
      && node.tcpVotesPrevious.at(0) < previousTotal
    ) {
      float previousShare = transformVoteShare(
        static_cast<float>(node.tcpVotesPrevious.at(0)) / previousTotal * 100.0f
      );
      node.tppSwing = node.tppShare.value() - previousShare;
    }
  }



  // Booths are either complete or not in, so confidence is 1 if there are votes, 0 otherwise
  // We'll handle partial results (like absent/postal votes that are in different batches) later
  // as well as the usually very minor changes that occur when the check count is performed.
  node.fpConfidence = node.fpSwings.size() > 0 ? 1 : 0;
  node.tcpConfidence = node.tcpSwings.size() > 1 ? 1 : 0;
  node.tppConfidence = node.tppSwing.has_value() ? 1 : 0;
}

void Booth::calculateTppSwing(int natPartyIndex) {
  int previousTotal = node.totalVotesPrevious();
  if (
    previousTotal > 0
    && isTppSet(node.tcpVotesPrevious, natPartyIndex)
    && node.tcpVotesPrevious.at(0) > 0
    && node.tcpVotesPrevious.at(0) < previousTotal
  ) {
    float previousShare = transformVoteShare(
      static_cast<float>(node.tcpVotesPrevious.at(0)) / previousTotal * 100.0f
    );
    node.tppSwing = node.tppShare.value() - previousShare;
  }
}

void Booth::log() const
{
  logger << "Booth: " << name << "\n";
  node.log();
}

LiveV2::Seat::Seat(Results2::Seat const& seat, int parentRegionId)
  : name(seat.name), parentRegionId(parentRegionId)
{
}

void LiveV2::Seat::log(Election const& election, bool includeBooths) const
{
  logger << "\nSeat: " << name << "\n";
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
  logger << "Initializing live election\n";
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

    if (partyOneShare > 0.0f && partyOneShare < 100.0f) {
      if (withTpp) {
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
      } else {
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
  node.specificPreferenceFlowDeviation = node.preferenceFlowDeviation.value() * obsWeight(node.preferenceFlowConfidence, PreferenceFlowObsWeightStrength);
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
        if (seat.node.totalVotesPrevious() > 0 && seat.node.fpVotesPrevious.contains(partyId)) {
          seat.node.fpSwingsBaseline[partyId] = seat.node.fpSharesBaseline[partyId] - 
            transformVoteShare(float(seat.node.fpVotesPrevious.at(partyId)) / float(seat.node.totalVotesPrevious()) * 100.0f);
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

void Election::extrapolateBaselineSwings() {
  for (auto& seat : seats) {
    for (int boothIndex : seat.booths) {
      auto& booth = booths.at(boothIndex);
      for (auto const& [partyId, swing] : seat.node.fpSwingsBaseline) {
        if (booth.node.fpVotesPrevious.contains(partyId)) {
          booth.node.fpSwingsBaseline[partyId] = swing;
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
    for (auto const& [partyId, swing] : thisNode->fpDeviations) {
      fpDeviationWeightedSum[partyId] += swing * weight;
      fpWeightSum[partyId] += weight;
    }
    fpConfidenceSum += thisNode->fpConfidence * thisNode->totalVotesPrevious();
    fpConfidenceWeightSum += thisNode->totalVotesPrevious();
  }
  // Node created for each new seat/region/election: should be <200 times
  // per election, so not major bottleneck
  for (auto const& [partyId, swing] : fpDeviationWeightedSum) {
    if (fpWeightSum[partyId] > 0) { // ignore parties with no votes
      aggregatedNode.fpDeviations[partyId] = swing / fpWeightSum[partyId];
      aggregatedNode.fpConfidence = fpConfidenceSum / fpConfidenceWeightSum; // will eventually have a more sophisticated nonlinear confidence calculation
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
    }
    tppConfidenceSum += thisNode->tppConfidence * thisNode->totalVotesPrevious();
    tppConfidenceWeightSum += thisNode->totalVotesPrevious();
  }
  if (tppWeightSum > 0) {
    aggregatedNode.tppDeviation = tppDeviationWeightedSum / tppWeightSum;
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
    }
    preferenceFlowConfidenceSum += thisNode->preferenceFlowConfidence * thisNode->totalVotesPrevious();
    preferenceFlowConfidenceWeightSum += thisNode->totalVotesPrevious();
  }
  if (preferenceFlowWeightSum > 0) {
    aggregatedNode.preferenceFlowDeviation = preferenceFlowDeviationWeightedSum / preferenceFlowWeightSum;
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
  node.specificTppDeviation = node.tppDeviation.value() * tppObsWeight;
}

void Election::determineLargeRegionSpecificDeviations() {
  for (auto& largeRegion : largeRegions) {
    float fpObsWeight = obsWeight(largeRegion.node.fpConfidence);
    for (auto const& [partyId, deviation] : largeRegion.node.fpDeviations) {
      float withoutElectionSpecific = 
        deviation - (node.specificFpDeviations.contains(partyId) ? node.specificFpDeviations.at(partyId) : 0.0f);
      largeRegion.node.specificFpDeviations[partyId] = withoutElectionSpecific * fpObsWeight;
    }
    float tppObsWeight = obsWeight(largeRegion.node.tppConfidence);
    float parentTppDeviation = node.specificTppDeviation.value_or(0.0f);
    float withoutElectionSpecific = largeRegion.node.tppDeviation ? largeRegion.node.tppDeviation.value() - parentTppDeviation : 0.0f;
    largeRegion.node.specificTppDeviation = withoutElectionSpecific * tppObsWeight;
  }
}

void Election::determineSeatSpecificDeviations() {
  for (auto& largeRegion : largeRegions) {
    for (int seatIndex : largeRegion.seats) {
      auto& seat = seats.at(seatIndex);
      float fpObsWeight = obsWeight(seat.node.fpConfidence);
      for (auto const& [partyId, deviation] : seat.node.fpDeviations) {
        float withoutElectionSpecific = 
          deviation - (largeRegion.node.specificFpDeviations.contains(partyId) ? largeRegion.node.specificFpDeviations.at(partyId) : 0.0f);
        seat.node.specificFpDeviations[partyId] = withoutElectionSpecific * fpObsWeight;
      }
      float tppObsWeight = obsWeight(seat.node.tppConfidence);
      float parentTppDeviation = largeRegion.node.specificTppDeviation.value_or(0.0f);
      parentTppDeviation += node.specificTppDeviation.value_or(0.0f);
      float withoutElectionSpecific = seat.node.tppDeviation ? seat.node.tppDeviation.value() - parentTppDeviation : 0.0f;
      seat.node.specificTppDeviation = withoutElectionSpecific * tppObsWeight;
    }
  }
}

void Election::determineBoothSpecificDeviations() {
  for (auto& seat : seats) {
    for (int boothIndex : seat.booths) {
      auto& booth = booths.at(boothIndex);
      float fpObsWeight = obsWeight(booth.node.fpConfidence);
      for (auto const& [partyId, deviation] : seat.node.fpDeviations) {
        float withoutElectionSpecific = 
          deviation - (seat.node.specificFpDeviations.contains(partyId) ? seat.node.specificFpDeviations.at(partyId) : 0.0f);
        booth.node.specificFpDeviations[partyId] = withoutElectionSpecific * fpObsWeight;
      }
      float tppObsWeight = obsWeight(booth.node.tppConfidence);
      float parentTppDeviation = seat.node.specificTppDeviation.value_or(0.0f);
      auto const& largeRegion = largeRegions.at(seat.parentRegionId);
      parentTppDeviation += largeRegion.node.specificTppDeviation.value_or(0.0f);
      parentTppDeviation += node.specificTppDeviation.value_or(0.0f);
      float withoutElectionSpecific = booth.node.tppDeviation ? booth.node.tppDeviation.value() - parentTppDeviation : 0.0f;
      booth.node.specificTppDeviation = withoutElectionSpecific * tppObsWeight;
    }
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

void Election::log(bool includeLargeRegions, bool includeSeats, bool includeBooths) const {
  logger << "\nElection:\n";
  node.log();
  if (includeLargeRegions) {
    for (auto const& largeRegion : largeRegions) {
      largeRegion.log(*this, includeSeats, includeBooths);
    }
  }
}
