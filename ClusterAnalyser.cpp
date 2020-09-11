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
	std::pair<unsigned short, unsigned short> clusters;
};

bool operator<(Pairing const& lhs, Pairing const& rhs) {
	return lhs.similarity < rhs.similarity;
}

std::priority_queue<Pairing, std::deque<Pairing>> orderedPairings;

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

inline int32_t findVoteFp(Results2::Election const& election, Results2::Booth const& booth, int32_t partyId) {
	int votes = 0;
	for (auto [candidate, thisVotes] : booth.votesFp) {
		auto candidateData = election.candidates.at(candidate);
		auto partyData = election.parties.at(candidateData.party);
		if (partyData.id == partyId) {
			votes = thisVotes;
		}
	}
	return votes;
}

inline float getSimilarityScore(ClusterAnalyser::Output::Cluster const& cluster1, ClusterAnalyser::Output::Cluster const& cluster2) {
	float totalSignificance = 0.0f;
	float electionCount = 0.0f;
	for (auto const& cluster1e : cluster1.elections) {
		auto cluster2e = cluster2.elections.find(cluster1e.first);
		if (cluster2e == cluster2.elections.end()) continue;
		if (!(cluster1e.second.alp2cp && cluster2e->second.alp2cp)) continue;
		totalSignificance += std::sqrt(float(cluster1e.second.votes2cp.value()) * float(cluster2e->second.votes2cp.value()));
		electionCount++;
	}
	totalSignificance /= electionCount;
	float squaredErrorSum = 0.0f;
	float dataDensityCount = 0.0f;
	for (auto const& cluster1e : cluster1.elections) {
		auto cluster2e = cluster2.elections.find(cluster1e.first);
		if (cluster2e == cluster2.elections.end()) continue;
		if (!(cluster1e.second.alp2cp && cluster2e->second.alp2cp)) continue;
		float significanceFactor = std::sqrt(float(cluster1e.second.votes2cp.value()) * float(cluster2e->second.votes2cp.value())) / totalSignificance;
		squaredErrorSum += std::pow((cluster1e.second.alp2cp.value() - cluster2e->second.alp2cp.value()) * 5.0f, 2) * significanceFactor;
		dataDensityCount += 0.25f * significanceFactor;
		if (cluster1e.second.greenFp && cluster2e->second.greenFp) {
			float ratioEffect = std::abs(cluster1e.second.greenFp.value() / cluster2e->second.greenFp.value() - 1.0f);
			squaredErrorSum += std::pow(ratioEffect * 0.5f, 2) * significanceFactor;
			dataDensityCount += 0.25f * significanceFactor;
		}
		if (cluster1e.second.othersFp && cluster2e->second.othersFp) {
			float ratioEffect = std::abs(cluster1e.second.othersFp.value() / cluster2e->second.othersFp.value() - 1.0f);
			ratioEffect = std::clamp(ratioEffect, 0.0f, 1.5f); // don't want this to skew outcomes too much
			squaredErrorSum += std::pow(ratioEffect * 0.3f, 2) * significanceFactor;
			dataDensityCount += 0.25f * significanceFactor;
		}
		if (!(cluster1e.second.alpSwing && cluster2e->second.alpSwing)) continue;
		squaredErrorSum += std::pow((cluster1e.second.alpSwing.value() - cluster2e->second.alpSwing.value()) * 0.6f, 2) * significanceFactor;
		dataDensityCount += 0.75f * significanceFactor;
	}
	constexpr float SubtractMatchedElection = 0.9f;
	if (dataDensityCount <= SubtractMatchedElection) return 0.0f;
	constexpr float DummyBoothError = 0.002f; // in order to prevent booth comparisons with few election comparisons from dominating,
										   // add a moderate-sized dummy election to help indicate which booth comparisons are most resilient to future errors
	float similarityScore = (std::pow(dataDensityCount, 1.2f) - SubtractMatchedElection) / std::sqrt(squaredErrorSum + DummyBoothError);
	// Some bias so that we try to put similarly sized clusters together. Avoid 
	float adjustedBoothRatio = float(std::min(cluster1.totalBooths, 30) + 15) / float(std::min(cluster2.totalBooths, 30) + 15);
	similarityScore /= std::sqrt(std::max(adjustedBoothRatio, 1.0f / adjustedBoothRatio));
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
		auto& election = newCluster.elections[electionKey];
		if (!cluster1.elections.count(electionKey) && !cluster2.elections.count(electionKey)) continue;
		if (!cluster1.elections.count(electionKey)) {
			election = cluster2.elections.at(electionKey);
			continue;
		}
		if (!cluster2.elections.count(electionKey)) {
			election = cluster1.elections.at(electionKey);
			continue;
		}
		auto const& election1 = cluster1.elections.at(electionKey);
		auto const& election2 = cluster2.elections.at(electionKey);

		int cluster1Votes2cp = election1.votes2cp.value_or(0);
		int cluster2Votes2cp = election2.votes2cp.value_or(0);
		if (!cluster1Votes2cp && !cluster2Votes2cp) continue;
		float cluster1Proportion2cp = float(cluster1Votes2cp) / float(cluster1Votes2cp + cluster2Votes2cp);
		election.votes2cp = cluster1Votes2cp + cluster2Votes2cp;
		election.alp2cp = election1.alp2cp.value_or(50.0f) * cluster1Proportion2cp +
			election2.alp2cp.value_or(50.0f) * (1.0f - cluster1Proportion2cp);

		float cluster1ProportionGreen = cluster1Proportion2cp;
		if (!election1.greenFp) cluster1ProportionGreen = 0.0f;
		else if (!election2.greenFp) cluster1ProportionGreen = 1.0f;
		if (election1.greenFp || election2.greenFp) {
			election.greenFp = election1.greenFp.value_or(0.0f) * cluster1Proportion2cp +
				election2.greenFp.value_or(0.0f) * (1.0f - cluster1Proportion2cp);
		}

		int cluster1SwingVotes = election1.swingVotes.value_or(0);
		int cluster2SwingVotes = election2.swingVotes.value_or(0);
		if (!cluster1SwingVotes && !cluster2SwingVotes) continue;
		float cluster1ProportionSwing = float(cluster1SwingVotes) / float(cluster1SwingVotes + cluster2SwingVotes);
		election.swingVotes = cluster1SwingVotes + cluster2SwingVotes;
		election.alpSwing = election1.alpSwing.value_or(0.0f) * cluster1ProportionSwing +
			election2.alpSwing.value_or(0.0f) * (1.0f - cluster1ProportionSwing);
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
	int totalVotes = std::accumulate(cluster.elections.begin(), cluster.elections.end(), 0,
		[](int a, auto b) {return b.second.votes2cp.value_or(0) + a; });
	clusterName += " (" + std::to_string(totalVotes) + ")";
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
		auto clpIt = std::find_if(election.parties.begin(), election.parties.end(),
			[](decltype(election.parties)::value_type party) {return party.second.shortCode == "CLP"; });
		int clpCode = (clpIt != election.parties.end() ? clpIt->second.id : -2);
		auto lnpIt = std::find_if(election.parties.begin(), election.parties.end(),
			[](decltype(election.parties)::value_type party) {return party.second.shortCode == "LNP" || party.second.shortCode == "LNQ"; });
		int lnpCode = (lnpIt != election.parties.end() ? lnpIt->second.id : -2);
		auto natIt = std::find_if(election.parties.begin(), election.parties.end(),
			[](decltype(election.parties)::value_type party) {return party.second.shortCode == "NP"; });
		int natCode = (natIt != election.parties.end() ? natIt->second.id : -2);
		auto grnIt = std::find_if(election.parties.begin(), election.parties.end(),
			[](decltype(election.parties)::value_type party) {return party.second.shortCode == "GRN"; });
		int grnCode = (grnIt != election.parties.end() ? grnIt->second.id : -2);
		auto grnVicIt = std::find_if(election.parties.begin(), election.parties.end(),
			[](decltype(election.parties)::value_type party) {return party.second.shortCode == "GVIC"; });
		int grnVicCode = (grnVicIt != election.parties.end() ? grnVicIt->second.id : -2);

		output.electionNames[electionKey] = election.name;
		for (auto const& [boothKey, booth] : election.booths) {
			auto& outputBooth = output.booths[booth.id];
			outputBooth.boothName = booth.name;
			auto& newVal = outputBooth.elections.insert({ election.id, {} }).first->second;
			int alpVotes = findVote2cp(election, booth, alpCode);
			int lpVotes = findVote2cp(election, booth, lpCode) + findVote2cp(election, booth, lnpCode)
				+ findVote2cp(election, booth, natCode) + findVote2cp(election, booth, clpCode);
			int grnFpVotes = findVoteFp(election, booth, grnCode) + findVoteFp(election, booth, grnVicCode);
			int majorFpVotes = findVoteFp(election, booth, alpCode) + findVoteFp(election, booth, lpCode)
				+ findVoteFp(election, booth, lnpCode) + findVoteFp(election, booth, natCode)
				+ findVoteFp(election, booth, clpCode) + findVoteFp(election, booth, grnCode)
				+ findVoteFp(election, booth, grnVicCode);
			int totalVotes = alpVotes + lpVotes;
			int othFpVotes = totalVotes - majorFpVotes;
			if (alpVotes && lpVotes) {
				float alpProportion = (alpVotes && lpVotes ? float(alpVotes) / float(alpVotes + lpVotes) : 0.0f);
				newVal.alp2cp = alpProportion;
				newVal.votes2cp = alpVotes + lpVotes;
				if (grnFpVotes && alpVotes && lpVotes) newVal.greenFp = float(grnFpVotes) / float(alpVotes + lpVotes);
				if (othFpVotes && alpVotes && lpVotes) newVal.othersFp = float(othFpVotes) / float(alpVotes + lpVotes);
				outputBooth.maxBoothVotes = std::max(outputBooth.maxBoothVotes, alpVotes + lpVotes);
			}
		}
		for (auto const& [seatKey, seat] : election.seats) {
			for (auto const& boothId : seat.booths) {
				output.booths[boothId].seatName = seat.name;
			}
		}
	}
	std::unordered_set<int> boothsToErase;
	for (auto& [key, booth] : output.booths) {
		int swingElections = 0;
		booth.elections.begin()->second.swingVotes.reset();
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
				secondElection->second.swingVotes = secondElection->second.votes2cp;
				swingElections++;
			}
		}
		if (swingElections <= 7) boothsToErase.insert(key);
	}
	for (auto boothKey : boothsToErase) {
		output.booths.erase(boothKey);
	}

	std::vector<int> seats;
	for (int seat = 101; seat <= 400; ++seat) seats.push_back(seat);
	std::unordered_set<int> boothsToUse;
	for (auto seat : seats) {
		if (!std::prev(elections.end())->second.seats.count(seat)) continue;
		for (auto thisBooth : std::prev(elections.end())->second.seats.at(seat).booths) {
			boothsToUse.insert(thisBooth);
		}
	}
	for (auto boothId : boothsToUse) {
		auto boothIt = output.booths.find(boothId);
		if (boothIt == output.booths.end()) continue;
		auto& relatedBooth = output.booths.find(boothId)->second;
		Output::Cluster thisCluster;
		thisCluster.firstBoothId = boothId;
		thisCluster.firstBoothVotes = relatedBooth.maxBoothVotes;
		thisCluster.totalVotes = relatedBooth.maxBoothVotes;
		thisCluster.totalBooths = 1;
		thisCluster.elections = relatedBooth.elections;
		output.clusters.push_back(thisCluster);
	}

	for (int firstClusterId = 0; firstClusterId < int(output.clusters.size()) - 1; ++firstClusterId) {
		for (auto secondClusterId = firstClusterId + 1; secondClusterId < int(output.clusters.size()); ++secondClusterId) {
			Pairing pairing;
			pairing.clusters = { unsigned short(firstClusterId), unsigned short(secondClusterId) };
			pairing.similarity = getSimilarityScore(output.clusters[firstClusterId], output.clusters[secondClusterId]);
			orderedPairings.push(pairing);
		}
	}

	std::unordered_set<int> usedClusters;

	do {
		auto topPairing = orderedPairings.top();
		orderedPairings.pop();
		if (usedClusters.count(topPairing.clusters.first) || usedClusters.count(topPairing.clusters.second)) continue;
		logger << orderedPairings.size() << " pairing, merging ";
		logger << clusterName(output.clusters[topPairing.clusters.first], output);
		logger << " and " << clusterName(output.clusters[topPairing.clusters.second], output);
		logger << " with similarity " << topPairing.similarity;
		if (output.clusters[topPairing.clusters.first].totalBooths > 20 && output.clusters[topPairing.clusters.second].totalBooths > 20) {
			logger << " *** Large Cluster Pairing ***";
		}
		logger << "\n";
		Output::Cluster mergedCluster = mergeClusters(output.clusters[topPairing.clusters.first], output.clusters[topPairing.clusters.second]);
		mergedCluster.children = { topPairing.clusters.first , topPairing.clusters.second };
		mergedCluster.similarity = topPairing.similarity;
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
			pairing.clusters = { unsigned short(output.clusters.size() - 1), unsigned short(otherClusterId) };
			pairing.similarity = getSimilarityScore(output.clusters.back(), output.clusters[otherClusterId]);
			orderedPairings.push(pairing);
		}
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
