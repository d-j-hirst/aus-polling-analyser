#include "LiveV2.h"

#include "ElectionData.h"
#include "Log.h"

using namespace Live;

Node::Node()
{
}

void Node::log() const
{
  for (auto const& [party, votes] : fpVotes) {
    logger << "  FP: " << party << " " << votes << "\n";
  }
}

Booth::Booth(Results2::Booth const& currentBooth, std::optional<Results2::Booth const*> previousBooth, std::map<int, int>& aecPartyToNetParty)
  : name(currentBooth.name)
{
  if (previousBooth) {
    for (auto const& [party, votes] : previousBooth.value()->fpVotes) {
      logger << "Previous booth: " << previousBooth.value()->name << " " << party << " " << votes << "\n";
      auto [it, inserted] = aecPartyToNetParty.try_emplace(party, aecPartyToNetParty.size());
      node.fpVotes[it->second] = votes;
    }
  }
}

void Booth::log() const
{
  logger << "Booth: " << name << "\n";
  node.log();
}

Election::Election(Results2::Election const& previousElection, Results2::Election const& currentElection, PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), sim(sim), run(run), previousElection(previousElection), currentElection(currentElection)
{
  for (auto const& [id, seat] : currentElection.seats) {
    for (auto const& boothId : seat.booths) {
      if (!currentElection.booths.contains(boothId)) {
        continue;
      }
      auto const& currentBooth = currentElection.booths.at(boothId);

      // Extract lambda for better readability
      auto matchBoothId = [&currentBooth](auto const& booth) {
        return booth.second.id == currentBooth.id;
      };

      auto const& previousBooth = std::find_if(previousElection.booths.begin(), previousElection.booths.end(), matchBoothId);

      aecBoothToNetBooth[currentBooth.id] = booths.size();

      booths.push_back(Booth(
        currentBooth, 
        previousBooth != previousElection.booths.end() 
          ? std::optional(&previousBooth->second) 
          : std::nullopt, 
        aecPartyToNetParty)
      );
    }
  }
  for (auto const& booth : booths) {
    booth.log();
  }
}

