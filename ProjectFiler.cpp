#include "ProjectFiler.h"

#include "PollingProject.h"
#include "SaveIO.h"

#include <fstream>
#include <regex>

// Version 2: Rework models
// Version 3: Add new model seconds & data
// Version 4: "Include in others" party option
// Version 5: Mean/deviation adjustments
// Version 6: Preference flow, tpp series, timepoint expectations
// Version 7: Preference deviations/samples
// Version 8: Don't save old projection settings
// Version 9: Don't save obsolete projection means/stdevs
// Version 10: Save new projection series
// Version 11: Save additional model parameters
// Version 12: Save latest simulation report
// Version 13: Save all simulation reports
// Version 14: Save source file for polls
// Version 15: Don't save obsolete model settings
// Version 16: Save latest macro
// Version 17: Save medians for parties in simulation reports
// Version 18: Save distributions of tpp and party primaries
// Version 19: Save several more data sets for simulation reports
// Version 20: Save term codes of previous elections for simulations
// Version 21: Save alternate fp results for new seats
// Version 22: Sophomore/retirement settings for seats
// Version 23: Save home regions for minor parties and emerging party home region modifiers
// Version 24: Save minor party seat targets
// Version 25: Remove incumbent-relative margins and stats
// Version 26: Party names/abbr/colour now stored as maps rather than vectors
// Version 27: More conversion of party-sorted info from vectors to maps
// Version 28: Different possible end dates for election projections (with odds for each)
// Version 29: Custom non-classic preference flows
// Version 30: More election outcomes in report, 3rd party wins as well as "most seats" for all parties
// Version 31: Probability bands for seat fps
// Version 32: Tcp probability bands, scenario likelihood and party win rate
// Version 33: Save analysis codes for regions
// Version 34: Save previous tpp swings for seats
// Version 35: Disendorsement flags for seats
// Version 36: Remove various legacy data
// Version 37: Save election name
// Version 38: Save simulation report mode
// Version 39: Improve flexibility of report probability bands
// Version 40: Save incumbent margin on simulation reports
// Version 41: Save confirmed recontesting 3rd parties + confirmed prominent independents
// Version 42: Prominent minor party figures for seats + trend info in reports
// Version 43: Betting odds for individual seats
// Version 44: Polls for individual seats
// Version 45: Save polling info in reports
// Version 46: List running parties (on the ballot)
// Version 47: UseTpp value for live-manual non-classic seats.
// Version 48: tcpChange value for redistributed non-classic seats.
// Version 49: Record seats where (non-incumbent) independent is re-running
// Version 50: Save previous-results URL
// Version 51: Save preload URL
constexpr int VersionNum = 51;

ProjectFiler::ProjectFiler(PollingProject & project)
	: project(project)
{
}

void ProjectFiler::save(std::string filename)
{
	SaveFileOutput saveOutput(filename);
	saveOutput << VersionNum;
	saveOutput << project.name;
	saveOutput << project.electionName;
	saveOutput << project.lastMacro;
	saveParties(saveOutput);
	savePollsters(saveOutput);
	savePolls(saveOutput);
	saveModels(saveOutput);
	saveProjections(saveOutput);
	saveRegions(saveOutput);
	saveSeats(saveOutput);
	saveSimulations(saveOutput);
	saveOutcomes(saveOutput);
	saveElections(saveOutput);
}

void ProjectFiler::open(std::string filename)
{
	project.valid = false;
	SaveFileInput saveInput(filename);
	const int versionNum = saveInput.extract<int>();
	saveInput >> project.name;
	if (versionNum >= 37) saveInput >> project.electionName;
	if (versionNum >= 16) saveInput >> project.lastMacro;
	loadParties(saveInput, versionNum);
	loadPollsters(saveInput, versionNum);
	loadPolls(saveInput, versionNum);
	if (versionNum <= 35) loadEvents(saveInput, versionNum); // needed for legacy files
	loadModels(saveInput, versionNum);
	loadProjections(saveInput, versionNum);
	loadRegions(saveInput, versionNum);
	loadSeats(saveInput, versionNum);
	loadSimulations(saveInput, versionNum);
	loadOutcomes(saveInput, versionNum);
	loadElections(saveInput, versionNum);

	project.finalizeFileLoading();
	project.valid = true;
}

void ProjectFiler::saveParties(SaveFileOutput& saveOutput)
{
	saveOutput << project.parties().getOthersPreferenceFlow();
	saveOutput << project.parties().getOthersExhaustRate();
	saveOutput.outputAsType<int32_t>(project.partyCollection.count());
	for (auto const& [key, thisParty] : project.partyCollection) {
		saveOutput << thisParty.name;
		saveOutput << thisParty.p1PreferenceFlow;
		saveOutput << thisParty.exhaustRate;
		saveOutput << thisParty.ncPreferenceFlow;
		saveOutput << thisParty.abbreviation;
		saveOutput << thisParty.homeRegion;
		saveOutput << thisParty.seatTarget;
		saveOutput.outputAsType<int32_t>(thisParty.relationTarget);
		saveOutput.outputAsType<int32_t>(thisParty.relationType);
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
		saveOutput << thisParty.includeInOthers;
	}
}

