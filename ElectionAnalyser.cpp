#include "ElectionAnalyser.h"

#include "Log.h"
#include "PartiesAnalyser.h"
#include "SwingAnalyser.h"

ElectionAnalyser::ElectionAnalyser(ElectionCollection const& elections)
	: elections(elections)
{
}

void ElectionAnalyser::run(Type type, int electionFocus)
{
	if (type == Type::Parties) {
		lastPartiesOutput = PartiesAnalyser(elections).run(electionFocus);
		lastOutputString = PartiesAnalyser::getTextOutput(lastPartiesOutput);
	}
	else if (type == Type::Swing) {
		lastSwingOutput = SwingAnalyser(elections).run();
		lastOutputString = SwingAnalyser::getTextOutput(lastSwingOutput);
	}
}

std::string ElectionAnalyser::textOutput() const
{	
	return lastOutputString;
}
