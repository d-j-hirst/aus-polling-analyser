#include "Simulation.h"

#include "General.h"
#include "Log.h"
#include "PollingProject.h"
#include "ProjectionCollection.h"
#include "SpecialPartyCodes.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <tuple>
#include <utility>

#undef min
#undef max

using Mp = Simulation::MajorParty;

namespace {
	using FrequencyDistribution = std::map<short, int>;

	int sampleCount(FrequencyDistribution const& frequency)
	{
		return std::accumulate(
			frequency.begin(), frequency.end(), 0,
			[](int sum, auto const& bucket) {
				return sum + bucket.second;
			});
	}

	float sampleExpectation(FrequencyDistribution const& frequency)
	{
		int const totalCount = sampleCount(frequency);
		if (!totalCount) return 0.0f;
		float const total = std::accumulate(
			frequency.begin(), frequency.end(), 0.0f,
			[](float sum, auto const& bucket) {
				return sum + float(bucket.second) *
					(float(bucket.first) * 0.1f + 0.05f);
			});
		return total / float(totalCount);
	}

	float samplePercentile(
		FrequencyDistribution const& frequency, float percentile)
	{
		int const totalCount = sampleCount(frequency);
		if (!totalCount) return 0.0f;
		percentile = std::clamp(percentile, 0.0f, 100.0f);
		int const targetCount =
			int(std::floor(float(totalCount) * percentile * 0.01f));
		int currentCount = 0;
		for (auto const& [bucketKey, bucketCount] : frequency) {
			int const previousCount = currentCount;
			currentCount += bucketCount;
			if (currentCount > targetCount) {
				float const fractionThroughBucket =
					float(targetCount - previousCount) /
					float(currentCount - previousCount);
				return float(bucketKey) * 0.1f +
					fractionThroughBucket * 0.1f;
			}
		}
		return std::min(100.0f, float(frequency.rbegin()->first) * 0.1f + 0.1f);
	}

	int seatCountPercentile(
		std::vector<int> const& frequency, float percentile)
	{
		int const totalCount =
			std::accumulate(frequency.begin(), frequency.end(), 0);
		if (!totalCount || frequency.empty()) return 0;
		percentile = std::clamp(percentile, 0.0f, 100.0f);
		int const targetCount =
			int(std::floor(float(totalCount) * percentile * 0.01f));
		int currentCount = 0;
		for (int seatCount = 0; seatCount < int(frequency.size()); ++seatCount) {
			currentCount += frequency[seatCount];
			if (currentCount > targetCount) return seatCount;
		}
		return int(frequency.size()) - 1;
	}

	float nonMajorOutcomePercent(std::map<int, float> const& outcomes)
	{
		return std::accumulate(
			outcomes.begin(), outcomes.end(), 0.0f,
			[](float total, auto const& outcome) {
				return total +
					(outcome.first == Mp::One || outcome.first == Mp::Two ?
						0.0f : outcome.second);
			});
	}
}

const std::vector<float> Simulation::Report::CurrentlyUsedProbabilityBands = {
	0.1f, 0.5f, 1.0f, 2.5f, 5.0f, 10.0f, 25.0f, 50.0f,
	75.0f, 90.0f, 95.0f, 97.5f, 99.0f, 99.5f, 99.9f
};

bool Simulation::run(
	PollingProject& project,
	SimulationRun::FeedbackFunc feedback,
	SimulationRun::ActionRequiredFunc actionRequired)
{
	if (!actionRequired) actionRequired = feedback;
	if (project.projections().idToIndex(settings.baseProjection) ==
		ProjectionCollection::InvalidIndex) {
		feedback("The simulation does not have a valid base projection.");
		return false;
	}
	auto const& baseModel = project.projections()
		.view(settings.baseProjection).getBaseModel(project.models());
	if (!baseModel.isReadyForProjection()) {
		feedback("The base model (" + baseModel.getName() + ") is not ready for projecting. Please run the base model once before running projections it is based on.");
		return false;
	}
	auto nextRun = std::make_shared<SimulationRun>(project, *this);
	if (!nextRun->run(feedback, actionRequired)) return false;
	latestRun = std::move(nextRun);
	PA_LOG_VAR(latestReport.getCoalitionFpSampleMedian());
	if (isLive()) checkLiveSeats(project, feedback);
	return true;
}