void ProjectFiler::loadParties(SaveFileInput& saveInput, int versionNum)
{
	project.parties().setOthersPreferenceFlow(saveInput.extract<float>());
	project.parties().setOthersExhaustRate(saveInput.extract<float>());
	auto partyCount = saveInput.extract<int32_t>();
	for (int partyIndex = 0; partyIndex < partyCount; ++partyIndex) {
		Party thisParty;
		saveInput >> thisParty.name;
		saveInput >> thisParty.p1PreferenceFlow;
		saveInput >> thisParty.exhaustRate;
		if (versionNum >= 29) {
			saveInput >> thisParty.ncPreferenceFlow;
		}
		saveInput >> thisParty.abbreviation;
		if (versionNum >= 23) {
			saveInput >> thisParty.homeRegion;
		}
		if (versionNum >= 24) {
			saveInput >> thisParty.seatTarget;
		}
		// Some legacy files may have a value of -1 which will cause problems for the simulation
		// and edit-party function, so make sure it's brought up to zero.
		thisParty.relationTarget = std::max(0, saveInput.extract<int32_t>());
		thisParty.relationType = Party::RelationType(saveInput.extract<int32_t>());
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
		if (versionNum >= 4) {
			saveInput >> thisParty.includeInOthers;
		}
		project.partyCollection.add(thisParty);
	}
}

void ProjectFiler::savePollsters(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.pollsterCollection.count());
	for (auto const& [key, thisPollster] : project.pollsterCollection) {
		saveOutput << thisPollster.name;
		saveOutput.outputAsType<uint64_t>(thisPollster.colour);
	}
}

void ProjectFiler::loadPollsters(SaveFileInput& saveInput, int versionNum)
{
	auto pollsterCount = saveInput.extract<int32_t>();
	for (int pollsterIndex = 0; pollsterIndex < pollsterCount; ++pollsterIndex) {
		Pollster thisPollster;
		saveInput >> thisPollster.name;
		if (versionNum <= 35) {
			saveInput.extract<int32_t>();
		}
		thisPollster.colour = saveInput.extract<uint64_t>();
		if (versionNum <= 35) {
			saveInput.extract<bool>();
			saveInput.extract<bool>();
		}
		project.pollsterCollection.add(thisPollster);
	}
}

void ProjectFiler::savePolls(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.pollCollection.count());
	saveOutput << project.pollCollection.sourceFile;
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
	if (versionNum >= 14) {
		saveInput >> project.pollCollection.sourceFile;
	}
	for (int pollIndex = 0; pollIndex < pollCount; ++pollIndex) {
		Poll thisPoll;
		thisPoll.pollster = saveInput.extract<int32_t>();
		thisPoll.date.SetDay(1); // prevent issue with following commands resulting in an invalid date
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

void ProjectFiler::loadEvents(SaveFileInput& saveInput, int versionNum)
{
	// "Events" are obsolete, but for legacy files we still need to extract the data
	// to match the save format, and just not do anything with it
	if (versionNum <= 35) {
		auto eventCount = saveInput.extract<int32_t>();
		for (int eventIndex = 0; eventIndex < eventCount; ++eventIndex) {
			saveInput.extract<std::string>();
			saveInput.extract<int32_t>();
			saveInput.extract<double>();
			saveInput.extract<float>();
		}
	}
}

void saveSeries(SaveFileOutput& saveOutput, StanModel::Series const& series)
{
	saveOutput.outputAsType<int32_t>(series.timePoint.size());
	for (auto day : series.timePoint) {
		saveOutput.outputAsType<int32_t>(day.values.size());
		for (auto spreadVal : day.values) {
			saveOutput << spreadVal;
		}
		saveOutput << day.expectation;
	}
}

void ProjectFiler::saveModels(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.modelCollection.count());
	for (auto const& [key, thisModel] : project.modelCollection) {
		saveOutput << thisModel.name;
		saveOutput << thisModel.termCode;
		saveOutput << thisModel.partyCodes;
		saveOutput << thisModel.preferenceFlow;
		saveOutput << thisModel.preferenceDeviation;
		saveOutput << thisModel.preferenceSamples;
		saveOutput << thisModel.startDate.GetJulianDayNumber();
		saveOutput << thisModel.lastUpdatedDate.GetJulianDayNumber();
		saveOutput.outputAsType<uint32_t>(thisModel.rawSupport.size());
		for (auto [seriesKey, series] : thisModel.rawSupport) {
			saveOutput << seriesKey;
			saveSeries(saveOutput, series);
		}
		saveOutput.outputAsType<uint32_t>(thisModel.adjustedSupport.size());
		for (auto [seriesKey, series] : thisModel.adjustedSupport) {
			saveOutput << seriesKey;
			saveSeries(saveOutput, series);
		}
		saveSeries(saveOutput, thisModel.tppSupport);
	}
}

