#include "PollingProject.h"

#include <algorithm>
#include <utility>
#include <vector>
#include <thread>

namespace {
	constexpr char ConfigFilename[] = "config.cfg";
}

const Party PollingProject::invalidParty =
	Party("Invalid", 50.0f, 0.0f, "INV", Party::CountAsParty::None);

PollingProject::PollingProject(WorkspacePaths workspacePaths)
	: workspacePaths(std::move(workspacePaths)),
	configObj(this->workspacePaths.resolveString(ConfigFilename)),
	partyCollection(*this),
	pollsterCollection(*this),
	pollCollection(*this),
	modelCollection(*this),
	projectionCollection(*this),
	regionCollection(*this),
	seatCollection(*this),
	simulationCollection(*this)
{}

PollingProject::~PollingProject() = default;

std::unique_ptr<PollingProject> PollingProject::createLiveRunContext(std::vector<Simulation::Id> const& ids) const
{
	auto context = std::unique_ptr<PollingProject>(new PollingProject(workspacePaths));
	copyLiveRunInputsTo(*context, ids);
	context->configObj.limitBackgroundSimulationThreads(std::thread::hardware_concurrency());
	return context;
}

void PollingProject::copyLiveRunInputsTo(PollingProject& target, std::vector<Simulation::Id> const& ids) const
{
	// Constructing the empty context binds every collection to its new owner.
	// Copy only values and ID counters. GUI-only lazy coordinators and callbacks
	// stay empty, and fitted data is copied once rather than for each snapshot.
	target.name = name; target.electionName = electionName;
	target.lastFileName = lastFileName; target.configObj = configObj;
	target.valid = valid;
	target.partyCollection.parties = partyCollection.parties;
	target.partyCollection.nextId = partyCollection.nextId;
	target.partyCollection.othersPreferenceFlow = partyCollection.othersPreferenceFlow;
	target.partyCollection.othersExhaustRate = partyCollection.othersExhaustRate;
	target.pollsterCollection.pollsters = pollsterCollection.pollsters;
	target.pollsterCollection.nextId = pollsterCollection.nextId;
	target.pollCollection.polls = pollCollection.polls;
	target.pollCollection.nextId = pollCollection.nextId;
	target.pollCollection.sourceFile = pollCollection.sourceFile;
	target.modelCollection.models = modelCollection.models;
	target.modelCollection.nextId = modelCollection.nextId;
	target.projectionCollection.projections = projectionCollection.projections;
	target.projectionCollection.nextId = projectionCollection.nextId;
	target.regionCollection.regions = regionCollection.regions;
	target.regionCollection.nextId = regionCollection.nextId;
	target.seatCollection.seats = seatCollection.seats;
	target.seatCollection.nextId = seatCollection.nextId;
	target.seatCollection.configuredImportSource = seatCollection.configuredImportSource;
	target.seatCollection.strictImportSource = seatCollection.strictImportSource;
	target.outcomeCollection = outcomeCollection;
	target.simulationCollection.nextId = simulationCollection.nextId;
	for (auto id : ids)
		target.simulationCollection.simulations.emplace(id, simulations().view(id).copyForLiveRun());
}

SeatCollection::SeatContainer PollingProject::liveSeatValues() const { return seatCollection.seats; }

void PollingProject::publishLiveSeats(SeatCollection::SeatContainer values) {
	// Imported seat parameters accompany completed reports. Keeping the GUI
	// collection object itself preserves its owner and existing page pointers.
	seatCollection.seats = std::move(values);
	if (!seatCollection.seats.empty())
		seatCollection.nextId = std::max(seatCollection.nextId, seatCollection.seats.rbegin()->first + 1);
}

void PollingProject::refreshCalc2PP()
{
	for (auto& [id, poll] : polls()) {
		static_cast<void>(id);
		partyCollection.recalculatePollCalc2PP(poll);
	}
}

