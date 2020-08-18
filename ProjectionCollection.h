#pragma once

#include "Projection.h"

#include <map>
#include <optional>
#include <stdexcept>

class PollingProject;
class ModelCollection;

class ProjectionDoesntExistException : public std::runtime_error {
public:
	ProjectionDoesntExistException() : std::runtime_error("") {}
};

class ProjectionCollection {
public:
	// Collection is a map between ID values and projections
	// IDs are not preserved between sessions, and are used to ensure
	// consistent display with projection deletions etc. while making sure references
	// from other components are preserved
	// Map must be ordered to ensure order of projections is in order they are added.
	typedef std::map<Projection::Id, Projection> ProjectionContainer;

	// Projection index refers to the position of the projection in the order of currently existing projections
	// Should not be stored persistently as removal of a projection will change the indices
	// (use the ProjectionKey for that)
	typedef int Index;
	constexpr static Index InvalidIndex = -1;

	ProjectionCollection(PollingProject& project);

	// Any post-processing of files that must be done after loading from the file
	void finaliseFileLoading();

	enum class Result {
		Ok,
		ProjectionDoesntExist,
	};

	// Adds the projection "projection".
	// Throws an exception if the number of projections is over the limit, check this beforehand using canAdd();
	void add(Projection projection);

	// Replaces the projection with index "projectionIndex" by "projection".
	void replace(Projection::Id id, Projection projection);

	// Checks if it is currently possible to add the given projection
	// Returns Result::Ok if it's possible and Result::ProjectionDoesntExist if that projection doesn't exist
	Result canRemove(Projection::Id id);

	// Removes the projection with index "projectionIndex".
	void remove(Projection::Id id);

	void run(Projection::Id id);

	void setAsNowCast(Projection::Id id);

	// Returns access to the projection with the given id
	Projection& access(Projection::Id id);

	// Returns the projection with the given id
	Projection const& view(Projection::Id id) const;

	// Returns the projection with index "projectionIndex".
	Projection const& viewByIndex(Index projectionIndex) const { return view(indexToId(projectionIndex)); }

	Index idToIndex(Projection::Id id) const;
	Projection::Id indexToId(Index id) const;

	// Returns the number of projections.
	int count() const;

	void startLoadingProjection();

	void finaliseLoadedProjection();

	void logAll(ModelCollection const& models) const;

	// Gets the begin iterator for the pollster list.
	ProjectionContainer::iterator begin() { return projections.begin(); }
	ProjectionContainer::const_iterator begin() const { return projections.cbegin(); }

	// Gets the end iterator for the pollster list.
	ProjectionContainer::iterator end() { return projections.end(); }
	ProjectionContainer::const_iterator end() const { return projections.cend(); }

	// Gets the begin iterator for the pollster list.
	ProjectionContainer::const_iterator cbegin() const { return projections.cbegin(); }

	// Gets the end iterator for the pollster list.
	ProjectionContainer::const_iterator cend() const { return projections.cend(); }

	std::optional<Projection::SaveData> loadingProjection;

private:

	// what the next ID for an item in the container will be
	int nextId = 0;

	ProjectionContainer projections;

	PollingProject& project;
};