StanModel::Series loadSeries(SaveFileInput& saveInput, int versionNum)
{
	StanModel::Series thisSeries;
	size_t numTimePoints = saveInput.extract<uint32_t>();
	for (size_t timePointIndex = 0; timePointIndex < numTimePoints; ++timePointIndex) {
		StanModel::Spread spread;
		size_t numValues = saveInput.extract<uint32_t>();
		for (size_t valueIndex = 0; valueIndex < numValues; ++valueIndex) {
			float spreadVal = saveInput.extract<float>();
			if (valueIndex < StanModel::Spread::Size) {
				spread.values[valueIndex] = spreadVal;
			}
		}
		if (versionNum >= 6) {
			saveInput >> spread.expectation;
		}
		thisSeries.timePoint.push_back(spread);
	}
	return thisSeries;
}

void ProjectFiler::loadModels(SaveFileInput& saveInput, int versionNum)
{
	auto modelCount = saveInput.extract<int32_t>();
	for (int modelIndex = 0; modelIndex < modelCount; ++modelIndex) {
		StanModel thisModel;
		saveInput >> thisModel.name;
		// Handle legacy model settings not used any more
		if (versionNum <= 1) {
			saveInput.extract<int32_t>();
			for (int i = 0; i < 4; ++i) saveInput.extract<float>();
			for (int i = 0; i < 3; ++i) saveInput.extract<double>();
			auto dayCount = saveInput.extract<int32_t>();
			for (int day = 0; day < dayCount; ++day) {
				saveInput.extract<float>();
			}
		}
		if (versionNum >= 3) {
			saveInput >> thisModel.termCode;
			saveInput >> thisModel.partyCodes;
			if (versionNum >= 5 && versionNum <= 14) {
				for (int i = 0; i < 2; ++i) saveInput.extract<std::string>();
			}
			if (versionNum >= 6) {
				saveInput >> thisModel.preferenceFlow;
			}
			if (versionNum >= 7) {
				saveInput >> thisModel.preferenceDeviation;
				saveInput >> thisModel.preferenceSamples;
			}
			if (versionNum >= 11 && versionNum <= 14) {
				for (int i = 0; i < 6; ++i) saveInput.extract<std::string>();
			}
			thisModel.startDate = wxDateTime(saveInput.extract<double>());
			thisModel.lastUpdatedDate = wxDateTime(saveInput.extract<double>());
			size_t numSeries = saveInput.extract<uint32_t>();
			for (size_t seriesIndex = 0; seriesIndex < numSeries; ++seriesIndex) {
				std::string seriesKey = saveInput.extract<std::string>();
				auto thisSeries = loadSeries(saveInput, versionNum);
				thisModel.rawSupport.insert({ seriesKey, thisSeries });
			}
			if (versionNum >= 5) {
				numSeries = saveInput.extract<uint32_t>();
				for (size_t seriesIndex = 0; seriesIndex < numSeries; ++seriesIndex) {
					std::string seriesKey = saveInput.extract<std::string>();
					auto thisSeries = loadSeries(saveInput, versionNum);
					thisModel.adjustedSupport.insert({ seriesKey, thisSeries });
				}
			}
			if (versionNum >= 6) {
				thisModel.tppSupport = loadSeries(saveInput, versionNum);
			}
		}
		project.modelCollection.add(thisModel);
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
		saveOutput << thisProjection.getSettings().possibleDates;
		saveOutput << thisProjection.getLastUpdatedDate().GetJulianDayNumber();
		saveOutput.outputAsType<uint32_t>(thisProjection.projectedSupport.size());
		for (auto [seriesKey, series] : thisProjection.projectedSupport) {
			saveOutput << seriesKey;
			saveSeries(saveOutput, series);
		}
		saveSeries(saveOutput, thisProjection.tppSupport);
	}
}

void ProjectFiler::loadProjections(SaveFileInput& saveInput, int versionNum)
{
	int projectionCount = saveInput.extract<int32_t>();
	for (int projectionIndex = 0; projectionIndex < projectionCount; ++projectionIndex) {
		Projection thisProjection;
		saveInput >> thisProjection.settings.name;
		thisProjection.settings.numIterations = saveInput.extract<int32_t>();
		thisProjection.settings.baseModel = saveInput.extract<int32_t>();
		thisProjection.settings.endDate = wxDateTime(saveInput.extract<double>());
		if (versionNum >= 28) {
			saveInput >> thisProjection.settings.possibleDates;
		}
		thisProjection.lastUpdated = wxDateTime(saveInput.extract<double>());
		if (versionNum <= 7) { // some legacy data no longer needed
			for (int i = 0; i < 3; ++i) saveInput.extract<float>();
			saveInput.extract<int32_t>();
		}
		if (versionNum <= 8) {
			auto projLength = saveInput.extract<int32_t>();
			for (int dayIndex = 0; dayIndex < projLength; ++dayIndex) {
				Projection::ProjectionDay thisDay;
				saveInput >> thisDay.mean;
				saveInput >> thisDay.sd;
				thisProjection.projection.push_back(thisDay);
			}
		}
		if (versionNum >= 10) {
			size_t numSeries = saveInput.extract<uint32_t>();
			for (size_t seriesIndex = 0; seriesIndex < numSeries; ++seriesIndex) {
				std::string seriesKey = saveInput.extract<std::string>();
				auto thisSeries = loadSeries(saveInput, versionNum);
				thisProjection.projectedSupport.insert({ seriesKey, thisSeries });
			}
			thisProjection.tppSupport = loadSeries(saveInput, versionNum);
		}
		
		project.projectionCollection.add(thisProjection);
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
		saveOutput << thisRegion.analysisCode;
		saveOutput << thisRegion.swingDeviation;
		saveOutput << thisRegion.homeRegionMod;
	}
}

