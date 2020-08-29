#pragma once

class ElectionCollection;

class PartiesAnalyser {
public:
	class Output {

	};

	PartiesAnalyser(ElectionCollection const& elections);

	Output run(int electionFocus);
private:
	ElectionCollection const& elections;
};