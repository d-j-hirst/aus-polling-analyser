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
	hasClusterAnalysis_ = false;
	switch (type) {
	case Type::Parties:
		lastPartiesOutput = PartiesAnalyser(elections).run(electionFocus);
		lastOutputString = PartiesAnalyser::getTextOutput(lastPartiesOutput);
	case Type::Swing:
		lastSwingOutput = SwingAnalyser(elections).run();
		lastOutputString = SwingAnalyser::getTextOutput(lastSwingOutput);
		break;
	case Type::Cluster:
		lastClusterOutput = ClusterAnalyser(elections).run();
		lastOutputString = ClusterAnalyser::getTextOutput(lastClusterOutput);
		hasClusterAnalysis_ = true;
		break;
	}
}

std::string ElectionAnalyser::textOutput() const
{	
	return lastOutputString;
}

ClusterAnalyser::Output const& ElectionAnalyser::clusterOutput() const
{
	return lastClusterOutput;
}
