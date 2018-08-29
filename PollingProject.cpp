#include "PollingProject.h"
#include <iomanip>
#include <algorithm>

#undef max
#undef min

PollingProject::PollingProject(NewProjectData& newProjectData) :
		name(newProjectData.projectName),
		lastFileName(newProjectData.projectName + ".pol"),
		valid(true)
{
	// The project must always have at least two parties, no matter what. This initializes them with default values.
	addParty(Party("Labor", 100, "ALP", Party::CountAsParty::IsPartyOne));
	addParty(Party("Liberals", 0, "LIB", Party::CountAsParty::IsPartyTwo));

	addPollster(Pollster("Default Pollster", 1.0f, 0, true, false));
}

PollingProject::PollingProject(std::string pathName) :
		lastFileName(pathName.substr(pathName.rfind("\\")+1))
{
	PrintDebugLine(lastFileName);
	open(pathName);
}

void PollingProject::refreshCalc2PP() {
	for (auto it = polls.begin(); it != polls.end(); it++)
		recalculatePollCalc2PP(*it);
}

void PollingProject::addParty(Party party) {
	parties.push_back(party);
}

void PollingProject::replaceParty(int partyIndex, Party party) {
	*getPartyPtr(partyIndex) = party;
}

Party PollingProject::getParty(int partyIndex) const {
	auto it = parties.begin();
	for (int i = 0; i < partyIndex; i++) it++;
	return *it;
}

Party* PollingProject::getPartyPtr(int partyIndex) {
	auto it = parties.begin();
	for (int i = 0; i < partyIndex; i++) it++;
	return &*it;
}

Party const* PollingProject::getPartyPtr(int partyIndex) const {
	auto it = parties.begin();
	for (int i = 0; i < partyIndex; i++) it++;
	return &*it;
}

void PollingProject::removeParty(int partyIndex) {
	auto it = parties.begin();
	for (int i = 0; i < partyIndex; i++) it++;
	parties.erase(it);
	adjustPollsAfterPartyRemoval(partyIndex);
}

int PollingProject::getPartyCount() const {
	return parties.size();
}

int PollingProject::getPartyIndex(Party const* const partyPtr) {
	int i = 0;
	for (auto it = parties.begin(); it != parties.end(); it++) {
		if (&*it == partyPtr) return i;
		i++;
	}
	return -1;
}

std::list<Party>::const_iterator PollingProject::getPartyBegin() const {
	return parties.begin();
}

std::list<Party>::const_iterator PollingProject::getPartyEnd() const {
	return parties.end();
}

void PollingProject::addPollster(Pollster pollster) {
	pollsters.push_back(pollster);
}

void PollingProject::replacePollster(int pollsterIndex, Pollster pollster) {
	auto it = pollsters.begin();
	for (int i = 0; i < pollsterIndex; i++) it++;
	*it = pollster;
}

Pollster PollingProject::getPollster(int pollsterIndex) const {
	auto it = pollsters.begin();
	for (int i = 0; i < pollsterIndex; i++) it++;
	return *it;
}

Pollster const* PollingProject::getPollsterPtr(int pollsterIndex) const {
	auto it = pollsters.begin();
	for (int i = 0; i < pollsterIndex; i++) it++;
	return &*it;
}

int PollingProject::getPollsterIndex(Pollster const* const pollsterPtr) {
	int i = 0;
	for (auto it = pollsters.begin(); it != pollsters.end(); it++) {
		if (&*it == pollsterPtr) return i;
		i++;
	}
	return -1;
}

void PollingProject::removePollster(int pollsterIndex) {
	auto it = pollsters.begin();
	for (int i = 0; i < pollsterIndex; i++) it++;
	removePollsFromPollster(std::addressof(*it));
	pollsters.erase(it);
}

int PollingProject::getPollsterCount() const {
	return pollsters.size();
}

std::list<Pollster>::const_iterator PollingProject::getPollsterBegin() const {
	return pollsters.begin();
}

std::list<Pollster>::const_iterator PollingProject::getPollsterEnd() const {
	return pollsters.end();
}

void PollingProject::addPoll(Poll poll) {
	polls.push_back(poll);
}