void ProjectFiler::loadRegions(SaveFileInput& saveInput, int versionNum)
{
	auto regionCount = saveInput.extract<int32_t>();
	for (int regionIndex = 0; regionIndex < regionCount; ++regionIndex) {
		Region thisRegion;
		saveInput >> thisRegion.name;
		thisRegion.population = saveInput.extract<int32_t>();
		saveInput >> thisRegion.lastElection2pp;
		saveInput >> thisRegion.sample2pp;
		if (versionNum >= 33) {
			saveInput >> thisRegion.analysisCode;
		}
		saveInput >> thisRegion.swingDeviation;
		if (versionNum <= 35) {
			saveInput.extract<float>(); // additionalUncertainty
		}
		if (versionNum >= 23) {
			saveInput >> thisRegion.homeRegionMod;
		}
		project.regionCollection.add(thisRegion);
	}
}

void ProjectFiler::saveSeats(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.seatCollection.count());
	for (auto const& [key, thisSeat] : project.seatCollection) {
		saveOutput << thisSeat.name;
		saveOutput << thisSeat.previousName;
		saveOutput << thisSeat.useFpResults;
		saveOutput.outputAsType<int32_t>(project.partyCollection.idToIndex(thisSeat.incumbent));
		saveOutput.outputAsType<int32_t>(project.partyCollection.idToIndex(thisSeat.challenger));
		saveOutput.outputAsType<int32_t>(project.partyCollection.idToIndex(thisSeat.challenger2));
		saveOutput.outputAsType<int32_t>(project.regions().idToIndex(thisSeat.region));
		saveOutput << thisSeat.tppMargin;
		saveOutput << thisSeat.previousSwing;
		saveOutput << thisSeat.localModifier;
		saveOutput << thisSeat.incumbentOdds;
		saveOutput << thisSeat.challengerOdds;
		saveOutput << thisSeat.challenger2Odds;
		saveOutput << thisSeat.incumbentWinPercent;
		saveOutput << thisSeat.tippingPointPercent;
		saveOutput << 0.0;
		saveOutput.outputAsType<int32_t>(project.partyCollection.idToIndex(thisSeat.livePartyOne));
		saveOutput.outputAsType<int32_t>(project.partyCollection.idToIndex(thisSeat.livePartyTwo));
		saveOutput.outputAsType<int32_t>(project.partyCollection.idToIndex(thisSeat.livePartyThree));
		saveOutput << thisSeat.partyTwoProb;
		saveOutput << thisSeat.partyThreeProb;
		saveOutput << thisSeat.overrideBettingOdds;
		saveOutput.outputAsType<int32_t>(thisSeat.liveUseTpp);
		saveOutput << thisSeat.sophomoreCandidate;
		saveOutput << thisSeat.sophomoreParty;
		saveOutput << thisSeat.retirement;
		saveOutput << thisSeat.disendorsement;
		saveOutput << thisSeat.previousDisendorsement;
		saveOutput << thisSeat.incumbentRecontestConfirmed;
		saveOutput << thisSeat.confirmedProminentIndependent;
		saveOutput << thisSeat.prominentMinors;
		saveOutput << thisSeat.bettingOdds;
		saveOutput << thisSeat.polls;
		saveOutput << thisSeat.runningParties;
		saveOutput << thisSeat.tcpChange;
		saveOutput << thisSeat.previousIndRunning;
	}
}

