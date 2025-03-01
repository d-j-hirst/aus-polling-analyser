#pragma once

#include "ElectionData.h"
#include "General.h"

#include <map>

class PollingProject;
class Simulation;
class SimulationRun;

namespace Live {

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
  std::optional<float> tppShare; // transformed vote share, only filled if 2pp is available at both elections 
  std::optional<float> tppSwing; // change in transformed vote share, only filled if 2pp is available at both elections

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

  int totalVotesCurrent() const;
  int totalVotesPrevious() const;
};

class Booth {
public:
  Booth(
    Results2::Booth const& currentBooth,
    std::optional<Results2::Booth const*> previousBooth,
    std::function<int(int)> partyMapper,
    int parentSeatId,
    int natPartyIndex
  );

  void log() const;

  std::string name;

  const int parentSeatId;

  Node node;
};

class Seat {
public:
  Seat(Results2::Seat const& seat);

  void log(bool includeBooths = false) const;

  std::string name;

  std::vector<Booth> booths;

  Node node;
};

class Election {
public:
	Election(Results2::Election const& previousElection, Results2::Election const& currentElection, PollingProject& project, Simulation& sim, SimulationRun& run);

  // Propagates information from lower levels to higher levels
  void aggregate();

private:
  void getNatPartyIndex();

  void aggregateToSeat(Seat& seat);

  Node aggregateFromChildren(std::vector<Node const*>& nodesToAggregate);

  void initializePartyMappings();

  void createBoothsFromElectionData();

  // map AEC party IDs to internal party IDs
  int mapPartyId(int ecCandidateId);

  std::vector<Booth> booths;
  std::vector<Seat> seats;

  std::map<int, int> ecPartyToNetParty;
  std::map<int, int> ecBoothToNetBooth;

  std::map<std::string, int> ecAbbreviationToNetParty;

  int natPartyIndex;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;

  Results2::Election const& previousElection;
  Results2::Election const& currentElection;
};

} // namespace Live
