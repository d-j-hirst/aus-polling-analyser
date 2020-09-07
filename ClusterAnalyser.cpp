#include "ClusterAnalyser.h"

#include "ElectionCollection.h"
#include "ElectionData.h"
#include "Log.h"

#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <queue>

struct Pairing {
	float similarity;
	std::pair<int, int> clusters;
};

bool operator<(Pairing const& lhs, Pairing const& rhs) {
	return lhs.similarity < rhs.similarity;
}

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

inline float getSimilarityScore(ClusterAnalyser::Output::Cluster const& cluster1, ClusterAnalyser::Output::Cluster const& cluster2) {
	float squaredErrorSum = 0.0f;
	float matchedElectionCount = 0.0f;
	for (auto const& cluster1e : cluster1.elections) {
		auto booth2e = cluster2.elections.find(cluster1e.first);
		if (booth2e == cluster2.elections.end()) continue;
		if (!(cluster1e.second.alpSwing && booth2e->second.alpSwing)) continue;
		squaredErrorSum += std::pow(cluster1e.second.alpSwing.value() - booth2e->second.alpSwing.value(), 2);
		matchedElectionCount++;
	}
	constexpr float SubtractMatchedElection = 0.9f;
	if (matchedElectionCount <= SubtractMatchedElection) return 0.0f;
	constexpr float DummyBoothError = 0.001f; // in order to prevent booth comparisons with few election comparisons from dominating,
										   // add a moderate-sized dummy election to help indicate which booth comparisons are most resilient to future errors
	float similarityScore = (matchedElectionCount - SubtractMatchedElection) / std::sqrt(squaredErrorSum + DummyBoothError);
	return similarityScore;
}

ClusterAnalyser::Output::Cluster ClusterAnalyser::mergeClusters(ClusterAnalyser::Output::Cluster const& cluster1, ClusterAnalyser::Output::Cluster const& cluster2) {
	ClusterAnalyser::Output::Cluster newCluster;
	if (cluster1.firstBoothVotes > cluster2.firstBoothVotes) {
		newCluster.firstBoothId = cluster1.firstBoothId;
		newCluster.firstBoothVotes = cluster1.firstBoothVotes;
		if (cluster1.secondBoothVotes > cluster2.firstBoothVotes) {
			newCluster.secondBoothId = cluster1.secondBoothId;
			newCluster.secondBoothVotes = cluster1.secondBoothVotes;
		}
		else {
			newCluster.secondBoothId = cluster2.firstBoothId;
			newCluster.secondBoothVotes = cluster2.firstBoothVotes;
		}
	}
	else {
		newCluster.firstBoothId = cluster2.firstBoothId;
		newCluster.firstBoothVotes = cluster2.firstBoothVotes;
		if (cluster2.secondBoothVotes > cluster1.firstBoothVotes) {
			newCluster.secondBoothId = cluster2.secondBoothId;
			newCluster.secondBoothVotes = cluster2.secondBoothVotes;
		}
		else {
			newCluster.secondBoothId = cluster1.firstBoothId;
			newCluster.secondBoothVotes = cluster1.firstBoothVotes;
		}
	}
	newCluster.totalVotes = cluster1.totalVotes + cluster2.totalVotes;
	newCluster.totalBooths = cluster1.totalBooths + cluster2.totalBooths;
	std::unordered_set<int> allElections;
	for (auto const& [electionKey, election] : cluster1.elections) {
		allElections.insert(electionKey);
	}
	for (auto const& [electionKey, election] : cluster2.elections) {
		allElections.insert(electionKey);
	}
	for (auto electionKey : allElections) {
		int cluster1SwingVotes = 0;
		int cluster2SwingVotes = 0;
		if (cluster1.elections.count(electionKey)) cluster1SwingVotes = cluster1.elections.at(electionKey).swingVotes.value_or(0);
		if (cluster2.elections.count(electionKey)) cluster2SwingVotes = cluster2.elections.at(electionKey).swingVotes.value_or(0);
		if (!cluster1SwingVotes && !cluster2SwingVotes) continue;
		float cluster1Proportion = float(cluster1SwingVotes) / float(cluster1SwingVotes + cluster2SwingVotes);
		auto& election = newCluster.elections[electionKey];
		election.swingVotes = cluster1SwingVotes + cluster2SwingVotes;
		election.alpSwing = 0.0f;
		if (cluster1SwingVotes) {
			election.alpSwing.value() += cluster1.elections.at(electionKey).alpSwing.value() * cluster1Proportion;
		}
		if (cluster2SwingVotes) {
			election.alpSwing.value() += cluster2.elections.at(electionKey).alpSwing.value() * (1.0f - cluster1Proportion);
		}
	}

	return newCluster;
}

