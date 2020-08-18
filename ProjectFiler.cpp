#include "ProjectFiler.h"

#include "PollingProject.h"
#include "SaveIO.h"

#include <fstream>
#include <regex>

constexpr int VersionNum = 1;

ProjectFiler::ProjectFiler(PollingProject & project)
	: project(project)
{
}

int ProjectFiler::save(std::string filename) {
	if (isDetailedFormat(filename)) {
		return saveDetailed(filename);
	}
	std::ofstream os = std::ofstream(filename, std::ios_base::trunc);
	os << std::setprecision(12);
	if (!os) return 1;
	os << "#Project" << "\n";
	os << "name=" << project.name << "\n";
	os << "opre=" << project.parties().getOthersPreferenceFlow() << "\n";
	os << "oexh=" << project.parties().getOthersExhaustRate() << "\n";
	os << "#Parties" << "\n";
	for (auto const&[key, thisParty] : project.partyCollection) {
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
	for (auto const&[key, thisPollster] : project.pollsterCollection) {
		os << "@Pollster" << "\n";
		os << "name=" << thisPollster.name << "\n";
		os << "weig=" << thisPollster.weight << "\n";
		os << "colr=" << thisPollster.colour << "\n";
		os << "cali=" << int(thisPollster.useForCalibration) << "\n";
		os << "igin=" << int(thisPollster.ignoreInitially) << "\n";
	}
	os << "#Polls" << "\n";
	for (auto const&[key, thisPoll] : project.pollCollection) {
		os << "@Poll" << "\n";
		os << "poll=" << project.pollsters().idToIndex(thisPoll.pollster) << "\n";
		os << "year=" << thisPoll.date.GetYear() << "\n";
		os << "mont=" << thisPoll.date.GetMonth() << "\n";
		os << "day =" << thisPoll.date.GetDay() << "\n";
		os << "prev=" << thisPoll.reported2pp << "\n";
		os << "resp=" << thisPoll.respondent2pp << "\n";
		os << "calc=" << thisPoll.calc2pp << "\n";
		for (int i = 0; i < project.partyCollection.count(); i++) {
			os << "py" << (i<10 ? "0" : "") << i << "=" << thisPoll.primary[i] << "\n";
		}
		os << "py15=" << thisPoll.primary[PartyCollection::MaxParties] << "\n";
	}
	os << "#Events" << "\n";
	for (auto const&[key, thisEvent] : project.eventCollection) {
		os << "@Event" << "\n";
		os << "name=" << thisEvent.name << "\n";
		os << "type=" << thisEvent.eventType << "\n";
		os << "date=" << thisEvent.date.GetJulianDayNumber() << "\n";
		os << "vote=" << thisEvent.vote << "\n";
	}
	os << "#Models" << "\n";
	for (auto const&[key, thisModel] : project.modelCollection) {
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
	for (auto const&[key, thisProjection] : project.projectionCollection) {
		os << "@Projection" << "\n";
		os << "name=" << thisProjection.getSettings().name << "\n";
		os << "iter=" << thisProjection.getSettings().numIterations << "\n";
		os << "base=" << project.models().idToIndex(thisProjection.getSettings().baseModel) << "\n";
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
	for (auto const&[key, thisRegion] : project.regionCollection) {
		os << "@Region" << "\n";
		os << "name=" << thisRegion.name << "\n";
		os << "popn=" << thisRegion.population << "\n";
		os << "lele=" << thisRegion.lastElection2pp << "\n";
		os << "samp=" << thisRegion.sample2pp << "\n";
		os << "swng=" << thisRegion.swingDeviation << "\n";
		os << "addu=" << thisRegion.additionalUncertainty << "\n";
	}
	os << "#Seats" << "\n";
	for (auto const&[key, thisSeat] : project.seatCollection) {
		os << "@Seat" << "\n";
		os << "name=" << thisSeat.name << "\n";
		os << "pvnm=" << thisSeat.previousName << "\n";
		os << "incu=" << project.partyCollection.idToIndex(thisSeat.incumbent) << "\n";
		os << "chal=" << project.partyCollection.idToIndex(thisSeat.challenger) << "\n";
		os << "cha2=" << project.partyCollection.idToIndex(thisSeat.challenger2) << "\n";
		os << "regn=" << project.regions().idToIndex(thisSeat.region) << "\n";
		os << "marg=" << thisSeat.margin << "\n";
		os << "lmod=" << thisSeat.localModifier << "\n";
		os << "iodd=" << thisSeat.incumbentOdds << "\n";
		os << "codd=" << thisSeat.challengerOdds << "\n";
		os << "c2od=" << thisSeat.challenger2Odds << "\n";
		os << "winp=" << thisSeat.incumbentWinPercent << "\n";
		os << "tipp=" << thisSeat.tippingPointPercent << "\n";
		os << "sma =" << thisSeat.simulatedMarginAverage << "\n";
		os << "lp1 =" << project.partyCollection.idToIndex(thisSeat.livePartyOne) << "\n";
		os << "lp2 =" << project.partyCollection.idToIndex(thisSeat.livePartyTwo) << "\n";
		os << "lp3 =" << project.partyCollection.idToIndex(thisSeat.livePartyThree) << "\n";
		os << "p2pr=" << thisSeat.partyTwoProb << "\n";
		os << "p3pr=" << thisSeat.partyThreeProb << "\n";
		os << "over=" << int(thisSeat.overrideBettingOdds) << "\n";
	}
	os << "#Simulations" << "\n";
	for (auto const&[key, thisSimulation] : project.simulationCollection) {
		os << "@Simulation" << "\n";
		os << "name=" << thisSimulation.getSettings().name << "\n";
		os << "iter=" << thisSimulation.getSettings().numIterations << "\n";
		os << "base=" << project.projections().idToIndex(thisSimulation.getSettings().baseProjection) << "\n";
		os << "prev=" << thisSimulation.getSettings().prevElection2pp << "\n";
		os << "stsd=" << thisSimulation.getSettings().stateSD << "\n";
		os << "stde=" << thisSimulation.getSettings().stateDecay << "\n";
		os << "live=" << int(thisSimulation.getSettings().live) << "\n";
	}
	os << "#Results" << "\n";
	for (auto const& thisOutcome : project.outcomes()) {
		os << "@Result" << "\n";
		os << "seat=" << project.seats().idToIndex(thisOutcome.seat) << "\n";
		os << "swng=" << thisOutcome.incumbentSwing << "\n";
		os << "cnt =" << thisOutcome.percentCounted << "\n";
		os << "btin=" << thisOutcome.boothsIn << "\n";
		os << "btto=" << thisOutcome.totalBooths << "\n";
		os << "updt=" << thisOutcome.updateTime.GetJulianDayNumber() << "\n";
	}
	os << "#End";
	os.close();
	return 0; // success
}

void ProjectFiler::open(std::string filename) {
	project.valid = false;
	if (isDetailedFormat(filename)) {
		openDetailed(filename);
		return;
	}

	std::ifstream is = std::ifstream(filename);
	if (!is) { project.valid = false; return; }
	project.valid = true;

	FileOpeningState fos;

	while (is) {
		std::string s;
		std::getline(is, s);
		if (!processFileLine(s, fos)) break;
	}

	is.close();

	project.finalizeFileLoading();
}

bool ProjectFiler::isDetailedFormat(std::string filename)
{
	std::smatch filename_match;
	return std::regex_match(filename, filename_match, std::basic_regex(".+\\.pol2"));
}

int ProjectFiler::saveDetailed(std::string filename)
{
	SaveFileOutput saveOutput(filename);
	saveOutput << VersionNum;
	saveOutput << project.name;
	saveParties(saveOutput);
	savePollsters(saveOutput);
	savePolls(saveOutput);
	saveEvents(saveOutput);
	saveModels(saveOutput);
	saveProjections(saveOutput);
	saveRegions(saveOutput);
	saveSeats(saveOutput);
	saveSimulations(saveOutput);
	saveOutcomes(saveOutput);
	return 1;
}

int ProjectFiler::openDetailed(std::string filename)
{
	SaveFileInput saveInput(filename);
	const int versionNum = saveInput.extract<int>();
	saveInput >> project.name;
	loadParties(saveInput, versionNum);
	loadPollsters(saveInput, versionNum);
	loadPolls(saveInput, versionNum);
	loadEvents(saveInput, versionNum);
	loadModels(saveInput, versionNum);
	loadProjections(saveInput, versionNum);
	loadRegions(saveInput, versionNum);
	loadSeats(saveInput, versionNum);
	loadSimulations(saveInput, versionNum);
	loadOutcomes(saveInput, versionNum);

	project.parties().logAll();
	project.pollsters().logAll();
	project.polls().logAll(project.parties(), project.pollsters());
	project.events().logAll();
	project.models().logAll();
	project.projections().logAll(project.models());
	project.regions().logAll();
	project.seats().logAll(project.parties(), project.regions());
	project.simulations().logAll(project.projections());
	project.outcomes().logAll(project.seats());
	return 1;
}

void ProjectFiler::saveParties(SaveFileOutput& saveOutput)
{
	saveOutput << project.parties().getOthersPreferenceFlow();
	saveOutput << project.parties().getOthersExhaustRate();
	saveOutput.outputAsType<int32_t>(project.partyCollection.count());
	for (auto const& [key, thisParty] : project.partyCollection) {
		saveOutput << thisParty.name;
		saveOutput << thisParty.preferenceShare;
		saveOutput << thisParty.exhaustRate;
		saveOutput << thisParty.abbreviation;
		saveOutput.outputAsType<int32_t>(thisParty.countAsParty);
		saveOutput.outputAsType<int32_t>(thisParty.supportsParty);
		saveOutput << thisParty.boothColourMult;
		saveOutput.outputAsType<int32_t>(thisParty.ideology);
		saveOutput.outputAsType<int32_t>(thisParty.consistency);
		saveOutput.outputAsType<uint64_t>(thisParty.officialCodes.size());
		for (std::string officialCode : thisParty.officialCodes) {
			saveOutput << officialCode;
		}
		saveOutput.outputAsType<int32_t>(thisParty.colour.r);
		saveOutput.outputAsType<int32_t>(thisParty.colour.g);
		saveOutput.outputAsType<int32_t>(thisParty.colour.b);
	}
}

void ProjectFiler::loadParties(SaveFileInput& saveInput, [[maybe_unused]] int versionNum)
{
	project.parties().setOthersPreferenceFlow(saveInput.extract<float>());
	project.parties().setOthersExhaustRate(saveInput.extract<float>());
	auto partyCount = saveInput.extract<int32_t>();
	for (int partyIndex = 0; partyIndex < partyCount; ++partyIndex) {
		Party thisParty;
		saveInput >> thisParty.name;
		saveInput >> thisParty.preferenceShare;
		saveInput >> thisParty.exhaustRate;
		saveInput >> thisParty.abbreviation;
		thisParty.countAsParty = Party::CountAsParty(saveInput.extract<int32_t>());
		thisParty.supportsParty = Party::SupportsParty(saveInput.extract<int32_t>());
		saveInput >> thisParty.boothColourMult;
		thisParty.ideology = saveInput.extract<int32_t>();
		thisParty.consistency = saveInput.extract<int32_t>();
		auto officialCodeCount = saveInput.extract<uint64_t>();
		for (int officialCodeIndex = 0; officialCodeIndex < officialCodeCount; ++officialCodeIndex) {
			thisParty.officialCodes.push_back(saveInput.extract<std::string>());
		}
		thisParty.colour.r = saveInput.extract<int32_t>();
		thisParty.colour.g = saveInput.extract<int32_t>();
		thisParty.colour.b = saveInput.extract<int32_t>();
		project.partyCollection.add(thisParty);
	}
}

void ProjectFiler::savePollsters(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.pollsterCollection.count());
	for (auto const& [key, thisPollster] : project.pollsterCollection) {
		saveOutput << thisPollster.name;
		saveOutput << thisPollster.weight;
		saveOutput.outputAsType<uint64_t>(thisPollster.colour);
		saveOutput << thisPollster.useForCalibration;
		saveOutput << thisPollster.ignoreInitially;
	}
}

void ProjectFiler::loadPollsters(SaveFileInput& saveInput, [[maybe_unused]] int versionNum)
{
	auto pollsterCount = saveInput.extract<int32_t>();
	for (int pollsterIndex = 0; pollsterIndex < pollsterCount; ++pollsterIndex) {
		Pollster thisPollster;
		saveInput >> thisPollster.name;
		saveInput >> thisPollster.weight;
		thisPollster.colour = saveInput.extract<uint64_t>();
		saveInput >> thisPollster.useForCalibration;
		saveInput >> thisPollster.ignoreInitially;
		project.pollsterCollection.add(thisPollster);
	}
}

void ProjectFiler::savePolls(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.pollCollection.count());
	for (auto const& [key, thisPoll] : project.pollCollection) {
		saveOutput.outputAsType<int32_t>(project.pollsters().idToIndex(thisPoll.pollster));
		saveOutput.outputAsType<int32_t>(thisPoll.date.GetYear());
		saveOutput.outputAsType<int32_t>(thisPoll.date.GetMonth());
		saveOutput.outputAsType<int32_t>(thisPoll.date.GetDay());
		saveOutput << thisPoll.reported2pp;
		saveOutput << thisPoll.respondent2pp;
		saveOutput << thisPoll.calc2pp;
		saveOutput.outputAsType<uint32_t>(thisPoll.primary.size());
		for (auto primary : thisPoll.primary) {
			saveOutput << primary;
		}
	}
}

void ProjectFiler::loadPolls(SaveFileInput& saveInput, [[maybe_unused]] int versionNum)
{
	auto pollCount = saveInput.extract<int32_t>();
	for (int pollIndex = 0; pollIndex < pollCount; ++pollIndex) {
		Poll thisPoll;
		thisPoll.pollster = saveInput.extract<int32_t>();
		thisPoll.date.SetYear(saveInput.extract<int32_t>());
		thisPoll.date.SetMonth(wxDateTime::Month(saveInput.extract<int32_t>()));
		thisPoll.date.SetDay(saveInput.extract<int32_t>());
		saveInput >> thisPoll.reported2pp;
		saveInput >> thisPoll.respondent2pp;
		saveInput >> thisPoll.calc2pp;
		size_t numPrimaries = saveInput.extract<uint32_t>();
		for (size_t partyIndex = 0; partyIndex < numPrimaries; ++partyIndex) {
			saveInput >> thisPoll.primary[partyIndex];
		}
		project.pollCollection.add(thisPoll);
	}
}

void ProjectFiler::saveEvents(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.eventCollection.count());
	for (auto const& [key, thisEvent] : project.eventCollection) {
		saveOutput << thisEvent.name;
		saveOutput.outputAsType<int32_t>(thisEvent.eventType);
		saveOutput << thisEvent.date.GetJulianDayNumber();
		saveOutput << thisEvent.vote;
	}
}

