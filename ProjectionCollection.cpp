#include "ProjectionCollection.h"

#include "General.h"
#include "PollingProject.h"

#include <exception>

class ModelCollection;

ProjectionCollection::ProjectionCollection(PollingProject & project)
	: project(project)
{
}

void ProjectionCollection::finaliseFileLoading() {
}

void ProjectionCollection::add(Projection projection) {
	projections.insert({ nextId, projection });
	++nextId;
}

void ProjectionCollection::replace(Projection::Id id, Projection projection) {
	projections[id] = projection;
}

Projection const& ProjectionCollection::view(Projection::Id id) const {
	return projections.at(id);
}

ProjectionCollection::Index ProjectionCollection::idToIndex(Projection::Id id) const
{
	auto foundIt = projections.find(id);
	if (foundIt == projections.end()) return InvalidIndex;
	return std::distance(projections.begin(), foundIt);
}

Projection::Id ProjectionCollection::indexToId(Index index) const
{
	if (index >= count() || index < 0) return Projection::InvalidId;
	return std::next(projections.begin(), index)->first;
}

ProjectionCollection::Result ProjectionCollection::canRemove(Projection::Id id)
{
	auto projectionIt = projections.find(id);
	if (projectionIt == projections.end()) return Result::ProjectionDoesntExist;
	return Result::Ok;
}

void ProjectionCollection::remove(Projection::Id id) {
	// A lot of projection management is simplified by keeping the first two projections consistent,
	// so we forbid removal of these projections to avoid messier code.
	// If the user wants different top-two projections they can just edit them
	// and having less than two projections doesn't make a lot of sense.
	auto removeAllowed = canRemove(id);
	if (removeAllowed == Result::ProjectionDoesntExist) throw ProjectionDoesntExistException();
	Index index = idToIndex(id);
	auto projectionIt = projections.find(id);
	projections.erase(projectionIt);
	project.adjustAfterProjectionRemoval(index, id);
}

void ProjectionCollection::run(Projection::Id id)
{
	auto projectionIt = projections.find(id);
	if (projectionIt == projections.end()) throw ProjectionDoesntExistException();
	Projection& projection = projectionIt->second;
	projection.run(project.models());
}

void ProjectionCollection::setAsNowCast(Projection::Id id)
{
	auto projectionIt = projections.find(id);
	if (projectionIt == projections.end()) throw ProjectionDoesntExistException();
	Projection& projection = projectionIt->second;
	projection.setAsNowCast(project.models());
}

Projection& ProjectionCollection::access(Projection::Id id)
{
	return projections.at(id);
}

int ProjectionCollection::count() const {
	return projections.size();
}

void ProjectionCollection::startLoadingProjection()
{
	loadingProjection.emplace(Projection::SaveData());
}

void ProjectionCollection::finaliseLoadedProjection()
{
	if (!loadingProjection.has_value()) return;
	add(Projection(loadingProjection.value()));
	loadingProjection.reset();
}

void ProjectionCollection::logAll(ModelCollection const& models) const
{
	for (auto const& [key, thisProjection] : projections) {
		logger << thisProjection.textReport(models);
	}
}
