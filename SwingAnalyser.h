#pragma once

#include <map>
#include <optional>
#include <unordered_map>

class ElectionCollection;

class SwingAnalyser {
public:
	struct Output {
		struct BoothSwingData {
			struct SingleElection {
				std::optional<float> alp2cp;
				std::optional<float> alpSwing;
			};
			std::string boothName;
			std::string seatName;
			std::map<int, SingleElection> elections;
		};

		std::unordered_map<int, BoothSwingData> booths;
		std::unordered_map<int, std::string> electionNames;
	};

	SwingAnalyser(ElectionCollection const& elections);

	Output run();

	static std::string getTextOutput(Output const& data);
private:
	ElectionCollection const& elections;
};