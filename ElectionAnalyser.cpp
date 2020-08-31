#include "ElectionAnalyser.h"

#include "Log.h"
#include "PartiesAnalyser.h"

ElectionAnalyser::ElectionAnalyser(ElectionCollection const& elections)
	: elections(elections)
{
}

void ElectionAnalyser::run(Type type, int electionFocus)
{
	if (type == Type::Parties) {
		auto result = PartiesAnalyser(elections).run(electionFocus);
		logger << "Party analysis results:\n";
		for (auto const& [key, party] : result.parties) {
			logger << " " << party.name << " (" <<party.shortCode << ", " << key << ") - " << party.candidateCount << " candidates\n";
		}
	}
}
