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
	simulationCollection(*this)
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
	simulationCollection(*this)
{
	logger << lastFileName << "\n";
	open(pathName);
}

void PollingProject::incorporatePreviousElectionResults(PreviousElectionDataRetriever const& dataRetriever)
{
	int seatMatchCount = 0;
	for (auto seatIt = dataRetriever.beginSeats(); seatIt != dataRetriever.endSeats(); ++seatIt) {
		auto seatData = seatIt->second;
		auto seatMatchFunc = [seatData](SeatCollection::SeatContainer::value_type const& seat)
			{ return seat.second.name == seatData.name || seat.second.previousName == seatData.name; };
		auto matchedSeat = std::find_if(seats().begin(), seats().end(), seatMatchFunc);
		if (matchedSeat != seats().end()) {
			matchedSeat->second.officialId = seatData.officialId;
			matchedSeat->second.previousResults = seatData;
			++seatMatchCount;
		}
		else {
			logger << "Note - No seat match found for " << seatData.name << ".\n";
			logger << "If this seat was abolished then this is ok, otherwise check the spelling of the existing seat data.\n";
			logger << "Also check that the existing seat data download link is correct and hasn't been set to skip the download.\n";
		}
	}
	logger << seatMatchCount << " seats matched.\n";
	std::copy(dataRetriever.beginBooths(), dataRetriever.endBooths(), std::inserter(booths, booths.end()));
	std::copy(dataRetriever.beginCandidates(), dataRetriever.endCandidates(), std::inserter(candidates, candidates.end()));
	collectAffiliations(dataRetriever);

	candidateParties.insert({ -1, Party::InvalidId });
	for (auto candidateIt = dataRetriever.beginCandidates(); candidateIt != dataRetriever.endCandidates(); ++candidateIt) {
		candidates.insert(*candidateIt);
		int affiliationId = candidateIt->second.affiliationId;
		auto affiliationIt = affiliationParties.find(affiliationId);
		if (affiliationIt != affiliationParties.end()) {
			candidateParties.insert({ candidateIt->first, affiliationIt->second });
			candidateAffiliations.insert({ candidateIt->first, affiliationId });
		}
		else {
			// treat unknown party as independent
			candidateParties.insert({ candidateIt->first, affiliationParties[0] });
			candidateAffiliations.insert({ candidateIt->first, -1 });
		}
	}
}

void PollingProject::incorporatePreloadData(PreloadDataRetriever const& dataRetriever)
{
	collectCandidatesFromPreload(dataRetriever);
	collectBoothsFromPreload(dataRetriever);
}

