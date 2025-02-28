#pragma once

#include "ElectionData.h"

#include <map>

class PollingProject;
class Simulation;
class SimulationRun;

namespace Live {

class Node {
public:
  Node();

  void log() const;

  float tpp;
  std::map<int, int> fpVotes;
  std::map<int, int> fpVotesPrevious;
};

class Booth {
public:
  Booth(Results2::Booth const& currentBooth, std::optional<Results2::Booth const*> previousBooth, std::map<int, int>& aecPartyToNetParty);

  void log() const;

  std::string name;

  Node node;
};

class Election {
public:
	Election(Results2::Election const& previousElection, Results2::Election const& currentElection, PollingProject& project, Simulation& sim, SimulationRun& run);

private:
  std::vector<Booth> booths;

  Results2::Election const& previousElection;
  Results2::Election const& currentElection;

  std::map<int, int> aecPartyToNetParty;
  std::map<int, int> aecBoothToNetBooth;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;
};

} // namespace Live