void ProjectFiler::loadSeats(SaveFileInput& saveInput, [[maybe_unused]] int versionNum)
{
	auto seatCount = saveInput.extract<int32_t>();
	for (int seatIndex = 0; seatIndex < seatCount; ++seatIndex) {
		Seat thisSeat;
		saveInput >> thisSeat.name;
		saveInput >> thisSeat.previousName;
		if (versionNum >= 21) {
			saveInput >> thisSeat.useFpResults;
		}
		thisSeat.incumbent = saveInput.extract<int32_t>();
		thisSeat.challenger = saveInput.extract<int32_t>();
		thisSeat.challenger2 = saveInput.extract<int32_t>();
		thisSeat.region = saveInput.extract<int32_t>();
		saveInput >> thisSeat.tppMargin;
		if (versionNum >= 34) saveInput >> thisSeat.previousSwing;
		saveInput >> thisSeat.localModifier;
		saveInput >> thisSeat.incumbentOdds;
		saveInput >> thisSeat.challengerOdds;
		saveInput >> thisSeat.challenger2Odds;
		saveInput >> thisSeat.incumbentWinPercent;
		saveInput >> thisSeat.tippingPointPercent;
		saveInput.extract<double>(); // old simulatedMarginAverage
		thisSeat.livePartyOne = saveInput.extract<int32_t>();
		thisSeat.livePartyTwo = saveInput.extract<int32_t>();
		thisSeat.livePartyThree = saveInput.extract<int32_t>();
		saveInput >> thisSeat.partyTwoProb;
		saveInput >> thisSeat.partyThreeProb;
		saveInput >> thisSeat.overrideBettingOdds;
		if (versionNum >= 47) {
			thisSeat.liveUseTpp = Seat::UseTpp(saveInput.extract<int32_t>());
		}
		if (versionNum >= 22) {
			saveInput >> thisSeat.sophomoreCandidate;
			saveInput >> thisSeat.sophomoreParty;
			saveInput >> thisSeat.retirement;
		}
		if (versionNum >= 35) {
			saveInput >> thisSeat.disendorsement;
			saveInput >> thisSeat.previousDisendorsement;
		}
		if (versionNum >= 41) {
			saveInput >> thisSeat.incumbentRecontestConfirmed;
			saveInput >> thisSeat.confirmedProminentIndependent;
		}
		if (versionNum >= 42) {
			saveInput >> thisSeat.prominentMinors;
		}
		if (versionNum >= 43) {
			saveInput >> thisSeat.bettingOdds;
		}
		if (versionNum >= 44) {
			saveInput >> thisSeat.polls;
		}
		if (versionNum >= 46) {
			saveInput >> thisSeat.runningParties;
		}
		if (versionNum >= 48) {
			saveInput >> thisSeat.tcpChange;
		}
		if (versionNum >= 49) {
			saveInput >> thisSeat.previousIndRunning;
		}

		project.seatCollection.add(thisSeat);
	}
}

void saveReport(SaveFileOutput& saveOutput, Simulation::Report const& report)
{
	saveOutput << report.majorityPercent;
	saveOutput << report.minorityPercent;
	saveOutput << report.mostSeatsPercent;
	saveOutput << report.tiedPercent;
	saveOutput << report.partyWinExpectation;
	saveOutput << report.partyWinMedian;
	saveOutput << report.regionPartyWinExpectation;
	saveOutput << report.partySeatWinFrequency;
	saveOutput << report.othersWinFrequency;
	saveOutput << report.total2cpPercentCounted;
	saveOutput << report.partyOneProbabilityBounds;
	saveOutput << report.partyTwoProbabilityBounds;
	saveOutput << report.othersProbabilityBounds;
	saveOutput << report.partyAbbr;
	saveOutput << report.partyName;
	saveOutput << report.partyColour;
	saveOutput << report.regionName;
	saveOutput << report.seatName;
	saveOutput << report.seatIncumbents;
	saveOutput << report.seatMargins;
	saveOutput << report.seatIncumbentMargins;
	saveOutput << report.seatPartyOneMarginAverage;
	saveOutput << report.partyOneWinPercent;
	saveOutput << report.partyTwoWinPercent;
	saveOutput << report.othersWinPercent;
	saveOutput << report.seatPartyWinPercent;
	saveOutput << report.probabilityBands;
	saveOutput << report.seatFpProbabilityBand;
	saveOutput << report.seatTcpProbabilityBand;
	saveOutput << report.seatTcpScenarioPercent;
	saveOutput << report.seatTcpWinPercent;
	saveOutput << report.classicSeatIndices;
	saveOutput << report.regionPartyIncuments;
	saveOutput << report.prevElection2pp;
	saveOutput << report.partyPrimaryFrequency;
	saveOutput << report.tppFrequency;
	saveOutput << report.trendProbBands;
	saveOutput << report.trendPeriod;
	saveOutput << report.finalTrendValue;
	saveOutput << report.trendStartDate;
	saveOutput << report.tppTrend;
	saveOutput << report.fpTrend;
	saveOutput << report.getSaveablePolls();
}