void ProjectFiler::loadEvents(SaveFileInput& saveInput, [[maybe_unused]] int versionNum)
{
	auto eventCount = saveInput.extract<int32_t>();
	for (int eventIndex = 0; eventIndex < eventCount; ++eventIndex) {
		Event thisEvent;
		saveInput >> thisEvent.name;
		thisEvent.eventType = EventType(saveInput.extract<int32_t>());
		thisEvent.date = wxDateTime(saveInput.extract<double>());
		saveInput >> thisEvent.vote;
		project.eventCollection.add(thisEvent);
	}
}

void ProjectFiler::saveModels(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.modelCollection.count());
	for (auto const& [key, thisModel] : project.modelCollection) {
		saveOutput << thisModel.getSettings().name;
		saveOutput.outputAsType<int32_t>(thisModel.getSettings().numIterations);
		saveOutput << thisModel.getSettings().trendTimeScoreMultiplier;
		saveOutput << thisModel.getSettings().houseEffectTimeScoreMultiplier;
		saveOutput << thisModel.getSettings().calibrationFirstPartyBias;
		saveOutput << thisModel.getFinalStandardDeviation();
		saveOutput << thisModel.getSettings().startDate.GetJulianDayNumber();
		saveOutput << thisModel.getSettings().endDate.GetJulianDayNumber();
		saveOutput << thisModel.getLastUpdatedDate().GetJulianDayNumber();
		saveOutput.outputAsType<int32_t>(thisModel.numDays());
		for (auto const& thisDay : thisModel) {
			saveOutput << thisDay.trend2pp;
		}
	}
}

