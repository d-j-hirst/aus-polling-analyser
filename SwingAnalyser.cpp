#include "SwingAnalyser.h"

#include "ElectionCollection.h"
#include "ElectionData.h"
#include "Log.h"

#include <sstream>
#include <unordered_map>

SwingAnalyser::SwingAnalyser(ElectionCollection const& elections)
	: elections (elections)
{
}

int32_t findVote2cp(Results2::Election const& election, Results2::Booth const& booth, int32_t partyId) {
	int votes = 0;
	for (auto [candidate, thisVotes] : booth.votes2cp) {
		auto candidateData = election.candidates.at(candidate);
		auto partyData = election.parties.at(candidateData.party);
		if (partyData.id == partyId) {
			votes = thisVotes;
		}
	}
	return votes;
}

SwingAnalyser::Output SwingAnalyser::run()
{
	Output output;
	for (auto const& [electionKey, election] : elections) {
		auto alpIt = std::find_if(election.parties.begin(), election.parties.end(),
			[](decltype(election.parties)::value_type party) {return party.second.shortCode == "ALP"; });
		int alpCode = (alpIt != election.parties.end() ? alpIt->second.id : -2);
		auto lpIt = std::find_if(election.parties.begin(), election.parties.end(),
			[](decltype(election.parties)::value_type party) {return party.second.shortCode == "LP"; });
		int lpCode = (lpIt != election.parties.end() ? lpIt->second.id : -2);
		auto lnpIt = std::find_if(election.parties.begin(), election.parties.end(),
			[](decltype(election.parties)::value_type party) {return party.second.shortCode == "LNP"; });
		int lnpCode = (lnpIt != election.parties.end() ? lnpIt->second.id : -2);
		auto natIt = std::find_if(election.parties.begin(), election.parties.end(),
			[](decltype(election.parties)::value_type party) {return party.second.shortCode == "NAT"; });
		int natCode = (natIt != election.parties.end() ? natIt->second.id : -2);

		output.electionNames[electionKey] = election.name;
		for (auto const& [boothKey, booth] : election.booths) {
			if (!output.booths.count(booth.id)) output.booths[booth.id].boothName = booth.name;
			int alpVotes = findVote2cp(election, booth, alpCode);
			int lpVotes = findVote2cp(election, booth, lpCode) + findVote2cp(election, booth, lnpCode)
				+ findVote2cp(election, booth, natCode);
			float alpProportion = (alpVotes && lpVotes ? float(alpVotes) / float(alpVotes + lpVotes) : 0.0f);
			output.booths[booth.id].elections.insert({ election.id, {alpProportion} });
		}
		for (auto const& [seatKey, seat] : election.seats) {
			for (auto const& boothId : seat.booths) {
				output.booths[boothId].seatName = seat.name;
			}
		}
	}
	for (auto& [key, booth] : output.booths) {
		for (auto firstElection = booth.elections.begin(); firstElection != booth.elections.end(); ++firstElection) {
			auto secondElection = std::next(firstElection);
			if (secondElection == booth.elections.end()) break;
			if (firstElection->second.alp2cp && secondElection->second.alp2cp) {
				secondElection->second.alpSwing = secondElection->second.alp2cp.value() - firstElection->second.alp2cp.value();
			}
		}
	}
	return output;
}

std::string SwingAnalyser::getTextOutput(Output const& data)
{
	std::stringstream output;
	output << "Swing analysis results:\n";
	for (auto const& [key, booth] : data.booths) {
		output << booth.boothName << " (" << booth.seatName << ")" << ": ";
		bool firstElement = true;
		for (auto electionIt : booth.elections) {
			output << (firstElement ? "" : ", ") << data.electionNames.at(electionIt.first);
			if (electionIt.second.alp2cp) {
				output << " (ALP 2cp " << electionIt.second.alp2cp.value() * 100.0f << "%";
				if (electionIt.second.alpSwing) {
					output << ", " << electionIt.second.alpSwing.value() * 100.0f << "%";
				}
				output << ")";
			}
			firstElement = false;
		}
		output << "\n";
	}
	return output.str();
}