void PollingProject::incorporateLatestResults(LatestResultsDataRetriever const& dataRetriever)
{
	for (auto booth = dataRetriever.beginBooths(); booth != dataRetriever.endBooths(); ++booth) {
		auto const& newBooth = booth->second;

		// Determine which booth (if any) from the previous election this corresponds to
		auto oldBoothIt = booths.find(newBooth.officialId);
		if (oldBoothIt == booths.end()) {
			continue;
		}
		auto& matchedBooth = oldBoothIt->second;
		matchedBooth.fpCandidates = newBooth.fpCandidates; // always record fp candidates regardless of whether booth matching is successful

		// Check if the partyCollection match
		bool allValid = true;
		Party::Id newParty[2] = { candidateParties[newBooth.tcpCandidateId[0]], candidateParties[newBooth.tcpCandidateId[1]] };
		Party::Id oldParty[2] = { affiliationParties[matchedBooth.tcpAffiliationId[0]], affiliationParties[matchedBooth.tcpAffiliationId[1]] };
		for (auto& a : newParty) if (a == Party::InvalidId) { allValid = false; };
		for (auto& a : oldParty) if (a == Party::InvalidId) { allValid = false; };
		bool matchedDirect = newParty[0] == oldParty[0] && (newParty[1] == oldParty[1]) && allValid;
		bool matchedOpposite = newParty[0] == oldParty[1] && (newParty[1] == oldParty[0]) && allValid;
		bool noOldResults = !matchedBooth.hasOldResults(); // no old results, therefore don't need to match for swing purposes, just get the results in whatever order
		bool newResults = newBooth.totalNewTcpVotes();

		if (matchedDirect || matchedOpposite || noOldResults) {
			if (matchedDirect) {
				matchedBooth.newTcpVote[0] = newBooth.newTcpVote[0];
				matchedBooth.newTcpVote[1] = newBooth.newTcpVote[1];
				matchedBooth.tcpCandidateId[0] = newBooth.tcpCandidateId[0];
				matchedBooth.tcpCandidateId[1] = newBooth.tcpCandidateId[1];
				matchedBooth.newResultsZero = newBooth.newResultsZero;
			}
			else if (matchedOpposite) {
				matchedBooth.newTcpVote[0] = newBooth.newTcpVote[1];
				matchedBooth.newTcpVote[1] = newBooth.newTcpVote[0];
				matchedBooth.tcpCandidateId[0] = newBooth.tcpCandidateId[1];
				matchedBooth.tcpCandidateId[1] = newBooth.tcpCandidateId[0];
				matchedBooth.newResultsZero = newBooth.newResultsZero;
			}
			else if (noOldResults) {
				matchedBooth.newTcpVote[0] = newBooth.newTcpVote[0];
				matchedBooth.newTcpVote[1] = newBooth.newTcpVote[1];
				matchedBooth.tcpCandidateId[0] = newBooth.tcpCandidateId[0];
				matchedBooth.tcpCandidateId[1] = newBooth.tcpCandidateId[1];
				matchedBooth.tcpAffiliationId[0] = candidateAffiliations[newBooth.tcpCandidateId[0]];
				matchedBooth.tcpAffiliationId[1] = candidateAffiliations[newBooth.tcpCandidateId[1]];
				matchedBooth.newResultsZero = newBooth.newResultsZero;
			}
		}
		else if (newResults) {
			// Could not match partyCollection are there are some results, wipe previous results
			matchedBooth.newTcpVote[0] = newBooth.newTcpVote[0];
			matchedBooth.newTcpVote[1] = newBooth.newTcpVote[1];
			matchedBooth.tcpCandidateId[0] = newBooth.tcpCandidateId[0];
			matchedBooth.tcpCandidateId[1] = newBooth.tcpCandidateId[1];
			matchedBooth.tcpAffiliationId[0] = candidateAffiliations[newBooth.tcpCandidateId[0]];
			matchedBooth.tcpAffiliationId[1] = candidateAffiliations[newBooth.tcpCandidateId[1]];
			matchedBooth.tcpVote[0] = 0;
			matchedBooth.tcpVote[1] = 0;
			matchedBooth.newResultsZero = newBooth.newResultsZero;
		}
	}

	for (auto retrievedSeat = dataRetriever.beginSeats(); retrievedSeat != dataRetriever.endSeats(); ++retrievedSeat) {
		auto seatData = retrievedSeat->second;
		auto seatMatchFunc = [seatData](SeatCollection::SeatContainer::value_type const& seat)
		{ return seat.second.name == seatData.name || seat.second.previousName == seatData.name; };
		auto matchingSeatPair = std::find_if(seats().begin(), seats().end(), seatMatchFunc);
		Seat& matchingSeat = matchingSeatPair->second;
		matchingSeat.latestResults = retrievedSeat->second;
		for (auto& candidate : matchingSeat.latestResults->finalCandidates) {
			candidate.affiliationId = candidateAffiliations[candidate.candidateId];
		}
		for (auto& candidate : matchingSeat.latestResults->fpCandidates) {
			candidate.affiliationId = candidateAffiliations[candidate.candidateId];
		}
		// *** need something here to check if two-candidate preferred is not recorded because of seat maverick status
		if (matchingSeat.latestResults->total2cpVotes() && matchingSeat.latestResults->classic2pp) {
			Party::Id partyOne = candidateParties[matchingSeat.latestResults->finalCandidates[0].candidateId];
			Party::Id partyTwo = candidateParties[matchingSeat.latestResults->finalCandidates[1].candidateId];
			if (!parties().oppositeMajors(partyOne, partyTwo)) matchingSeat.latestResults->classic2pp = false;
		}
		if (matchingSeat.previousResults.has_value() && matchingSeat.previousResults->total2cpVotes() && matchingSeat.previousResults->classic2pp) {
			Party::Id partyOne = affiliationParties[matchingSeat.previousResults->finalCandidates[0].affiliationId];
			Party::Id partyTwo = affiliationParties[matchingSeat.previousResults->finalCandidates[1].affiliationId];
			if (!parties().oppositeMajors(partyOne, partyTwo)) matchingSeat.previousResults->classic2pp = false;
		}
	}

	// Code below stores information

	updateLatestResultsForSeats(); // only overwrite different results
	wxDateTime dateTime = wxDateTime::Now();
	for (auto& seatPair : seats()) {
		Seat& seat = seatPair.second;
		float percentCounted2cp = calculate2cpPercentComplete(seat);
		if (!percentCounted2cp) {
			if (seat.isClassic2pp(true)) continue;
			float percentCountedFp = calculateFpPercentComplete(seat);
			if (!percentCountedFp) continue;
			Result thisResult;
			thisResult.seat = seatPair.first;
			thisResult.percentCounted = percentCountedFp;
			thisResult.updateTime = dateTime;
			if (!seat.latestResult || seat.latestResult->percentCounted != percentCountedFp) {
				addResult(thisResult);
			}
			continue;
		}
		float incumbentSwing = calculateSwingToIncumbent(seat);
		Result thisResult;
		thisResult.seat = seatPair.first;
		thisResult.incumbentSwing = incumbentSwing;
		thisResult.percentCounted = percentCounted2cp;
		thisResult.updateTime = dateTime;
		if (!seat.latestResult || seat.latestResult->percentCounted != percentCounted2cp) {
			addResult(thisResult);
		}
	}
	updateLatestResultsForSeats(); // only overwrite different results
}

