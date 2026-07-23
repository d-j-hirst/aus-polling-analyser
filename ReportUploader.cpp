#include "ReportUploader.h"

#include "Log.h"
#include "PollingProject.h"

#include <array>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "json.h"

using json = nlohmann::json;

namespace {
	constexpr char OutputFilename[] = "uploads/latest_json.dat";
	constexpr std::array<float, 15> PercentileThresholds = {
		0.1f, 0.5f, 1.0f, 2.5f, 5.0f, 10.0f, 25.0f, 50.0f,
		75.0f, 90.0f, 95.0f, 97.5f, 99.0f, 99.5f, 99.9f
	};

	template <typename Value, typename Getter>
	std::vector<Value> percentileValues(Getter getter)
	{
		std::vector<Value> values;
		values.reserve(PercentileThresholds.size());
		for (float const percentile : PercentileThresholds) {
			values.push_back(getter(percentile));
		}
		return values;
	}

	std::optional<std::string> exportFailure(std::string const& detail)
	{
		std::string const message = "Could not export report: " + detail;
		logger << message << "\n";
		return message;
	}

	std::optional<std::string> reportModeCode(
		Simulation::Settings::ReportMode reportMode)
	{
		switch (reportMode) {
		case Simulation::Settings::ReportMode::RegularForecast: return "RF";
		case Simulation::Settings::ReportMode::LiveForecast: return "LF";
		case Simulation::Settings::ReportMode::Nowcast: return "NC";
		}
		return std::nullopt;
	}
}

ReportUploader::ReportUploader(Simulation::SavedReport const& thisReport, Simulation const& simulation, PollingProject const& project)
	: project(project), simulation(simulation), thisReport(thisReport)
{
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StanModel::ModelledPoll, pollster, day, base, adjusted, reported)

