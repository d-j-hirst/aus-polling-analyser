#pragma once

#include <unordered_map>

class ElectionCollection;

class PartiesAnalyser {
public:
	struct Output {
		struct PartyInfo {
			std::string name;
			std::string shortCode;
			int candidateCount;
		};

		std::unordered_map<int, PartyInfo> parties;
	};

	PartiesAnalyser(ElectionCollection const& elections);

	Output run(int electionFocus);

	static std::string getTextOutput(Output const& data);
private:
	ElectionCollection const& elections;
};