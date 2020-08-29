#include "PartiesAnalyser.h"

#include "ElectionCollection.h"
#include "Log.h"

PartiesAnalyser::PartiesAnalyser(ElectionCollection const& elections)
	: elections(elections)
{
}

PartiesAnalyser::Output PartiesAnalyser::run(int electionFocus)
{
	logger << "Running party analysis for election: " << elections.viewByIndex(electionFocus).name << "\n";
	return PartiesAnalyser::Output();
}