void Simulation::checkLiveSeats(PollingProject const& project, SimulationRun::FeedbackFunc feedback)
{
	using Change = std::tuple<int, int, float>;
	std::vector<Change> changes; // seat, party, change
	if (previousLiveSeats.size() < latestReport.seatPartyWinPercent.size()) {
		previousLiveSeats.resize(latestReport.seatPartyWinPercent.size());
	}
	int const indPartyIndex = project.parties().indexByShortCode("IND");
	bool const hasIndependentParty = indPartyIndex >= 0;
	for (int seatIndex = 0; seatIndex < int(latestReport.seatPartyWinPercent.size()); ++seatIndex) {
		for (auto const& [partyIndex, winFrequency] : latestReport.seatPartyWinPercent[seatIndex]) {
			if (hasIndependentParty && partyIndex == indPartyIndex &&
				!previousLiveSeats[seatIndex].contains(indPartyIndex) &&
				previousLiveSeats[seatIndex].contains(EmergingIndIndex)) {
				// Special case where an emerging ind gets promoted to a normal ind, can handle this
				float percentShift = winFrequency - previousLiveSeats[seatIndex][EmergingIndIndex];
				if (std::abs(percentShift) > 0.1f) changes.push_back({ seatIndex, partyIndex, percentShift });
				continue;
			}
			else if (!previousLiveSeats[seatIndex].contains(partyIndex)) {
				if (previousLiveSeats[seatIndex].size()) {
					float percentShift = winFrequency;
					if (std::abs(percentShift) > 0.1f) changes.push_back({ seatIndex, partyIndex, percentShift });
				}
				continue;
			}
			float percentShift = winFrequency - previousLiveSeats[seatIndex][partyIndex];
			if (std::abs(percentShift) > 0.1f) changes.push_back({ seatIndex, partyIndex, percentShift });
		}
		if (hasIndependentParty &&
			previousLiveSeats[seatIndex].contains(indPartyIndex) &&
			!latestReport.seatPartyWinPercent[seatIndex].contains(indPartyIndex)) {
			// An established independent is a reportable party even when its new
			// probability map has no entry for the resulting zero.
			float percentShift = -previousLiveSeats[seatIndex][indPartyIndex];
			if (std::abs(percentShift) > 0.1f) changes.push_back({ seatIndex, indPartyIndex, percentShift });
		}
		if (hasIndependentParty &&
			previousLiveSeats[seatIndex].contains(EmergingIndIndex) &&
			!previousLiveSeats[seatIndex].contains(indPartyIndex) &&
			!latestReport.seatPartyWinPercent[seatIndex].contains(EmergingIndIndex) &&
			!latestReport.seatPartyWinPercent[seatIndex].contains(indPartyIndex)) {
			// Present a disappearing emerging independent through the reportable
			// independent party rather than exposing the internal aggregate category.
			float percentShift = -previousLiveSeats[seatIndex][EmergingIndIndex];
			if (std::abs(percentShift) > 0.1f) changes.push_back({ seatIndex, indPartyIndex, percentShift });
		}
		for (auto const& [partyIndex, previousWinFrequency] :
			previousLiveSeats[seatIndex]) {
			if (partyIndex < 0 ||
				partyIndex >= project.parties().count() ||
				latestReport.seatPartyWinPercent[seatIndex].contains(partyIndex) ||
				(hasIndependentParty && partyIndex == indPartyIndex)) {
				continue;
			}
			float const percentShift = -previousWinFrequency;
			if (std::abs(percentShift) > 0.1f) {
				changes.push_back({ seatIndex, partyIndex, percentShift });
			}
		}
	}
	previousLiveSeats = latestReport.seatPartyWinPercent;
	std::sort(changes.begin(), changes.end(), [](Change a, Change b) {
		return std::abs(std::get<2>(a)) > std::abs(std::get<2>(b));
		});
	std::stringstream messages;
	for (auto const& change : changes) {
		// Internal aggregate categories are not meaningful in this notification.
		if (std::get<1>(change) < 0) continue;
		messages << project.seats().viewByIndex(std::get<0>(change)).name;
		messages << ": " << formatFloat(std::get<2>(change), 1, true);
		messages << " to " << project.parties().viewByIndex(std::get<1>(change)).name << "\n";
	}
	feedback(messages.str());
}

