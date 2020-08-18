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


void Simulation::run(PollingProject & project)
{
	latestRun.emplace(project, *this);
	latestRun->run();
}

void Simulation::replaceSettings(Simulation::Settings newSettings)
{
	settings = newSettings;
	lastUpdated = wxInvalidDateTime;
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

float Simulation::getPartyMajorityPercent(MajorParty whichParty) const
{
	return majorityPercent[whichParty];
}

float Simulation::getPartyMinorityPercent(MajorParty whichParty) const
{
	return minorityPercent[whichParty];
}

float Simulation::getHungPercent() const
{
	return hungPercent;
}

int Simulation::classicSeatCount() const
{
	return classicSeatIds.size();
}

Seat::Id Simulation::classicSeatId(int index) const
{
	return classicSeatIds[index];
}

int Simulation::internalPartyCount() const
{
	return partyWinExpectation.size();
}

int Simulation::internalRegionCount() const
{
	return regionPartyWinExpectation.size();
}

float Simulation::getPartyWinExpectation(int partyIndex) const
{
	return partyWinExpectation[partyIndex];
}

float Simulation::getOthersWinExpectation() const
{
	if (partyWinExpectation.size() < 3) return 0.0f;
	return std::accumulate(std::next(partyWinExpectation.begin(), 2), partyWinExpectation.end(), 0.0f);
}

float Simulation::getRegionPartyWinExpectation(int regionIndex, int partyIndex) const
{
	return regionPartyWinExpectation[regionIndex][partyIndex];
}

float Simulation::getRegionOthersWinExpectation(int regionIndex) const
{
	if (regionIndex < 0 || regionIndex >= int(regionPartyWinExpectation.size())) return 0.0f;
	if (regionPartyWinExpectation[regionIndex].size() < 3) return 0.0f;
	return std::accumulate(regionPartyWinExpectation[regionIndex].begin() + 2, regionPartyWinExpectation[regionIndex].end(), 0.0f);
}

float Simulation::getPartySeatWinFrequency(int partyIndex, int seatIndex) const
{
	return partySeatWinFrequency[partyIndex][seatIndex];
}

float Simulation::getOthersWinFrequency(int seatIndex) const
{
	return othersWinFrequency[seatIndex];
}

float Simulation::getIncumbentWinPercent(int seatIndex) const
{
	return incumbentWinPercent[seatIndex];
}

float Simulation::getClassicSeatMajorPartyWinRate(int classicSeatIndex, int partyIndex, PollingProject const& project) const {
	Seat const& seat = project.seats().view(classicSeatIds[classicSeatIndex]);
	return (seat.incumbent == partyIndex ?
		incumbentWinPercent[classicSeatIds[classicSeatIndex]] :
		100.0f - incumbentWinPercent[classicSeatIds[classicSeatIndex]]);
}

int Simulation::getProbabilityBound(int bound, MajorParty whichParty) const
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

float Simulation::getPartyWinPercent(MajorParty whichParty) const
{
	return majorityPercent[whichParty] + minorityPercent[whichParty] + hungPercent * 0.5f;
}

int Simulation::getMinimumSeatFrequency(int partyIndex) const
{
	if (int(partySeatWinFrequency.size()) < partyIndex) return 0;
	if (partySeatWinFrequency[partyIndex].size() == 0) return 0;
	for (int i = 0; i < int(partySeatWinFrequency[partyIndex].size()); ++i) {
		if (partySeatWinFrequency[partyIndex][i] > 0) return i;
	}
	return 0;
}

int Simulation::getMaximumSeatFrequency(int partyIndex) const
{
	if (int(partySeatWinFrequency.size()) < partyIndex) return 0;
	if (partySeatWinFrequency[partyIndex].size() == 0) return 0;
	for (int i = int(partySeatWinFrequency[partyIndex].size()) - 1; i >= 0; --i) {
		if (partySeatWinFrequency[partyIndex][i] > 0) return i;
	}
	return 0;
}

int Simulation::getModalSeatFrequencyCount(int partyIndex) const
{
	if (int(partySeatWinFrequency.size()) < partyIndex) return 0;
	if (partySeatWinFrequency[partyIndex].size() == 0) return 0;
	return *std::max_element(partySeatWinFrequency[partyIndex].begin(), partySeatWinFrequency[partyIndex].end());
}

double Simulation::getPartyOne2pp() const
{
	return partyOneSwing + settings.prevElection2pp;
}

int Simulation::findBestSeatDisplayCenter(Party::Id partySorted, int numSeatsDisplayed, PollingProject const& project) const {
	// aim here is to find the range of seats with the greatest difference between most and least likely for "partySorted" to wion
	float bestProbRange = 50.0f;
	int bestCenter = int(classicSeatIds.size()) / 2;
	for (int lastSeatIndex = numSeatsDisplayed - 1; lastSeatIndex < int(classicSeatIds.size()); ++lastSeatIndex) {
		if (!project.seats().exists(classicSeatIds[lastSeatIndex])) continue;
		if (!project.seats().exists(classicSeatIds[lastSeatIndex - numSeatsDisplayed + 1])) continue;
		float lastSeatProb = getClassicSeatMajorPartyWinRate(lastSeatIndex, partySorted, project);
		float firstSeatProb = getClassicSeatMajorPartyWinRate(lastSeatIndex - numSeatsDisplayed + 1, partySorted, project);
		float probRange = abs(50.0f - (lastSeatProb + firstSeatProb) / 2);
		if (probRange < bestProbRange) {
			bestProbRange = probRange;
			bestCenter = lastSeatIndex - numSeatsDisplayed / 2;
		}
	}
	return bestCenter;
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
