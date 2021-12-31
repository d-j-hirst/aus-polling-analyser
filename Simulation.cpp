#include "Simulation.h"

#include "CountProgress.h"
#include "Log.h"
#include "Party.h"
#include "PollingProject.h"
#include "Projection.h"
#include "Region.h"
#include <algorithm>

#undef min
#undef max

const std::vector<float> Simulation::Report::CurrentlyUsedProbabilityBands = { 0.1f, 0.5f, 1.0f, 2.5f, 5.0f, 10.0f, 25.0f, 50.0f, 75.0f, 90.0f, 95.0f, 97.5f, 99.0f, 99.5f, 99.9f };

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

float Simulation::Report::getPartyMajorityPercent(int whichParty) const
{
	if (majorityPercent.contains(whichParty)) {
		return majorityPercent.at(whichParty);
	}
	else {
		return 0.0f;
	}
}

float Simulation::Report::getPartyMinorityPercent(int whichParty) const
{
	if (minorityPercent.contains(whichParty)) {
		return minorityPercent.at(whichParty);
	}
	else {
		return 0.0f;
	}
}

float Simulation::Report::getHungPercent() const
{
	float mostSeatsSum = std::accumulate(mostSeatsPercent.begin(), mostSeatsPercent.end(), 0.0f,
		[](float sum, decltype(mostSeatsPercent)::value_type a) {return sum + a.second; });
	return mostSeatsSum + tiedPercent;
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

float Simulation::Report::getPartyOverallWinPercent(int whichParty) const
{
	float thisWinPercent = 0.0f;
	float totalWinPercent = 0.0f;
	for (auto [party, percent] : majorityPercent) {
		if (party == whichParty) thisWinPercent += percent;
		totalWinPercent += percent;
	}
	for (auto [party, percent] : minorityPercent) {
		if (party == whichParty) thisWinPercent += percent;
		totalWinPercent += percent;
	}
	for (auto [party, percent] : mostSeatsPercent) {
		if (party == whichParty) thisWinPercent += percent;
		totalWinPercent += percent;
	}
	// Distribute exact ties in proportion to other wins
	return thisWinPercent + 0.5f * (100.0f - totalWinPercent);
}

float Simulation::Report::getOthersOverallWinPercent() const
{
	float othersOverallWinPercent = 0.0f;
	float totalWinPercent = 0.0f;
	for (auto [party, _] : this->partyName) {
		if (majorityPercent.count(party)) {
			if (party != 0 && party != 1) othersOverallWinPercent += majorityPercent.at(party);
			totalWinPercent += majorityPercent.at(party);
		}
		if (minorityPercent.count(party)) {
			if (party != 0 && party != 1) othersOverallWinPercent += minorityPercent.at(party);
			totalWinPercent += minorityPercent.at(party);
		}
		if (mostSeatsPercent.count(party)) {
			if (party != 0 && party != 1) othersOverallWinPercent += mostSeatsPercent.at(party);
			totalWinPercent += mostSeatsPercent.at(party);
		}
	}
	return othersOverallWinPercent;
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

int Simulation::Report::getPartySeatsSampleCount(int partyIndex) const
{
	return std::accumulate(partySeatWinFrequency.at(partyIndex).begin(), partySeatWinFrequency.at(partyIndex).end(), 0);
}

int Simulation::Report::getPartySeatsPercentile(int partyIndex, float percentile) const
{
	int totalCount = getPartySeatsSampleCount(partyIndex);
	if (!totalCount) return 0.0f;
	int targetCount = int(floor(float(totalCount * percentile * 0.01f)));
	int currentCount = 0;
	auto const& thisSeatFreqs = partySeatWinFrequency.at(partyIndex);
	for (int seatCount = 0; seatCount < int(thisSeatFreqs.size()); ++seatCount) {
		int bucketCount = thisSeatFreqs.at(seatCount);
		currentCount += bucketCount;
		if (currentCount > targetCount) {
			return seatCount;
		}
	}
	return 100.0f;
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

int Simulation::Report::getFpSampleCount(int partyIndex) const
{
	return std::accumulate(partyPrimaryFrequency.at(partyIndex).begin(), partyPrimaryFrequency.at(partyIndex).end(), 0,
		[](int sum, std::pair<short, int> a) {return sum + a.second; });
}

float Simulation::Report::getFpSampleExpectation(int partyIndex) const
{
	int totalCount = getFpSampleCount(partyIndex);
	if (!totalCount) return 0.0f;
	return std::accumulate(partyPrimaryFrequency.at(partyIndex).begin(), partyPrimaryFrequency.at(partyIndex).end(), 0.0f,
		[](float sum, std::pair<short, int> a) {
			return sum + float(a.second) * (float(a.first) * 0.1f + 0.05f);
		}
	) / float(totalCount);
}

float Simulation::Report::getFpSamplePercentile(int partyIndex, float percentile) const
{
	int totalCount = getFpSampleCount(partyIndex);
	if (!totalCount) return 0.0f;
	int targetCount = int(floor(float(totalCount * percentile * 0.01f)));
	int currentCount = 0;
	for (auto const& [bucketKey, bucketCount] : partyPrimaryFrequency.at(partyIndex)) {
		int prevCount = currentCount;
		currentCount += bucketCount;
		if (currentCount > targetCount) {
			float extra = (float(targetCount) - float(prevCount)) / (float(currentCount) - float(prevCount)) * 0.1f;
			return float(bucketKey) * 0.1f + extra;
		}
	}
	return 100.0f;
}

float Simulation::Report::getFpSampleMedian(int partyIndex) const
{
	return getFpSamplePercentile(partyIndex, 50.0f);
}

int Simulation::Report::getTppSampleCount() const
{
	return std::accumulate(tppFrequency.begin(), tppFrequency.end(), 0,
		[](int sum, std::pair<short, int> a) {return sum + a.second; });
}

float Simulation::Report::getTppSampleExpectation() const
{
	int totalCount = getTppSampleCount();
	if (!totalCount) return 0;
	return std::accumulate(tppFrequency.begin(), tppFrequency.end(), 0.0f,
		[](float sum, std::pair<short, int> a) {
			return sum + float(a.first) * float(a.second) * 0.1f;
		}
	) / float(totalCount);
}

float Simulation::Report::getTppSampleMedian() const
{
	return getTppSamplePercentile(50.0f);
}

float Simulation::Report::getTppSamplePercentile(float percentile) const
{
	int totalCount = getTppSampleCount();
	if (!totalCount) return 0.0f;
	int targetCount = int(floor(float(totalCount * percentile * 0.01f)));
	int currentCount = 0;
	for (auto const& [bucketKey, bucketCount] : tppFrequency) {
		int prevCount = currentCount;
		currentCount += bucketCount;
		if (currentCount > targetCount) {
			float extra = (float(targetCount) - float(prevCount)) / (float(currentCount) - float(prevCount)) * 0.1f;
			return float(bucketKey) * 0.1f + extra;
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
	report << " Live Status: " << getLiveString() << "\n";
	return report.str();
}