void PollingProject::replacePoll(int pollIndex, Poll poll) {
	polls[pollIndex] = poll;
}

Poll PollingProject::getPoll(int pollIndex) const {
	return polls[pollIndex];
}

Poll const* PollingProject::getPollPtr(int pollIndex) const {
	return &polls[pollIndex];
}

void PollingProject::removePoll(int pollIndex) {
	auto it = polls.begin();
	for (int i = 0; i < pollIndex; i++) it++;
	polls.erase(it);
}

int PollingProject::getPollCount() const {
	return polls.size();
}

int PollingProject::getVisStartDay() const {
	return visStartDay;
}

int PollingProject::getVisEndDay() const {
	return visEndDay;
}

void PollingProject::setVisualiserBounds(int startDay, int endDay) {
	startDay = std::max(getEarliestDate(), startDay);
	endDay = std::min(getLatestDate(), endDay);
	visStartDay = startDay; visEndDay = endDay;
}

int PollingProject::getEarliestPollDate() const {
	if (!getPollCount()) return -100000000;
	int earliestDay = 1000000000;
	for (int i = 0; i < getPollCount(); ++i) {
		int date = int(floor(getPoll(i).date.GetModifiedJulianDayNumber()));
		if (date < earliestDay) earliestDay = date;
	}
	return earliestDay;
}

int PollingProject::getLatestPollDate() const {
	if (!getPollCount()) return -100000000;
	int latestDay = -1000000000;
	for (int i = 0; i < getPollCount(); ++i) {
		int date = int(floor(getPoll(i).date.GetModifiedJulianDayNumber()));
		if (date > latestDay) latestDay = date;
	}
	return latestDay;
}

int PollingProject::getEarliestDate() const {
	int earliestDay = getEarliestPollDate();
	for (int i = 0; i < getModelCount(); ++i) {
		int date = int(floor(getModel(i).endDate.GetModifiedJulianDayNumber()));
		if (date < earliestDay) earliestDay = date;
	}
	return earliestDay;
}

int PollingProject::getLatestDate() const {
	int latestDay = getLatestPollDate();
	for (int i = 0; i < getModelCount(); ++i) {
		int date = int(floor(getModel(i).endDate.GetModifiedJulianDayNumber()));
		if (date > latestDay) latestDay = date;
	}
	for (int i = 0; i < getProjectionCount(); ++i) {
		int date = int(floor(getProjection(i).endDate.GetModifiedJulianDayNumber()));
		if (date > latestDay) latestDay = date;
	}
	return latestDay;
}

wxDateTime PollingProject::MjdToDate(int mjd) const {
	if (mjd <= -1000000) return wxInvalidDateTime;
	wxDateTime tempDate = wxDateTime(double(mjd) + 2400000.5);
	tempDate.SetHour(18);
	return tempDate;
}

// generates a basic model with the standard start and end dates.
Model PollingProject::generateBaseModel() const {
	Model tempModel = Model();
	tempModel.startDate = MjdToDate(getEarliestPollDate());
	tempModel.endDate = MjdToDate(getLatestPollDate());
	return tempModel;
}

void PollingProject::addEvent(Event event) {
	events.push_back(event);
}

void PollingProject::replaceEvent(int eventIndex, Event event) {
	events[eventIndex] = event;
}

Event PollingProject::getEvent(int eventIndex) const {
	return events[eventIndex];
}

Event* PollingProject::getEventPtr(int eventIndex) {
	return &events[eventIndex];
}

void PollingProject::removeEvent(int eventIndex) {
	auto it = events.begin();
	for (int i = 0; i < eventIndex; i++) it++;
	events.erase(it);
}

int PollingProject::getEventCount() const {
	return events.size();
}

void PollingProject::addModel(Model model) {
	models.push_back(model);
}

void PollingProject::replaceModel(int modelIndex, Model model) {
	invalidateProjectionsFromModel(getModelPtr(modelIndex));
	*getModelPtr(modelIndex) = model;
}

Model PollingProject::getModel(int modelIndex) const {
	auto it = models.begin();
	for (int i = 0; i < modelIndex; i++) it++;
	return *it;
}