void PollingProject::refreshCalc2PP() {
	for (auto it = polls().begin(); it != polls().end(); it++)
		partyCollection.recalculatePollCalc2PP(it->second);
}

void PollingProject::adjustAfterPartyRemoval(PartyCollection::Index partyIndex, Party::Id partyId)
{
	polls().adjustAfterPartyRemoval(partyIndex, partyId);
	adjustSeatsAfterPartyRemoval(partyIndex, partyId);
	adjustAffiliationsAfterPartyRemoval(partyIndex, partyId);
	adjustCandidatesAfterPartyRemoval(partyIndex, partyId);
}

void PollingProject::adjustAfterPollsterRemoval(PollsterCollection::Index /*pollsterIndex*/, Party::Id pollsterId)
{
	polls().removePollsFromPollster(pollsterId);
}

int PollingProject::getEarliestDate() const {
	int earliestDay = polls().getEarliestDate();
	for (auto modelIt = models().cbegin(); modelIt != models().cend(); ++modelIt) {
		int date = int(floor(modelIt->second.endDate.GetModifiedJulianDayNumber()));
		if (date < earliestDay) earliestDay = date;
	}
	return earliestDay;
}

int PollingProject::getLatestDate() const {
	int latestDay = polls().getLatestDate();
	for (auto modelIt = models().cbegin(); modelIt != models().cend(); ++modelIt) {
		int date = int(floor(modelIt->second.endDate.GetModifiedJulianDayNumber()));
		if (date > latestDay) latestDay = date;
	}
	for (auto projectionIt = projections().cbegin(); projectionIt != projections().cend(); ++projectionIt) {
		int date = int(floor(projectionIt->second.endDate.GetModifiedJulianDayNumber()));
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
	results.push_front(result);
}

Result PollingProject::getResult(int resultIndex) const
{
	auto it = results.begin();
	std::advance(it, resultIndex);
	return *it;
}

int PollingProject::getResultCount() const
{
	return results.size();
}

std::list<Result>::iterator PollingProject::getResultBegin()
{
	return results.begin();
}

std::list<Result>::iterator PollingProject::getResultEnd()
{
	return results.end();
}

Results::Booth const& PollingProject::getBooth(int boothId) const
{
	return booths.at(boothId);
}

Point2Df PollingProject::boothLatitudeRange() const
{
	if (!booths.size()) return { 0.0f, 0.0f };
	bool latitudeInitiated = false;
	float minLatitude = 0.0f;
	float maxLatitude = 0.0f;
	for (auto const& booth : booths) {
		float latitude = booth.second.coords.latitude;
		if (std::abs(latitude) < 0.000001f) continue; // discontinued booths won't have a location
		if (!latitudeInitiated) {
			minLatitude = latitude;
			maxLatitude = latitude;
			latitudeInitiated = true;
		}
		minLatitude = std::min(minLatitude, latitude);
		maxLatitude = std::max(maxLatitude, latitude);
	}
	if (!latitudeInitiated) return { 0.0f, 0.0f };
	return { minLatitude, maxLatitude };
}

Point2Df PollingProject::boothLongitudeRange() const
{
	if (!booths.size()) return { 0.0f, 0.0f };
	bool longitudeInitiated = false;
	float minLongitude = 0.0f;
	float maxLongitude = 0.0f;
	for (auto const& booth : booths) {
		float longitude = booth.second.coords.longitude;
		if (std::abs(longitude) < 0.000001f) continue; // discontinued booths won't have a location
		if (longitude > 180.0f) {
			logger << booth.second.name << " - bad longitude - " << longitude << "\n";
		}
		if (!longitudeInitiated) {
			minLongitude = longitude;
			maxLongitude = longitude;
			longitudeInitiated = true;
		}
		minLongitude = std::min(minLongitude, longitude);
		maxLongitude = std::max(maxLongitude, longitude);
	}
	if (!longitudeInitiated) return { 0.0f, 0.0f };
	return { minLongitude, maxLongitude };
}

Party::Id PollingProject::getPartyByCandidate(int candidateId) const
{
	auto candidateIt = candidateParties.find(candidateId);
	if (candidateIt == candidateParties.end()) return Party::InvalidId;
	return candidateIt->second;
}

Party::Id PollingProject::getPartyByAffiliation(int affiliationId) const
{
	auto affiliationIt = affiliationParties.find(affiliationId);
	if (affiliationIt == affiliationParties.end()) return Party::InvalidId;
	return affiliationIt->second;
}

Results::Candidate const * PollingProject::getCandidateById(int candidateId) const
{
	auto candidateIt = candidates.find(candidateId);
	if (candidateIt == candidates.end()) return nullptr;
	return &candidateIt->second;
}

Results::Affiliation const * PollingProject::getAffiliationById(int affiliationId) const
{
	auto affiliationIt = affiliations.find(affiliationId);
	if (affiliationIt == affiliations.end()) return nullptr;
	return &affiliationIt->second;
}

int PollingProject::getCandidateAffiliationId(int candidateId) const
{
	auto affiliationIt = candidateAffiliations.find(candidateId);
	if (affiliationIt == candidateAffiliations.end()) return -1;
	return affiliationIt->second;
}

void PollingProject::updateLatestResultsForSeats() {
	for (auto& thisResult : results) {
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
		os << "name=" << thisModel.name << "\n";
		os << "iter=" << thisModel.numIterations << "\n";
		os << "trnd=" << thisModel.trendTimeScoreMultiplier << "\n";
		os << "hsm =" << thisModel.houseEffectTimeScoreMultiplier << "\n";
		os << "cfpb=" << thisModel.calibrationFirstPartyBias << "\n";
		os << "fstd=" << thisModel.finalStandardDeviation << "\n";
		os << "strt=" << thisModel.startDate.GetJulianDayNumber() << "\n";
		os << "end =" << thisModel.endDate.GetJulianDayNumber() << "\n";
		os << "updt=" << thisModel.lastUpdated.GetJulianDayNumber() << "\n";
		for (auto const& thisDay : thisModel.day) {
			os << "$Day" << "\n";
			os << "mtnd=" << thisDay.trend2pp << "\n";
			for (int pollsterIndex = 0; pollsterIndex < int(thisDay.houseEffect.size()); ++pollsterIndex) {
				os << "he" << pollsterIndex << (pollsterIndex < 10 ? " " : "") << "=" <<
					thisDay.houseEffect[pollsterIndex] << "\n";
			}
		}
	}
	os << "#Projections" << "\n";
	for (auto const& [key, thisProjection] : projectionCollection) {
		os << "@Projection" << "\n";
		os << "name=" << thisProjection.name << "\n";
		os << "iter=" << thisProjection.numIterations << "\n";
		os << "base=" << models().idToIndex(thisProjection.baseModel) << "\n";
		os << "end =" << thisProjection.endDate.GetJulianDayNumber() << "\n";
		os << "updt=" << thisProjection.lastUpdated.GetJulianDayNumber() << "\n";
		os << "dlyc=" << thisProjection.dailyChange << "\n";
		os << "inic=" << thisProjection.initialStdDev << "\n";
		os << "vtls=" << thisProjection.leaderVoteDecay << "\n";
		os << "nele=" << thisProjection.numElections << "\n";
		for (int dayIndex = 0; dayIndex < int(thisProjection.meanProjection.size()); ++dayIndex) {
			os << "mean=" << thisProjection.meanProjection[dayIndex] << "\n";
			os << "stdv=" << thisProjection.sdProjection[dayIndex] << "\n";
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
		os << "name=" << thisSimulation.name << "\n";
		os << "iter=" << thisSimulation.numIterations << "\n";
		os << "base=" << projections().idToIndex(thisSimulation.baseProjection) << "\n";
		os << "prev=" << thisSimulation.prevElection2pp << "\n";
		os << "stsd=" << thisSimulation.stateSD << "\n";
		os << "stde=" << thisSimulation.stateDecay << "\n";
		os << "live=" << int(thisSimulation.live) << "\n";
	}
	os << "#Results" << "\n";
	for (auto const& thisResult : results) {
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
	for (auto& thisProjection : projections()) {
		if (thisProjection.second.baseModel == modelId) { thisProjection.second.lastUpdated = wxInvalidDateTime; }
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

	results.clear(); // *** remove!

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
	else if (!line.compare("#Results")) {
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
			modelCollection.add(Model());
			return true;
		}
	}
	else if (fos.section == FileSection_Projections) {
		if (!line.compare("@Projection")) {
			projectionCollection.add(Projection());
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
			simulationCollection.add(Simulation());
			return true;
		}
	}
	else if (fos.section == FileSection_Results) {
		if (!line.compare("@Result")) {
			results.push_back(Result());
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
		if (!modelCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			modelCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			modelCollection.back().numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("trnd=")) {
			modelCollection.back().trendTimeScoreMultiplier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("hsm =")) {
			modelCollection.back().houseEffectTimeScoreMultiplier = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("cfpb=")) {
			modelCollection.back().calibrationFirstPartyBias = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("fstd=")) {
			modelCollection.back().finalStandardDeviation = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("strt=")) {
			modelCollection.back().startDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("end =")) {
			modelCollection.back().endDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("updt=")) {
			modelCollection.back().lastUpdated = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 4).compare("$Day")) {
			modelCollection.back().day.push_back(ModelTimePoint(pollsterCollection.count()));
			return true;
		}
		else if (!line.substr(0, 5).compare("mtnd=")) {
			if (!modelCollection.back().day.size()) return true;
			modelCollection.back().day.back().trend2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 2).compare("he") && !line.substr(4, 1).compare("=")) {
			int pollsterIndex = std::stoi(line.substr(2, 2));
			if (pollsterIndex < pollsterCollection.count()) {
				modelCollection.back().day.back().houseEffect[pollsterIndex] = std::stof(line.substr(5));
			}
		}
	}
	else if (fos.section == FileSection_Projections) {
		if (!projectionCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			projectionCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			projectionCollection.back().numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("base=")) {
			projectionCollection.back().baseModel = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("end =")) {
			projectionCollection.back().endDate = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("updt=")) {
			projectionCollection.back().lastUpdated = wxDateTime(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("dlyc=")) {
			projectionCollection.back().dailyChange = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("inic=")) {
			projectionCollection.back().initialStdDev = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("vtls=")) {
			projectionCollection.back().leaderVoteDecay = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("nele=")) {
			projectionCollection.back().numElections = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("mean=")) {
			projectionCollection.back().meanProjection.push_back(std::stod(line.substr(5)));
			return true;
		}
		else if (!line.substr(0, 5).compare("stdv=")) {
			projectionCollection.back().sdProjection.push_back(std::stod(line.substr(5)));
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
		if (!simulationCollection.count()) return true; //prevent crash from mixed-up data.
		if (!line.substr(0, 5).compare("name=")) {
			simulationCollection.back().name = line.substr(5);
			return true;
		}
		else if (!line.substr(0, 5).compare("iter=")) {
			simulationCollection.back().numIterations = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("base=")) {
			simulationCollection.back().baseProjection = std::stoi(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("prev=")) {
			simulationCollection.back().prevElection2pp = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("stsd=")) {
			simulationCollection.back().stateSD = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("stde=")) {
			simulationCollection.back().stateDecay = std::stof(line.substr(5));
			return true;
		}
		else if (!line.substr(0, 5).compare("live=")) {
			simulationCollection.back().live = Simulation::Mode(std::stoi(line.substr(5)));
			return true;
		}
	}
	else if (fos.section == FileSection_Results) {
		if (!results.size()) return true; //prevent crash from mixed-up data.
		auto it = results.end();
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
	for (auto const& projection : projections()) {
		if (projection.second.baseModel == modelId) projections().remove(projection.first);
	}
}

void PollingProject::removeSimulationsFromProjection(Projection::Id projectionId)
{
	for (int i = 0; i < simulations().count(); i++) {
		Simulation const& simulation = simulations().viewByIndex(i);
		if (simulation.baseProjection == projectionId) { 
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

void PollingProject::adjustCandidatesAfterPartyRemoval(PartyCollection::Index, Party::Id partyId)
{
	for (auto& candidate : candidateParties) {
		if (candidate.second == partyId) {
			candidate.second = -1;
		}
	}
}

void PollingProject::adjustAffiliationsAfterPartyRemoval(PartyCollection::Index, Party::Id partyId)
{
	for (auto& affiliation : affiliationParties) {
		if (affiliation.second == partyId) affiliation.second = -1;
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
	// sets the correct effective start/end dates
	for (auto& model : models()) {
		model.second.updateEffectiveDates(mjdToDate(polls().getEarliestDate()), mjdToDate(polls().getLatestDate()));
	}

	partyCollection.finaliseFileLoading();
}

void PollingProject::collectAffiliations(PreviousElectionDataRetriever const & dataRetriever)
{
	affiliationParties.insert({-1, Party::InvalidId});
	affiliations.insert({ -1, {"Invalid"} });
	for (auto affiliationIt = dataRetriever.beginAffiliations(); affiliationIt != dataRetriever.endAffiliations(); ++affiliationIt) {
		affiliations.insert({ affiliationIt->first, affiliationIt->second });
		// Don't bother doing any string comparisons if this affiliation is already recorded
		if (affiliationParties.find(affiliationIt->first) == affiliationParties.end()) {
			for (auto const& party : partyCollection) {
				for (auto partyCode : party.second.officialCodes) {
					if (affiliationIt->second.shortCode == partyCode) {
						affiliationParties.insert({ affiliationIt->first, party.first });
					}
				}
			}
		}
	}
}

void PollingProject::collectCandidatesFromPreload(PreloadDataRetriever const & dataRetriever)
{
	affiliationParties.insert({ -1, Party::InvalidId });
	for (auto affiliationIt = dataRetriever.beginAffiliations(); affiliationIt != dataRetriever.endAffiliations(); ++affiliationIt) {
		affiliations.insert({ affiliationIt->first, affiliationIt->second });
		// Don't bother doing any string comparisons if this affiliation is already recorded
		if (affiliationParties.find(affiliationIt->first) == affiliationParties.end()) {
			for (auto const& party : partyCollection) {
				for (auto partyCode : party.second.officialCodes) {
					if (affiliationIt->second.shortCode == partyCode) {
						affiliationParties.insert({ affiliationIt->first, party.first });
					}
				}
			}
		}
	}

	candidateParties.insert({ -1, Party::InvalidId });
	for (auto candidateIt = dataRetriever.beginCandidates(); candidateIt != dataRetriever.endCandidates(); ++candidateIt) {
		candidates.insert(*candidateIt);
		int affiliationId = candidateIt->second.affiliationId;
		candidateAffiliations.insert({ candidateIt->first, affiliationId });
		auto affiliationIt = affiliationParties.find(affiliationId);
		if (affiliationIt != affiliationParties.end()) {
			candidateParties.insert({ candidateIt->first, affiliationIt->second });
		}
		else {
			// treat unknown party as independent
			candidateParties.insert({ candidateIt->first, affiliationParties[0] });
		}
	}
}

void PollingProject::collectBoothsFromPreload(PreloadDataRetriever const & dataRetriever)
{
	for (auto boothIt = dataRetriever.beginBooths(); boothIt != dataRetriever.endBooths(); ++boothIt) {
		auto foundBooth = booths.find(boothIt->first);
		if (foundBooth == booths.end()) {
			booths.insert({ boothIt->first, boothIt->second });
		}
		else {
			foundBooth->second.coords = boothIt->second.coords;
			foundBooth->second.name = boothIt->second.name;
		}
	}
}

float PollingProject::calculateSwingToIncumbent(Seat const & seat)
{
	std::array<int, 2> seatTotalVotes = { 0, 0 };
	std::array<int, 2> seatTotalVotesOld = { 0, 0 };
	for (auto booth : seat.latestResults->booths) {
		Results::Booth thisBooth = booths[booth];
		int totalOld = thisBooth.tcpVote[0] + thisBooth.tcpVote[1];
		int totalNew = thisBooth.newTcpVote[0] + thisBooth.newTcpVote[1];
		if (totalOld && totalNew) {
			bool matchedSame = (affiliationParties[thisBooth.tcpAffiliationId[0]] == candidateParties[seat.latestResults->finalCandidates[0].candidateId]);
			if (matchedSame) {
				seatTotalVotes[0] += thisBooth.newTcpVote[0];
				seatTotalVotes[1] += thisBooth.newTcpVote[1];
				seatTotalVotesOld[0] += thisBooth.tcpVote[0];
				seatTotalVotesOld[1] += thisBooth.tcpVote[1];
			}
			else {
				seatTotalVotes[0] += thisBooth.newTcpVote[1];
				seatTotalVotes[1] += thisBooth.newTcpVote[0];
				seatTotalVotesOld[0] += thisBooth.tcpVote[1];
				seatTotalVotesOld[1] += thisBooth.tcpVote[0];
			}
		}
	}

	int totalOldSeat = seatTotalVotesOld[0] + seatTotalVotesOld[1];
	int totalNewSeat = seatTotalVotes[0] + seatTotalVotes[1];

	if (totalOldSeat && totalNewSeat) {
		float swing = (float(seatTotalVotes[0]) / float(totalNewSeat) -
			float(seatTotalVotesOld[0]) / float(totalOldSeat)) * 100.0f;
		float swingToIncumbent = swing * (seat.incumbent == candidateParties[seat.latestResults->finalCandidates[0].candidateId] ? 1 : -1);
		return swingToIncumbent;
	}
	return 0;
}

float PollingProject::calculate2cpPercentComplete(Seat const & seat)
{
	if (seat.latestResults->enrolment <= 0) return 0;
	int totalVotes = 0;
	for (auto booth : seat.latestResults->booths) {
		Results::Booth thisBooth = booths[booth];
		totalVotes += thisBooth.newTcpVote[0];
		totalVotes += thisBooth.newTcpVote[1];
	}

	totalVotes += seat.latestResults->declarationVotes();

	return float(totalVotes) / float(seat.latestResults->enrolment) * 100.0f;
}

float PollingProject::calculateFpPercentComplete(Seat const & seat)
{
	if (seat.latestResults->enrolment <= 0) return 0;
	int totalVotes = seat.latestResults->totalFpVotes();

	return float(totalVotes) / float(seat.latestResults->enrolment) * 100.0f;
}