void Simulation::replaceSettings(Simulation::Settings newSettings)
{
	settings = std::move(newSettings);
	lastUpdated = {};
}

void Simulation::saveReport(std::string label)
{
	if (!isValid()) throw std::runtime_error("Tried to save a report although the simulation hasn't been run yet!");
	savedReports.push_back({ latestReport, Timestamp::now(), std::move(label) });
}

std::string Simulation::getLastUpdatedString() const
{
	return lastUpdated.formatIsoDateLocal();
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

std::vector<LiveData::BoothSnapshot> Simulation::getLiveBoothSnapshots() const
{
	if (!latestRun || !latestRun->getLiveElection()) return {};
	return latestRun->getLiveElection()->getBoothSnapshots();
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
	return getAt(majorityPercent, whichParty, 0.0f);
}

float Simulation::Report::getPartyMinorityPercent(int whichParty) const
{
	return getAt(minorityPercent, whichParty, 0.0f);
}

float Simulation::Report::getHungPercent() const
{
	float const mostSeatsSum = std::accumulate(
		mostSeatsPercent.begin(), mostSeatsPercent.end(), 0.0f,
		[](float sum, auto const& outcome) {
			return sum + outcome.second;
		});
	return mostSeatsSum + tiedPercent;
}

int Simulation::Report::internalRegionCount() const
{
	return int(regionPartyWinExpectation.size());
}

float Simulation::Report::getPartyWinExpectation(int partyIndex) const
{
	return getAt(partyWinExpectation, partyIndex, 0.0f);
}

float Simulation::Report::getPartyWinMedian(int partyIndex) const
{
	return getAt(partyWinMedian, partyIndex, 0.0f);
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
	float totalExpectation = std::accumulate(
		partyWinExpectation.begin(), partyWinExpectation.end(), 0.0f,
		[](float total, auto const& item) {
			return total + item.second;
		});
	float const partyOneExpectation =
		getAt(partyWinExpectation, Mp::One, 0.0f);
	float const coalitionExpectation =
		coalitionSeatWinFrequency.empty() ?
		getAt(partyWinExpectation, Mp::Two, 0.0f) :
		coalitionWinExpectation;
	return std::max(
		0.0f,
		totalExpectation - partyOneExpectation - coalitionExpectation);
}

float Simulation::Report::getRegionPartyWinExpectation(
	int regionIndex, int partyIndex) const
{
	if (regionIndex < 0 ||
		regionIndex >= int(regionPartyWinExpectation.size())) {
		return 0.0f;
	}
	return getAt(regionPartyWinExpectation[regionIndex], partyIndex, 0.0f);
}

float Simulation::Report::getRegionCoalitionWinExpectation(int regionIndex) const
{
	if (regionIndex < 0 ||
		regionIndex >= int(regionPartyWinExpectation.size())) {
		return 0.0f;
	}
	if (regionIndex >= int(regionCoalitionWinExpectation.size())) {
		return getAt(regionPartyWinExpectation[regionIndex], Mp::Two, 0.0f);
	}
	return regionCoalitionWinExpectation[regionIndex];
}

float Simulation::Report::getRegionOthersWinExpectation(int regionIndex) const
{
	if (regionIndex < 0 ||
		regionIndex >= int(regionPartyWinExpectation.size())) {
		return 0.0f;
	}
	auto const& expectations = regionPartyWinExpectation[regionIndex];
	float const totalExpectation = std::accumulate(
		expectations.begin(), expectations.end(), 0.0f,
		[](float total, auto const& item) {
			return total + item.second;
		});
	return std::max(
		0.0f,
		totalExpectation -
			getAt(expectations, Mp::One, 0.0f) -
			getRegionCoalitionWinExpectation(regionIndex));
}

float Simulation::Report::getPartySeatWinFrequency(int partyIndex, int seatIndex) const
{
	auto const partyFrequency = partySeatWinFrequency.find(partyIndex);
	if (partyFrequency == partySeatWinFrequency.end() ||
		seatIndex < 0 || seatIndex >= int(partyFrequency->second.size())) {
		return 0.0f;
	}
	return float(partyFrequency->second[seatIndex]);
}

float Simulation::Report::getCoalitionWinFrequency(int seatIndex) const
{
	if (seatIndex < 0 || seatIndex >= int(coalitionSeatWinFrequency.size())) {
		return 0.0f;
	}
	return float(coalitionSeatWinFrequency[seatIndex]);
}

float Simulation::Report::getOthersWinFrequency(int seatIndex) const
{
	if (seatIndex < 0 || seatIndex >= int(othersSeatWinFrequency.size())) {
		return 0.0f;
	}
	return float(othersSeatWinFrequency[seatIndex]);
}

int Simulation::Report::getProbabilityBound(int bound, MajorParty whichParty) const
{
	if (bound < 0 || bound >= NumProbabilityBoundIndices) return 0;
	switch (whichParty) {
	case MajorParty::One: return partyOneProbabilityBounds[bound];
	case MajorParty::Two:
		return coalitionSeatWinFrequency.empty() ?
			partyTwoProbabilityBounds[bound] : coalitionProbabilityBounds[bound];
	case MajorParty::Others: return othersProbabilityBounds[bound];
	default: return 0;
	}
}

bool Simulation::isValid() const
{
	return lastUpdated.isValid();
}

float Simulation::Report::getPartyOverallWinPercent(int whichParty) const
{
	float thisWinPercent = 0.0f;
	float totalWinPercent = 0.0f;
	for (auto const& [party, percent] : majorityPercent) {
		if (party == whichParty) thisWinPercent += percent;
		totalWinPercent += percent;
	}
	for (auto const& [party, percent] : minorityPercent) {
		if (party == whichParty) thisWinPercent += percent;
		totalWinPercent += percent;
	}
	for (auto const& [party, percent] : mostSeatsPercent) {
		if (party == whichParty) thisWinPercent += percent;
		totalWinPercent += percent;
	}
	// Overall win percentages are currently published only for the two major
	// sides, so split unresolved exact ties evenly between them.
	return thisWinPercent + 0.5f * (100.0f - totalWinPercent);
}

float Simulation::Report::getOthersOverallWinPercent() const
{
	// Outcome maps are authoritative. Party metadata may be absent in older
	// reports, and special simulated outcomes do not necessarily have project
	// party records.
	return nonMajorOutcomePercent(majorityPercent) +
		nonMajorOutcomePercent(minorityPercent) +
		nonMajorOutcomePercent(mostSeatsPercent);
}

int Simulation::Report::getMinimumSeatFrequency(int partyIndex) const
{
	auto const frequency = partySeatWinFrequency.find(partyIndex);
	if (frequency == partySeatWinFrequency.end() || frequency->second.empty()) {
		return 0;
	}
	for (int i = 0; i < int(frequency->second.size()); ++i) {
		if (frequency->second[i] > 0) return i;
	}
	return 0;
}

int Simulation::Report::getMaximumSeatFrequency(int partyIndex) const
{
	auto const frequency = partySeatWinFrequency.find(partyIndex);
	if (frequency == partySeatWinFrequency.end() || frequency->second.empty()) {
		return 0;
	}
	for (int i = int(frequency->second.size()) - 1; i >= 0; --i) {
		if (frequency->second[i] > 0) return i;
	}
	return 0;
}

int Simulation::Report::getPartySeatsSampleCount(int partyIndex) const
{
	auto const frequency = partySeatWinFrequency.find(partyIndex);
	return frequency == partySeatWinFrequency.end() ?
		0 : std::accumulate(
			frequency->second.begin(), frequency->second.end(), 0);
}

int Simulation::Report::getPartySeatsPercentile(int partyIndex, float percentile) const
{
	auto const frequency = partySeatWinFrequency.find(partyIndex);
	if (frequency == partySeatWinFrequency.end()) return 0;
	return seatCountPercentile(frequency->second, percentile);
}

int Simulation::Report::getCoalitionSeatsSampleCount() const
{
	return std::accumulate(coalitionSeatWinFrequency.begin(), coalitionSeatWinFrequency.end(), 0);
}

int Simulation::Report::getCoalitionSeatsPercentile(float percentile) const
{
	return seatCountPercentile(coalitionSeatWinFrequency, percentile);
}

int Simulation::Report::getModalSeatFrequencyCount(int partyIndex) const
{
	auto const frequency = partySeatWinFrequency.find(partyIndex);
	if (frequency == partySeatWinFrequency.end() || frequency->second.empty()) {
		return 0;
	}
	return *std::max_element(
		frequency->second.begin(), frequency->second.end());
}

double Simulation::Report::getPartyOne2pp() const
{
	return partyOneSwing + prevElection2pp;
}

// This process for the following functions is inefficient as the result could be cached upon first use or saved in the save file.
// However, for now it's not enough to cause any issues, so leaving it as is

int Simulation::Report::getFpSampleCount(int partyIndex) const
{
	auto const frequency = partyPrimaryFrequency.find(partyIndex);
	return frequency == partyPrimaryFrequency.end() ?
		0 : sampleCount(frequency->second);
}

float Simulation::Report::getFpSampleExpectation(int partyIndex) const
{
	auto const frequency = partyPrimaryFrequency.find(partyIndex);
	return frequency == partyPrimaryFrequency.end() ?
		0.0f : sampleExpectation(frequency->second);
}

float Simulation::Report::getFpSamplePercentile(int partyIndex, float percentile) const
{
	auto const frequency = partyPrimaryFrequency.find(partyIndex);
	return frequency == partyPrimaryFrequency.end() ?
		0.0f : samplePercentile(frequency->second, percentile);
}

float Simulation::Report::getFpSampleMedian(int partyIndex) const
{
	return getFpSamplePercentile(partyIndex, 50.0f);
}

int Simulation::Report::getTppSampleCount() const
{
	return sampleCount(tppFrequency);
}

float Simulation::Report::getTppSampleExpectation() const
{
	return sampleExpectation(tppFrequency);
}

float Simulation::Report::getTppSampleMedian() const
{
	return getTppSamplePercentile(50.0f);
}

float Simulation::Report::getTppSamplePercentile(float percentile) const
{
	return samplePercentile(tppFrequency, percentile);
}

int Simulation::Report::getCoalitionFpSampleCount() const
{
	return sampleCount(coalitionFpFrequency);
}

float Simulation::Report::getCoalitionFpSampleExpectation() const
{
	return sampleExpectation(coalitionFpFrequency);
}

float Simulation::Report::getCoalitionFpSampleMedian() const
{
	return getCoalitionFpSamplePercentile(50.0f);
}

float Simulation::Report::getCoalitionFpSamplePercentile(float percentile) const
{
	return samplePercentile(coalitionFpFrequency, percentile);
}

int Simulation::Report::getOthersLeading(int regionIndex) const
{
	if (regionIndex < 0 ||
		regionIndex >= int(regionPartyIncumbents.size())) {
		return 0;
	}
	if (regionPartyIncumbents[regionIndex].size() < 3) return 0;
	int othersLeading = std::accumulate(
		regionPartyIncumbents[regionIndex].begin() + 2,
		regionPartyIncumbents[regionIndex].end(), 0);
	if (regionIndex < int(regionCoalitionIncumbents.size())) {
		int const coalitionPartnerLeading =
			regionCoalitionIncumbents[regionIndex] -
			regionPartyIncumbents[regionIndex][Mp::Two];
		othersLeading -= coalitionPartnerLeading;
	}
	return std::max(0, othersLeading);
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

void Simulation::Report::retrieveSaveablePolls(SaveablePolls const& saveablePolls)
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
	report << " Base Projection: ";
	if (projections.idToIndex(settings.baseProjection) ==
		ProjectionCollection::InvalidIndex) {
		report << "<invalid>\n";
	}
	else {
		report << projections.view(settings.baseProjection).getSettings().name << "\n";
	}
	report << " Forecast/report mode: ";
	switch (settings.reportMode) {
	case Settings::ReportMode::RegularForecast:
		report << "Regular Forecast\n";
		break;
	case Settings::ReportMode::LiveForecast:
		report << "Live Forecast\n";
		break;
	case Settings::ReportMode::Nowcast:
		report << "Nowcast\n";
		break;
	default:
		report << "Unknown\n";
		break;
	}
	report << " Previous Election 2pp: " << settings.prevElection2pp << "\n";
	report << " Live Status: " << getLiveString() << "\n";
	return report.str();
}