Model const* PollingProject::getModelPtr(int modelIndex) const {
	auto it = models.begin();
	for (int i = 0; i < modelIndex; i++) it++;
	return &*it;
}

Model* PollingProject::getModelPtr(int modelIndex) {
	auto it = models.begin();
	for (int i = 0; i < modelIndex; i++) it++;
	return &*it;
}

void PollingProject::removeModel(int modelIndex) {
	auto it = models.begin();
	for (int i = 0; i < modelIndex; i++) it++;
	removeProjectionsFromModel(std::addressof(*it));
	models.erase(it);
}

void PollingProject::extendModel(int modelIndex) {
	int latestMjd = getLatestPollDate();
	if (latestMjd < getModelPtr(modelIndex)->endDate.GetMJD()) return;
	getModelPtr(modelIndex)->endDate = MjdToDate(latestMjd);
}

int PollingProject::getModelCount() const {
	return models.size();
}

std::list<Model>::const_iterator PollingProject::getModelBegin() const {
	return models.begin();
}

std::list<Model>::const_iterator PollingProject::getModelEnd() const {
	return models.end();
}

int PollingProject::getModelIndex(Model const* const modelPtr) {
	int i = 0;
	for (auto it = models.begin(); it != models.end(); it++) {
		if (&*it == modelPtr) return i;
		i++;
	}
	return -1;
}

void PollingProject::addProjection(Projection projection) {
	projections.push_back(projection);
}

void PollingProject::replaceProjection(int projectionIndex, Projection projection) {
	*getProjectionPtr(projectionIndex) = projection;
}

Projection PollingProject::getProjection(int projectionIndex) const {
	auto it = projections.begin();
	for (int i = 0; i < projectionIndex; i++) it++;
	return *it;
}

Projection* PollingProject::getProjectionPtr(int projectionIndex) {
	auto it = projections.begin();
	for (int i = 0; i < projectionIndex; i++) it++;
	return &*it;
}

Projection const* PollingProject::getProjectionPtr(int projectionIndex) const {
	auto it = projections.begin();
	for (int i = 0; i < projectionIndex; i++) it++;
	return &*it;
}

void PollingProject::removeProjection(int projectionIndex) {
	auto it = projections.begin();
	for (int i = 0; i < projectionIndex; i++) it++;
	projections.erase(it);
}

int PollingProject::getProjectionCount() const {
	return projections.size();
}

std::list<Projection>::const_iterator PollingProject::getProjectionBegin() const {
	return projections.begin();
}

std::list<Projection>::const_iterator PollingProject::getProjectionEnd() const {
	return projections.end();
}

int PollingProject::getProjectionIndex(Projection const* const projectionPtr) {
	int i = 0;
	for (auto it = projections.begin(); it != projections.end(); it++) {
		if (&*it == projectionPtr) return i;
		i++;
	}
	return -1;
}

void PollingProject::addRegion(Region region) {
	regions.push_back(region);
	calculateRegionSwingDeviations();
}

void PollingProject::replaceRegion(int regionIndex, Region region) {
	*getRegionPtr(regionIndex) = region;
	calculateRegionSwingDeviations();
}

Region PollingProject::getRegion(int regionIndex) const {
	return *getRegionPtr(regionIndex);
}

Region* PollingProject::getRegionPtr(int regionIndex) {
	auto it = regions.begin();
	for (int i = 0; i < regionIndex; i++) it++;
	return &*it;
}

Region const* PollingProject::getRegionPtr(int regionIndex) const {
	auto it = regions.begin();
	for (int i = 0; i < regionIndex; i++) it++;
	return &*it;
}

void PollingProject::removeRegion(int regionIndex) {
	auto it = regions.begin();
	for (int i = 0; i < regionIndex; i++) it++;
	regions.erase(it);
	calculateRegionSwingDeviations();
}

int PollingProject::getRegionCount() const {
	return regions.size();
}

int PollingProject::getRegionIndex(Region const* const regionPtr) {
	int i = 0;
	for (auto it = regions.begin(); it != regions.end(); it++) {
		if (&*it == regionPtr) return i;
		i++;
	}
	return -1;
}

std::list<Region>::iterator PollingProject::getRegionBegin() {
	return regions.begin();
}

