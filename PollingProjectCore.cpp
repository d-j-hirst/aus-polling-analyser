#include "PollingProject.h"

#include <algorithm>
#include <utility>
#include <vector>

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
