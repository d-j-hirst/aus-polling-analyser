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

void ModelCollection::add(Model model) {
	models.insert({ nextId, model });
	++nextId;
}

void ModelCollection::replace(Model::Id id, Model model) {
	models[id] = model;
}

Model const& ModelCollection::view(Model::Id id) const {
	return models.at(id);
}

ModelCollection::Index ModelCollection::idToIndex(Model::Id id) const
{
	auto foundIt = models.find(id);
	if (foundIt == models.end()) return InvalidIndex;
	return std::distance(models.begin(), foundIt);
}

Model::Id ModelCollection::indexToId(Index index) const
{
	if (index >= count() || index < 0) return Model::InvalidId;
	return std::next(models.begin(), index)->first;
}

ModelCollection::Result ModelCollection::canRemove(Model::Id id)
{
	auto modelIt = models.find(id);
	if (modelIt == models.end()) return Result::ModelDoesntExist;
	return Result::Ok;
}

void ModelCollection::remove(Model::Id id) {
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

void ModelCollection::extend(Model::Id id)
{
	auto modelIt = models.find(id);
	if (modelIt == models.end()) throw ModelDoesntExistException();
	Model& model = modelIt->second;
	int latestMjd = project.polls().getLatestDate();
	wxDateTime latestDate = mjdToDate(latestMjd);
	model.extendToDate(latestDate);
}

Model& ModelCollection::access(Model::Id id)
{
	return models.at(id);
}

int ModelCollection::count() const {
	return models.size();
}

void ModelCollection::startLoadingModel()
{
	loadingModel.emplace(generateBasicModelSaveData());
}

void ModelCollection::finaliseLoadedModel()
{
	if (!loadingModel.has_value()) return;
	add(Model(loadingModel.value()));
	loadingModel.reset();
}

// generates a basic model with the standard start and end dates.
Model::SaveData ModelCollection::generateBasicModelSaveData() const {
	Model::SaveData saveData;
	saveData.settings.startDate = mjdToDate(project.polls().getEarliestDate());
	saveData.settings.endDate = mjdToDate(project.polls().getLatestDate());
	return saveData;
}