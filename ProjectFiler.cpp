#include "ProjectFiler.h"

#include "PollingProject.h"

#include <fstream>

ProjectFiler::ProjectFiler(PollingProject & project)
	: project(project)
{
}

int ProjectFiler::save(std::string filename) {
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
	project.valid = true;
	std::ifstream is = std::ifstream(filename);
	if (!is) { project.valid = false; return; }

	FileOpeningState fos;

	while (is) {
		std::string s;
		std::getline(is, s);
		if (!processFileLine(s, fos)) break;
	}

	is.close();

	project.finalizeFileLoading();
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
			if (project.pollCollection.count() == 54) logger << line;
			project.pollCollection.back().pollster = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("year=")) {
			if (project.pollCollection.count() == 54) logger << line;
			project.pollCollection.back().date.SetYear(std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("mont=")) {
			project.pollCollection.back().date.SetMonth((wxDateTime::Month)std::stoi(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("day =")) {
			if (project.pollCollection.count() == 54) logger << line;
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
