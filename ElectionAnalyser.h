#pragma once

#include "PartiesAnalyser.h"

#include <string>

class ElectionCollection;

class ElectionAnalyser {
public:
	enum class Type {
		Parties
	};

	ElectionAnalyser(ElectionCollection const& elections);

	void run(Type type, int electionFocus);

	std::string textOutput() const;
private:
	PartiesAnalyser::Output lastPartiesOutput;

	std::string lastOutputString;

	ElectionCollection const& elections;
};