void PollingProject::adjustAfterPartyRemoval(
	PartyCollection::Index partyIndex,
	Party::Id partyId)
{
	polls().adjustAfterPartyRemoval(partyIndex, partyId);
	adjustSeatsAfterPartyRemoval(partyIndex, partyId);
	if (legacyPartyRemovalHandler) {
		legacyPartyRemovalHandler(partyIndex, partyId);
	}
}

void PollingProject::adjustAfterPollsterRemoval(
	PollsterCollection::Index /*pollsterIndex*/,
	Party::Id pollsterId)
{
	polls().removePollsFromPollster(pollsterId);
}

int PollingProject::getEarliestDate() const
{
	return polls().getEarliestDate();
}

int PollingProject::getLatestDate() const
{
	int latestDay = polls().getLatestDate();
	for (auto const& [key, model] : models()) {
		static_cast<void>(key);
		latestDay = std::max(
			latestDay, model.getEndDate().modifiedJulianDay());
	}
	for (auto const& [key, projection] : projections()) {
		static_cast<void>(key);
		latestDay = std::max(
			latestDay,
			projection.getSettings().endDate.modifiedJulianDay());
	}
	return latestDay;
}

void PollingProject::adjustAfterModelRemoval(
	ModelCollection::Index,
	StanModel::Id modelId)
{
	removeProjectionsFromModel(modelId);
}

void PollingProject::adjustAfterProjectionRemoval(
	ProjectionCollection::Index,
	Projection::Id projectionId)
{
	resetSimulationsFromProjection(projectionId);
}

void PollingProject::adjustAfterRegionRemoval(
	RegionCollection::Index regionIndex,
	Region::Id regionId)
{
	adjustSeatsAfterRegionRemoval(regionIndex, regionId);
}

bool PollingProject::isValid() const
{
	return valid;
}

void PollingProject::invalidateProjectionsFromModel(
	StanModel::Id modelId)
{
	for (auto& [projectionId, projection] : projections()) {
		if (projection.getSettings().baseModel == modelId) {
			projection.invalidate();
			invalidateSimulationsFromProjection(projectionId);
		}
	}
}

void PollingProject::invalidateSimulationsFromProjection(
	Projection::Id projectionId)
{
	for (auto& [simulationId, simulation] : simulations()) {
		static_cast<void>(simulationId);
		if (simulation.getSettings().baseProjection == projectionId) {
			simulation.invalidate();
		}
	}
}

void PollingProject::removeProjectionsFromModel(StanModel::Id modelId)
{
	std::vector<Projection::Id> projectionsToRemove;
	for (auto const& [key, projection] : projections()) {
		if (projection.getSettings().baseModel == modelId) {
			projectionsToRemove.push_back(key);
		}
	}
	for (auto const projectionId : projectionsToRemove) {
		projections().remove(projectionId);
	}
}

void PollingProject::resetSimulationsFromProjection(
	Projection::Id projectionId)
{
	Projection::Id const replacementProjection = projections().indexToId(0);
	for (auto& [id, simulation] : simulations()) {
		static_cast<void>(id);
		if (simulation.getSettings().baseProjection == projectionId) {
			auto settings = simulation.getSettings();
			settings.baseProjection = replacementProjection;
			simulation.replaceSettings(std::move(settings));
		}
	}
}

void PollingProject::adjustSeatsAfterPartyRemoval(
	PartyCollection::Index,
	Party::Id partyId)
{
	for (auto& [id, seat] : seats()) {
		static_cast<void>(id);
		if (seat.incumbent == partyId) {
			seat.incumbent = seat.challenger ? 0 : 1;
		}
		if (seat.challenger == partyId) {
			seat.challenger = seat.incumbent ? 0 : 1;
		}
	}
}

void PollingProject::adjustSeatsAfterRegionRemoval(
	RegionCollection::Index,
	Party::Id regionId)
{
	for (auto& [id, seat] : seats()) {
		static_cast<void>(id);
		if (seat.region == regionId) {
			seat.region = regions().indexToId(0);
		}
	}
}

void PollingProject::finalizeFileLoading()
{
	partyCollection.finaliseFileLoading();
}
