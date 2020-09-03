#pragma once

#include "PartiesAnalyser.h"
#include "SwingAnalyser.h"

#include <string>

class ElectionCollection;

class ElectionAnalyser {
public:
	enum class Type {
		Parties,
		Swing
	};

	ElectionAnalyser(ElectionCollection const& elections);

	void run(Type type, int electionFocus);

	std::string textOutput() const;
private:
	PartiesAnalyser::Output lastPartiesOutput;
	SwingAnalyser::Output lastSwingOutput;

	std::string lastOutputString;

	ElectionCollection const& elections;
};