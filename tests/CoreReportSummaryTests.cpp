#include "../CoreReportSummary.h"
#include "../SpecialPartyCodes.h"

#include <iostream>
#include <string>

namespace {
	bool expectContains(
		std::string const& output,
		std::string const& expected,
		std::string const& description)
	{
		if (output.find(expected) != std::string::npos) return true;
		std::cerr << "FAILED: " << description << "\nMissing: " <<
			expected << "\nOutput:\n" << output;
		return false;
	}
}

int main()
{
	Simulation::Report report;
	report.partyAbbr = {
		{ Simulation::MajorParty::One, "ALP" },
		{ Simulation::MajorParty::Two, "LIB" },
	};
	report.partyName = {
		{ Simulation::MajorParty::One, "Labor" },
		{ Simulation::MajorParty::Two, "Liberals" },
		{ OthersIndex, "Others" },
	};
	report.majorityPercent = {
		{ Simulation::MajorParty::One, 44.5f },
		{ Simulation::MajorParty::Two, 30.0f },
	};
	report.minorityPercent = {
		{ Simulation::MajorParty::One, 10.5f },
		{ Simulation::MajorParty::Two, 5.0f },
	};
	report.mostSeatsPercent = {
		{ Simulation::MajorParty::One, 4.0f },
		{ Simulation::MajorParty::Two, 5.5f },
	};
	report.tiedPercent = 0.5f;

	report.partyWinExpectation = {
		{ Simulation::MajorParty::One, 70.25f },
		{ Simulation::MajorParty::Two, 60.75f },
		{ OthersIndex, 1.25f },
	};
	report.partyWinMedian = {
		{ Simulation::MajorParty::One, 70.0f },
		{ Simulation::MajorParty::Two, 61.0f },
		{ OthersIndex, 1.0f },
	};

	report.partyPrimaryFrequency = {
		{ Simulation::MajorParty::One, {{350, 2}} },
		{ Simulation::MajorParty::Two, {{400, 2}} },
		{ OthersIndex, {{150, 2}} },
	};
	report.tppFrequency = {{520, 2}};

	auto const output = formatCoreReportSummary(
		"now-cast-simulation", "Now-cast Simulation", report);
	bool valid = true;
	valid &= expectContains(
		output,
		"Simulation summary: Now-cast Simulation [now-cast-simulation]",
		"the heading should include both display and specification names");
	valid &= expectContains(
		output, "ALP minority",
		"outcome labels should use party abbreviations");
	valid &= expectContains(
		output, "10.50%",
		"outcome probabilities should use two decimal places");
	valid &= expectContains(
		output, "Hung",
		"the hung outcome should be present");
	valid &= expectContains(
		output, "10.00%",
		"the hung probability should combine most-seats and tied outcomes");
	valid &= expectContains(
		output, "Labor",
		"party seat rows should use full party names");
	valid &= expectContains(
		output, "70.25 / 70",
		"seat rows should contain mean and median");
	valid &= expectContains(
		output, "35.05% / 35.05%",
		"FP rows should contain bucket-based mean and median");
	valid &= expectContains(
		output, "52.05% / 52.05%",
		"TPP rows should contain mean and median");
	valid &= expectContains(
		output, "47.95% / 47.95%",
		"the second major party TPP should be complementary");

	Simulation::Report emptyReport;
	emptyReport.partyAbbr = report.partyAbbr;
	auto const emptyOutput = formatCoreReportSummary(
		"empty", "Empty Simulation", emptyReport);
	valid &= expectContains(
		emptyOutput, "n/a / n/a",
		"missing vote distributions should be reported as unavailable");

	return valid ? 0 : 1;
}