std::optional<std::string> ReportUploader::exportReport()
{
	auto const& settings = simulation.getSettings();
	if (project.projections().idToIndex(settings.baseProjection) ==
		ProjectionCollection::InvalidIndex) {
		return exportFailure("the simulation does not have a valid base projection.");
	}
	auto const& projection = project.projections().view(settings.baseProjection);
	if (project.models().idToIndex(projection.getSettings().baseModel) ==
		ModelCollection::InvalidIndex) {
		return exportFailure("the simulation's base projection does not have a valid model.");
	}
	auto const modeString = reportModeCode(settings.reportMode);
	if (!modeString) {
		return exportFailure("the simulation has an invalid report mode.");
	}
	if (!thisReport.dateSaved.isValid()) {
		return exportFailure("the report does not have a valid saved date.");
	}

	json j;
	// The election identity currently belongs to the model rather than the project.
	auto const& termCode = projection.getBaseModel(project.models()).getTermCode();
	if (termCode.empty()) {
		return exportFailure("the simulation's base model does not have an election term code.");
	}
	j["termCode"] = termCode;
	j["electionName"] = project.getElectionName();
	j["reportLabel"] = thisReport.label;
	j["reportDate"] = thisReport.dateSaved.formatIsoUtc();
	bool const isLiveManual = settings.reportMode == Simulation::Settings::ReportMode::LiveForecast &&
		settings.live == Simulation::Settings::Mode::LiveManual;
	j["reportMode"] = *modeString;
	j["partyName"] = thisReport.report.partyName;
	j["partyAbbr"] = thisReport.report.partyAbbr;
	j["overallWinPc"] = {
		{0, thisReport.report.getPartyOverallWinPercent(Simulation::MajorParty::One)},
		{1, thisReport.report.getPartyOverallWinPercent(Simulation::MajorParty::Two)},
		{-1, thisReport.report.getOthersOverallWinPercent()}
	};
	j["majorityWinPc"] = thisReport.report.majorityPercent;
	j["minorityWinPc"] = thisReport.report.minorityPercent;
	j["mostSeatsWinPc"] = thisReport.report.mostSeatsPercent;
	j["voteTotalThresholds"] = PercentileThresholds;
	if (!isLiveManual) {
		std::map<int, std::vector<float>> fpFrequencies;
		for (auto const& partyFrequency : thisReport.report.partyPrimaryFrequency) {
			int const partyIndex = partyFrequency.first;
			if (thisReport.report.getFpSampleExpectation(partyIndex) > 0.0f) {
				fpFrequencies[partyIndex] = percentileValues<float>(
					[this, partyIndex](float percentile) {
						return thisReport.report.getFpSamplePercentile(
							partyIndex, percentile);
					});
			}
		}
		j["fpFrequencies"] = fpFrequencies;
	}
	j["tppFrequencies"] = percentileValues<float>(
		[this](float percentile) {
			return thisReport.report.getTppSamplePercentile(percentile);
		});
	std::map<int, std::vector<int>> seatFrequencies;
	for (auto const& partyFrequency : thisReport.report.partySeatWinFrequency) {
		int const partyIndex = partyFrequency.first;
		seatFrequencies[partyIndex] = percentileValues<int>(
			[this, partyIndex](float percentile) {
				return thisReport.report.getPartySeatsPercentile(
					partyIndex, percentile);
			});
	}
	j["seatCountFrequencies"] = seatFrequencies;
	if (!isLiveManual) {
		j["trendProbBands"] = thisReport.report.trendProbBands;
		j["trendPeriod"] = thisReport.report.trendPeriod;
		j["finalTrendValue"] = thisReport.report.finalTrendValue;
		j["trendStartDate"] = thisReport.report.trendStartDate;
		j["tppTrend"] = thisReport.report.tppTrend;
		j["fpTrend"] = thisReport.report.fpTrend;
	}
	j["seatNames"] = thisReport.report.seatName;
	j["seatIncumbents"] = thisReport.report.seatIncumbents;
	j["seatMargins"] = thisReport.report.seatIncumbentMargins;
	j["seatHideTcps"] = thisReport.report.seatHideTcps;
	j["seatPartyWinFrequencies"] = thisReport.report.seatPartyWinPercent;
	if (!isLiveManual) {
		j["seatTcpScenarios"] = thisReport.report.seatTcpScenarioPercent;
		j["seatFpBands"] = thisReport.report.seatFpProbabilityBand;
		j["seatTcpBands"] = thisReport.report.seatTcpProbabilityBand;
		j["polls"] = thisReport.report.modelledPolls;
	}
	auto seatCandidateNames = thisReport.report.seatCandidateNames;
	bool reportHasCandidateNames = false;
	for (auto const& seatNames : seatCandidateNames) {
		if (!seatNames.empty()) {
			reportHasCandidateNames = true;
			break;
		}
	}
	// Reports saved before project format 64 did not persist candidate names.
	// Reconstruct those reports from the same election project's seat metadata.
	if (seatCandidateNames.size() != size_t(project.seats().count()) ||
		!reportHasCandidateNames) {
		seatCandidateNames.clear();
		seatCandidateNames.resize(project.seats().count());
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			for (auto const& [candidateName, partyCode] :
				project.seats().viewByIndex(seatIndex).candidateNames) {
				int const partyIndex = project.parties().indexByShortCode(partyCode);
				if (partyIndex >= 0) {
					seatCandidateNames[seatIndex][partyIndex] = candidateName;
				}
			}
		}
	}
	j["seatCandidateNames"] = seatCandidateNames;
	j["seatSwingFactors"] = thisReport.report.swingFactors;
	if (thisReport.report.getCoalitionFpSampleExpectation() > 0.0f) {
		j["coalitionFpFrequencies"] = percentileValues<float>(
			[this](float percentile) {
				return thisReport.report.getCoalitionFpSamplePercentile(percentile);
			});
	}
	if (!thisReport.report.coalitionSeatWinFrequency.empty()) {
		j["coalitionSeatCountFrequencies"] = percentileValues<int>(
			[this](float percentile) {
				return thisReport.report.getCoalitionSeatsPercentile(percentile);
			});
	}

	std::string serialisedReport;
	try {
		// Complete serialization before truncating the previous prepared report.
		serialisedReport = j.dump(4);
	}
	catch (nlohmann::json::exception const& e) {
		return exportFailure(std::string("the report could not be serialized: ") + e.what());
	}

	auto const outputFilename = project.paths().resolveString(OutputFilename);
	std::ofstream output(outputFilename, std::ios::binary | std::ios::trunc);
	if (!output) {
		return exportFailure("could not open " + outputFilename + " for writing.");
	}
	output << serialisedReport;
	output.close();
	if (output.fail()) {
		return exportFailure("could not finish writing " + outputFilename + ".");
	}
	return std::nullopt;
}
