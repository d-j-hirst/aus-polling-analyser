#pragma once

class ElectionCollection;

class ElectionAnalyser {
public:
	enum class Type {
		Parties
	};

	ElectionAnalyser(ElectionCollection const& elections);

	void run(Type type, int electionFocus);
private:
	ElectionCollection const& elections;
};