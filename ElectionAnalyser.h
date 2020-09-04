#pragma once

#include "ClusterAnalyser.h"
#include "PartiesAnalyser.h"
#include "SwingAnalyser.h"

#include <string>

class ElectionCollection;

class ElectionAnalyser {
public:
	enum class Type {
		Parties,
		Swing,
		Cluster
	};

	ElectionAnalyser(ElectionCollection const& elections);

	void run(Type type, int electionFocus);

	std::string textOutput() const;
private:
	PartiesAnalyser::Output lastPartiesOutput;
	SwingAnalyser::Output lastSwingOutput;
	ClusterAnalyser::Output lastClusterOutput;

	std::string lastOutputString;

	ElectionCollection const& elections;
};