std::list<Region>::iterator PollingProject::getRegionEnd() {
	return regions.end();
}

std::list<Region>::const_iterator PollingProject::getRegionBegin() const {
	return regions.begin();
}

std::list<Region>::const_iterator PollingProject::getRegionEnd() const {
	return regions.end();
}

void PollingProject::calculateRegionSwingDeviations() {
	int totalPopulation = 0;
	float total2pp = 0.0f;
	float totalOld2pp = 0.0f;
	for (Region const& thisRegion : regions) {
		totalPopulation += thisRegion.population;
		total2pp += float(thisRegion.population) * thisRegion.sample2pp;
		totalOld2pp += float(thisRegion.population) * thisRegion.lastElection2pp;
	}
	total2pp /= float(totalPopulation);
	totalOld2pp /= float(totalPopulation);
	for (Region& thisRegion : regions) {
		thisRegion.swingDeviation = (thisRegion.sample2pp - thisRegion.lastElection2pp) - (total2pp - totalOld2pp);
	}
}

void PollingProject::addSeat(Seat seat) {
	seats.push_back(seat);
}

void PollingProject::replaceSeat(int seatIndex, Seat seat) {
	*getSeatPtr(seatIndex) = seat;
}

Seat PollingProject::getSeat(int seatIndex) const {
	auto it = seats.begin();
	for (int i = 0; i < seatIndex; i++) it++;
	return *it;
}

Seat* PollingProject::getSeatPtr(int seatIndex) {
	auto it = seats.begin();
	for (int i = 0; i < seatIndex; i++) it++;
	return &*it;
}

void PollingProject::removeSeat(int seatIndex) {
	auto it = seats.begin();
	for (int i = 0; i < seatIndex; i++) it++;
	seats.erase(it);
}

int PollingProject::getSeatCount() const {
	return seats.size();
}

std::list<Seat>::iterator PollingProject::getSeatBegin() {
	return seats.begin();
}

std::list<Seat>::iterator PollingProject::getSeatEnd() {
	return seats.end();
}

void PollingProject::addSimulation(Simulation simulation) {
	simulations.push_back(simulation);
}

void PollingProject::replaceSimulation(int simulationIndex, Simulation simulation) {
	*getSimulationPtr(simulationIndex) = simulation;
}

Simulation PollingProject::getSimulation(int simulationIndex) const {
	auto it = simulations.begin();
	for (int i = 0; i < simulationIndex; i++) it++;
	return *it;
}

Simulation* PollingProject::getSimulationPtr(int simulationIndex) {
	auto it = simulations.begin();
	for (int i = 0; i < simulationIndex; i++) it++;
	return &*it;
}

Simulation const* PollingProject::getSimulationPtr(int simulationIndex) const {
	auto it = simulations.begin();
	for (int i = 0; i < simulationIndex; i++) it++;
	return &*it;
}

void PollingProject::removeSimulation(int simulationIndex) {
	auto it = simulations.begin();
	for (int i = 0; i < simulationIndex; i++) it++;
	simulations.erase(it);
}

int PollingProject::getSimulationCount() const {
	return simulations.size();
}

std::list<Simulation>::const_iterator PollingProject::getSimulationBegin() const {
	return simulations.begin();
}

std::list<Simulation>::const_iterator PollingProject::getSimulationEnd() const {
	return simulations.end();
}

