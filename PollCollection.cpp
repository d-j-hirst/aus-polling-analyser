#include "PollCollection.h"

#include "PollingProject.h"

#include <exception>
#include <limits>

const std::string DefaultFileName = "./analysis/Data/poll-data-fed.csv";

struct PollInfo {
	wxDateTime date;
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
		int date = dateToIntMjd(poll.date);
		if (date < earliestDay) earliestDay = date;
	}
	return earliestDay;
}

int PollCollection::getEarliestDateFrom(wxDateTime const& dateAfter) const {
	if (!count()) return -100000000;
	int earliestDay = 1000000000;
	int afterThis = dateToIntMjd(dateAfter);
	for (auto const& [key, poll] : polls) {
		int date = dateToIntMjd(poll.date);
		if (date < earliestDay && date >= afterThis) earliestDay = date;
	}
	return earliestDay;
}

int PollCollection::getLatestDate() const {
	if (!count()) return -100000000;
	int latestDay = -1000000000;
	for (auto const& [key, poll] : polls) {
		int date = dateToIntMjd(poll.date);
		if (date > latestDay) latestDay = date;
	}
	return latestDay;
}

int PollCollection::getLatestDateUpTo(wxDateTime const & dateBefore) const
{
	if (!count()) return -100000000;
	int latestDay = -1000000000;
	int beforeThis = dateToIntMjd(dateBefore);
	for (auto const& [key, poll] : polls) {
		int date = dateToIntMjd(poll.date);
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

void PollCollection::collectPolls(RequestFunc requestFunc, MessageFunc messageFunc)
{
	if (sourceFile == "") sourceFile = DefaultFileName;
	sourceFile = requestFunc("Enter a path for the poll data.", sourceFile);
	auto file = std::ifstream(sourceFile);
	if (!file) {
		messageFunc("Polls file not present! Expected a file at " + sourceFile);
		return;
	}
	messageFunc("Successfully found file at: " + sourceFile);
	std::string line;
	std::getline(file, line); // first line is just a legend, skip it
	auto splitLine = splitString(line, ",");
	std::map<int, std::string> partyNames;
	std::map<int, int> partyIds;
	for (int heading = 4; heading < int(splitLine.size()); ++heading) {
		auto splitHeading = splitString(splitLine[heading], " ");
		if (splitHeading.size() != 2) continue;
		if (splitHeading[1] == "FP") {
			std::string partyName = splitHeading[0];
			if (partyName == "OTH") {
				partyIds[heading] = PartyCollection::MaxParties;
			}
			bool partyFound = (partyName == "OTH");
			for (auto const& [id, party] : project.parties()) {
				if (contains(party.officialCodes, partyName)) {
					partyFound = true;
					partyIds[heading] = id;
					break;
				}
			}
			if (!partyFound) {
				messageFunc("Could not find a party with party code " + partyName + ". Please add this party code to a party and try again.");
			}
		}
	}
	wxDateTime cutoffDate(1, wxDateTime::Month(0), 9999);
	for (auto const& [key, event] : project.events()) {
		if (event.eventType == Event::Type::EndOfPeriod) {
			if (event.date < cutoffDate) cutoffDate = event.date;
		}
	}
	std::vector<PollInfo> pollInfos;
	do {
		std::getline(file, line);
		if (!file) break;
		splitLine = splitString(line, ",");
		PollInfo pollInfo;
		auto dateParts = splitStringI(splitLine[0], "-");
		pollInfo.date = wxDateTime(dateParts[2], wxDateTime::Month(dateParts[1] - 1), dateParts[0]);
		if (pollInfo.date > cutoffDate) continue;
		std::string pollsterName = splitLine[2];
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
			messageFunc("Could not find a pollster with name " + pollsterName + ". Please add this party code to a party and try again.");
			return;
		}
		for (auto const& [column, id] : partyIds) {
			try {
				pollInfo.results[id] = std::stof(splitLine[column]);
			}
			catch (std::invalid_argument) {
				// N/A value, just silently ignore as it's not actually an error
			}
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
}

void PollCollection::logAll(PartyCollection const& parties, PollsterCollection const& pollsters) const
{
	for (auto const& [key, thisPoll] : polls) {
		logger << thisPoll.textReport(parties, pollsters);
	}
}
