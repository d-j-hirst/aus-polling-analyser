#pragma once

#include <unordered_map>

class ElectionCollection;

class SwingAnalyser {
public:
	struct Output {
		struct BoothSwingData {
			struct SingleElection {
				float alp2cp;
			};
			std::string boothName;
			std::string seatName;
			std::unordered_map<int, SingleElection> elections;
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