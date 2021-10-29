#include "Simulation.h"

#include "CountProgress.h"
#include "Log.h"
#include "Model.h"
#include "Party.h"
#include "PollingProject.h"
#include "Projection.h"
#include "Region.h"
#include <algorithm>

#undef min
#undef max

void Simulation::run(PollingProject & project, SimulationRun::FeedbackFunc feedback)
{
	auto const& baseModel = project.projections().view(settings.baseProjection).getBaseModel(project.models());
	if (!baseModel.isReadyForProjection()) {
		feedback("The base model (" + baseModel.getName() + ") is not ready for projecting. Please run the base model once before running projections it is based on.");
		return;
	}
	lastUpdated = wxInvalidDateTime;
	latestRun.emplace(project, *this);
	latestRun->run(feedback);
}

void Simulation::replaceSettings(Simulation::Settings newSettings)
{
	settings = newSettings;
	lastUpdated = wxInvalidDateTime;
}

void Simulation::saveReport(std::string label)
{
	if (!isValid()) throw std::runtime_error("Tried to save a report although the simulation hasn't been run yet!");
	savedReports.push_back({ latestReport, wxDateTime::Now(), label });
}

std::string Simulation::getLastUpdatedString() const
{
	if (!lastUpdated.IsValid()) return "";
	else return lastUpdated.FormatISODate().ToStdString();
}

std::string Simulation::getLiveString() const
{
	switch (settings.live) {
	case Settings::Mode::LiveAutomatic: return "Live Automatic";
	case Settings::Mode::LiveManual: return "Live Manual";
	case Settings::Mode::Projection: return "Projection Only";
	default: return "Invalid Mode";
	}
}

Simulation::Report const& Simulation::getLatestReport() const
{
	return latestReport;
}

Simulation::SavedReports const& Simulation::viewSavedReports() const
{
	return savedReports;
}

float Simulation::Report::getPartyMajorityPercent(MajorParty whichParty) const
{
	return majorityPercent[whichParty];
}

float Simulation::Report::getPartyMinorityPercent(MajorParty whichParty) const
{
	return minorityPercent[whichParty];
}

float Simulation::Report::getHungPercent() const
{
	return hungPercent;
}

int Simulation::Report::classicSeatCount() const
{
	return classicSeatIndices.size();
}

Seat::Id Simulation::Report::classicSeatIndex(int index) const
{
	return classicSeatIndices[index];
}

int Simulation::Report::internalRegionCount() const
{
	return regionPartyWinExpectation.size();
}

float Simulation::Report::getPartyWinExpectation(int partyIndex) const
{
	return partyWinExpectation.at(partyIndex);
}

float Simulation::Report::getPartyWinMedian(int partyIndex) const
{
	if (partyWinMedian.contains(partyIndex)) return partyWinMedian.at(partyIndex);
	return 0;
}

float Simulation::Report::getOthersWinExpectation() const
{
	if (partyWinExpectation.size() < 3) return 0.0f;
	float totalExpectation = 0.0f;
	for (auto [partyIndex, expectation] : partyWinExpectation) {
		if (partyIndex && partyIndex != 1) {
			totalExpectation += expectation;
		}
	}
	return totalExpectation;
}

float Simulation::Report::getRegionPartyWinExpectation(int regionIndex, int partyIndex) const
{
	return regionPartyWinExpectation[regionIndex].at(partyIndex);
}

float Simulation::Report::getRegionOthersWinExpectation(int regionIndex) const
{
	if (regionIndex < 0 || regionIndex >= int(regionPartyWinExpectation.size())) return 0.0f;
	if (regionPartyWinExpectation[regionIndex].size() < 3) return 0.0f;
	float totalExpectation = 0.0f;
	for (auto [partyIndex, expectation] : regionPartyWinExpectation[regionIndex]) {
		if (partyIndex && partyIndex != 1) {
			totalExpectation += expectation;
		}
	}
	return totalExpectation;
}

float Simulation::Report::getPartySeatWinFrequency(int partyIndex, int seatIndex) const
{
	return partySeatWinFrequency.at(partyIndex)[seatIndex];
}

float Simulation::Report::getOthersWinFrequency(int seatIndex) const
{
	return othersWinFrequency[seatIndex];
}

float Simulation::Report::getClassicSeatMajorPartyWinRate(int classicSeatIndex, int partyIndex) const {
	return (partyIndex ? 100.0f - partyOneWinPercent[classicSeatIndices[classicSeatIndex]] :
		partyOneWinPercent[classicSeatIndices[classicSeatIndex]]);
}

int Simulation::Report::getProbabilityBound(int bound, MajorParty whichParty) const
{
	switch (whichParty) {
	case MajorParty::One: return partyOneProbabilityBounds[bound];
	case MajorParty::Two: return partyTwoProbabilityBounds[bound];
	case MajorParty::Others: return othersProbabilityBounds[bound];
	default: return 0.0f;
	}
}

bool Simulation::isValid() const
{
	return lastUpdated.IsValid();
}

float Simulation::Report::getPartyWinPercent(MajorParty whichParty) const
{
	return majorityPercent[whichParty] + 
		minorityPercent[whichParty] + 
		hungPercent * 0.5f;
}

int Simulation::Report::getMinimumSeatFrequency(int partyIndex) const
{
	if (int(partySeatWinFrequency.size()) < partyIndex) return 0;
	if (partySeatWinFrequency.at(partyIndex).size() == 0) return 0;
	for (int i = 0; i < int(partySeatWinFrequency.at(partyIndex).size()); ++i) {
		if (partySeatWinFrequency.at(partyIndex)[i] > 0) return i;
	}
	return 0;
}