void ProjectFiler::loadModels(SaveFileInput& saveInput, [[maybe_unused]] int versionNum)
{
	auto modelCount = saveInput.extract<int32_t>();
	for (int modelIndex = 0; modelIndex < modelCount; ++modelIndex) {
		Model::SaveData thisModel;
		saveInput >> thisModel.settings.name;
		thisModel.settings.numIterations = saveInput.extract<int32_t>();
		saveInput >> thisModel.settings.trendTimeScoreMultiplier;
		saveInput >> thisModel.settings.houseEffectTimeScoreMultiplier;
		saveInput >> thisModel.settings.calibrationFirstPartyBias;
		saveInput >> thisModel.finalStandardDeviation;
		thisModel.settings.startDate = wxDateTime(saveInput.extract<double>());
		thisModel.settings.endDate = wxDateTime(saveInput.extract<double>());
		thisModel.lastUpdated = wxDateTime(saveInput.extract<double>());
		auto dayCount = saveInput.extract<int32_t>();
		for (int day = 0; day < dayCount; ++day) {
			thisModel.trend.push_back(saveInput.extract<float>());
		}
		project.modelCollection.add(Model(thisModel));
	}
}

void ProjectFiler::saveProjections(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.projectionCollection.count());
	for (auto const& [key, thisProjection] : project.projectionCollection) {
		saveOutput << thisProjection.getSettings().name;
		saveOutput.outputAsType<int32_t>(thisProjection.getSettings().numIterations);
		saveOutput.outputAsType<int32_t>(project.models().idToIndex(thisProjection.getSettings().baseModel));
		saveOutput << thisProjection.getSettings().endDate.GetJulianDayNumber();
		saveOutput << thisProjection.getLastUpdatedDate().GetJulianDayNumber();
		saveOutput << thisProjection.getSettings().dailyChange;
		saveOutput << thisProjection.getSettings().initialStdDev;
		saveOutput << thisProjection.getSettings().leaderVoteDecay;
		saveOutput.outputAsType<int32_t>(thisProjection.getSettings().numElections);
		saveOutput.outputAsType<int32_t>(thisProjection.getProjectionLength());
		for (int dayIndex = 0; dayIndex < int(thisProjection.getProjectionLength()); ++dayIndex) {
			saveOutput << thisProjection.getMeanProjection(dayIndex);
			saveOutput << thisProjection.getSdProjection(dayIndex);
		}
	}
}

