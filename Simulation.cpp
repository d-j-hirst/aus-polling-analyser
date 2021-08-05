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

int Simulation::Report::internalPartyCount() const
{
	return partyWinExpectation.size();
}

int Simulation::Report::internalRegionCount() const
{
	return regionPartyWinExpectation.size();
}

float Simulation::Report::getPartyWinExpectation(int partyIndex) const
{
	return partyWinExpectation[partyIndex];
}

float Simulation::Report::getPartyWinMedian(int partyIndex) const
{
	if (partyIndex < int(partyWinMedian.size())) return partyWinMedian[partyIndex];
	return 0;
}

float Simulation::Report::getOthersWinExpectation() const
{
	if (partyWinExpectation.size() < 3) return 0.0f;
	return std::accumulate(std::next(partyWinExpectation.begin(), 2), partyWinExpectation.end(), 0.0f);
}

float Simulation::Report::getRegionPartyWinExpectation(int regionIndex, int partyIndex) const
{
	return regionPartyWinExpectation[regionIndex][partyIndex];
}

float Simulation::Report::getRegionOthersWinExpectation(int regionIndex) const
{
	if (regionIndex < 0 || regionIndex >= int(regionPartyWinExpectation.size())) return 0.0f;
	if (regionPartyWinExpectation[regionIndex].size() < 3) return 0.0f;
	return std::accumulate(regionPartyWinExpectation[regionIndex].begin() + 2, regionPartyWinExpectation[regionIndex].end(), 0.0f);
}

float Simulation::Report::getPartySeatWinFrequency(int partyIndex, int seatIndex) const
{
	return partySeatWinFrequency[partyIndex][seatIndex];
}

float Simulation::Report::getOthersWinFrequency(int seatIndex) const
{
	return othersWinFrequency[seatIndex];
}

float Simulation::Report::getIncumbentWinPercent(int seatIndex) const
{
	return incumbentWinPercent[seatIndex];
}

float Simulation::Report::getClassicSeatMajorPartyWinRate(int classicSeatIndex, int partyIndex) const {
	return (seatIncumbents[classicSeatIndices[classicSeatIndex]] == partyIndex ?
		incumbentWinPercent[classicSeatIndices[classicSeatIndex]] :
		100.0f - incumbentWinPercent[classicSeatIndices[classicSeatIndex]]);
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
	if (partySeatWinFrequency[partyIndex].size() == 0) return 0;
	for (int i = 0; i < int(partySeatWinFrequency[partyIndex].size()); ++i) {
		if (partySeatWinFrequency[partyIndex][i] > 0) return i;
	}
	return 0;
}

int Simulation::Report::getMaximumSeatFrequency(int partyIndex) const
{
	if (int(partySeatWinFrequency.size()) < partyIndex) return 0;
	if (partySeatWinFrequency[partyIndex].size() == 0) return 0;
	for (int i = int(partySeatWinFrequency[partyIndex].size()) - 1; i >= 0; --i) {
		if (partySeatWinFrequency[partyIndex][i] > 0) return i;
	}
	return 0;
}

int Simulation::Report::getModalSeatFrequencyCount(int partyIndex) const
{
	if (int(partySeatWinFrequency.size()) < partyIndex) return 0;
	if (partySeatWinFrequency[partyIndex].size() == 0) return 0;
	return *std::max_element(partySeatWinFrequency[partyIndex].begin(), partySeatWinFrequency[partyIndex].end());
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

int Simulation::Report::getOthersLeading(int regionIndex) const
{
	if (regionPartyLeading[regionIndex].size() < 3) return 0;
	return std::accumulate(regionPartyLeading[regionIndex].begin() + 2, regionPartyLeading[regionIndex].end(), 0);
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
