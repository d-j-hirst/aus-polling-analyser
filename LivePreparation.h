#pragma once

#include "ElectionData.h"

#include "tinyxml2.h"

#include <map>
#include <stdexcept>
#include <vector>
#include <utility>

class PollingProject;
class Seat;
class Simulation;
class SimulationRun;

class LivePreparation {
public:
	class Exception : public std::runtime_error {
	public:
		Exception(std::string what) : std::runtime_error(what) {}
	};

	LivePreparation(PollingProject& project, Simulation& sim, SimulationRun& run);
	void prepareLiveAutomatic();

private:

	void downloadPreviousResults();
	void parsePreviousResults();
	void downloadPreload();
	void parsePreload();
	void downloadCurrentResults();
	void downloadLatestResults();
	void parseCurrentResults();
	void doMiscellaneousUpdates();
	void preparePartyCodeGroupings();
	void calculateBoothFpSwings();
	void calculateTppPreferenceFlows();
	void calculateSeatPreferenceFlows();
	void estimateBoothTcps();
	void calculateBoothTcpSwings();
	void calculateCountProgress();
	void determinePpvcBias();
	void calculateSeatSwings();
	void determinePpvcBiasSensitivity();
	void projectOrdinaryVoteTotals();
	void projectDeclarationVotes();
	void determineDecVoteSensitivity();
	void determinePartyIdConversions();
	void determineSeatIdConversions();
	void prepareLiveTppSwings();
	void prepareLiveTcpSwings();
	void prepareLiveFpSwings();
	void prepareOverallLiveFpSwings();

	std::string getTermCode();

	Results2::Seat const& findBestMatchingPreviousSeat(int currentSeatId);
	std::map<int, int> findMatchedParties(Results2::Seat const& previousSeat, Results2::Seat const& currentSeat);

	std::string xmlFilename;
	tinyxml2::XMLDocument xml;
	Results2::Election previousElection;
	Results2::Election currentElection;

	// maps the AEC's party IDs to the simulation's party index
	std::unordered_map<int, int> aecPartyToSimParty;

	// maps the AEC's seat IDs to the simulation's seat index
	std::unordered_map<int, int> aecSeatToSimSeat;

	std::unordered_map<int, float> updatedPreferenceFlows;

	std::map<int, int> seatOrdinaryVotesCountedFp; // actual number of votes
	std::map<int, int> seatOrdinaryVotesCountedTcp; // actual number of votes
	std::map<int, int> seatOrdinaryVotesProjection; // actual number of votes
	std::map<int, std::map<int, float>> seatOrdinaryTcpPercent; // seatId, then party Id
	std::map<int, std::map<int, float>> seatOrdinaryTcpSwing; // seatId, then party Id
	std::map<int, std::map<int, float>> seatDecVotePercent;
	std::map<int, float> seatDecVotePercentOfCounted; // proportion to ordinary votes
	std::map<int, std::map<int, float>> seatDecVoteTcpSwing;
	std::map<int, float> seatDecVoteProjectedProportion; // proportion to ordinary votes
	std::map<int, float> seatDecVoteSwingBasis; // proportion of counted dec votes out of estimated total
	std::map<int, std::map<int, float>> seatPostCountTcpEstimate; // full estimate of the postcount TCP

	std::unordered_map<std::string, int> partyCodeGroupings;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;
};