void ProjectFiler::loadProjections(SaveFileInput& saveInput, [[maybe_unused]] int versionNum)
{
	int projectionCount = saveInput.extract<int32_t>();
	for (int projectionIndex = 0; projectionIndex < projectionCount; ++projectionIndex) {
		Projection::SaveData thisProjection;
		saveInput >> thisProjection.settings.name;
		thisProjection.settings.numIterations = saveInput.extract<int32_t>();
		thisProjection.settings.baseModel = saveInput.extract<int32_t>();
		thisProjection.settings.endDate = wxDateTime(saveInput.extract<double>());
		thisProjection.lastUpdated = wxDateTime(saveInput.extract<double>());
		saveInput >> thisProjection.settings.dailyChange;
		saveInput >> thisProjection.settings.initialStdDev;
		saveInput >> thisProjection.settings.leaderVoteDecay;
		thisProjection.settings.numElections = saveInput.extract<int32_t>();
		auto projLength = saveInput.extract<int32_t>();
		for (int dayIndex = 0; dayIndex < projLength; ++dayIndex) {
			Projection::ProjectionDay thisDay;
			saveInput >> thisDay.mean;
			saveInput >> thisDay.sd;
			thisProjection.projection.push_back(thisDay);
		}
		project.projectionCollection.add(Projection(thisProjection));
	}
}

