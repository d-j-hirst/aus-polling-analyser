#include "PollingProject.h"

#include "LatestResultsDataRetriever.h"
#include "Log.h"
#include "PreloadDataRetriever.h"
#include "PreviousElectionDataRetriever.h"

#include <iomanip>
#include <algorithm>

const Party PollingProject::invalidParty = Party("Invalid", 50.0f, 0.0f, "INV", Party::CountAsParty::None);

PollingProject::PollingProject(NewProjectData& newProjectData) :
	name(newProjectData.projectName),
	lastFileName(newProjectData.projectName + ".pol"),
	valid(true),
	partyCollection(*this),
	pollsterCollection(*this),
	pollCollection(*this),
	eventCollection(*this),
	modelCollection(*this),
	projectionCollection(*this),
	regionCollection(*this),
	seatCollection(*this),
	simulationCollection(*this),
	resultCoordinator(*this)
{
	// The project must always have at least two partyCollection, no matter what. This initializes them with default values.
	partyCollection.add(Party("Labor", 100, 0.0f, "ALP", Party::CountAsParty::IsPartyOne));
	partyCollection.add(Party("Liberals", 0, 0.0f, "LIB", Party::CountAsParty::IsPartyTwo));

	pollsterCollection.add(Pollster("Default Pollster", 1.0f, 0, true, false));
}

PollingProject::PollingProject(std::string pathName) :
	lastFileName(pathName.substr(pathName.rfind("\\")+1)),
	partyCollection(*this),
	pollsterCollection(*this),
	pollCollection(*this),
	eventCollection(*this),
	modelCollection(*this),
	projectionCollection(*this),
	regionCollection(*this),
	seatCollection(*this),
	simulationCollection(*this),
	resultCoordinator(*this)
{
	logger << lastFileName << "\n";
	open(pathName);
}


void PollingProject::refreshCalc2PP() {
	for (auto it = polls().begin(); it != polls().end(); it++)
		partyCollection.recalculatePollCalc2PP(it->second);
}

void PollingProject::adjustAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId)
{
	polls().adjustAfterPartyRemoval(partyIndex, partyId);
	adjustSeatsAfterPartyRemoval(partyIndex, partyId);
	resultCoordinator.adjustAffiliationsAfterPartyRemoval(partyIndex, partyId);
	resultCoordinator.adjustCandidatesAfterPartyRemoval(partyIndex, partyId);
}

void PollingProject::adjustAfterPollsterRemoval(PollsterCollection::Index /*pollsterIndex*/, Party::Id pollsterId)
{
	polls().removePollsFromPollster(pollsterId);
}

int PollingProject::getEarliestDate() const {
	int earliestDay = polls().getEarliestDate();
	return earliestDay;
}

int PollingProject::getLatestDate() const {
	int latestDay = polls().getLatestDate();
	for (auto const& [key, projection] : projections()) {
		int date = int(floor(projection.getSettings().endDate.GetModifiedJulianDayNumber()));
		if (date > latestDay) latestDay = date;
	}
	return latestDay;
}

void PollingProject::adjustAfterModelRemoval(ModelCollection::Index, Model::Id modelId)
{
	removeProjectionsFromModel(modelId);
}

void PollingProject::adjustAfterProjectionRemoval(ProjectionCollection::Index, Projection::Id projectionId)
{
	removeSimulationsFromProjection(projectionId);
}

void PollingProject::adjustAfterRegionRemoval(RegionCollection::Index regionIndex, Region::Id regionId)
{
	adjustSeatsAfterRegionRemoval(regionIndex, regionId);
}

void PollingProject::addResult(Result result)
{
	resultList.push_front(result);
}

Result PollingProject::getResult(int resultIndex) const
{
	auto it = resultList.begin();
	std::advance(it, resultIndex);
	return *it;
}

int PollingProject::getResultCount() const
{
	return resultList.size();
}

std::list<Result>::iterator PollingProject::getResultBegin()
{
	return resultList.begin();
}

std::list<Result>::iterator PollingProject::getResultEnd()
{
	return resultList.end();
}

void PollingProject::updateLatestResultsForSeats() {
	for (auto& thisResult : resultList) {
		auto& latestResult = seats().access(thisResult.seat).latestResult;
		if (!latestResult) latestResult = &thisResult;
		else if (latestResult->updateTime < thisResult.updateTime) latestResult = &thisResult;
	}
}

