#include "RegionCollection.h"

#include "PollingProject.h"

#include <exception>

RegionCollection::RegionCollection(PollingProject& project)
	: project(project)
{
}

void RegionCollection::finaliseFileLoading() {
}

void RegionCollection::add(Region region) {
	regions.insert({ nextId, region });
	++nextId;
	calculateSwingDeviations();
}

void RegionCollection::replace(Region::Id id, Region region) {
	regions[id] = region;
	calculateSwingDeviations();
}

Region const& RegionCollection::view(Region::Id id) const {
	return regions.at(id);
}

std::pair<Region::Id, Region const*> RegionCollection::findbyName(std::string const& name) const
{
	for (auto const& [id, region] : regions) {
		if (region.name == name) return { id, &region };
	}
	return { Region::InvalidId, nullptr };
}

RegionCollection::Index RegionCollection::idToIndex(Region::Id id) const
{
	auto foundIt = regions.find(id);
	if (foundIt == regions.end()) return InvalidIndex;
	return std::distance(regions.begin(), foundIt);
}

Region::Id RegionCollection::indexToId(Index index) const
{
	if (index >= count() || index < 0) return Region::InvalidId;
	return std::next(regions.begin(), index)->first;
}

RegionCollection::Result RegionCollection::canRemove(Region::Id id)
{
	if (count() <= NumRequiredRegions) return Result::CantRemoveRequiredRegion;
	auto regionIt = regions.find(id);
	if (regionIt == regions.end()) return Result::RegionDoesntExist;
	return Result::Ok;
}

void RegionCollection::remove(Region::Id id) {
	// A lot of region management is simplified by keeping the first two regions consistent,
	// so we forbid removal of these regions to avoid messier code.
	// If the user wants different top-two regions they can just edit them
	// and having less than two regions doesn't make a lot of sense.
	auto removeAllowed = canRemove(id);
	if (removeAllowed == Result::CantRemoveRequiredRegion) throw RemoveRequiredRegionException();
	if (removeAllowed == Result::RegionDoesntExist) throw RegionDoesntExistException();
	Index index = idToIndex(id);
	auto regionIt = regions.find(id);
	regions.erase(regionIt);
	calculateSwingDeviations();
	project.adjustAfterRegionRemoval(index, id);
}

Region& RegionCollection::access(Region::Id id)
{
	return regions.at(id);
}

int RegionCollection::count() const {
	return regions.size();
}

void RegionCollection::logAll() const
{
	for (auto const& [key, thisRegion] : regions) {
		logger << thisRegion.textReport();
	}
}

void RegionCollection::calculateSwingDeviations()
{
	int totalPopulation = 0;
	float total2pp = 0.0f;
	float totalOld2pp = 0.0f;
	for (auto const& regionPair : regions) {
		totalPopulation += regionPair.second.population;
		total2pp += float(regionPair.second.population) * regionPair.second.sample2pp;
		totalOld2pp += float(regionPair.second.population) * regionPair.second.lastElection2pp;
	}
	total2pp /= float(totalPopulation);
	totalOld2pp /= float(totalPopulation);
	for (auto& regionPair : regions) {
		regionPair.second.swingDeviation = (regionPair.second.sample2pp - regionPair.second.lastElection2pp) - (total2pp - totalOld2pp);
	}
}
