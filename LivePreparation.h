#pragma once

#include "ElectionData.h"

#include "tinyxml2.h"

#include <stdexcept>
#include <string>

class PollingProject;
class Simulation;
class SimulationRun;

// Validates and acquires the files needed by an automatic live simulation,
// builds previous/current Results2 elections, then passes them to LiveV2.
// ElectionData owns the jurisdiction-specific parsing rules.
class LivePreparation {
public:
	class Exception : public std::runtime_error {
	public:
		explicit Exception(std::string what) : std::runtime_error(what) {}
	};

	LivePreparation(PollingProject& project, Simulation& sim, SimulationRun& run);
	static void validateAutomaticSetup(PollingProject const& project, Simulation const& sim);
	void prepareLiveAutomatic();

private:

	void downloadPreviousResults();
	void parsePreviousResults();
	void downloadPreload();
	void parsePreload();
	void downloadPollingPlaces();
	void parsePollingPlaces();
	void acquireCurrentResults();
	void downloadLatestResults();
	void parseCurrentResults();

	std::string getTermCode() const;
	void loadEcsaXmlDocument(tinyxml2::XMLDocument& document, std::string const& filename) const;

	PollingProject& project;
	Simulation& sim;
	SimulationRun& run;

	std::string xmlFilename;
	tinyxml2::XMLDocument xml;
	Results2::Election previousElection;
	Results2::Election currentElection;
};