Simulation::Report loadReport(SaveFileInput& saveInput, int versionNum)
{
	Simulation::Report report;
	if (versionNum >= 30) {
		saveInput >> report.majorityPercent;
		saveInput >> report.minorityPercent;
		saveInput >> report.mostSeatsPercent;
	}
	else {
		std::array<float, 2> temp;
		saveInput >> temp;
		report.majorityPercent[0] = temp[0];
		report.majorityPercent[1] = temp[1];
		saveInput >> temp;
		report.minorityPercent[0] = temp[0];
		report.minorityPercent[1] = temp[1];
	}
	saveInput >> report.tiedPercent;
	if (versionNum >= 27) {
		saveInput >> report.partyWinExpectation;
		saveInput >> report.partyWinMedian;
		saveInput >> report.regionPartyWinExpectation;
		saveInput >> report.partySeatWinFrequency;
	}
	else {
		std::vector<float> winExpectationVec; saveInput >> winExpectationVec;
		for (int i = 0; i < int(winExpectationVec.size()); ++i) {
			report.partyWinExpectation[i] = winExpectationVec[i];
		}
		if (versionNum >= 17) {
			std::vector<float> winMedianVec; saveInput >> winMedianVec;
			for (int i = 0; i < int(winMedianVec.size()); ++i) {
				report.partyWinMedian[i] = winMedianVec[i];
			}
		}
		std::vector<std::vector<float>> regionWinExpectationVec; saveInput >> regionWinExpectationVec;
		for (int i = 0; i < int(regionWinExpectationVec.size()); ++i) {
			auto const& subVec = regionWinExpectationVec[i];
			std::map<int, float> subMap;
			for (int j = 0; j < int(regionWinExpectationVec[i].size()); ++j) {
				subMap[j] = subVec[j];
			}
			report.regionPartyWinExpectation.push_back(subMap);
		}
		std::vector<std::vector<int>> winFrequencyVec; saveInput >> winFrequencyVec;
		for (int i = 0; i < int(winExpectationVec.size()); ++i) {
			report.partySeatWinFrequency[i] = winFrequencyVec[i];
		}
	}
	saveInput >> report.othersWinFrequency;
	saveInput >> report.total2cpPercentCounted;
	saveInput >> report.partyOneProbabilityBounds;
	saveInput >> report.partyTwoProbabilityBounds;
	saveInput >> report.othersProbabilityBounds;
	if (versionNum >= 26) {
		saveInput >> report.partyAbbr;
		saveInput >> report.partyName;
		saveInput >> report.partyColour;
	}
	else {
		std::vector<std::string> abbrs; saveInput >> abbrs;
		std::vector<std::string> names; saveInput >> names;
		std::vector<Party::Colour> colours; saveInput >> colours;
		for (int i = 0; i < int(abbrs.size()); ++i) {
			report.partyAbbr[i] = abbrs[i];
			report.partyName[i] = names[i];
			report.partyColour[i] = colours[i];
		}
	}
	saveInput >> report.regionName;
	saveInput >> report.seatName;
	saveInput >> report.seatIncumbents;
	saveInput >> report.seatMargins;
	if (versionNum >= 40) {
		saveInput >> report.seatIncumbentMargins;
	}
	if (versionNum <= 24) {
		std::vector<float> tempObj;
		saveInput >> tempObj;
		if (versionNum <= 18) {
			report.partyOneWinPercent.resize(report.seatName.size());
			for (int seatIndex = 0; seatIndex < int(report.partyOneWinPercent.size()); ++seatIndex) {
				if (report.seatIncumbents[seatIndex] == 1) {
					report.partyOneWinPercent[seatIndex] = 100.0f - tempObj[seatIndex];
				}
				else {
					report.partyOneWinPercent[seatIndex] = tempObj[seatIndex];
				}
			}
		}
	}
	if (versionNum >= 19) {
		saveInput >> report.seatPartyOneMarginAverage;
		saveInput >> report.partyOneWinPercent;
		saveInput >> report.partyTwoWinPercent;
		saveInput >> report.othersWinPercent;
		saveInput >> report.seatPartyWinPercent;
	}
	if (!report.seatPartyOneMarginAverage.size()) {
		report.seatPartyOneMarginAverage.resize(report.seatName.size());
		report.partyOneWinPercent.resize(report.seatName.size());
		report.partyTwoWinPercent.resize(report.seatName.size());
		report.othersWinPercent.resize(report.seatName.size());
		report.seatPartyWinPercent.resize(report.seatName.size());
	}

	typedef std::array<float, 9> ProbabilityBands;
	if (versionNum >= 31 && versionNum <= 38) {
		// Not worth keeping legacy probability bands, just consume them
		// The old reports will likely be regenerated anyway
		std::vector<std::map<int, ProbabilityBands>> legacyFpBands;
		saveInput >> legacyFpBands;
	}
	if (versionNum >= 39) {
		saveInput >> report.probabilityBands;
		saveInput >> report.seatFpProbabilityBand;
	}
	if (versionNum >= 32) {
		if (versionNum <= 38) {
			// Not worth keeping legacy probability bands, just consume them
			// The old reports will likely be regenerated anyway
			std::vector<std::map<std::pair<int, int>, ProbabilityBands>> legacyTcpBands;
			saveInput >> legacyTcpBands;
		}
		if (versionNum >= 39) {
			saveInput >> report.seatTcpProbabilityBand;
		}
		saveInput >> report.seatTcpScenarioPercent;
		saveInput >> report.seatTcpWinPercent;
	}
	saveInput >> report.classicSeatIndices;
	saveInput >> report.regionPartyIncuments;
	saveInput >> report.prevElection2pp;
	if (versionNum >= 18) {
		if (versionNum >= 27) {
			saveInput >> report.partyPrimaryFrequency;
		}
		else {
			std::vector<std::map<short, int>> primaryFrequencyVec; saveInput >> primaryFrequencyVec;
			for (int i = 0; i < int(primaryFrequencyVec.size()); ++i) {
				report.partyPrimaryFrequency[i] = primaryFrequencyVec[i];
			}
		}
		saveInput >> report.tppFrequency;
	}
	if (versionNum >= 42) {
		saveInput >> report.trendProbBands;
		saveInput >> report.trendPeriod;
		saveInput >> report.finalTrendValue;
		saveInput >> report.trendStartDate;
		saveInput >> report.tppTrend;
		saveInput >> report.fpTrend;
	}
	if (versionNum >= 45) {
		Simulation::Report::SaveablePolls saveablePolls;
		saveInput >> saveablePolls;
		report.retrieveSaveablePolls(saveablePolls);
	}
	return report;
}

