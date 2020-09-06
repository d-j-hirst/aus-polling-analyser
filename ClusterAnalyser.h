#pragma once

#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

class ElectionCollection;

class ClusterAnalyser {
public:
	struct Output {
		struct SingleElection {
			std::optional<float> alp2cp;
			std::optional<float> alpSwing;
			std::optional<int> alpVotes;
			std::optional<int> lpVotes;
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
			std::map<int, ClusterAnalyser::Output::SingleElection> elections;
		};

		std::unordered_map<int, BoothSwingData> booths;
		std::vector<Cluster> clusters;
		std::unordered_map<int, std::string> electionNames;
	};

	ClusterAnalyser(ElectionCollection const& elections);

	Output run();

	static std::string getTextOutput(Output const& data);
private:

	ClusterAnalyser::Output::Cluster mergeClusters(ClusterAnalyser::Output::Cluster const& cluster1, ClusterAnalyser::Output::Cluster const& cluster2);

	ElectionCollection const& elections;
};