#pragma once

#include "OrthogonalRegression.h"

#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

class ElectionCollection;

class ClusterAnalyser {
public:
	struct Output {
		struct SingleElection {
			std::optional<float> alp2cp; // only for booths
			std::optional<float> alpSwing;
			std::optional<int> swingVotes; // only for clusters
			std::optional<int> votes2cp;
			std::optional<float> greenFp;
			std::optional<float> othersFp;
		};

		struct BoothSwingData {
			std::string boothName;
			std::string seatName;
			int maxBoothVotes;
			float similarityScore;
			std::map<int, ClusterAnalyser::Output::SingleElection> elections;
		};

		struct Cluster {
			int firstBoothId;
			int firstBoothVotes;
			int secondBoothId = -1;
			int secondBoothVotes = 0;
			int totalVotes = 0;
			int totalBooths = 0;
			std::pair<int, int> children = { -1, -1 };
			int parent = -1;
			float similarity = 0.0f;
			OrthogonalRegression regression;
			std::map<int, ClusterAnalyser::Output::SingleElection> elections;
		};

		std::unordered_map<int, BoothSwingData> booths;
		std::vector<Cluster> clusters;
		std::map<int, std::string> electionNames;
	};

	ClusterAnalyser(ElectionCollection const& elections);

	Output run();

	static std::string getTextOutput(Output const& data);

	static std::string clusterName(Output::Cluster const& cluster, Output const& data);
private:

	ClusterAnalyser::Output::Cluster mergeClusters(Output::Cluster const& cluster1, Output::Cluster const& cluster2);

	ElectionCollection const& elections;
};