int Simulation::Report::getMaximumSeatFrequency(int partyIndex) const
{
	if (int(partySeatWinFrequency.size()) < partyIndex) return 0;
	if (partySeatWinFrequency.at(partyIndex).size() == 0) return 0;
	for (int i = int(partySeatWinFrequency.at(partyIndex).size()) - 1; i >= 0; --i) {
		if (partySeatWinFrequency.at(partyIndex)[i] > 0) return i;
	}
	return 0;
}

int Simulation::Report::getModalSeatFrequencyCount(int partyIndex) const
{
	if (int(partySeatWinFrequency.size()) < partyIndex) return 0;
	if (partySeatWinFrequency.at(partyIndex).size() == 0) return 0;
	return *std::max_element(partySeatWinFrequency.at(partyIndex).begin(), partySeatWinFrequency.at(partyIndex).end());
}

double Simulation::Report::getPartyOne2pp() const
{
	return partyOneSwing + prevElection2pp;
}

int Simulation::Report::findBestSeatDisplayCenter(Party::Id partySorted, int numSeatsDisplayed) const {
	// aim here is to find the range of seats with the greatest difference between most and least likely for "partySorted" to wion
	float bestProbRange = 50.0f;
	int bestCenter = int(classicSeatIndices.size()) / 2;
	for (int lastSeatIndex = numSeatsDisplayed - 1; lastSeatIndex < int(classicSeatIndices.size()); ++lastSeatIndex) {
		float lastSeatProb = getClassicSeatMajorPartyWinRate(lastSeatIndex, partySorted);
		float firstSeatProb = getClassicSeatMajorPartyWinRate(lastSeatIndex - numSeatsDisplayed + 1, partySorted);
		float probRange = abs(50.0f - (lastSeatProb + firstSeatProb) / 2);
		if (probRange < bestProbRange) {
			bestProbRange = probRange;
			bestCenter = lastSeatIndex - numSeatsDisplayed / 2;
		}
	}
	return bestCenter;
}

// This process for the following functions is inefficient as the result could be cached upon first use or saved in the save file.
// However, for now it's not enough to cause any issues, so leaving it as is

int Simulation::Report::getPrimarySampleCount(int partyIndex) const
{
	return std::accumulate(partyPrimaryFrequency.at(partyIndex).begin(), partyPrimaryFrequency.at(partyIndex).end(), 0,
		[](int sum, std::pair<short, int> a) {return sum + a.second; });
}

float Simulation::Report::getPrimarySampleExpectation(int partyIndex) const
{
	int totalCount = getPrimarySampleCount(partyIndex);
	if (!totalCount) return 0;
	return std::accumulate(partyPrimaryFrequency.at(partyIndex).begin(), partyPrimaryFrequency.at(partyIndex).end(), 0.0f,
		[](float sum, std::pair<short, int> a) {
			return sum + float(a.second) * (float(a.first) * 0.1f + 0.05f);
		}
	) / float(totalCount);
}

float Simulation::Report::getPrimarySampleMedian(int partyIndex) const
{
	int totalCount = getPrimarySampleCount(partyIndex);
	if (!totalCount) return 0.0f;
	int currentCount = 0;
	for (auto const& [bucketKey, bucketCount] : partyPrimaryFrequency.at(partyIndex)) {
		currentCount += bucketCount;
		if (currentCount > totalCount / 2) {
			return float(bucketKey) * 0.1f;
		}
	}
	return 100.0f;
}

int Simulation::Report::get2ppSampleCount() const
{
	return std::accumulate(tppFrequency.begin(), tppFrequency.end(), 0,
		[](int sum, std::pair<short, int> a) {return sum + a.second; });
}

float Simulation::Report::get2ppSampleExpectation() const
{
	int totalCount = get2ppSampleCount();
	if (!totalCount) return 0;
	return std::accumulate(tppFrequency.begin(), tppFrequency.end(), 0.0f,
		[](float sum, std::pair<short, int> a) {
			return sum + float(a.first) * float(a.second) * 0.1f;
		}
	) / float(totalCount);
}

float Simulation::Report::get2ppSampleMedian() const
{
	int totalCount = get2ppSampleCount();
	if (!totalCount) return 0.0f;
	int currentCount = 0;
	for (auto const& [bucketKey, bucketCount] : tppFrequency) {
		currentCount += bucketCount;
		if (currentCount > totalCount / 2) {
			return float(bucketKey) * 0.1f;
		}
	}
	return 100.0f;
}

int Simulation::Report::getOthersLeading(int regionIndex) const
{
	if (regionPartyIncuments[regionIndex].size() < 3) return 0;
	return std::accumulate(regionPartyIncuments[regionIndex].begin() + 2, regionPartyIncuments[regionIndex].end(), 0);
}

std::string Simulation::textReport(ProjectionCollection const& projections) const
{
	std::stringstream report;
	report << "Reporting Model: \n";
	report << " Name: " << settings.name << "\n";
	report << " Number of Iterations: " << settings.numIterations << "\n";
	report << " Base Projection: " << projections.view(settings.baseProjection).getSettings().name << "\n";
	report << " Previous Election 2pp: " << settings.prevElection2pp << "\n";
	report << " State Standard Deviation (Base): " << settings.stateSD << "\n";
	report << " State Deviation Decay (per day): " << settings.stateDecay << "\n";
	report << " Live Status: " << getLiveString() << "\n";
	return report.str();
}