void ProjectFiler::saveRegions(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.regionCollection.count());
	for (auto const& [key, thisRegion] : project.regionCollection) {
		saveOutput << thisRegion.name;
		saveOutput.outputAsType<int32_t>(thisRegion.population);
		saveOutput << thisRegion.lastElection2pp;
		saveOutput << thisRegion.sample2pp;
		saveOutput << thisRegion.swingDeviation;
		saveOutput << thisRegion.additionalUncertainty;
	}
}

void ProjectFiler::loadRegions(SaveFileInput& saveInput,  [[maybe_unused]] int versionNum)
{
	auto regionCount = saveInput.extract<int32_t>();
	for (int regionIndex = 0; regionIndex < regionCount; ++regionIndex) {
		Region thisRegion;
		saveInput >> thisRegion.name;
		thisRegion.population = saveInput.extract<int32_t>();
		saveInput >> thisRegion.lastElection2pp;
		saveInput >> thisRegion.sample2pp;
		saveInput >> thisRegion.swingDeviation;
		saveInput >> thisRegion.additionalUncertainty;
		project.regionCollection.add(thisRegion);
	}
}

void ProjectFiler::saveSeats(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.seatCollection.count());
	for (auto const& [key, thisSeat] : project.seatCollection) {
		saveOutput << thisSeat.name;
		saveOutput << thisSeat.previousName;
		saveOutput.outputAsType<int32_t>(project.partyCollection.idToIndex(thisSeat.incumbent));
		saveOutput.outputAsType<int32_t>(project.partyCollection.idToIndex(thisSeat.challenger));
		saveOutput.outputAsType<int32_t>(project.partyCollection.idToIndex(thisSeat.challenger2));
		saveOutput.outputAsType<int32_t>(project.regions().idToIndex(thisSeat.region));
		saveOutput << thisSeat.margin;
		saveOutput << thisSeat.localModifier;
		saveOutput << thisSeat.incumbentOdds;
		saveOutput << thisSeat.challengerOdds;
		saveOutput << thisSeat.challenger2Odds;
		saveOutput << thisSeat.incumbentWinPercent;
		saveOutput << thisSeat.tippingPointPercent;
		saveOutput << thisSeat.simulatedMarginAverage;
		saveOutput.outputAsType<int32_t>(project.partyCollection.idToIndex(thisSeat.livePartyOne));
		saveOutput.outputAsType<int32_t>(project.partyCollection.idToIndex(thisSeat.livePartyTwo));
		saveOutput.outputAsType<int32_t>(project.partyCollection.idToIndex(thisSeat.livePartyThree));
		saveOutput << thisSeat.partyTwoProb;
		saveOutput << thisSeat.partyThreeProb;
		saveOutput << thisSeat.overrideBettingOdds;
	}
}

