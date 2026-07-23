#include "CoreReportSummary.h"

#include "Simulation.h"
#include "SpecialPartyCodes.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace {
	using Mp = Simulation::MajorParty;

	std::string partyLabel(
		Simulation::Report const& report,
		int partyIndex)
	{
		auto const name = report.partyName.find(partyIndex);
		if (name != report.partyName.end() && !name->second.empty()) {
			return name->second;
		}
		auto const abbreviation = report.partyAbbr.find(partyIndex);
		if (abbreviation != report.partyAbbr.end() &&
			!abbreviation->second.empty()) {
			return abbreviation->second;
		}
		return "Party " + std::to_string(partyIndex);
	}

	std::string partyAbbreviation(
		Simulation::Report const& report,
		int partyIndex)
	{
		auto const abbreviation = report.partyAbbr.find(partyIndex);
		if (abbreviation != report.partyAbbr.end() &&
			!abbreviation->second.empty()) {
			return abbreviation->second;
		}
		return partyLabel(report, partyIndex);
	}

	void addProbabilityRow(
		std::ostringstream& output,
		std::string const& label,
		float value)
	{
		output << "  " << std::left << std::setw(28) << label <<
			std::right << std::fixed << std::setprecision(2) <<
			value << "%\n";
	}

	void addMeanMedianRow(
		std::ostringstream& output,
		std::string const& label,
		float mean,
		float median,
		bool percentages,
		int medianPrecision)
	{
		output << "  " << std::left << std::setw(28) << label <<
			std::right << std::fixed << std::setprecision(2) << mean;
		if (percentages) output << '%';
		output << " / " << std::fixed << std::setprecision(medianPrecision) <<
			median;
		if (percentages) output << '%';
		output << '\n';
	}

	void addUnavailableRow(
		std::ostringstream& output,
		std::string const& label)
	{
		output << "  " << std::left << std::setw(28) << label <<
			std::right << "n/a / n/a\n";
	}
}

std::string formatCoreReportSummary(
	std::string_view specificationId,
	Simulation const& simulation)
{
	return formatCoreReportSummary(
		specificationId,
		simulation.getSettings().name,
		simulation.getLatestReport());
}

std::string formatCoreReportSummary(
	std::string_view specificationId,
	std::string_view simulationName,
	Simulation::Report const& report)
{
	std::ostringstream output;
	output << "\nSimulation summary: " << simulationName;
	if (!specificationId.empty()) output << " [" << specificationId << ']';
	output << "\n\nOutcome probabilities\n";

	auto const partyOne = partyAbbreviation(report, Mp::One);
	auto const partyTwo = partyAbbreviation(report, Mp::Two);
	addProbabilityRow(output, partyOne + " majority",
		report.getPartyMajorityPercent(Mp::One));
	addProbabilityRow(output, partyOne + " minority",
		report.getPartyMinorityPercent(Mp::One));
	addProbabilityRow(output, "Hung",
		report.getHungPercent());
	addProbabilityRow(output, partyTwo + " minority",
		report.getPartyMinorityPercent(Mp::Two));
	addProbabilityRow(output, partyTwo + " majority",
		report.getPartyMajorityPercent(Mp::Two));

	output << "\nParty seats (mean / median)\n";
	bool addedSeatRow = false;
	auto addSeatRows = [&](bool negativeIndices) {
		for (auto const& [partyIndex, expectation] :
			report.partyWinExpectation) {
			if ((partyIndex < 0) != negativeIndices) continue;
			float mean = expectation;
			float median = report.getPartyWinMedian(partyIndex);
			auto label = partyLabel(report, partyIndex);
			if (partyIndex == Mp::Two &&
				!report.coalitionSeatWinFrequency.empty()) {
				mean = report.getCoalitionWinExpectation();
				median = report.getCoalitionWinMedian();
				label = "Coalition";
			}
			addMeanMedianRow(output, label, mean, median, false, 0);
			addedSeatRow = true;
		}
	};
	addSeatRows(false);
	addSeatRows(true);
	if (!addedSeatRow) output << "  n/a\n";

	output << "\nFirst preferences (mean / median)\n";
	int totalFpSamples = 0;
	for (auto const& [partyIndex, frequency] :
		report.partyPrimaryFrequency) {
		static_cast<void>(frequency);
		totalFpSamples = std::max(
			totalFpSamples, report.getFpSampleCount(partyIndex));
		if (partyIndex == OthersIndex) continue;
		if (partyIndex == Mp::Two) {
			bool const hasCoalitionSamples =
				report.getCoalitionFpSampleCount() > 0;
			int const sampleCount = hasCoalitionSamples ?
				report.getCoalitionFpSampleCount() :
				report.getFpSampleCount(Mp::Two);
			if (!sampleCount) {
				addUnavailableRow(output, "Coalition");
				continue;
			}
			float const mean = hasCoalitionSamples ?
				report.getCoalitionFpSampleExpectation() :
				report.getFpSampleExpectation(Mp::Two);
			float const median = hasCoalitionSamples ?
				report.getCoalitionFpSampleMedian() :
				report.getFpSampleMedian(Mp::Two);
			addMeanMedianRow(
				output, "Coalition", mean, median, true, 2);
			continue;
		}

		int const sampleCount = report.getFpSampleCount(partyIndex);
		float const mean = report.getFpSampleExpectation(partyIndex);
		if (!sampleCount || mean <= 0.0f || !std::isfinite(mean)) {
			addUnavailableRow(output, partyLabel(report, partyIndex));
			continue;
		}
		addMeanMedianRow(output, partyLabel(report, partyIndex),
			mean, report.getFpSampleMedian(partyIndex), true, 2);
	}

	if (!totalFpSamples) {
		addUnavailableRow(output, "Others");
	}
	else if (report.getFpSampleCount(OthersIndex)) {
		addMeanMedianRow(output, "Others",
			report.getFpSampleExpectation(OthersIndex),
			report.getFpSampleMedian(OthersIndex), true, 2);
	}
	else {
		float totalMean = 0.0f;
		for (auto const& [partyIndex, frequency] :
			report.partyPrimaryFrequency) {
			static_cast<void>(frequency);
			totalMean += report.getFpSampleExpectation(partyIndex);
		}
		output << "  " << std::left << std::setw(28) << "Others" <<
			std::right << std::fixed << std::setprecision(2) <<
			std::max(0.0f, 100.0f - totalMean) << "% / n/a\n";
	}

	output << "\nTwo-party preferred (mean / median)\n";
	if (report.getTppSampleCount()) {
		auto const mean = report.getTppSampleExpectation();
		auto const median = report.getTppSampleMedian();
		addMeanMedianRow(
			output, partyOne, mean, median, true, 2);
		addMeanMedianRow(
			output, partyTwo, 100.0f - mean, 100.0f - median, true, 2);
	}
	else {
		addUnavailableRow(output, partyOne);
		addUnavailableRow(output, partyTwo);
	}
	return output.str();
}