std::string ClusterAnalyser::clusterName(ClusterAnalyser::Output::Cluster const& cluster, ClusterAnalyser::Output const& data) {
	std::string clusterName = "";
	auto const& correspondingFirstBooth = data.booths.at(cluster.firstBoothId);
	clusterName = correspondingFirstBooth.boothName + " (" + correspondingFirstBooth.seatName + ")";
	if (cluster.secondBoothId >= 0) {
		auto const& correspondingSecondBooth = data.booths.at(cluster.secondBoothId);
		clusterName += " + " + correspondingSecondBooth.boothName + " (" + correspondingSecondBooth.seatName + ")";
	}
	if (cluster.totalBooths > 2) {
		clusterName += " + " + std::to_string(cluster.totalBooths - 2) + " others";
	}
	return clusterName;
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
			auto& outputBooth = output.booths[booth.id];
			outputBooth.boothName = booth.name;
			auto newVal = outputBooth.elections.insert({ election.id, {} });
			int alpVotes = findVote2cp(election, booth, alpCode);
			int lpVotes = findVote2cp(election, booth, lpCode) + findVote2cp(election, booth, lnpCode)
				+ findVote2cp(election, booth, natCode);
			if (alpVotes && lpVotes) {
				float alpProportion = (alpVotes && lpVotes ? float(alpVotes) / float(alpVotes + lpVotes) : 0.0f);
				newVal.first->second.alp2cp = alpProportion;
				newVal.first->second.swingVotes = alpVotes + lpVotes;
				outputBooth.maxBoothVotes = std::max(outputBooth.maxBoothVotes, alpVotes + lpVotes);
			}
		}
		for (auto const& [seatKey, seat] : election.seats) {
			for (auto const& boothId : seat.booths) {
				output.booths[boothId].seatName = seat.name;
			}
		}
	}
	for (auto& [key, booth] : output.booths) {
		booth.elections.begin()->second.swingVotes.reset();
		for (auto firstElection = booth.elections.begin(); firstElection != booth.elections.end(); ++firstElection) {
			auto secondElection = std::next(firstElection);
			if (secondElection == booth.elections.end()) break;

			// get the number of votes recorded so it can be used if there is indeed a valid swing here
			int swingVotes = secondElection->second.swingVotes.value_or(0);
			secondElection->second.swingVotes.reset();

			auto generalFirstElection = std::find_if(elections.begin(), elections.end(),
				[&](decltype(*elections.begin()) generalElection) {return generalElection.first == firstElection->first; });
			if (generalFirstElection == elections.end()) continue;
			auto generalSecondElection = std::next(generalFirstElection);
			if (generalSecondElection == elections.end()) continue;
			if (generalSecondElection->first != secondElection->first) continue;
			if (firstElection->second.alp2cp && secondElection->second.alp2cp) {
				secondElection->second.alpSwing = secondElection->second.alp2cp.value() - firstElection->second.alp2cp.value();
				secondElection->second.swingVotes = swingVotes;
			}
		}
	}

	auto mentoneBooth = output.booths.find(4150);

	auto isaacsSeat = std::prev(elections.end())->second.seats.find(219);
	for (auto boothId : isaacsSeat->second.booths) {
		auto& relatedBooth = output.booths.find(boothId)->second;
		logger << output.booths.find(boothId)->second.boothName << "\n";
		Output::Cluster thisCluster;
		thisCluster.firstBoothId = boothId;
		thisCluster.firstBoothVotes = relatedBooth.maxBoothVotes;
		thisCluster.totalVotes = relatedBooth.maxBoothVotes;
		thisCluster.totalBooths = 1;
		thisCluster.elections = relatedBooth.elections;
		output.clusters.push_back(thisCluster);
	}

	std::priority_queue<Pairing> orderedPairings;
	for (int firstClusterId = 0; firstClusterId < int(output.clusters.size()) - 1; ++firstClusterId) {
		for (auto secondClusterId = firstClusterId + 1; secondClusterId < int(output.clusters.size()); ++secondClusterId) {
			Pairing pairing;
			pairing.clusters = { firstClusterId, secondClusterId };
			pairing.similarity = getSimilarityScore(output.clusters[firstClusterId], output.clusters[secondClusterId]);
			orderedPairings.push(pairing);
		}
	}

	std::unordered_set<int> usedClusters;


	do {
		auto topPairing = orderedPairings.top();
		orderedPairings.pop();
		if (usedClusters.count(topPairing.clusters.first) || usedClusters.count(topPairing.clusters.second)) continue;
		Output::Cluster mergedCluster = mergeClusters(output.clusters[topPairing.clusters.first], output.clusters[topPairing.clusters.second]);
		mergedCluster.children = { topPairing.clusters.first , topPairing.clusters.second };
		output.clusters.push_back(mergedCluster);
		output.clusters[topPairing.clusters.first].parent = int(output.clusters.size()) - 1;
		output.clusters[topPairing.clusters.second].parent = int(output.clusters.size()) - 1;
		usedClusters.insert(topPairing.clusters.first);
		usedClusters.insert(topPairing.clusters.second);
		// generate new pairings between this new cluster and existing ones
		// loop deliberately avoids the last cluster to avoid self-pairing
		for (int otherClusterId = 0; otherClusterId < int(output.clusters.size()) - 1; ++otherClusterId) {
			if (usedClusters.count(otherClusterId)) continue;
			Pairing pairing;
			pairing.clusters = { int(output.clusters.size()) - 1, otherClusterId };
			pairing.similarity = getSimilarityScore(output.clusters.back(), output.clusters[otherClusterId]);
			orderedPairings.push(pairing);
		}
		logger << clusterName(output.clusters[topPairing.clusters.first], output);
		logger << ", ";
		logger << clusterName(output.clusters[topPairing.clusters.second], output);
		logger << ": " << topPairing.similarity << "\n";
	} while (orderedPairings.size());
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
	output << "Cluster analysis results:\n";
	for (auto const& cluster : data.clusters) {
		output << clusterName(cluster, data);
		output << ": ";
		output << "Total votes: " << cluster.totalVotes << ", ";
		bool firstElement = true;
		for (auto electionIt : cluster.elections) {
			output << (firstElement ? "" : ", ") << getFirstWord(data.electionNames.at(electionIt.first));
			if (electionIt.second.alpSwing) {
				output << ": ALP swing ";
				output << (electionIt.second.alpSwing.value() >= 0 ? "+" : "");
				output << electionIt.second.alpSwing.value() * 100.0f << "%";
				output << " from " << electionIt.second.swingVotes.value() << " votes";
			}
			firstElement = false;
		}
		output << "\n";
	}
	return output.str();
}
