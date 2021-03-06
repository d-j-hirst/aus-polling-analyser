#include "ModelCollection.h"

#include "General.h"
#include "PollingProject.h"

#include <exception>

ModelCollection::ModelCollection(PollingProject & project)
	: project(project)
{
}

void ModelCollection::finaliseFileLoading() {
}

void ModelCollection::add(StanModel model) {
	models.insert({ nextId, model });
	++nextId;
}

void ModelCollection::replace(Id id, StanModel model) {
	models[id] = model;
}

StanModel const& ModelCollection::view(ModelCollection::Id id) const {
	return models.at(id);
}

ModelCollection::Index ModelCollection::idToIndex(StanModel::Id id) const
{
	auto foundIt = models.find(id);
	if (foundIt == models.end()) return InvalidIndex;
	return std::distance(models.begin(), foundIt);
}

ModelCollection::Id ModelCollection::indexToId(Index index) const
{
	if (index >= count() || index < 0) return InvalidId;
	return std::next(models.begin(), index)->first;
}

ModelCollection::Result ModelCollection::canRemove(StanModel::Id id)
{
	auto modelIt = models.find(id);
	if (modelIt == models.end()) return Result::ModelDoesntExist;
	return Result::Ok;
}

void ModelCollection::remove(StanModel::Id id) {
	// A lot of model management is simplified by keeping the first two models consistent,
	// so we forbid removal of these models to avoid messier code.
	// If the user wants different top-two models they can just edit them
	// and having less than two models doesn't make a lot of sense.
	auto removeAllowed = canRemove(id);
	if (removeAllowed == Result::ModelDoesntExist) throw ModelDoesntExistException();
	Index index = idToIndex(id);
	auto modelIt = models.find(id);
	models.erase(modelIt);
	project.adjustAfterModelRemoval(index, id);
}

void ModelCollection::run(StanModel::Id id)
{
	auto modelIt = models.find(id);
	if (modelIt == models.end()) throw ModelDoesntExistException();
	// StanModel& model = modelIt->second;
}

void ModelCollection::extend(StanModel::Id id)
{
	auto modelIt = models.find(id);
	if (modelIt == models.end()) throw ModelDoesntExistException();
	//StanModel& model = modelIt->second;
	//int latestMjd = project.polls().getLatestDate();
	//wxDateTime latestDate = mjdToDate(latestMjd);
	//model.extendToDate(latestDate);
}

StanModel& ModelCollection::access(StanModel::Id id)
{
	return models.at(id);
}

int ModelCollection::count() const {
	return models.size();
}

void ModelCollection::logAll() const
{
	for (auto const& [key, thisModel] : models) {
		// logger << thisModel.textReport();
	}
}