int PollingProject::save(std::string filename) {
	std::ofstream os = std::ofstream(filename, std::ios_base::trunc);
	os << std::setprecision(12);
	if (!os) return 1;
	os << "#Project" << "\n";
	os << "name=" << name << "\n";
	os << "opre=" << parties().getOthersPreferenceFlow() << "\n";
	os << "oexh=" << parties().getOthersExhaustRate() << "\n";
	os << "#Parties" << "\n";
	for (auto const& [key, thisParty] : partyCollection) {
		os << "@Party" << "\n";
		os << "name=" << thisParty.name << "\n";
		os << "pref=" << thisParty.preferenceShare << "\n";
		os << "exha=" << thisParty.exhaustRate << "\n";
		os << "abbr=" << thisParty.abbreviation << "\n";
		os << "cap =" << int(thisParty.countAsParty) << "\n";
		os << "supp=" << int(thisParty.supportsParty) << "\n";
		os << "bcmt=" << thisParty.boothColourMult << "\n";
		os << "ideo=" << thisParty.ideology << "\n";
		os << "cons=" << thisParty.consistency << "\n";
		for (std::string officialCode : thisParty.officialCodes) {
			os << "code=" << officialCode << "\n";
		}
		os << "colr=" << thisParty.colour.r << "\n";
		os << "colg=" << thisParty.colour.g << "\n";
		os << "colb=" << thisParty.colour.b << "\n";
	}
	os << "#Pollsters" << "\n";
	for (auto const& [key, thisPollster]: pollsterCollection) {
		os << "@Pollster" << "\n";
		os << "name=" << thisPollster.name << "\n";
		os << "weig=" << thisPollster.weight << "\n";
		os << "colr=" << thisPollster.colour << "\n";
		os << "cali=" << int(thisPollster.useForCalibration) << "\n";
		os << "igin=" << int(thisPollster.ignoreInitially) << "\n";
	}
	os << "#Polls" << "\n";
	for (auto const& [key, thisPoll] : pollCollection) {
		os << "@Poll" << "\n";
		os << "poll=" << pollsters().idToIndex(thisPoll.pollster) << "\n";
		os << "year=" << thisPoll.date.GetYear() << "\n";
		os << "mont=" << thisPoll.date.GetMonth() << "\n";
		os << "day =" << thisPoll.date.GetDay() << "\n";
		os << "prev=" << thisPoll.reported2pp << "\n";
		os << "resp=" << thisPoll.respondent2pp << "\n";
		os << "calc=" << thisPoll.calc2pp << "\n";
		for (int i = 0; i < partyCollection.count(); i++) {
			os << "py" << (i<10 ? "0" : "") << i << "=" << thisPoll.primary[i] << "\n";
		}
		os << "py15=" << thisPoll.primary[PartyCollection::MaxParties] << "\n";
	}
	os << "#Events" << "\n";
	for (auto const& [key, thisEvent]: eventCollection) {
		os << "@Event" << "\n";
		os << "name=" << thisEvent.name << "\n";
		os << "type=" << thisEvent.eventType << "\n";
		os << "date=" << thisEvent.date.GetJulianDayNumber() << "\n";
		os << "vote=" << thisEvent.vote << "\n";
	}
	os << "#Models" << "\n";
	for (auto const& [key, thisModel] : modelCollection) {
		os << "@Model" << "\n";
		os << "name=" << thisModel.getSettings().name << "\n";
		os << "iter=" << thisModel.getSettings().numIterations << "\n";
		os << "trnd=" << thisModel.getSettings().trendTimeScoreMultiplier << "\n";
		os << "hsm =" << thisModel.getSettings().houseEffectTimeScoreMultiplier << "\n";
		os << "cfpb=" << thisModel.getSettings().calibrationFirstPartyBias << "\n";
		os << "fstd=" << thisModel.getFinalStandardDeviation() << "\n";
		os << "strt=" << thisModel.getSettings().startDate.GetJulianDayNumber() << "\n";
		os << "end =" << thisModel.getSettings().endDate.GetJulianDayNumber() << "\n";
		os << "updt=" << thisModel.getLastUpdatedDate().GetJulianDayNumber() << "\n";
		for (auto const& thisDay : thisModel) {
			os << "$Day" << "\n";
			os << "mtnd=" << thisDay.trend2pp << "\n";
		}
	}
	os << "#Projections" << "\n";
	for (auto const& [key, thisProjection] : projectionCollection) {
		os << "@Projection" << "\n";
		os << "name=" << thisProjection.getSettings().name << "\n";
		os << "iter=" << thisProjection.getSettings().numIterations << "\n";
		os << "base=" << models().idToIndex(thisProjection.getSettings().baseModel) << "\n";
		os << "end =" << thisProjection.getSettings().endDate.GetJulianDayNumber() << "\n";
		os << "updt=" << thisProjection.getLastUpdatedDate().GetJulianDayNumber() << "\n";
		os << "dlyc=" << thisProjection.getSettings().dailyChange << "\n";
		os << "inic=" << thisProjection.getSettings().initialStdDev << "\n";
		os << "vtls=" << thisProjection.getSettings().leaderVoteDecay << "\n";
		os << "nele=" << thisProjection.getSettings().numElections << "\n";
		for (int dayIndex = 0; dayIndex < int(thisProjection.getProjectionLength()); ++dayIndex) {
			os << "mean=" << thisProjection.getMeanProjection(dayIndex) << "\n";
			os << "stdv=" << thisProjection.getSdProjection(dayIndex) << "\n";
		}
	}
	os << "#Regions" << "\n";
	for (auto const& [key, thisRegion] : regionCollection) {
		os << "@Region" << "\n";
		os << "name=" << thisRegion.name << "\n";
		os << "popn=" << thisRegion.population << "\n";
		os << "lele=" << thisRegion.lastElection2pp << "\n";
		os << "samp=" << thisRegion.sample2pp << "\n";
		os << "swng=" << thisRegion.swingDeviation << "\n";
		os << "addu=" << thisRegion.additionalUncertainty << "\n";
	}
	os << "#Seats" << "\n";
	for (auto const& [key, thisSeat] : seatCollection) {
		os << "@Seat" << "\n";
		os << "name=" << thisSeat.name << "\n";
		os << "pvnm=" << thisSeat.previousName << "\n";
		os << "incu=" << partyCollection.idToIndex(thisSeat.incumbent) << "\n";
		os << "chal=" << partyCollection.idToIndex(thisSeat.challenger) << "\n";
		os << "cha2=" << partyCollection.idToIndex(thisSeat.challenger2) << "\n";
		os << "regn=" << regions().idToIndex(thisSeat.region) << "\n";
		os << "marg=" << thisSeat.margin << "\n";
		os << "lmod=" << thisSeat.localModifier << "\n";
		os << "iodd=" << thisSeat.incumbentOdds << "\n";
		os << "codd=" << thisSeat.challengerOdds << "\n";
		os << "c2od=" << thisSeat.challenger2Odds << "\n";
		os << "winp=" << thisSeat.incumbentWinPercent << "\n";
		os << "tipp=" << thisSeat.tippingPointPercent << "\n";
		os << "sma =" << thisSeat.simulatedMarginAverage << "\n";
		os << "lp1 =" << partyCollection.idToIndex(thisSeat.livePartyOne) << "\n";
		os << "lp2 =" << partyCollection.idToIndex(thisSeat.livePartyTwo) << "\n";
		os << "lp3 =" << partyCollection.idToIndex(thisSeat.livePartyThree) << "\n";
		os << "p2pr=" << thisSeat.partyTwoProb << "\n";
		os << "p3pr=" << thisSeat.partyThreeProb << "\n";
		os << "over=" << int(thisSeat.overrideBettingOdds) << "\n";
	}
	os << "#Simulations" << "\n";
	for (auto const& [key, thisSimulation] : simulationCollection) {
		os << "@Simulation" << "\n";
		os << "name=" << thisSimulation.getSettings().name << "\n";
		os << "iter=" << thisSimulation.getSettings().numIterations << "\n";
		os << "base=" << projections().idToIndex(thisSimulation.getSettings().baseProjection) << "\n";
		os << "prev=" << thisSimulation.getSettings().prevElection2pp << "\n";
		os << "stsd=" << thisSimulation.getSettings().stateSD << "\n";
		os << "stde=" << thisSimulation.getSettings().stateDecay << "\n";
		os << "live=" << int(thisSimulation.getSettings().live) << "\n";
	}
	os << "#Results" << "\n";
	for (auto const& thisResult : resultList) {
		os << "@Result" << "\n";
		os << "seat=" << seats().idToIndex(thisResult.seat) << "\n";
		os << "swng=" << thisResult.incumbentSwing << "\n";
		os << "cnt =" << thisResult.percentCounted << "\n";
		os << "btin=" << thisResult.boothsIn << "\n";
		os << "btto=" << thisResult.totalBooths << "\n";
		os << "updt=" << thisResult.updateTime.GetJulianDayNumber() << "\n";
	}
	os << "#End";
	os.close();
	return 0; // success
}

