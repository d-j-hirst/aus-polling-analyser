#include "Simulation.h"

#include "CountProgress.h"
#include "Log.h"
#include "Party.h"
#include "PollingProject.h"
#include "Projection.h"
#include "Region.h"
#include "SpecialPartyCodes.h"
#include <algorithm>

#undef min
#undef max

using Mp = Simulation::MajorParty;

const std::vector<float> Simulation::Report::CurrentlyUsedProbabilityBands = { 0.1f, 0.5f, 1.0f, 2.5f, 5.0f, 10.0f, 25.0f, 50.0f, 75.0f, 90.0f, 95.0f, 97.5f, 99.0f, 99.5f, 99.9f };

void Simulation::run(PollingProject & project, SimulationRun::FeedbackFunc feedback)
{
	auto const& baseModel = project.projections().view(settings.baseProjection).getBaseModel(project.models());
	if (!baseModel.isReadyForProjection()) {
		feedback("The base model (" + baseModel.getName() + ") is not ready for projecting. Please run the base model once before running projections it is based on.");
		return;
	}
	lastUpdated = wxInvalidDateTime;
	latestRun.reset(new SimulationRun(project, *this));
	latestRun->run(feedback);
	PA_LOG_VAR(latestReport.getCoalitionFpSampleMedian());
	if (isLive()) checkLiveSeats(project, feedback);
}

