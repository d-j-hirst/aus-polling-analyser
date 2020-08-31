#include "PartiesAnalyser.h"

#include "ElectionCollection.h"
#include "Log.h"

PartiesAnalyser::PartiesAnalyser(ElectionCollection const& elections)
	: elections(elections)
{
}

PartiesAnalyser::Output PartiesAnalyser::run(int electionFocus)
{
	PartiesAnalyser::Output output;
	Results2::Election const& thisElection = elections.viewByIndex(electionFocus);
	for (auto const& [key, party] : thisElection.parties) {
		auto& thisParty = output.parties.insert({ party.id, Output::PartyInfo() }).first->second;
		thisParty.name = party.name;
		thisParty.shortCode = party.shortCode;
	}
	for (auto const& [key, candidate] : thisElection.candidates) {
		output.parties[candidate.party].candidateCount++;
	}
	return output;
}