void ProjectFiler::loadSeats(SaveFileInput& saveInput, [[maybe_unused]] int versionNum)
{
	auto seatCount = saveInput.extract<int32_t>();
	for (int seatIndex = 0; seatIndex < seatCount; ++seatIndex) {
		Seat thisSeat;
		saveInput >> thisSeat.name;
		saveInput >> thisSeat.previousName;
		thisSeat.incumbent = saveInput.extract<int32_t>();
		thisSeat.challenger = saveInput.extract<int32_t>();
		thisSeat.challenger2 = saveInput.extract<int32_t>();
		thisSeat.region = saveInput.extract<int32_t>();
		saveInput >> thisSeat.margin;
		saveInput >> thisSeat.localModifier;
		saveInput >> thisSeat.incumbentOdds;
		saveInput >> thisSeat.challengerOdds;
		saveInput >> thisSeat.challenger2Odds;
		saveInput >> thisSeat.incumbentWinPercent;
		saveInput >> thisSeat.tippingPointPercent;
		saveInput >> thisSeat.simulatedMarginAverage;
		thisSeat.livePartyOne = saveInput.extract<int32_t>();
		thisSeat.livePartyTwo = saveInput.extract<int32_t>();
		thisSeat.livePartyThree = saveInput.extract<int32_t>();
		saveInput >> thisSeat.partyTwoProb;
		saveInput >> thisSeat.partyThreeProb;
		saveInput >> thisSeat.overrideBettingOdds;
		project.seatCollection.add(thisSeat);
	}
}

void ProjectFiler::saveSimulations(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.simulationCollection.count());
	for (auto const& [key, thisSimulation] : project.simulationCollection) {
		saveOutput << thisSimulation.getSettings().name;
		saveOutput.outputAsType<int32_t>(thisSimulation.getSettings().numIterations);
		saveOutput.outputAsType<int32_t>(project.projections().idToIndex(thisSimulation.getSettings().baseProjection));
		saveOutput << thisSimulation.getSettings().prevElection2pp;
		saveOutput << thisSimulation.getSettings().stateSD;
		saveOutput << thisSimulation.getSettings().stateDecay;
		saveOutput.outputAsType<int32_t>(thisSimulation.getSettings().live);
	}
}

void ProjectFiler::loadSimulations(SaveFileInput& saveInput, [[maybe_unused]] int versionNum)
{
	int simulationCount = saveInput.extract<int32_t>();
	for (int simulationIndex = 0; simulationIndex < simulationCount; ++simulationIndex) {
		Simulation::Settings thisSimulation;
		saveInput >> thisSimulation.name;
		thisSimulation.numIterations = saveInput.extract<int32_t>();
		thisSimulation.baseProjection = Projection::Id(saveInput.extract<int32_t>());
		saveInput >> thisSimulation.prevElection2pp;
		saveInput >> thisSimulation.stateSD;
		saveInput >> thisSimulation.stateDecay;
		thisSimulation.live = Simulation::Settings::Mode(saveInput.extract<int32_t>());
		project.simulationCollection.add(Simulation(thisSimulation));
	}
}

void ProjectFiler::saveOutcomes(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.outcomeCollection.count());
	for (auto const& thisOutcome : project.outcomeCollection) {
		saveOutput.outputAsType<int32_t>(project.seats().idToIndex(thisOutcome.seat));
		saveOutput << thisOutcome.incumbentSwing;
		saveOutput << thisOutcome.percentCounted;
		saveOutput.outputAsType<int32_t>(thisOutcome.boothsIn);
		saveOutput.outputAsType<int32_t>(thisOutcome.totalBooths);
		saveOutput << thisOutcome.updateTime.GetJulianDayNumber();
	}
}

void ProjectFiler::loadOutcomes(SaveFileInput& saveInput, [[maybe_unused]] int versionNum)
{
	int outcomeCount = saveInput.extract<int32_t>();
	for (int outcomeIndex = 0; outcomeIndex < outcomeCount; ++outcomeIndex) {
		Outcome thisOutcome;
		thisOutcome.seat = saveInput.extract<int32_t>();
		saveInput >> thisOutcome.incumbentSwing;
		saveInput >> thisOutcome.percentCounted;
		thisOutcome.boothsIn = saveInput.extract<int32_t>();
		thisOutcome.totalBooths = saveInput.extract<int32_t>();
		thisOutcome.updateTime = wxDateTime(saveInput.extract<double>());
		project.outcomeCollection.add(thisOutcome);
	}
}