int PollingProject::save(std::string filename) {
	std::ofstream os = std::ofstream(filename, std::ios_base::trunc);
	os << std::setprecision(12);
	if (!os) return 1;
	os << "#Project" << std::endl;
	os << "name=" << name << std::endl;
	os << "opre=" << othersPreferenceFlow << std::endl;
	os << "#Parties" << std::endl;
	for (Party const& thisParty : parties) {
		os << "@Party" << std::endl;
		os << "name=" << thisParty.name << std::endl;
		os << "pref=" << thisParty.preferenceShare << std::endl;
		os << "abbr=" << thisParty.abbreviation << std::endl;
		os << "cap =" << int(thisParty.countAsParty) << std::endl;
	}
	os << "#Pollsters" << std::endl;
	for (auto it = pollsters.begin(); it != pollsters.end(); ++it) {
		os << "@Pollster" << std::endl;
		os << "name=" << it->name << std::endl;
		os << "weig=" << it->weight << std::endl;
		os << "colr=" << it->colour << std::endl;
		os << "cali=" << int(it->useForCalibration) << std::endl;
		os << "igin=" << int(it->ignoreInitially) << std::endl;
	}
	os << "#Polls" << std::endl;
	for (auto it = polls.begin(); it != polls.end(); ++it) {
		os << "@Poll" << std::endl;
		os << "poll=" << getPollsterIndex(it->pollster) << std::endl;
		os << "year=" << it->date.GetYear() << std::endl;
		os << "mont=" << it->date.GetMonth() << std::endl;
		os << "day =" << it->date.GetDay() << std::endl;
		os << "prev=" << it->reported2pp << std::endl;
		os << "resp=" << it->respondent2pp << std::endl;
		os << "calc=" << it->calc2pp << std::endl;
		for (int i = 0; i < getPartyCount(); i++) {
			os << "py" << (i<10 ? "0" : "") << i << "=" << it->primary[i] << std::endl;
		}
		os << "py15=" << it->primary[15] << std::endl;
	}
	os << "#Events" << std::endl;
	for (auto const& thisEvent : events) {
		os << "@Event" << std::endl;
		os << "name=" << thisEvent.name << std::endl;
		os << "type=" << thisEvent.eventType << std::endl;
		os << "date=" << thisEvent.date.GetJulianDayNumber() << std::endl;
		os << "vote=" << thisEvent.vote << std::endl;
	}
	os << "#Models" << std::endl;
	for (auto const& thisModel : models) {
		os << "@Model" << std::endl;
		os << "name=" << thisModel.name << std::endl;
		os << "iter=" << thisModel.numIterations << std::endl;
		os << "trnd=" << thisModel.trendTimeScoreMultiplier << std::endl;
		os << "hsm =" << thisModel.houseEffectTimeScoreMultiplier << std::endl;
		os << "cfpb=" << thisModel.calibrationFirstPartyBias << std::endl;
		os << "strt=" << thisModel.startDate.GetJulianDayNumber() << std::endl;
		os << "end =" << thisModel.endDate.GetJulianDayNumber() << std::endl;
		os << "updt=" << thisModel.lastUpdated.GetJulianDayNumber() << std::endl;
		for (auto const& thisDay : thisModel.day) {
			os << "$Day" << std::endl;
			os << "mtnd=" << thisDay.trend2pp << std::endl;
			for (int pollsterIndex = 0; pollsterIndex < int(thisDay.houseEffect.size()); ++pollsterIndex) {
				os << "he" << pollsterIndex << (pollsterIndex < 10 ? " " : "") << "=" <<
					thisDay.houseEffect[pollsterIndex] << std::endl;
			}
		}
	}
	os << "#Projections" << std::endl;
	for (auto const& thisProjection : projections) {
		os << "@Projection" << std::endl;
		os << "name=" << thisProjection.name << std::endl;
		os << "iter=" << thisProjection.numIterations << std::endl;
		os << "base=" << getModelIndex(thisProjection.baseModel) << std::endl;
		os << "end =" << thisProjection.endDate.GetJulianDayNumber() << std::endl;
		os << "updt=" << thisProjection.lastUpdated.GetJulianDayNumber() << std::endl;
		os << "dlyc=" << thisProjection.dailyChange << std::endl;
		os << "inic=" << thisProjection.initialStdDev << std::endl;
		os << "vtls=" << thisProjection.leaderVoteLoss << std::endl;
		os << "nele=" << thisProjection.numElections << std::endl;
		for (int dayIndex = 0; dayIndex < int(thisProjection.meanProjection.size()); ++dayIndex) {
			os << "mean=" << thisProjection.meanProjection[dayIndex] << std::endl;
			os << "stdv=" << thisProjection.sdProjection[dayIndex] << std::endl;
		}
	}
	os << "#Regions" << std::endl;
	for (auto const& thisRegion : regions) {
		os << "@Region" << std::endl;
		os << "name=" << thisRegion.name << std::endl;
		os << "popn=" << thisRegion.population << std::endl;
		os << "lele=" << thisRegion.lastElection2pp << std::endl;
		os << "samp=" << thisRegion.sample2pp << std::endl;
		os << "swng=" << thisRegion.swingDeviation << std::endl;
		os << "addu=" << thisRegion.additionalUncertainty << std::endl;
	}
	os << "#Seats" << std::endl;
	for (auto const& thisSeat : seats) {
		os << "@Seat" << std::endl;
		os << "name=" << thisSeat.name << std::endl;
		os << "incu=" << getPartyIndex(thisSeat.incumbent) << std::endl;
		os << "chal=" << getPartyIndex(thisSeat.challenger) << std::endl;
		os << "regn=" << getRegionIndex(thisSeat.region) << std::endl;
		os << "marg=" << thisSeat.margin << std::endl;
		os << "lmod=" << thisSeat.localModifier << std::endl;
		os << "iodd=" << thisSeat.incumbentOdds << std::endl;
		os << "codd=" << thisSeat.challengerOdds << std::endl;
		os << "prjm=" << thisSeat.projectedMargin << std::endl;
		os << "winp=" << thisSeat.incumbentWinPercent << std::endl;
		os << "tipp=" << thisSeat.tippingPointPercent << std::endl;
	}
	os << "#Simulations" << std::endl;
	for (auto const& thisSimulation : simulations) {
		os << "@Simulation" << std::endl;
		os << "name=" << thisSimulation.name << std::endl;
		os << "iter=" << thisSimulation.numIterations << std::endl;
		os << "base=" << getProjectionIndex(thisSimulation.baseProjection) << std::endl;
		os << "prev=" << thisSimulation.prevElection2pp << std::endl;
		os << "stsd=" << thisSimulation.stateSD << std::endl;
		os << "stde=" << thisSimulation.stateDecay << std::endl;
	}
	os << "#End";
	os.close();
	return 0; // success
}