void Simulation::checkLiveSeats(PollingProject const& project, SimulationRun::FeedbackFunc feedback)
{
	typedef std::tuple<int, int, float> Change;
	std::vector<Change> changes; // seat, party, change
	if (previousLiveSeats.size() < latestReport.seatPartyWinPercent.size()) {
		previousLiveSeats.resize(latestReport.seatPartyWinPercent.size());
	}
	int indPartyIndex = project.parties().indexByShortCode("IND");
	for (int seatIndex = 0; seatIndex < int(latestReport.seatPartyWinPercent.size()); ++seatIndex) {
		for (auto const& [partyIndex, winFrequency] : latestReport.seatPartyWinPercent[seatIndex]) {
			if (partyIndex == indPartyIndex && !previousLiveSeats[seatIndex].contains(indPartyIndex) &&
				previousLiveSeats[seatIndex].contains(EmergingIndIndex)) {
				// Special case where an emerging ind gets promoted to a normal ind, can handle this
				float percentShift = winFrequency - previousLiveSeats[seatIndex][EmergingIndIndex];
				if (abs(percentShift) > 0.1f) changes.push_back({ seatIndex, partyIndex, percentShift });
				continue;
			}
			else if (!previousLiveSeats[seatIndex].contains(partyIndex)) {
				if (previousLiveSeats[seatIndex].size()) {
					float percentShift = winFrequency;
					if (abs(percentShift) > 0.1f) changes.push_back({ seatIndex, partyIndex, percentShift });
				}
				continue;
			}
			float percentShift = winFrequency - previousLiveSeats[seatIndex][partyIndex];
			if (abs(percentShift) > 0.1f) changes.push_back({ seatIndex, partyIndex, percentShift });
		}
		if (previousLiveSeats[seatIndex].contains(indPartyIndex) &&	!latestReport.seatPartyWinPercent[seatIndex].contains(indPartyIndex)) {
			// Special case where an emerging ind gets promoted to a normal ind, can handle this
			float percentShift = -previousLiveSeats[seatIndex][indPartyIndex];
			if (abs(percentShift) > 0.1f) changes.push_back({ seatIndex, indPartyIndex, percentShift });
			continue;
		}
		if (previousLiveSeats[seatIndex].contains(EmergingIndIndex) &&
			!previousLiveSeats[seatIndex].contains(indPartyIndex) &&
			!latestReport.seatPartyWinPercent[seatIndex].contains(EmergingIndIndex) &&
			!latestReport.seatPartyWinPercent[seatIndex].contains(indPartyIndex)) {
			// Special case where an emerging ind gets promoted to a normal ind, can handle this
			float percentShift = -previousLiveSeats[seatIndex][EmergingIndIndex];
			if (abs(percentShift) > 0.1f) changes.push_back({ seatIndex, indPartyIndex, percentShift });
			continue;
		}
	}
	previousLiveSeats = latestReport.seatPartyWinPercent;
	std::sort(changes.begin(), changes.end(), [](Change a, Change b) {return abs(std::get<2>(a)) > abs(std::get<2>(b)); });
	std::stringstream messages;
	for (auto change : changes) {
		if (std::get<1>(change) < 0) continue; // band-aid fix for a crash bug, just skip messages like this for now
		messages << project.seats().viewByIndex(std::get<0>(change)).name;
		messages << ": " << formatFloat(std::get<2>(change), 1, true);
		messages << " to " << project.parties().viewByIndex(std::get<1>(change)).name << "\n";
	}
	feedback(messages.str());
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

std::optional<Simulation::Report> const& Simulation::getLiveBaselineReport() const
{
	return liveBaselineReport;
}

Simulation::SavedReports const& Simulation::viewSavedReports() const
{
	return savedReports;
}

void Simulation::deleteReport(int reportIndex)
{
	if (reportIndex < 0 || reportIndex >= int(savedReports.size())) return;
	savedReports.erase(savedReports.begin() + reportIndex);
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

float Simulation::Report::getCoalitionWinExpectation() const
{
	return coalitionWinExpectation;
}

float Simulation::Report::getCoalitionWinMedian() const
{
	return coalitionWinMedian;
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

float Simulation::Report::getRegionCoalitionWinExpectation(int regionIndex) const
{
	if (!regionCoalitionWinExpectation.size()) return regionPartyWinExpectation[regionIndex].at(Mp::Two);
	return regionCoalitionWinExpectation[regionIndex];
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

float Simulation::Report::getCoalitionWinFrequency(int seatIndex) const
{
	return coalitionSeatWinFrequency[seatIndex];
}

float Simulation::Report::getOthersWinFrequency(int seatIndex) const
{
	return othersSeatWinFrequency[seatIndex];
}

int Simulation::Report::getProbabilityBound(int bound, MajorParty whichParty) const
{
	switch (whichParty) {
	case MajorParty::One: return partyOneProbabilityBounds[bound];
	case MajorParty::Two: return coalitionSeatWinFrequency.size() ? coalitionProbabilityBounds[bound] : partyTwoProbabilityBounds[bound];
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
	if (!partySeatWinFrequency.contains(partyIndex)) return 0;
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

int Simulation::Report::getCoalitionSeatsSampleCount() const
{
	return std::accumulate(coalitionSeatWinFrequency.begin(), coalitionSeatWinFrequency.end(), 0);
}

int Simulation::Report::getCoalitionSeatsPercentile(float percentile) const
{
	int totalCount = getCoalitionSeatsSampleCount();
	if (!totalCount) return 0.0f;
	int targetCount = int(floor(float(totalCount * percentile * 0.01f)));
	int currentCount = 0;
	auto const& thisSeatFreqs = coalitionSeatWinFrequency;
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

// This process for the following functions is inefficient as the result could be cached upon first use or saved in the save file.
// However, for now it's not enough to cause any issues, so leaving it as is

int Simulation::Report::getFpSampleCount(int partyIndex) const
{
	if (!partyPrimaryFrequency.contains(partyIndex)) return 0;
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

int Simulation::Report::getCoalitionFpSampleCount() const
{
	return std::accumulate(coalitionFpFrequency.begin(), coalitionFpFrequency.end(), 0,
		[](int sum, std::pair<short, int> a) {return sum + a.second; });
}

float Simulation::Report::getCoalitionFpSampleExpectation() const
{
	int totalCount = getCoalitionFpSampleCount();
	if (!totalCount) return 0;
	return std::accumulate(coalitionFpFrequency.begin(), coalitionFpFrequency.end(), 0.0f,
		[](float sum, std::pair<short, int> a) {
			return sum + float(a.first) * float(a.second) * 0.1f;
		}
	) / float(totalCount);
}

float Simulation::Report::getCoalitionFpSampleMedian() const
{
	return getCoalitionFpSamplePercentile(50.0f);
}

float Simulation::Report::getCoalitionFpSamplePercentile(float percentile) const
{
	int totalCount = getCoalitionFpSampleCount();
	if (!totalCount) return 0.0f;
	int targetCount = int(floor(float(totalCount * percentile * 0.01f)));
	int currentCount = 0;
	for (auto const& [bucketKey, bucketCount] : coalitionFpFrequency) {
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
	if (regionPartyIncumbents[regionIndex].size() < 3) return 0;
	return std::accumulate(regionPartyIncumbents[regionIndex].begin() + 2, regionPartyIncumbents[regionIndex].end(), 0);
}

Simulation::Report::SaveablePolls Simulation::Report::getSaveablePolls() const
{
	SaveablePolls saveablePolls;
	for (auto const& [party, polls] : modelledPolls) {
		for (auto const& poll : polls) {
			saveablePolls[party].push_back({ {poll.pollster, poll.day}, {poll.base, poll.adjusted, poll.reported} });
		}
	}
	return saveablePolls;
}

void Simulation::Report::retrieveSaveablePolls(SaveablePolls saveablePolls)
{
	modelledPolls.clear();
	for (auto const& [party, polls] : saveablePolls) {
		for (auto const& saveablePoll : polls) {
			StanModel::ModelledPoll poll;
			poll.pollster = saveablePoll.first.first;
			poll.day = saveablePoll.first.second;
			poll.base = saveablePoll.second[0];
			poll.adjusted = saveablePoll.second[1];
			poll.reported = saveablePoll.second[2];
			modelledPolls[party].push_back(poll);
		}
	}
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
