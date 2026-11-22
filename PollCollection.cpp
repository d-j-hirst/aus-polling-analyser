#include "PollCollection.h"

#include "PollingProject.h"

#include <exception>
#include <limits>

const std::string DefaultFileName = "./analysis/Data/poll-data-fed.csv";

struct PollInfo {
	Date date;
	int pollsterId;
	float tpp = std::numeric_limits<float>::quiet_NaN();
	std::map<int, float> results;
};

PollCollection::PollCollection(PollingProject& project)
	: project(project)
{
}

void PollCollection::finaliseFileLoading() {
}

void PollCollection::add(Poll poll) {
	polls.insert({ nextId, poll });
	++nextId;
}

void PollCollection::replace(Poll::Id id, Poll poll) {
	polls[id] = poll;
}

Poll const& PollCollection::view(Poll::Id id) const {
	return polls.at(id);
}

PollCollection::Index PollCollection::idToIndex(Poll::Id id) const
{
	auto foundIt = polls.find(id);
	if (foundIt == polls.end()) return InvalidIndex;
	return std::distance(polls.begin(), foundIt);
}

Poll::Id PollCollection::indexToId(Index index) const
{
	if (index >= count() || index < 0) return Poll::InvalidId;
	return std::next(polls.begin(), index)->first;
}

PollCollection::Result PollCollection::canRemove(Poll::Id id)
{
	if (count() <= NumRequiredPolls) return Result::CantRemoveRequiredPoll;
	auto pollIt = polls.find(id);
	if (pollIt == polls.end()) return Result::PollDoesntExist;
	return Result::Ok;
}

void PollCollection::remove(Poll::Id id) {
	// A lot of poll management is simplified by keeping the first two polls consistent,
	// so we forbid removal of these polls to avoid messier code.
	// If the user wants different top-two polls they can just edit them
	// and having less than two polls doesn't make a lot of sense.
	auto removeAllowed = canRemove(id);
	if (removeAllowed == Result::PollDoesntExist) throw PollDoesntExistException();
	auto pollIt = polls.find(id);
	polls.erase(pollIt);
}

int PollCollection::count() const {
	return polls.size();
}

int PollCollection::getEarliestDate() const {
	if (!count()) return -100000000;
	int earliestDay = 1000000000;
	for (auto const&[key, poll] : polls) {
		int date = poll.date.modifiedJulianDay();
		if (date < earliestDay) earliestDay = date;
	}
	return earliestDay;
}

int PollCollection::getEarliestDateFrom(Date dateAfter) const {
	if (!count()) return -100000000;
	int earliestDay = 1000000000;
	int afterThis = dateAfter.modifiedJulianDay();
	for (auto const& [key, poll] : polls) {
		int date = poll.date.modifiedJulianDay();
		if (date < earliestDay && date >= afterThis) earliestDay = date;
	}
	return earliestDay;
}

int PollCollection::getLatestDate() const {
	if (!count()) return -100000000;
	int latestDay = -1000000000;
	for (auto const& [key, poll] : polls) {
		int date = poll.date.modifiedJulianDay();
		if (date > latestDay) latestDay = date;
	}
	return latestDay;
}

int PollCollection::getLatestDateUpTo(Date dateBefore) const
{
	if (!count()) return -100000000;
	int latestDay = -1000000000;
	int beforeThis = dateBefore.modifiedJulianDay();
	for (auto const& [key, poll] : polls) {
		int date = poll.date.modifiedJulianDay();
		if (date > latestDay && date <= beforeThis) latestDay = date;
	}
	return latestDay;
}

void PollCollection::removePollsFromPollster(Pollster::Id pollster) {
	for (auto it = begin(); it != end();)
	{
		if (it->second.pollster == pollster) {
			it = polls.erase(it);
		}
		else {
			++it;
		}
	}
}

void PollCollection::adjustAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id) {
	for (auto& poll : polls) {
		for (int j = partyIndex; j < project.parties().count(); j++)
			poll.second.primary[j] = poll.second.primary[j + 1];
		poll.second.primary[project.parties().count()] = -1;
	}
}

