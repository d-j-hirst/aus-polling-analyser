#include "ReportUploader.h"

#include "PollingProject.h"

#include <fstream>

#include "json.h"

using json = nlohmann::json;

ReportUploader::ReportUploader(Simulation::SavedReport const& thisReport, Simulation const& simulation, PollingProject const& project)
	: project(project), simulation(simulation), thisReport(thisReport)
{
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StanModel::ModelledPoll, pollster, day, base, adjusted, reported)

std::string ReportUploader::upload()
{
	json j;
	// *** This term code retrieval from the model should be a temporary hack,
	// replace with a global project setting
	// once creating the UI for that can be justified
	j["termCode"] = project.models().view(0).getTermCode();
	j["electionName"] = project.getElectionName();
	j["reportLabel"] = thisReport.label;
	j["reportDate"] = thisReport.dateSaved.ToUTC().FormatISOCombined();
	std::string modeString;
	bool isLiveManual = simulation.getSettings().reportMode == Simulation::Settings::ReportMode::LiveForecast &&
		simulation.getSettings().live == Simulation::Settings::Mode::LiveManual;
	switch (simulation.getSettings().reportMode) {
	case Simulation::Settings::ReportMode::RegularForecast: modeString = "RF"; break;
	case Simulation::Settings::ReportMode::LiveForecast: modeString = "LF"; break;
	case Simulation::Settings::ReportMode::Nowcast: modeString = "NC"; break;
	}
	j["reportMode"] = modeString;
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
	const std::vector<float> thresholds = {0.1f, 0.5f, 1.0f, 2.5f, 5.0f, 10.0f, 25.0f, 50.0f, 75.0f, 90.0f, 95.0f, 97.5f, 99.0f, 99.5f, 99.9f};
	j["voteTotalThresholds"] = thresholds;
	typedef std::vector<float> VF;
	if (!isLiveManual) {
		std::map<int, VF> fpFrequencies;
		for (auto [partyIndex, frequencies] : thisReport.report.partyPrimaryFrequency) {
			if (thisReport.report.getFpSampleExpectation(partyIndex) > 0.0f) {
				VF partyThresholds = std::accumulate(thresholds.begin(), thresholds.end(), VF(),
					[this, partyIndex](VF v, float percentile) {
						v.push_back(thisReport.report.getFpSamplePercentile(partyIndex, percentile));
						return v;
					});
				fpFrequencies[partyIndex] = partyThresholds;
			}
		}
		j["fpFrequencies"] = fpFrequencies;
	}
	std::vector<float> tppFrequencies = std::accumulate(thresholds.begin(), thresholds.end(), VF(),
		[this](VF v, float percentile) {
			v.push_back(thisReport.report.getTppSamplePercentile(percentile));
			return v;
		});
	j["tppFrequencies"] = tppFrequencies;
	typedef std::vector<int> VI;
	std::map<int, VI> seatFrequencies;
	for (auto [partyIndex, frequencies] : thisReport.report.partySeatWinFrequency) {
		VI partyThresholds = std::accumulate(thresholds.begin(), thresholds.end(), VI(),
			[this, partyIndex](VI v, float percentile) {
				v.push_back(thisReport.report.getPartySeatsPercentile(partyIndex, percentile));
				return v;
			});
		seatFrequencies[partyIndex] = partyThresholds;
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
	j["seatPartyWinFrequencies"] = thisReport.report.seatPartyWinPercent;
	if (!isLiveManual) {
		j["seatTcpScenarios"] = thisReport.report.seatTcpScenarioPercent;
		j["seatFpBands"] = thisReport.report.seatFpProbabilityBand;
		j["seatTcpBands"] = thisReport.report.seatTcpProbabilityBand;
		j["polls"] = thisReport.report.modelledPolls;
	}
	std::ofstream file2("uploads/latest_json.dat");
	file2 << std::setw(4) << j;
	return "ok";
}