bool PollingProject::isValid() {
	return valid;
}

void PollingProject::invalidateProjectionsFromModel(Model::Id modelId) {
	for (auto& [key, projection] : projections()) {
		if (projection.getSettings().baseModel == modelId) projection.invalidate();
	}
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

	resultList.clear(); // *** remove!

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
		modelCollection.finaliseLoadedModel();
		fos.section = FileSection_Projections;
		return true;
	}
	else if (!line.compare("#Regions")) {
		projectionCollection.finaliseLoadedProjection();
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
	else if (!line.compare("#Results")) {
		simulationCollection.finaliseLoadedSimulation();
		fos.section = FileSection_Results;
		return true;
	}
	else if (!line.compare("#End")) {
		return false;
	}

	// New item changes
	if (fos.section == FileSection_Parties) {
		if (!line.compare("@Party")) {
			partyCollection.add(Party());
			return true;
		}
	}
	else if (fos.section == FileSection_Pollsters) {
		if (!line.compare("@Pollster")) {
			pollsterCollection.add(Pollster());
			return true;
		}
	}
	else if (fos.section == FileSection_Polls) {
		if (!line.compare("@Poll")) {
			pollCollection.add(Poll());
			// Avoid issues with temporarily setting a date to 30th February and the like.
			pollCollection.back().date.SetDay(1);
			return true;
		}
	}
	else if (fos.section == FileSection_Events) {
		if (!line.compare("@Event")) {
			eventCollection.add(Event());
			return true;
		}
	}
	else if (fos.section == FileSection_Models) {
		if (!line.compare("@Model")) {
			modelCollection.finaliseLoadedModel();
			modelCollection.startLoadingModel();
			return true;
		}
	}
	else if (fos.section == FileSection_Projections) {
		if (!line.compare("@Projection")) {
			projectionCollection.finaliseLoadedProjection();
			projectionCollection.startLoadingProjection();
			return true;
		}
	}
	else if (fos.section == FileSection_Regions) {
		if (!line.compare("@Region")) {
			regionCollection.add(Region());
			return true;
		}
	}
	else if (fos.section == FileSection_Seats) {
		if (!line.compare("@Seat")) {
			seatCollection.add(Seat());
			return true;
		}
	}
	else if (fos.section == FileSection_Simulations) {
		if (!line.compare("@Simulation")) {
			simulationCollection.finaliseLoadedSimulation();
			simulationCollection.startLoadingSimulation();
			return true;
		}
	}
	else if (fos.section == FileSection_Results) {
		if (!line.compare("@Result")) {
			resultList.push_back(Result());
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
			parties().setOthersPreferenceFlow(std::stof(line.substr(5)));
			return true;
		}
		if (!line.substr(0, 5).compare("oexh=")) {
			parties().setOthersExhaustRate(std::stof(line.substr(5)));
			return true;
		}
	}
	else if (fos.section == FileSection_Parties) {
		if (!partyCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			partyCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("pref=")) {
			partyCollection.back().preferenceShare = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("exha=")) {
			partyCollection.back().exhaustRate = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("abbr=")) {
			partyCollection.back().abbreviation = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("cap =")) {
			partyCollection.back().countAsParty = Party::CountAsParty(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("supp=")) {
			partyCollection.back().supportsParty = Party::SupportsParty(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("ideo=")) {
			partyCollection.back().ideology = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cons=")) {
			partyCollection.back().consistency = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("bcmt=")) {
			partyCollection.back().boothColourMult = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("code=")) {
			partyCollection.back().officialCodes.push_back(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colr=")) {
			partyCollection.back().colour.r = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colg=")) {
			partyCollection.back().colour.g = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colb=")) {
			partyCollection.back().colour.b = std::stoi(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Pollsters) {
		if (!pollsterCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			pollsterCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("weig=")) {
			pollsterCollection.back().weight = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colr=")) {
			pollsterCollection.back().colour = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cali=")) {
			pollsterCollection.back().useForCalibration = std::stoi(line.substr(5)) != 0;
			return true;
		}
		else if (!line.substr(0, 5).compare("igin=")) {
			pollsterCollection.back().ignoreInitially = std::stoi(line.substr(5)) != 0;
			return true;
		}
	}
	else if (fos.section == FileSection_Polls) {
		if (!pollCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("poll=")) {
			if (pollCollection.count() == 54) logger << line;
			pollCollection.back().pollster = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("year=")) {
			if (pollCollection.count() == 54) logger << line;
			pollCollection.back().date.SetYear(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("mont=")) {
			pollCollection.back().date.SetMonth((wxDateTime::Month)std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("day =")) {
			if (pollCollection.count() == 54) logger << line;
			pollCollection.back().date.SetDay(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("prev=")) {
			pollCollection.back().reported2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("resp=")) {
			pollCollection.back().respondent2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("calc=")) {
			pollCollection.back().calc2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 2).compare("py")) {
			int primaryIndex = std::stoi(line.substr(2, 2));
			pollCollection.back().primary[primaryIndex] = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Events) {
		if (!eventCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			eventCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("type=")) {
			eventCollection.back().eventType = EventType(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("date=")) {
			eventCollection.back().date = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("vote=")) {
			eventCollection.back().vote = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Models) {
		if (!modelCollection.loadingModel.has_value()) return true;
		if (!line.substr(0, 5).compare("name=")) {
			modelCollection.loadingModel->settings.name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			modelCollection.loadingModel->settings.numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("trnd=")) {
			modelCollection.loadingModel->settings.trendTimeScoreMultiplier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("hsm =")) {
			modelCollection.loadingModel->settings.houseEffectTimeScoreMultiplier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cfpb=")) {
			modelCollection.loadingModel->settings.calibrationFirstPartyBias = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("fstd=")) {
			modelCollection.loadingModel->finalStandardDeviation = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("strt=")) {
			modelCollection.loadingModel->settings.startDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("end =")) {
			modelCollection.loadingModel->settings.endDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("updt=")) {
			modelCollection.loadingModel->lastUpdated = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 4).compare("$Day")) {
			modelCollection.loadingModel->trend.push_back(50.0f);
			return true;
		}
		else if (!line.substr(0, 5).compare("mtnd=")) {
			if (!modelCollection.loadingModel->trend.size()) return true;
			modelCollection.loadingModel->trend.back() = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Projections) {
		if (!projectionCollection.loadingProjection.has_value()) return true;
		if (!line.substr(0, 5).compare("name=")) {
			projectionCollection.loadingProjection->settings.name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			projectionCollection.loadingProjection->settings.numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("base=")) {
			projectionCollection.loadingProjection->settings.baseModel = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("end =")) {
			projectionCollection.loadingProjection->settings.endDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("updt=")) {
			projectionCollection.loadingProjection->lastUpdated = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("dlyc=")) {
			projectionCollection.loadingProjection->settings.dailyChange = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("inic=")) {
			projectionCollection.loadingProjection->settings.initialStdDev = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("vtls=")) {
			projectionCollection.loadingProjection->settings.leaderVoteDecay = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("nele=")) {
			projectionCollection.loadingProjection->settings.numElections = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("mean=")) {
			double mean = std::stod(line.substr(5));
			projectionCollection.loadingProjection->projection.push_back({ mean, 0 });
			return true;
		}
		else if (!line.substr(0, 5).compare("stdv=")) {
			double sd = std::stod(line.substr(5));
			projectionCollection.loadingProjection->projection.back().sd = sd;
			return true;
		}
	}
	else if (fos.section == FileSection_Regions) {
		if (!regionCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			regionCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("popn=")) {
			regionCollection.back().population = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lele=")) {
			regionCollection.back().lastElection2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("samp=")) {
			regionCollection.back().sample2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("swng=")) {
			regionCollection.back().swingDeviation = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("addu=")) {
			regionCollection.back().additionalUncertainty = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Seats) {
		if (!seatCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			seatCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("pvnm=")) {
			seatCollection.back().previousName = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("incu=")) {
			seatCollection.back().incumbent = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("chal=")) {
			seatCollection.back().challenger = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cha2=")) {
			seatCollection.back().challenger2 = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("regn=")) {
			seatCollection.back().region = regions().indexToId(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("marg=")) {
			seatCollection.back().margin = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lmod=")) {
			seatCollection.back().localModifier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("iodd=")) {
			seatCollection.back().incumbentOdds = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("codd=")) {
			seatCollection.back().challengerOdds = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("c2od=")) {
			seatCollection.back().challenger2Odds = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("winp=")) {
			seatCollection.back().incumbentWinPercent = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("tipp=")) {
			seatCollection.back().tippingPointPercent = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("sma =")) {
			seatCollection.back().simulatedMarginAverage = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lp1 =")) {
			seatCollection.back().livePartyOne = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lp2 =")) {
			seatCollection.back().livePartyTwo = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lp3 =")) {
			seatCollection.back().livePartyThree = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("p2pr=")) {
			seatCollection.back().partyTwoProb = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("p3pr=")) {
			seatCollection.back().partyThreeProb = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("over=")) {
			seatCollection.back().overrideBettingOdds = std::stoi(line.substr(5)) != 0;
			return true;
		}
	}
	else if (fos.section == FileSection_Simulations) {
		if (!simulationCollection.loadingSimulation.has_value()) return true;
		if (!line.substr(0, 5).compare("name=")) {
			simulationCollection.loadingSimulation->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			simulationCollection.loadingSimulation->numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("base=")) {
			simulationCollection.loadingSimulation->baseProjection = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("prev=")) {
			simulationCollection.loadingSimulation->prevElection2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("stsd=")) {
			simulationCollection.loadingSimulation->stateSD = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("stde=")) {
			simulationCollection.loadingSimulation->stateDecay = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("live=")) {
			simulationCollection.loadingSimulation->live = Simulation::Settings::Mode(std::stoi(line.substr(5)));
			return true;
		}
	}
	else if (fos.section == FileSection_Results) {
		if (!resultList.size()) return true; //prevent crash from mixed-up data.
		auto it = resultList.end();
		it--;
		if (!line.substr(0, 5).compare("seat=")) {
			it->seat = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("swng=")) {
			it->incumbentSwing = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cnt =")) {
			it->percentCounted = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("btin=")) {
			it->boothsIn = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("btto=")) {
			it->totalBooths = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("updt=")) {
			it->updateTime = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
	}

	return true;
}

void PollingProject::removeProjectionsFromModel(Model::Id modelId) {
	for (auto const& [key, projection] : projections()) {
		if (projection.getSettings().baseModel == modelId) projections().remove(key);
	}
}

void PollingProject::removeSimulationsFromProjection(Projection::Id projectionId)
{
	for (int i = 0; i < simulations().count(); i++) {
		Simulation const& simulation = simulations().viewByIndex(i);
		if (simulation.getSettings().baseProjection == projectionId) {
			simulations().remove(simulations().indexToId(i));
			i--;
		}
	}
}

void PollingProject::adjustSeatsAfterPartyRemoval(PartyCollection::Index, Party::Id partyId) {
	for (auto& seatPair : seats()) {
		Seat& seat = seatPair.second;
		if (seat.incumbent == partyId) seat.incumbent = (seat.challenger ? 0 : 1);
		if (seat.challenger == partyId) seat.challenger = (seat.incumbent ? 0 : 1);
		if (seat.challenger2 == partyId) seat.challenger2 = 0;
	}
}

void PollingProject::adjustSeatsAfterRegionRemoval(RegionCollection::Index, Party::Id regionId)
{
	for (auto& seatPair : seats()) {
		Seat& seat = seatPair.second;
		if (seat.region == regionId) seat.region = regions().indexToId(0);
	}
}

void PollingProject::finalizeFileLoading() {
	partyCollection.finaliseFileLoading();
}