void ProjectFiler::saveSimulations(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.simulationCollection.count());
	for (auto const& [key, thisSimulation] : project.simulationCollection) {
		saveOutput << thisSimulation.getSettings().name;
		saveOutput.outputAsType<int32_t>(thisSimulation.getSettings().numIterations);
		saveOutput.outputAsType<int32_t>(project.projections().idToIndex(thisSimulation.getSettings().baseProjection));
		saveOutput << thisSimulation.getSettings().prevElection2pp;
		saveOutput << thisSimulation.getSettings().prevTermCodes;
		saveOutput.outputAsType<int32_t>(thisSimulation.getSettings().live);
		saveOutput.outputAsType<int32_t>(thisSimulation.getSettings().reportMode);
		saveOutput << thisSimulation.getSettings().previousResultsUrl;
		saveOutput << thisSimulation.getSettings().preloadUrl;
		saveOutput << thisSimulation.getSettings().currentTestUrl;
		saveOutput << thisSimulation.getSettings().currentRealUrl;
		saveOutput << thisSimulation.lastUpdated.GetJulianDayNumber();
		saveReport(saveOutput, thisSimulation.latestReport);
		saveOutput.outputAsType<uint32_t>(thisSimulation.savedReports.size());
		for (auto const& savedReport : thisSimulation.savedReports) {
			saveOutput << savedReport.label;
			saveOutput << savedReport.dateSaved.GetJulianDayNumber();
			saveReport(saveOutput, savedReport.report);
		}
	}
}

void ProjectFiler::loadSimulations(SaveFileInput& saveInput, [[maybe_unused]] int versionNum)
{
	int simulationCount = saveInput.extract<int32_t>();
	for (int simulationIndex = 0; simulationIndex < simulationCount; ++simulationIndex) {
		Simulation::Settings thisSettings;
		saveInput >> thisSettings.name;
		thisSettings.numIterations = saveInput.extract<int32_t>();
		thisSettings.baseProjection = Projection::Id(saveInput.extract<int32_t>());
		saveInput >> thisSettings.prevElection2pp;
		if (versionNum >= 20) {
			saveInput >> thisSettings.prevTermCodes;
		}
		if (versionNum <= 35) {
			saveInput.extract<float>();
			saveInput.extract<float>();
		}
		thisSettings.live = Simulation::Settings::Mode(saveInput.extract<int32_t>());
		if (versionNum >= 38) {
			thisSettings.reportMode = Simulation::Settings::ReportMode(saveInput.extract<int32_t>());
		}
		if (versionNum >= 50) {
			saveInput >> thisSettings.previousResultsUrl;
		}
		if (versionNum >= 51) {
			saveInput >> thisSettings.preloadUrl;
			saveInput >> thisSettings.currentTestUrl;
			saveInput >> thisSettings.currentRealUrl;
		}
		Simulation thisSimulation = Simulation(thisSettings);
		if (versionNum >= 12) {
			thisSimulation.lastUpdated = wxDateTime(saveInput.extract<double>());
			thisSimulation.latestReport = loadReport(saveInput, versionNum);
		}
		if (versionNum >= 13) {
			size_t numReports = saveInput.extract<uint32_t>();
			for (size_t reportIndex = 0; reportIndex < numReports; ++reportIndex) {
				Simulation::SavedReport savedReport;
				saveInput >> savedReport.label;
				savedReport.dateSaved = wxDateTime(saveInput.extract<double>());
				savedReport.report = loadReport(saveInput, versionNum);
				thisSimulation.savedReports.push_back(savedReport);
			}
		}
		project.simulationCollection.add(thisSimulation);
	}
}

void ProjectFiler::saveOutcomes(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.outcomeCollection.count());
	for (auto const& thisOutcome : project.outcomeCollection) {
		saveOutput.outputAsType<int32_t>(project.seats().idToIndex(thisOutcome.seat));
		saveOutput << thisOutcome.partyOneSwing;
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
		saveInput >> thisOutcome.partyOneSwing;
		saveInput >> thisOutcome.percentCounted;
		thisOutcome.boothsIn = saveInput.extract<int32_t>();
		thisOutcome.totalBooths = saveInput.extract<int32_t>();
		thisOutcome.updateTime = wxDateTime(saveInput.extract<double>());
		project.outcomeCollection.add(thisOutcome);
	}
}

