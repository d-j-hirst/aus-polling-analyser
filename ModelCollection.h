#pragma once

#include "Model.h"

#include <map>
#include <optional>

class PollingProject;

class ModelDoesntExistException : public std::runtime_error {
public:
	ModelDoesntExistException() : std::runtime_error("") {}
};

class ModelCollection {
public:
	// Collection is a map between ID values and models
	// IDs are not preserved between sessions, and are used to ensure
	// consistent display with model deletions etc. while making sure references
	// from other components are preserved
	// Map must be ordered to ensure order of models is in order they are added.
	typedef std::map<Model::Id, Model> ModelContainer;

	// Model index refers to the position of the model in the order of currently existing models
	// Should not be stored persistently as removal of a model will change the indices
	// (use the ModelKey for that)
	typedef int Index;
	constexpr static Index InvalidIndex = -1;

	ModelCollection(PollingProject& project);

	// Any post-processing of files that must be done after loading from the file
	void finaliseFileLoading();

	enum class Result {
		Ok,
		ModelDoesntExist,
	};

	// Adds the model "model".
	// Throws an exception if the number of models is over the limit, check this beforehand using canAdd();
	void add(Model model);

	// Replaces the model with index "modelIndex" by "model".
	void replace(Model::Id id, Model model);

	// Checks if it is currently possible to add the given model
	// Returns Result::Ok if it's possible and Result::ModelDoesntExist if that model doesn't exist
	Result canRemove(Model::Id id);

	// Removes the model with index "modelIndex".
	void remove(Model::Id id);

	void extend(Model::Id id);

	// Returns access to the model with the given id
	Model& access(Model::Id id);

	// Returns the model with the given id
	Model const& view(Model::Id id) const;

	// Returns the model with index "modelIndex".
	Model const& viewByIndex(Index modelIndex) const { return view(indexToId(modelIndex)); }

	Index idToIndex(Model::Id id) const;
	Model::Id indexToId(Index id) const;

	// Returns the number of models.
	int count() const;

	void startLoadingModel();

	void finaliseLoadedModel();

	ModelContainer::iterator begin() { return models.begin(); }
	ModelContainer::iterator end() { return models.end(); }
	ModelContainer::const_iterator begin() const { return models.begin(); }
	ModelContainer::const_iterator end() const { return models.end(); }

	std::optional<Model::SaveData> loadingModel;

private:
	Model::SaveData generateBasicModelSaveData() const;

	// what the next ID for an item in the container will be
	int nextId = 0;

	ModelContainer models;

	PollingProject& project;
};