#include "ClusterAnalyser.h"

#include "ElectionCollection.h"
#include "ElectionData.h"
#include "Log.h"

#include <sstream>
#include <unordered_map>

ClusterAnalyser::ClusterAnalyser(ElectionCollection const& elections)
	: elections(elections)
{
}

inline int32_t findVote2cp(Results2::Election const& election, Results2::Booth const& booth, int32_t partyId) {
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

inline float getSimilarityScore(ClusterAnalyser::Output::BoothSwingData const& booth1, ClusterAnalyser::Output::BoothSwingData const& booth2) {
	float squaredErrorSum = 0.0f;
	float matchedElectionCount = 0.0f;
	for (auto const& booth1e : booth1.elections) {
		auto booth2e = booth2.elections.find(booth1e.first);
		if (booth2e == booth2.elections.end()) continue;
		if (!(booth1e.second.alpSwing && booth2e->second.alpSwing)) continue;
		squaredErrorSum += std::pow(booth1e.second.alpSwing.value() - booth2e->second.alpSwing.value(), 2);
		matchedElectionCount++;
	}
	if (matchedElectionCount <= 1.0f) return 0.0f;
	constexpr float DummyBoothError = 0.001f; // in order to prevent booth comparisons with few election comparisons from dominating,
										   // add a moderate-sized dummy election to help indicate which booth comparisons are most resilient to future errors
	float similarityScore = (matchedElectionCount - 1.0f) / std::sqrt(squaredErrorSum + DummyBoothError);
	return similarityScore;
}

ClusterAnalyser::Output ClusterAnalyser::run()
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
			[](decltype(election.parties)::value_type party) {return party.second.shortCode == "LNP" || party.second.shortCode == "LNQ"; });
		int lnpCode = (lnpIt != election.parties.end() ? lnpIt->second.id : -2);
		auto natIt = std::find_if(election.parties.begin(), election.parties.end(),
			[](decltype(election.parties)::value_type party) {return party.second.shortCode == "NP"; });
		int natCode = (natIt != election.parties.end() ? natIt->second.id : -2);

		output.electionNames[electionKey] = election.name;
		for (auto const& [boothKey, booth] : election.booths) {
			if (!output.booths.count(booth.id)) output.booths[booth.id].boothName = booth.name;
			int alpVotes = findVote2cp(election, booth, alpCode);
			int lpVotes = findVote2cp(election, booth, lpCode) + findVote2cp(election, booth, lnpCode)
				+ findVote2cp(election, booth, natCode);
			auto newVal = output.booths[booth.id].elections.insert({ election.id, {} });
			if (alpVotes && lpVotes) {
				float alpProportion = (alpVotes && lpVotes ? float(alpVotes) / float(alpVotes + lpVotes) : 0.0f);
				newVal.first->second.alp2cp = alpProportion;
			}
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
			auto generalFirstElection = std::find_if(elections.begin(), elections.end(),
				[&](decltype(*elections.begin()) generalElection) {return generalElection.first == firstElection->first; });
			if (generalFirstElection == elections.end()) continue;
			auto generalSecondElection = std::next(generalFirstElection);
			if (generalSecondElection == elections.end()) continue;
			if (generalSecondElection->first != secondElection->first) continue;
			if (firstElection->second.alp2cp && secondElection->second.alp2cp) {
				secondElection->second.alpSwing = secondElection->second.alp2cp.value() - firstElection->second.alp2cp.value();
			}
		}
	}

	auto const& mentoneBooth = output.booths.find(4150);
	if (mentoneBooth != output.booths.end()) {
		for (auto& [key, booth] : output.booths) {
			booth.similarityScore = getSimilarityScore(mentoneBooth->second, booth);
		}
	}
	return output;
}

inline std::string getFirstWord(std::string const& entire) {
	return std::string(entire.begin(), entire.begin() + entire.find(" "));
}

std::string ClusterAnalyser::getTextOutput(Output const& data)
{
	std::vector<Output::BoothSwingData> sortedBooths;
	std::transform(data.booths.begin(), data.booths.end(), std::back_inserter(sortedBooths),
		[](decltype(data.booths)::value_type value) {return value.second; });
	std::sort(sortedBooths.begin(), sortedBooths.end(),
		[](Output::BoothSwingData const& lhs, Output::BoothSwingData const& rhs) {
			return lhs.similarityScore > rhs.similarityScore;
		});
	std::stringstream output;
	output << "Swing analysis results:\n";
	for (auto const& booth : sortedBooths) {
		output << booth.boothName << " (" << booth.seatName << ")" << ": ";
		output << "SS: " << booth.similarityScore << ", ";
		bool firstElement = true;
		for (auto electionIt : booth.elections) {
			output << (firstElement ? "" : ", ") << getFirstWord(data.electionNames.at(electionIt.first));
			if (electionIt.second.alp2cp && electionIt.second.alp2cp.value()) {
				//output << " (ALP 2cp " << electionIt.second.alp2cp.value() * 100.0f << "%";
				output << "(";
				if (electionIt.second.alpSwing) {
					output << ", " << (electionIt.second.alpSwing.value() >= 0 ? "+" : "");
					output << electionIt.second.alpSwing.value() * 100.0f << "%";
				}
				output << ")";
			}
			firstElement = false;
		}
		output << "\n";
	}
	return output.str();
}