void ProjectFiler::saveElections(SaveFileOutput& saveOutput)
{
	saveOutput.outputAsType<int32_t>(project.electionCollection.count());
	for (auto const& [electionKey, thisElection] : project.electionCollection) {
		saveOutput << thisElection.id; // this is the same as the key
		saveOutput << thisElection.name;
		saveOutput.outputAsType<int32_t>(thisElection.parties.size());
		for (auto const& [key, thisParty] : thisElection.parties) {
			saveOutput << thisParty.id;
			saveOutput << thisParty.name;
			saveOutput << thisParty.shortCode;
		}
		saveOutput.outputAsType<int32_t>(thisElection.candidates.size());
		for (auto const& [key, thisCandidate] : thisElection.candidates) {
			saveOutput << thisCandidate.id;
			saveOutput << thisCandidate.name;
			saveOutput << thisCandidate.party;
		}
		saveOutput.outputAsType<int32_t>(thisElection.booths.size());
		for (auto const& [key, thisBooth] : thisElection.booths) {
			saveOutput << thisBooth.id;
			saveOutput << thisBooth.name;
			saveOutput << thisBooth.type;
			saveOutput << thisBooth.fpVotes;
			saveOutput << thisBooth.tcpVotes;
		}
		saveOutput.outputAsType<int32_t>(thisElection.seats.size());
		for (auto const& [key, thisSeat] : thisElection.seats) {
			saveOutput.outputAsType<int32_t>(thisSeat.id);
			saveOutput << thisSeat.name;
			saveOutput.outputAsType<int32_t>(thisSeat.enrolment);
			saveOutput << thisSeat.booths;
			saveOutput << thisSeat.ordinaryVotesFp;
			saveOutput << thisSeat.absentVotesFp;
			saveOutput << thisSeat.provisionalVotesFp;
			saveOutput << thisSeat.prepollVotesFp;
			saveOutput << thisSeat.postalVotesFp;
			saveOutput << thisSeat.ordinaryVotesTcp;
			saveOutput << thisSeat.absentVotesTcp;
			saveOutput << thisSeat.provisionalVotesTcp;
			saveOutput << thisSeat.prepollVotesTcp;
			saveOutput << thisSeat.postalVotesTcp;
		}
	}
}

void ProjectFiler::loadElections(SaveFileInput& saveInput, [[maybe_unused]] int versionNum)
{
	auto electionCount = saveInput.extract<int32_t>();
	for (int electionIndex = 0; electionIndex < electionCount; ++electionIndex) {
		Results2::Election thisElection;
		saveInput >> thisElection.id; // this is the same as the key
		saveInput >> thisElection.name;
		auto partyCount = saveInput.extract<int32_t>();
		for (int partyIndex = 0; partyIndex < partyCount; ++partyIndex) {
			Results2::Party thisParty;
			saveInput >> thisParty.id;
			saveInput >> thisParty.name;
			saveInput >> thisParty.shortCode;
			thisElection.parties[thisParty.id] = thisParty;
		}
		auto candidateCount = saveInput.extract<int32_t>();
		for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex) {
			Results2::Candidate thisCandidate;
			saveInput >> thisCandidate.id;
			saveInput >> thisCandidate.name;
			saveInput >> thisCandidate.party;
			thisElection.candidates[thisCandidate.id] = thisCandidate;
		}
		auto boothCount = saveInput.extract<int32_t>();
		for (int boothIndex = 0; boothIndex < boothCount; ++boothIndex) {
			Results2::Booth thisBooth;
			saveInput >> thisBooth.id;
			saveInput >> thisBooth.name;
			saveInput >> thisBooth.type;
			saveInput >> thisBooth.fpVotes;
			saveInput >> thisBooth.tcpVotes;
			thisElection.booths[thisBooth.id] = thisBooth;
		}
		auto seatCount = saveInput.extract<int32_t>();
		for (int seatIndex = 0; seatIndex < seatCount; ++seatIndex) {
			Results2::Seat thisSeat;
			thisSeat.id = saveInput.extract<int32_t>();
			saveInput >> thisSeat.name;
			thisSeat.enrolment = saveInput.extract<int32_t>();
			saveInput >> thisSeat.booths;
			saveInput >> thisSeat.ordinaryVotesFp;
			saveInput >> thisSeat.absentVotesFp;
			saveInput >> thisSeat.provisionalVotesFp;
			saveInput >> thisSeat.prepollVotesFp;
			saveInput >> thisSeat.postalVotesFp;
			saveInput >> thisSeat.ordinaryVotesTcp;
			saveInput >> thisSeat.absentVotesTcp;
			saveInput >> thisSeat.provisionalVotesTcp;
			saveInput >> thisSeat.prepollVotesTcp;
			saveInput >> thisSeat.postalVotesTcp;
			thisElection.seats[thisSeat.id] = thisSeat;
		}
		project.electionCollection.add(thisElection);
	}
}