bool PollingProject::isValid() {
	return valid;
}

void PollingProject::recalculatePollCalc2PP(Poll& poll) const {
	int nParties = getPartyCount();
	float sum2PP = 0.0f;
	float sumPrimaries = 0.0f;
	for (int i = 0; i < nParties; i++) {
		if (poll.primary[i] < 0) continue;
		sum2PP += poll.primary[i] * getParty(i).preferenceShare;
		sumPrimaries += poll.primary[i];
	}
	if (poll.primary[15] > 0) {
		sum2PP += poll.primary[15] * othersPreferenceFlow;
		sumPrimaries += poll.primary[15];
	}
	poll.calc2pp = sum2PP / sumPrimaries + 0.14f; // the last 0.14f accounts for
												  // leakage in Lib-Nat contests
}

void PollingProject::open(std::string filename) {
	valid = true;
	std::ifstream is = std::ifstream(filename);
	if (!is) { valid = false; return; }

	FileOpeningState fos;

	while (is) {
		std::string s;
		std::getline(is, s);
		if(!processFileLine(s, fos)) break;
	}

	is.close();

	finalizeFileLoading();
}

bool PollingProject::processFileLine(std::string line, FileOpeningState& fos) {

	// Section changes
	if (!line.compare("#Project")) {
		fos.section = FileSection_Project;
		return true;
	}
	else if (!line.compare("#Parties")) {
		fos.section = FileSection_Parties;
		return true;
	}
	else if (!line.compare("#Pollsters")) {
		fos.section = FileSection_Pollsters;
		return true;
	}
	else if (!line.compare("#Polls")) {
		fos.section = FileSection_Polls;
		return true;
	}
	else if (!line.compare("#Events")) {
		fos.section = FileSection_Events;
		return true;
	}
	else if (!line.compare("#Models")) {
		fos.section = FileSection_Models;
		return true;
	}
	else if (!line.compare("#Projections")) {
		fos.section = FileSection_Projections;
		return true;
	}
	else if (!line.compare("#Regions")) {
		fos.section = FileSection_Regions;
		return true;
	}
	else if (!line.compare("#Seats")) {
		fos.section = FileSection_Seats;
		return true;
	}
	else if (!line.compare("#Simulations")) {
		fos.section = FileSection_Simulations;
		return true;
	}
	else if (!line.compare("#End")) {
		return false;
	}

	// New item changes
	if (fos.section == FileSection_Parties) {
		if (!line.compare("@Party")) {
			parties.push_back(Party());
			return true;
		}
	}
	else if (fos.section == FileSection_Pollsters) {
		if (!line.compare("@Pollster")) {
			pollsters.push_back(Pollster());
			return true;
		}
	}
	else if (fos.section == FileSection_Polls) {
		if (!line.compare("@Poll")) {
			polls.push_back(Poll());
			return true;
		}
	}
	else if (fos.section == FileSection_Events) {
		if (!line.compare("@Event")) {
			events.push_back(Event());
			return true;
		}
	}
	else if (fos.section == FileSection_Models) {
		if (!line.compare("@Model")) {
			models.push_back(Model());
			return true;
		}
	}
	else if (fos.section == FileSection_Projections) {
		if (!line.compare("@Projection")) {
			projections.push_back(Projection());
			return true;
		}
	}
	else if (fos.section == FileSection_Regions) {
		if (!line.compare("@Region")) {
			regions.push_back(Region());
			return true;
		}
	}
	else if (fos.section == FileSection_Seats) {
		if (!line.compare("@Seat")) {
			seats.push_back(Seat());
			return true;
		}
	}
	else if (fos.section == FileSection_Simulations) {
		if (!line.compare("@Simulation")) {
			simulations.push_back(Simulation());
			return true;
		}
	}

	// Values
	if (fos.section == FileSection_Project) {
		if (!line.substr(0, 5).compare("name=")) {
			name = line.substr(5);
			return true;
		}
		if (!line.substr(0, 5).compare("opre=")) {
			othersPreferenceFlow = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Parties) {
		if (!parties.size()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			parties.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("pref=")) {
			parties.back().preferenceShare = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("abbr=")) {
			parties.back().abbreviation = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("cap =")) {
			parties.back().countAsParty = Party::CountAsParty(std::stoi(line.substr(5)));
			return true;
		}
	}
	else if (fos.section == FileSection_Pollsters) {
		if (!pollsters.size()) return true; //prevent crash from mixed-up data.
		auto it = pollsters.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("weig=")) {
			it->weight = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colr=")) {
			it->colour = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cali=")) {
			it->useForCalibration = std::stoi(line.substr(5)) != 0;
			return true;
		}
		else if (!line.substr(0, 5).compare("igin=")) {
			it->ignoreInitially = std::stoi(line.substr(5)) != 0;
			return true;
		}
	}
	else if (fos.section == FileSection_Polls) {
		if (!polls.size()) return true; //prevent crash from mixed-up data.
		auto it = polls.end();
		it--;
		if (!line.substr(0, 5).compare("poll=")) {
			it->pollster = getPollsterPtr(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("year=")) {
			it->date.SetYear(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("mont=")) {
			it->date.SetMonth((wxDateTime::Month)std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("day =")) {
			it->date.SetDay(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("prev=")) {
			it->reported2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("resp=")) {
			it->respondent2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("calc=")) {
			it->calc2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 2).compare("py")) {
			int primaryIndex = std::stoi(line.substr(2, 2));
			it->primary[primaryIndex] = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Events) {
		if (!events.size()) return true; //prevent crash from mixed-up data.
		auto it = events.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("type=")) {
			it->eventType = EventType(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("date=")) {
			it->date = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("vote=")) {
			it->vote = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Models) {
		if (!models.size()) return true; //prevent crash from mixed-up data.
		auto it = models.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			it->numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("trnd=")) {
			it->trendTimeScoreMultiplier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("hsm =")) {
			it->houseEffectTimeScoreMultiplier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cfpb=")) {
			it->calibrationFirstPartyBias = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("strt=")) {
			it->startDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("end =")) {
			it->endDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("updt=")) {
			it->lastUpdated = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 4).compare("$Day")) {
			it->day.push_back(ModelTimePoint(getPollsterCount()));
			return true;
		}
		else if (!line.substr(0, 5).compare("mtnd=")) {
			if (!it->day.size()) return true;
			it->day.back().trend2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 2).compare("he") && !line.substr(4, 1).compare("=")) {
			int pollsterIndex = std::stoi(line.substr(2, 2));
			if (pollsterIndex < getPollsterCount()) {
				it->day.back().houseEffect[pollsterIndex] = std::stof(line.substr(5));
			}
		}
	}
	else if (fos.section == FileSection_Projections) {
		if (!projections.size()) return true; //prevent crash from mixed-up data.
		auto it = projections.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			it->numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("base=")) {
			it->baseModel = getModelPtr(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("end =")) {
			it->endDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("updt=")) {
			it->lastUpdated = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("dlyc=")) {
			it->dailyChange = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("inic=")) {
			it->initialStdDev = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("vtls=")) {
			it->leaderVoteLoss = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("nele=")) {
			it->numElections = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("mean=")) {
			it->meanProjection.push_back(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("stdv=")) {
			it->sdProjection.push_back(std::stod(line.substr(5)));
			return true;
		}
	}
	else if (fos.section == FileSection_Regions) {
		if (!regions.size()) return true; //prevent crash from mixed-up data.
		auto it = regions.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("popn=")) {
			it->population = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lele=")) {
			it->lastElection2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("samp=")) {
			it->sample2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("swng=")) {
			it->swingDeviation = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("addu=")) {
			it->additionalUncertainty = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Seats) {
		if (!seats.size()) return true; //prevent crash from mixed-up data.
		auto it = seats.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("incu=")) {
			it->incumbent = getPartyPtr(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("chal=")) {
			it->challenger = getPartyPtr(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("regn=")) {
			it->region = getRegionPtr(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("marg=")) {
			it->margin = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lmod=")) {
			it->localModifier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("iodd=")) {
			it->incumbentOdds = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("codd=")) {
			it->challengerOdds = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("prjm=")) {
			it->projectedMargin = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("winp=")) {
			it->incumbentWinPercent = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("tipp=")) {
			it->tippingPointPercent = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Simulations) {
		if (!simulations.size()) return true; //prevent crash from mixed-up data.
		auto it = simulations.end();
		it--;
		if (!line.substr(0, 5).compare("name=")) {
			it->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			it->numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("base=")) {
			it->baseProjection = getProjectionPtr(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("prev=")) {
			it->prevElection2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("stsd=")) {
			it->stateSD = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("stde=")) {
			it->stateDecay = std::stof(line.substr(5));
			return true;
		}
	}

	return true;
}

void PollingProject::adjustPollsAfterPartyRemoval(int partyIndex) {

	// This is the new party count after the party was already removed
	int partyCount = getPartyCount();

	int pollCount = getPollCount();

	for (int i = 0; i < pollCount; i++) {
		Poll* poll = &polls[i];
		for (int j = partyIndex; j < partyCount; j++)
			poll->primary[j] = poll->primary[j + 1];
		poll->primary[partyCount] = -1;
	}
}

void PollingProject::removePollsFromPollster(Pollster const* pollster) {
	for (int i = 0; i < getPollCount(); i++) {
		Poll* poll = &polls[i];
		if (poll->pollster == pollster) { removePoll(i); i--; }
	}
}

void PollingProject::removeProjectionsFromModel(Model const* model) {
	for (int i = 0; i < getProjectionCount(); i++) {
		Projection* projection = getProjectionPtr(i);
		if (projection->baseModel == model) { removeProjection(i); i--; }
	}
}

void PollingProject::invalidateProjectionsFromModel(Model const* model) {
	for (auto& thisProjection : projections) {
		if (thisProjection.baseModel == model) { thisProjection.lastUpdated = wxInvalidDateTime; }
	}
}

void PollingProject::finalizeFileLoading() {
	// sets the correct effective start/end dates
	for (auto& model : models) {
		model.updateEffectiveDates(MjdToDate(getEarliestPollDate()), MjdToDate(getLatestPollDate()));
	}

	// Set the two major parties, in case this file comes from a version in which "count-as-party" data was not recorded
	auto thisParty = parties.begin();
	thisParty->countAsParty = Party::CountAsParty::IsPartyOne;
	thisParty->countAsParty = Party::CountAsParty::IsPartyTwo;

	setVisualiserBounds(visStartDay, visEndDay);
}