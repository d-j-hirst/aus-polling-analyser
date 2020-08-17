#pragma once

#include "Region.h"

#include <map>

class PollingProject;

class RemoveRequiredRegionException : public std::runtime_error {
public:
	RemoveRequiredRegionException() : std::runtime_error("") {}
};

class RegionDoesntExistException : public std::runtime_error {
public:
	RegionDoesntExistException() : std::runtime_error("") {}
};

class RegionCollection {
public:
	// Collection is a map between ID values and regions
	// IDs are not preserved between sessions, and are used to ensure
	// consistent display with region deletions etc. while making sure references
	// from other components are preserved
	// Map must be ordered to ensure order of regions is in order they are added.
	typedef std::map<Region::Id, Region> RegionContainer;

	// Region index refers to the position of the region in the order of currently existing regions
	// Should not be stored persistently as removal of a region will change the indices
	// (use the RegionKey for that)
	typedef int Index;
	constexpr static Index InvalidIndex = -1;

	constexpr static int NumRequiredRegions = 1;

	RegionCollection(PollingProject& project);

	// Any post-processing of files that must be done after loading from the file
	void finaliseFileLoading();

	enum class Result {
		Ok,
		CantRemoveRequiredRegion,
		RegionDoesntExist,
	};

	// Adds the region "region".
	// Throws an exception if the number of regions is over the limit, check this beforehand using canAdd();
	void add(Region region);

	// Replaces the region with index "regionIndex" by "region".
	void replace(Region::Id id, Region region);

	// Checks if it is currently possible to add the given region
	// Returns Result::Ok if it's possible and Result::TooManyRegions if there are too many regions
	Result canRemove(Region::Id id);

	// Removes the region with index "regionIndex".
	void remove(Region::Id id);

	// Allows access to the region with index "regionIndex".
	Region& access(Region::Id id);

	// Returns the region with index "regionIndex".
	Region const& view(Region::Id id) const;

	// Returns the region with index "regionIndex".
	Region const& viewByIndex(Index regionIndex) const { return view(indexToId(regionIndex)); }

	Index idToIndex(Region::Id id) const;
	Region::Id indexToId(Index id) const;

	// Returns the number of regions.
	int count() const;

	void logAll() const;

	Region& back() { return std::prev(regions.end())->second; }

	// Gets the begin iterator for the region list.
	RegionContainer::iterator begin() { return regions.begin(); }

	// Gets the end iterator for the region list.
	RegionContainer::iterator end() { return regions.end(); }

	// Gets the begin iterator for the region list.
	RegionContainer::const_iterator begin() const { return regions.begin(); }

	// Gets the end iterator for the region list.
	RegionContainer::const_iterator end() const { return regions.end(); }

	// Gets the begin iterator for the region list.
	RegionContainer::const_iterator cbegin() const { return regions.cbegin(); }

	// Gets the end iterator for the region list.
	RegionContainer::const_iterator cend() const { return regions.cend(); }

private:

	void calculateSwingDeviations();

	// what the next ID for an item in the container will be
	int nextId = 0;

	RegionContainer regions;

	PollingProject& project;
};