bool PollCollection::collectPolls(RequestFunc requestFunc, MessageFunc messageFunc)
{
	if (sourceFile == "") sourceFile = DefaultFileName;
	sourceFile = requestFunc("Enter a path for the poll data.", sourceFile);
	auto const resolvedSourceFile = project.paths().resolveString(sourceFile);
	auto file = std::ifstream(resolvedSourceFile);
	if (!file) {
		messageFunc("Polls file not present! Expected a file at " + resolvedSourceFile);
		return false;
	}
	messageFunc("Successfully found file at: " + resolvedSourceFile);
	std::string line;
	std::getline(file, line); // first line is just a legend, skip it
	auto splitLine = splitString(line, ",");
	std::map<int, int> partyIds;
	auto const term = project.models().count() ? project.models().viewByIndex(0).getTermCode() : std::string{};
	constexpr int historicalOther = -1;
	for (int heading = 3; heading < int(splitLine.size()); ++heading) {
		auto splitHeading = splitString(splitLine[heading], " ");
		if (splitHeading.size() != 2) continue;
		if (splitHeading[1] == "FP") {
			std::string partyName = splitHeading[0];
			int const index = project.parties().indexByShortCode(partyName);
			if (partyName == "OTH") {
				partyIds[heading] = PartyCollection::MaxParties;
			}
			else if (index >= 0) {
				partyIds[heading] = index;
			}
			else if ((term == "2026sa" && (partyName == "SAB" || partyName == "DEM" || partyName == "FF")) ||
				(term == "2022vic" && (partyName == "ONP" || partyName == "ON" || partyName == "UAP" || partyName == "DEM"))) {
				// Historical polls itemised minor parties that the replay now
				// groups in Others. Preserve their contribution to the residual.
				partyIds[heading] = historicalOther;
			}
			else {
				messageFunc("Could not find a party with party code " + partyName + ". Please add this party code to a party and try again.");
				return false;
			}
		}
	}
	Date const cutoffDate = Date::fromYmd(9999, 1, 1).value();
	std::vector<PollInfo> pollInfos;
	do {
		std::getline(file, line);
		if (!file) break;
		splitLine = splitString(line, ",");
		PollInfo pollInfo;
		auto const parsedDate = Date::parseIso(splitLine[0]);
		if (!parsedDate) {
			messageFunc("Could not parse poll date: " + splitLine[0]);
			return false;
		}
		pollInfo.date = *parsedDate;
		if (pollInfo.date > cutoffDate) continue;
		std::string pollsterName = splitLine[1];
		try {
			pollInfo.tpp = std::stof(splitLine[3]);
		}
		catch (std::invalid_argument) {
			// N/A value, just silently ignore as it's not actually an error
		}
		bool pollsterFound = false;
		for (auto const& [id, pollster] : project.pollsters()) {
			if (pollster.name == pollsterName) {
				pollsterFound = true;
				pollInfo.pollsterId = id;
				break;
			}
		}
		if (!pollsterFound) {
			messageFunc("Could not find a pollster with name " + pollsterName +
				". Please add this pollster and try again.");
			return false;
		}
		float historicalOthersShare = 0;
		for (auto const& [column, id] : partyIds) {
			try {
				float share = std::stof(splitLine[column]);
				if (id == historicalOther) historicalOthersShare += std::max(0.0f, share);
				else pollInfo.results[id] = share;
			}
			catch (std::invalid_argument) {
				// N/A value, just silently ignore as it's not actually an error
			}
		}
		auto others = pollInfo.results.find(PartyCollection::MaxParties);
		if (others != pollInfo.results.end() && others->second >= 0)
			others->second += historicalOthersShare;
		// approval-only poll, don't add it
		if (!pollInfo.results.contains(0) && std::isnan(pollInfo.tpp)) {
			continue;
		}
		pollInfos.push_back(pollInfo);
	} while (true);
	nextId = 0;
	polls.clear();
	for (auto const& pollInfo : pollInfos) {
		Poll poll(pollInfo.pollsterId, pollInfo.date);
		if (!std::isnan(pollInfo.tpp)) poll.reported2pp = pollInfo.tpp;
		for (auto const& [index, result] : pollInfo.results) {
			poll.primary[index] = result;
		}
		project.parties().recalculatePollCalc2PP(poll);
		add(poll);
	}
	messageFunc("Polls loaded!");
	return true;
}

void PollCollection::logAll(PartyCollection const& parties, PollsterCollection const& pollsters) const
{
	for (auto const& [key, thisPoll] : polls) {
		logger << thisPoll.textReport(parties, pollsters);
	}
}