bool ProjectFiler::processFileLine(std::string line, FileOpeningState& fos) {

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
		project.modelCollection.finaliseLoadedModel();
		fos.section = FileSection_Projections;
		return true;
	}
	else if (!line.compare("#Regions")) {
		project.projectionCollection.finaliseLoadedProjection();
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
		project.simulationCollection.finaliseLoadedSimulation();
		fos.section = FileSection_Results;
		return true;
	}
	else if (!line.compare("#End")) {
		return false;
	}

	// New item changes
	if (fos.section == FileSection_Parties) {
		if (!line.compare("@Party")) {
			project.partyCollection.add(Party());
			return true;
		}
	}
	else if (fos.section == FileSection_Pollsters) {
		if (!line.compare("@Pollster")) {
			project.pollsterCollection.add(Pollster());
			return true;
		}
	}
	else if (fos.section == FileSection_Polls) {
		if (!line.compare("@Poll")) {
			project.pollCollection.add(Poll());
			// Avoid issues with temporarily setting a date to 30th February and the like.
			project.pollCollection.back().date.SetDay(1);
			return true;
		}
	}
	else if (fos.section == FileSection_Events) {
		if (!line.compare("@Event")) {
			project.eventCollection.add(Event());
			return true;
		}
	}
	else if (fos.section == FileSection_Models) {
		if (!line.compare("@Model")) {
			project.modelCollection.finaliseLoadedModel();
			project.modelCollection.startLoadingModel();
			return true;
		}
	}
	else if (fos.section == FileSection_Projections) {
		if (!line.compare("@Projection")) {
			project.projectionCollection.finaliseLoadedProjection();
			project.projectionCollection.startLoadingProjection();
			return true;
		}
	}
	else if (fos.section == FileSection_Regions) {
		if (!line.compare("@Region")) {
			project.regionCollection.add(Region());
			return true;
		}
	}
	else if (fos.section == FileSection_Seats) {
		if (!line.compare("@Seat")) {
			project.seatCollection.add(Seat());
			return true;
		}
	}
	else if (fos.section == FileSection_Simulations) {
		if (!line.compare("@Simulation")) {
			project.simulationCollection.finaliseLoadedSimulation();
			project.simulationCollection.startLoadingSimulation();
			return true;
		}
	}
	else if (fos.section == FileSection_Results) {
		if (!line.compare("@Result")) {
			project.outcomes().add(Outcome());
			return true;
		}
	}

	// Values
	if (fos.section == FileSection_Project) {
		if (!line.substr(0, 5).compare("name=")) {
			project.name = line.substr(5);
			return true;
		}
		if (!line.substr(0, 5).compare("opre=")) {
			project.parties().setOthersPreferenceFlow(std::stof(line.substr(5)));
			return true;
		}
		if (!line.substr(0, 5).compare("oexh=")) {
			project.parties().setOthersExhaustRate(std::stof(line.substr(5)));
			return true;
		}
	}
	else if (fos.section == FileSection_Parties) {
		if (!project.partyCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			project.partyCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("pref=")) {
			project.partyCollection.back().preferenceShare = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("exha=")) {
			project.partyCollection.back().exhaustRate = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("abbr=")) {
			project.partyCollection.back().abbreviation = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("cap =")) {
			project.partyCollection.back().countAsParty = Party::CountAsParty(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("supp=")) {
			project.partyCollection.back().supportsParty = Party::SupportsParty(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("ideo=")) {
			project.partyCollection.back().ideology = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cons=")) {
			project.partyCollection.back().consistency = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("bcmt=")) {
			project.partyCollection.back().boothColourMult = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("code=")) {
			project.partyCollection.back().officialCodes.push_back(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colr=")) {
			project.partyCollection.back().colour.r = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colg=")) {
			project.partyCollection.back().colour.g = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colb=")) {
			project.partyCollection.back().colour.b = std::stoi(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Pollsters) {
		if (!project.pollsterCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			project.pollsterCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("weig=")) {
			project.pollsterCollection.back().weight = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("colr=")) {
			project.pollsterCollection.back().colour = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cali=")) {
			project.pollsterCollection.back().useForCalibration = std::stoi(line.substr(5)) != 0;
			return true;
		}
		else if (!line.substr(0, 5).compare("igin=")) {
			project.pollsterCollection.back().ignoreInitially = std::stoi(line.substr(5)) != 0;
			return true;
		}
	}
	else if (fos.section == FileSection_Polls) {
		if (!project.pollCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("poll=")) {
			project.pollCollection.back().pollster = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("year=")) {
			project.pollCollection.back().date.SetYear(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("mont=")) {
			project.pollCollection.back().date.SetMonth((wxDateTime::Month)std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("day =")) {
			project.pollCollection.back().date.SetDay(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("prev=")) {
			project.pollCollection.back().reported2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("resp=")) {
			project.pollCollection.back().respondent2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("calc=")) {
			project.pollCollection.back().calc2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 2).compare("py")) {
			int primaryIndex = std::stoi(line.substr(2, 2));
			project.pollCollection.back().primary[primaryIndex] = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Events) {
		if (!project.eventCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			project.eventCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("type=")) {
			project.eventCollection.back().eventType = EventType(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("date=")) {
			project.eventCollection.back().date = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("vote=")) {
			project.eventCollection.back().vote = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Models) {
		if (!project.modelCollection.loadingModel.has_value()) return true;
		if (!line.substr(0, 5).compare("name=")) {
			project.modelCollection.loadingModel->settings.name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			project.modelCollection.loadingModel->settings.numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("trnd=")) {
			project.modelCollection.loadingModel->settings.trendTimeScoreMultiplier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("hsm =")) {
			project.modelCollection.loadingModel->settings.houseEffectTimeScoreMultiplier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cfpb=")) {
			project.modelCollection.loadingModel->settings.calibrationFirstPartyBias = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("fstd=")) {
			project.modelCollection.loadingModel->finalStandardDeviation = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("strt=")) {
			project.modelCollection.loadingModel->settings.startDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("end =")) {
			project.modelCollection.loadingModel->settings.endDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("updt=")) {
			project.modelCollection.loadingModel->lastUpdated = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 4).compare("$Day")) {
			project.modelCollection.loadingModel->trend.push_back(50.0f);
			return true;
		}
		else if (!line.substr(0, 5).compare("mtnd=")) {
			if (!project.modelCollection.loadingModel->trend.size()) return true;
			project.modelCollection.loadingModel->trend.back() = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Projections) {
		if (!project.projectionCollection.loadingProjection.has_value()) return true;
		if (!line.substr(0, 5).compare("name=")) {
			project.projectionCollection.loadingProjection->settings.name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			project.projectionCollection.loadingProjection->settings.numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("base=")) {
			project.projectionCollection.loadingProjection->settings.baseModel = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("end =")) {
			project.projectionCollection.loadingProjection->settings.endDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("updt=")) {
			project.projectionCollection.loadingProjection->lastUpdated = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("dlyc=")) {
			project.projectionCollection.loadingProjection->settings.dailyChange = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("inic=")) {
			project.projectionCollection.loadingProjection->settings.initialStdDev = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("vtls=")) {
			project.projectionCollection.loadingProjection->settings.leaderVoteDecay = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("nele=")) {
			project.projectionCollection.loadingProjection->settings.numElections = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("mean=")) {
			double mean = std::stod(line.substr(5));
			project.projectionCollection.loadingProjection->projection.push_back({ mean, 0 });
			return true;
		}
		else if (!line.substr(0, 5).compare("stdv=")) {
			double sd = std::stod(line.substr(5));
			project.projectionCollection.loadingProjection->projection.back().sd = sd;
			return true;
		}
	}
	else if (fos.section == FileSection_Regions) {
		if (!project.regionCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			project.regionCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("popn=")) {
			project.regionCollection.back().population = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lele=")) {
			project.regionCollection.back().lastElection2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("samp=")) {
			project.regionCollection.back().sample2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("swng=")) {
			project.regionCollection.back().swingDeviation = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("addu=")) {
			project.regionCollection.back().additionalUncertainty = std::stof(line.substr(5));
			return true;
		}
	}
	else if (fos.section == FileSection_Seats) {
		if (!project.seatCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			project.seatCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("pvnm=")) {
			project.seatCollection.back().previousName = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("incu=")) {
			project.seatCollection.back().incumbent = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("chal=")) {
			project.seatCollection.back().challenger = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cha2=")) {
			project.seatCollection.back().challenger2 = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("regn=")) {
			project.seatCollection.back().region = project.regions().indexToId(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("marg=")) {
			project.seatCollection.back().margin = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lmod=")) {
			project.seatCollection.back().localModifier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("iodd=")) {
			project.seatCollection.back().incumbentOdds = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("codd=")) {
			project.seatCollection.back().challengerOdds = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("c2od=")) {
			project.seatCollection.back().challenger2Odds = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("winp=")) {
			project.seatCollection.back().incumbentWinPercent = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("tipp=")) {
			project.seatCollection.back().tippingPointPercent = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("sma =")) {
			project.seatCollection.back().simulatedMarginAverage = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lp1 =")) {
			project.seatCollection.back().livePartyOne = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lp2 =")) {
			project.seatCollection.back().livePartyTwo = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("lp3 =")) {
			project.seatCollection.back().livePartyThree = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("p2pr=")) {
			project.seatCollection.back().partyTwoProb = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("p3pr=")) {
			project.seatCollection.back().partyThreeProb = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("over=")) {
			project.seatCollection.back().overrideBettingOdds = std::stoi(line.substr(5)) != 0;
			return true;
		}
	}
	else if (fos.section == FileSection_Simulations) {
		if (!project.simulationCollection.loadingSimulation.has_value()) return true;
		if (!line.substr(0, 5).compare("name=")) {
			project.simulationCollection.loadingSimulation->name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			project.simulationCollection.loadingSimulation->numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("base=")) {
			project.simulationCollection.loadingSimulation->baseProjection = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("prev=")) {
			project.simulationCollection.loadingSimulation->prevElection2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("stsd=")) {
			project.simulationCollection.loadingSimulation->stateSD = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("stde=")) {
			project.simulationCollection.loadingSimulation->stateDecay = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("live=")) {
			project.simulationCollection.loadingSimulation->live = Simulation::Settings::Mode(std::stoi(line.substr(5)));
			return true;
		}
	}
	else if (fos.section == FileSection_Results) {
		if (!project.outcomes().count()) return true; //prevent crash from mixed-up data.
		auto it = project.outcomes().end();
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
