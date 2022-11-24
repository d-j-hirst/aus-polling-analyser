#include "LivePreparation.h"

#include "LinearRegression.h"
#include "PollingProject.h"
#include "ResultsDownloader.h"
#include "Simulation.h"
#include "SimulationRun.h"
#include "SpecialPartyCodes.h"
#include "LatestResultsDataRetriever.h"

#include <filesystem>
#include <queue>
#include <random>
#include <set>

float cappedTransformedSwing(float previousPercent, float currentPercent, float capMultiplier) {
	currentPercent = std::clamp(currentPercent, 0.001f, 99.999f);
	currentPercent = std::clamp(currentPercent, 0.001f, 99.999f);
	float transformedSwing = transformVoteShare(currentPercent) - transformVoteShare(previousPercent);
	float cap = capMultiplier * std::abs(currentPercent - previousPercent);
	return std::clamp(transformedSwing, -cap, cap);
}

bool simPartyIsTpp(int simParty) {
	return simParty == 0 || simParty == 1 || simParty == CoalitionPartnerIndex;
};

constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

LivePreparation::LivePreparation(PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), run(run), sim(sim)
{
}

void LivePreparation::prepareLiveAutomatic()
{
	downloadPreviousResults();
	parsePreviousResults();
	downloadPreload();
	parsePreload();
	if (sim.settings.currentRealUrl.size()) {
		downloadLatestResults();
	}
	else {
		downloadCurrentResults();
	}
	parseCurrentResults();
	doMiscellaneousUpdates();
	preparePartyCodeGroupings();
	determinePartyIdConversions();
	determineSeatIdConversions();
	calculateTppPreferenceFlows();
	calculateSeatPreferenceFlows();
	calculateBoothFpSwings();
	estimateBoothTcps();
	calculateBoothTcpSwings();
	calculateCountProgress();
	projectOrdinaryVoteTotals();
	projectDeclarationVotes();
	determinePpvcBiasSensitivity();
	determineDecVoteSensitivity();
	determinePpvcBias();
	calculateSeatSwings();
	prepareLiveTppSwings();
	prepareLiveTcpSwings();
	prepareLiveFpSwings();
	prepareOverallLiveFpSwings();
}

void LivePreparation::downloadPreviousResults()
{
	if (getTermCode() == "2022vic") return;
	ResultsDownloader resultsDownloader;
	std::string mangledName = sim.settings.previousResultsUrl;
	if (mangledName.substr(0, 6) == "local:") {
		xmlFilename = mangledName.substr(6);
		return;
	}
	std::replace(mangledName.begin(), mangledName.end(), '/', '$');
	std::replace(mangledName.begin(), mangledName.end(), '.', '$');
	std::replace(mangledName.begin(), mangledName.end(), ':', '$');
	mangledName = "downloads/" + mangledName + ".xml";
	std::filesystem::path mangledPath(mangledName);
	if (std::filesystem::exists(mangledPath)) {
		logger << "Already found previous results file at: " << mangledName << "\n";
	}
	else {
		resultsDownloader.loadZippedFile(sim.settings.previousResultsUrl, mangledName);
		logger << "Downloaded file: " << sim.settings.previousResultsUrl << "\n";
		logger << "and saved it as: " << mangledName << "\n";
	}
	xmlFilename = mangledName;
}

void LivePreparation::parsePreviousResults()
{
	if (getTermCode() == "2022fed") {
		xml.LoadFile(xmlFilename.c_str());
		previousElection = Results2::Election(xml);
	}
	else if (getTermCode() == "2022vic") {
		tinyxml2::XMLDocument candidatesXml;
		candidatesXml.LoadFile(("downloads/" + getTermCode() + "_candidates.xml").c_str());
		tinyxml2::XMLDocument boothsXml;
		boothsXml.LoadFile(("downloads/" + getTermCode() + "_booths.xml").c_str());
		std::ifstream f("analysis/Booth Results/2018vic.json");
		nlohmann::json resultsJson = nlohmann::json::parse(f);
		previousElection = Results2::Election(resultsJson, candidatesXml, boothsXml);
	}
}

void LivePreparation::downloadPreload()
{
	if (getTermCode() == "2022vic") return;
	ResultsDownloader resultsDownloader;
	std::string mangledName = sim.settings.preloadUrl;
	std::replace(mangledName.begin(), mangledName.end(), '/', '$');
	std::replace(mangledName.begin(), mangledName.end(), '.', '$');
	std::replace(mangledName.begin(), mangledName.end(), ':', '$');
	mangledName = "downloads/" + mangledName + ".xml";
	std::filesystem::path mangledPath(mangledName);
	if (std::filesystem::exists(mangledPath)) {
		logger << "Already found preload file at: " << mangledName << "\n";
	}
	else {
		resultsDownloader.loadZippedFile(sim.settings.preloadUrl, mangledName, "preload");
		logger << "Downloaded file: " << sim.settings.preloadUrl << "\n";
		logger << "and saved it as: " << mangledName << "\n";
	}
	xmlFilename = mangledName;
}

void LivePreparation::parsePreload()
{
	if (getTermCode() == "2022fed") {
		xml.LoadFile(xmlFilename.c_str());
		currentElection = Results2::Election(xml);
	}
	else if (getTermCode() == "2022vic") {
		tinyxml2::XMLDocument candidatesXml;
		candidatesXml.LoadFile(("downloads/" + getTermCode() + "_candidates.xml").c_str());
		tinyxml2::XMLDocument boothsXml;
		boothsXml.LoadFile(("downloads/" + getTermCode() + "_booths.xml").c_str());
		currentElection = Results2::Election(candidatesXml, boothsXml);
	}
}

void LivePreparation::downloadCurrentResults()
{
	ResultsDownloader resultsDownloader;
	if (getTermCode() == "2022fed") {
		std::string mangledName = sim.settings.currentTestUrl;
		std::replace(mangledName.begin(), mangledName.end(), '/', '$');
		std::replace(mangledName.begin(), mangledName.end(), '.', '$');
		std::replace(mangledName.begin(), mangledName.end(), ':', '$');
		mangledName = "downloads/" + mangledName + ".xml";
		std::filesystem::path mangledPath(mangledName);
		if (std::filesystem::exists(mangledPath)) {
			logger << "Already found currenttest file at: " << mangledName << "\n";
		}
		else {
			resultsDownloader.loadZippedFile(sim.settings.currentTestUrl, mangledName);
			logger << "Downloaded file: " << sim.settings.currentTestUrl << "\n";
			logger << "and saved it as: " << mangledName << "\n";
		}
		xmlFilename = mangledName;

		auto dotOffset = sim.settings.currentTestUrl.rfind('.');
		auto subStr = sim.settings.currentTestUrl.substr(dotOffset - 14, 14);
		sim.latestReport.dateCode = subStr;
	}
	else if (getTermCode() == "2022vic") {
		std::filesystem::path downloadsPath("../../../Downloads");
		int bestDate = 0;
		int bestTime = 0;
		std::string bestFilename;
		for (const auto& entry : std::filesystem::directory_iterator(downloadsPath)) {
			auto entryStr = entry.path().string();
			if (entryStr.find("State2022mediafilelitepplh_") != std::string::npos) {
				std::string dateStamp = splitString(entryStr, "_")[1];
				std::string timeStamp = splitString(splitString(entryStr, "_")[2], ".")[0];
				int date = std::stoi(dateStamp);
				int time = std::stoi(timeStamp);
				if (date > bestDate || (date == bestDate && time > bestTime)) {
					bestFilename = entryStr;
				}
			}
		}
		xmlFilename = "downloads/2022vic_latest.xml";
		resultsDownloader.unzipFile(bestFilename, xmlFilename);
		PA_LOG_VAR(bestFilename);
		PA_LOG_VAR(xmlFilename);
	}
}

void LivePreparation::parseCurrentResults()
{
	xml.LoadFile(xmlFilename.c_str());
	Results2::Election::Format format;
	if (getTermCode() == "2022fed") format = Results2::Election::Format::AEC;
	else if (getTermCode() == "2022vic") format = Results2::Election::Format::VEC;
	else format = Results2::Election::Format::AEC;
	currentElection.update(xml, format);
}

void LivePreparation::doMiscellaneousUpdates()
{
	// --- Adjust certain election-specific things to make some processing easier
	if (getTermCode() == "2022fed") {
		// For some reason Corangamite's ID got changed from 207 to 328
		// To easily enable matching of declaration votes, manually adjust this
		// (nothing else should refer to it)
		//auto corangamiteData = currentElection.seats.at(328);
		//currentElection.seats.erase(328);
		//currentElection.seats[207] = corangamiteData;
	}
}

void LivePreparation::downloadLatestResults()
{
	ResultsDownloader resultsDownloader;
	std::string directoryListing;
	resultsDownloader.loadUrlToString(sim.settings.currentRealUrl, directoryListing);
	std::string latestFileName = directoryListing.substr(directoryListing.rfind(" ") + 1);
	latestFileName = latestFileName.substr(0, latestFileName.length() - 1);
	std::string latestUrl = sim.settings.currentRealUrl + latestFileName;
	resultsDownloader.loadZippedFile(latestUrl, LatestResultsDataRetriever::UnzippedFileName);
	xmlFilename = LatestResultsDataRetriever::UnzippedFileName;
	logger << "Downloaded file: " << latestUrl << "\n";
	logger << "and saved it as: " << xmlFilename << "\n";

	auto dotOffset = latestUrl.rfind('.');
	auto subStr = latestUrl.substr(dotOffset - 14, 14);
	sim.latestReport.dateCode = subStr;
}

void LivePreparation::preparePartyCodeGroupings()
{
	for (auto [id, party] : project.parties()) {
		for (auto shortCode : party.officialCodes) {
			if (id == 1 && shortCode[0] == 'N') {
				partyCodeGroupings[shortCode] = CoalitionPartnerIndex;
			}
			else {
				partyCodeGroupings[shortCode] = project.parties().idToIndex(id);
			}
		}
	}
}

void LivePreparation::calculateTppPreferenceFlows()
{
	std::map<int, int> partyIdToPos;
	int greensParty = -1;
	int greensPos = -1;
	int pos = 0;
	for (auto const& [partyId, party] : currentElection.parties) {
		if (aecPartyToSimParty[partyId] == 2) {
			if (greensPos != -1) continue;
			partyIdToPos[partyId] = pos;
			greensPos = pos;
			greensParty = partyId;
			++pos;
		}
		if (!partyIdToPos.contains(partyId)) {
			partyIdToPos[partyId] = pos;
			++pos;
		}
	}
	DataSet data;
	std::map<int, int> partyIdFrequency;
	for (auto const& [seatId, seat] : currentElection.seats) {
		for (auto [candidateId, votes] : seat.fpVotes) {
			int partyId = currentElection.candidates[candidateId].party;
			if (aecPartyToSimParty[partyId] == 2) partyId = greensParty;
			++partyIdFrequency[partyId];
		}
		if (seat.tcpVotes.size() != 2) continue;
		int numCoalition = 0;
		for (auto [candidateId, votes] : seat.fpVotes) {
			int aecPartyId = currentElection.candidates[candidateId].party;
			if (aecPartyToSimParty[aecPartyId] == 1) ++numCoalition;
			if (aecPartyToSimParty[aecPartyId] == CoalitionPartnerIndex) ++numCoalition;
		}
		if (numCoalition > 1) continue; // don't get preferences from intra-Coalition contests
		bool firstPartyFound = false;
		bool secondPartyFound = false;
		int partyOneThisSeat = -1;
		for (auto [party, votes] : seat.tcpVotes) {
			int simParty = aecPartyToSimParty[party];
			if (simParty == 0) {
				firstPartyFound = true;
				partyOneThisSeat = party;
			}
			else if (simParty == 1 || simParty == -4) {
				secondPartyFound = true;
			}
		}
		if (!firstPartyFound || !secondPartyFound) continue; // not classic 2cp
		for (int boothId : seat.booths) {
			auto const& booth = currentElection.booths[boothId];
			if (!booth.totalVotesTcp()) continue;
			if (!booth.totalVotesFp()) continue;
			double totalFpVotes = double(booth.totalVotesFp());
			double totalTcpVotes = double(booth.totalVotesTcp());
			std::vector<double> fpData(partyIdToPos.size());
			for (auto [candidateId, votes] : booth.fpVotes) {
				int partyId = currentElection.candidates[candidateId].party;
				if (aecPartyToSimParty[partyId] == 2) partyId = greensParty;
				fpData[partyIdToPos.at(partyId)] = double(votes) / totalFpVotes;
				++partyIdFrequency[partyId];
			}
			double tcpData = double(booth.tcpVotes.at(partyOneThisSeat)) / totalTcpVotes;
			for (double voteIncrement = 100.0; voteIncrement < totalTcpVotes; voteIncrement += 100.0) {
				data.push_back({ fpData, tcpData });
			}
		}
	}
	// Fill with lots of dummy data to make sure that major party preferences are "forced" to what they should be
	for (auto [party, partyPos] : partyIdToPos) {
		int simParty = aecPartyToSimParty[party];
		if (simParty == 0) {
			std::vector<double> fpData(partyIdToPos.size());
			fpData[partyPos] = 1000;
			double tcpData = 1000;
			data.push_back({ fpData, tcpData });
		}
		else if (simParty == 1 || simParty == -4) {
			std::vector<double> fpData(partyIdToPos.size());
			fpData[partyPos] = 1000;
			double tcpData = 0;
			data.push_back({ fpData, tcpData });
		}
	}
	for (auto [partyId, frequency] : partyIdFrequency) {
		int simParty = aecPartyToSimParty[partyId];
		float priorPrefs = simParty >= 0 ? project.parties().viewByIndex(simParty).p1PreferenceFlow
			: project.parties().getOthersPreferenceFlow();
		std::vector<double> fpData(partyIdToPos.size());
		fpData[partyIdToPos.at(partyId)] = 10;
		double tcpData = priorPrefs * 0.1;
		data.push_back({ fpData, tcpData });
	}
	auto weights = runLeastSquares(data);
	for (auto const& [partyId, partyPos] : partyIdToPos) {
		updatedPreferenceFlows[partyId] = std::clamp(float(weights[partyPos]) * 100.0f, 0.0f, 100.0f);
		logger << "Party: " << currentElection.parties[partyId].name <<
			" - current preference flow to ALP: " << formatFloat(weights[partyPos] * 100.0f, 2) << "%, " <<
			partyIdFrequency[partyId] << "booths/seats\n";
	}
	for (auto [partyId, party] : currentElection.parties) {
		if (aecPartyToSimParty[partyId] == 2) {
			updatedPreferenceFlows[partyId] = updatedPreferenceFlows[greensParty];
		}
	}
}

void LivePreparation::calculateSeatPreferenceFlows()
{
	for (auto const& [seatId, seat] : currentElection.seats) {
		if (!seat.tcpVotes.size()) continue;
		std::map<int, int> candidateIdToPos;
		int index = 0;
		for (auto const& [candidateId, votes] : seat.fpVotes) {
			candidateIdToPos[candidateId] = index;
			++index;
		}
		int partyIdToUse = seat.tcpVotes.begin()->first;
		DataSet data;
		for (int boothId : seat.booths) {
			auto const& booth = currentElection.booths[boothId];
			if (!booth.totalVotesTcp()) continue;
			if (!booth.totalVotesFp()) continue;
			double totalFpVotes = double(booth.totalVotesFp());
			double totalTcpVotes = double(booth.totalVotesTcp());
			std::vector<double> fpData(index);
			for (auto const& [candidateId, votes] : booth.fpVotes) {
				fpData[candidateIdToPos.at(candidateId)] = double(votes) / totalFpVotes;
			}
			double tcpData = double(booth.tcpVotes.at(partyIdToUse)) / totalTcpVotes;
			data.push_back({ fpData, tcpData });
		}
		// don't bother calculating for tiny data sets as it'll be worse than
		// just using previous-election preferences
		if (data.size() < 2) continue;
		std::vector<std::string> partyNames;
		for (auto const& [candidateId, votes] : seat.fpVotes) {
			partyNames.push_back(currentElection.parties[currentElection.candidates[candidateId].party].name);
		}
		runLeastSquares(data);
	}
}

void LivePreparation::estimateBoothTcps()
{
	for (auto const& [seatId, seat] : currentElection.seats) {
		std::pair<int, int> tcpParties;
		// May or may not be a tcp pair recorded, if not assume classic 2CP
		auto const& simSeat = project.seats().viewByIndex(aecSeatToSimSeat[seatId]);
		bool seatIsTpp = simPartyIsTpp(simSeat.incumbent);
		// If there is a 2CP pair recorded check if it's classic 2pp
		if (seat.tcpVotes.size()) {
			tcpParties = { seat.tcpVotes.begin()->first, std::next(seat.tcpVotes.begin())->first };
			std::pair<int, int> tcpSimParties = { aecPartyToSimParty[tcpParties.first], aecPartyToSimParty[tcpParties.second] };
			seatIsTpp = (simPartyIsTpp(tcpSimParties.first) && simPartyIsTpp(tcpSimParties.second));
			if (tcpSimParties.first != 0 && tcpSimParties.second != 0) seatIsTpp = false;
		}
		// for now don't estimate for confirmed non-classic tcp
		if (!seatIsTpp) continue;
		// Establish which AEC parties actually best represent the TPP here
		int partyOneParty = -1;
		int partyTwoParty = -1;
		// First option - if we have an actual 2cp count then go with that
		for (auto [partyId, votes] : seat.tcpVotes) {
			if (aecPartyToSimParty[partyId] == 0) partyOneParty = partyId;
			if (aecPartyToSimParty[partyId] == 1 || aecPartyToSimParty[partyId] == -4) partyTwoParty = partyId;
		}
		// Otherwise, find coalition candidate with highest fp vote
		if (partyTwoParty == -1) {
			int partyTwoVotes = 0;
			for (auto [candidateId, votes] : seat.fpVotes) {
				int voteCount = seat.totalVotesFpCandidate(candidateId);
				if (aecPartyToSimParty[currentElection.candidates[candidateId].party] == 0) partyOneParty = currentElection.candidates[candidateId].party;
				if (aecPartyToSimParty[currentElection.candidates[candidateId].party] == 1 ||
					aecPartyToSimParty[currentElection.candidates[candidateId].party] == -4) {
					if (voteCount > partyTwoVotes) {
						partyTwoParty = currentElection.candidates[candidateId].party;
						partyTwoVotes = voteCount;
					}
				}
			}
		}
		int coalitionMain = aecPartyToSimParty[partyTwoParty];
		int coalitionPartner = -3 - coalitionMain;
		// Got through each booth and estimate tpp based on booth votes
		for (int boothId : seat.booths) {
			auto& booth = currentElection.booths[boothId];
			if (!booth.totalVotesFp()) continue;
			float partyOnePrefs = 0.0f;
			for (auto const& [candidateId, percent] : booth.fpPercent) {
				int aecParty = currentElection.candidates[candidateId].party;
				int partyIndex = aecPartyToSimParty[aecParty];
				float prefFlow = 50.0f;
				if (partyIndex == coalitionPartner) prefFlow = 20.0f;
				else if (partyIndex == coalitionMain) prefFlow = 0.0f;
				else if (updatedPreferenceFlows.contains(aecParty)) prefFlow = updatedPreferenceFlows.at(aecParty);
				else if (partyIndex < 0) prefFlow = project.parties().getOthersPreferenceFlow();
				else prefFlow = project.parties().viewByIndex(partyIndex).p1PreferenceFlow;
				partyOnePrefs += percent * prefFlow * 0.01f;
			}
			booth.tcpEstimate[partyOneParty] = partyOnePrefs;
			booth.tcpEstimate[partyTwoParty] = 100.0f - partyOnePrefs;
		}
	}
}

void LivePreparation::calculateBoothFpSwings()
{
	// fp swings
	for (auto& [id, currentBooth] : currentElection.booths) {
		if (!currentBooth.fpVotes.size()) continue;
		if (previousElection.booths.contains(id)) {
			auto const& previousBooth = previousElection.booths.at(id);
			int currentTotal = currentBooth.totalVotesFp();
			if (!currentTotal) continue;
			int previousTotal = previousBooth.totalVotesFp();
			if (!previousTotal) continue;
			for (auto [candidateId, votes] : currentBooth.fpVotes) {
				int currentParty = currentElection.candidates[candidateId].party;
				float currentPercent = float(votes) * 100.0f / float(currentTotal);
				bool matchFound = false;
				for (auto [prevCandidateId, prevVotes] : previousBooth.fpVotes) {
					if (currentParty == Results2::Candidate::Independent) {
						if (currentElection.candidates[candidateId].name == previousElection.candidates[prevCandidateId].name) {
							float previousPercent = float(prevVotes) * 100.0f / float(previousTotal);
							currentBooth.fpSwing[candidateId] = currentPercent - previousPercent;
							currentBooth.fpTransformedSwing[candidateId] = cappedTransformedSwing(previousPercent, currentPercent, BoothTransformCap);
							currentBooth.fpPercent[candidateId] = currentPercent;
							matchFound = true;
						}
					}
					else {
						int previousParty = previousElection.candidates[prevCandidateId].party;
						bool shortCodeMatch = previousElection.parties[previousParty].shortCode ==
							currentElection.parties[currentParty].shortCode;
						bool nameMatch = previousElection.parties[previousParty].name ==
							currentElection.parties[currentParty].name;
						bool groupingMatch = partyCodeGroupings.contains(previousElection.parties[previousParty].shortCode)
							&& partyCodeGroupings.contains(currentElection.parties[currentParty].shortCode) &&
							partyCodeGroupings[previousElection.parties[previousParty].shortCode] ==
							partyCodeGroupings[currentElection.parties[currentParty].shortCode];
						bool idMatch = previousParty == currentParty;
						bool partyMatch = shortCodeMatch || nameMatch || groupingMatch || idMatch;
						if (partyMatch) {
							float previousPercent = float(prevVotes) * 100.0f / float(previousTotal);
							currentBooth.fpSwing[candidateId] = currentPercent - previousPercent;
							currentBooth.fpTransformedSwing[candidateId] = cappedTransformedSwing(previousPercent, currentPercent, BoothTransformCap);
							currentBooth.fpPercent[candidateId] = currentPercent;
							matchFound = true;
							break;
						}
					}
				}
				if (!matchFound) {
					currentBooth.fpSwing[candidateId] = NaN;
					currentBooth.fpTransformedSwing[candidateId] = NaN;
					currentBooth.fpPercent[candidateId] = currentPercent;
				}
			}
		}
		else {
			int currentTotal = currentBooth.totalVotesFp();
			if (!currentTotal) continue;
			for (auto [candidateId, votes] : currentBooth.fpVotes) {
				float currentPercent = float(votes) * 100.0f / float(currentTotal);
				currentBooth.fpSwing[candidateId] = NaN;
				currentBooth.fpTransformedSwing[candidateId] = NaN;
				currentBooth.fpPercent[candidateId] = currentPercent;
			}
		}
	}
}

void LivePreparation::calculateBoothTcpSwings()
{
	// tcp swings
	for (auto& [id, currentBooth] : currentElection.booths) {
		int previousTotal = 0;
		int currentTotal = currentBooth.totalVotesTcp();
		if (currentTotal) {
			Results2::Booth const* previousBooth = nullptr;
			std::map<int, int> matchedAffiliation;
			if (previousElection.booths.contains(id)) {
				previousBooth = &previousElection.booths.at(id);
				for (auto [affiliation, votes] : currentBooth.tcpVotes) {
					for (auto [prevAffiliation, prevVotes] : previousBooth->tcpVotes) {
						if (aecPartyToSimParty[affiliation] == aecPartyToSimParty[prevAffiliation] && aecPartyToSimParty[affiliation] != -1) {
							matchedAffiliation[affiliation] = prevAffiliation;
							break;
						}
					}
				}
				previousTotal = previousBooth->totalVotesTcp();
			}
			for (auto [affiliation, votes] : currentBooth.tcpVotes) {
				float currentPercent = float(votes) * 100.0f / float(currentTotal);
				if (matchedAffiliation.size() == 2 && previousTotal && previousBooth) {
					float previousPercent = float(previousBooth->tcpVotes.at(matchedAffiliation[affiliation])) * 100.0f / float(previousTotal);
					currentBooth.tcpSwing[affiliation] = currentPercent - previousPercent;
				}
				else {
					currentBooth.tcpSwing[affiliation] = NaN;
				}
				currentBooth.tcpPercent[affiliation] = currentPercent;
			}
		}
		else if (currentBooth.tcpEstimate.size()) {
			Results2::Booth const* previousBooth = nullptr;
			bool matched = false;
			if (previousElection.booths.contains(id)) {
				matched = true;
				previousBooth = &previousElection.booths.at(id);
				for (auto [affiliation, _] : currentBooth.tcpEstimate) {
					if (!previousBooth->tcpVotes.contains(affiliation)) {
						matched = false;
						break;
					}
				}
				previousTotal = previousBooth->totalVotesTcp();
			}
			for (auto [affiliation, currentPercent] : currentBooth.tcpEstimate) {
				if (matched && previousTotal && previousBooth) {
					float previousPercent = float(previousBooth->tcpVotes.at(affiliation)) * 100.0f / float(previousTotal);
					currentBooth.tcpEstimateSwing[affiliation] = currentPercent - previousPercent;
				}
				else {
					currentBooth.tcpEstimateSwing[affiliation] = NaN;
				}
			}
		}
	}
}

void LivePreparation::calculateCountProgress()
{
	for (auto& [seatId, seat] : currentElection.seats) {
		seat.fpProgress = float(seat.totalVotesFp()) * 100.0f / float(seat.enrolment);
		seat.tcpProgress = float(seat.totalVotesTcp({})) * 100.0f / float(seat.enrolment);

		int totalFpBoothVotes = std::accumulate(seat.booths.begin(), seat.booths.end(), 0,
			[&](int acc, decltype(seat.booths)::value_type val) {
				auto currentBooth = currentElection.booths[val];
				if (!currentBooth.fpSwing.size()) return acc;
				return acc + currentElection.booths[val].totalVotesFp();
			});
		int totalFpSwingVotes = totalFpBoothVotes + seat.totalVotesFp(Results2::VoteType::Ordinary);
		seat.fpSwingProgress = float(totalFpSwingVotes) * 100.0f / float(seat.enrolment);

		int totalTcpBoothVotes = std::accumulate(seat.booths.begin(), seat.booths.end(), 0,
			[&](int acc, decltype(seat.booths)::value_type val) {
				auto currentBooth = currentElection.booths[val];
				if (!currentBooth.tcpSwing.size()) return acc;
				return acc + currentElection.booths[val].totalVotesTcp();
			});
		int totalTcpSwingVotes = totalTcpBoothVotes + seat.totalVotesTcp({ Results2::VoteType::Ordinary });
		seat.tcpSwingProgress = float(totalTcpSwingVotes) * 100.0f / float(seat.enrolment);
	}
}

void LivePreparation::calculateSeatSwings()
{

	// tcp swings
	for (auto& [seatId, seat] : currentElection.seats) {

		std::map<int, float> prevDecVoteBias;
		float decVoteProportion = 0.0f;
		if (previousElection.seats.contains(seatId)) {
			auto& previousSeat = previousElection.seats.at(seatId);
			std::map<int, int> matchedAffiliation = findMatchedParties(previousSeat, seat);
			std::map<int, float> ordinaryVotePercent;
			std::map<int, float> decVotePercent;
			float decVoteTotal = 0.0f;
			float ordinaryVoteTotal = 0.0f;
			if (matchedAffiliation.size() == 2) {
				for (auto [affiliation, votes] : seat.tcpVotes) {
					float ordinaryVotes = previousSeat.tcpVotes[matchedAffiliation[affiliation]][Results2::VoteType::Ordinary];
					// *** Probably want to not count VEC "early" votes as dec votes?
					// if (previousSeat.tcpVotes[matchedAffiliation[affiliation]].contains(Results2::VoteType::Early))
					float allVotes = previousSeat.totalVotesTcpParty(matchedAffiliation[affiliation]);
					float decVotes = allVotes - ordinaryVotes;
					ordinaryVotePercent[affiliation] = float(ordinaryVotes);
					decVotePercent[affiliation] = float(decVotes);
					ordinaryVoteTotal += float(ordinaryVotes);
					decVoteTotal += float(decVotes);
				}
			}
			if (ordinaryVoteTotal && decVoteTotal) {
				decVoteProportion = decVoteTotal / (decVoteTotal + ordinaryVoteTotal);
				for (auto& [party, votes] : ordinaryVotePercent) votes /= ordinaryVoteTotal;
				for (auto& [party, votes] : decVotePercent) {
					votes /= decVoteTotal;
					prevDecVoteBias[party] = (decVotePercent[party] - ordinaryVotePercent[party]);
				}
			}
		}

		// Determine if this seat is TPP
		bool coalitionPartyPresent = false;
		if (!seatOrdinaryTcpPercent[seatId].size()) {
			seat.isTpp = simPartyIsTpp(project.seats().viewByIndex(aecSeatToSimSeat[seatId]).incumbent);
		}
		for (auto [party, percent] : seatOrdinaryTcpPercent[seatId]) {
			int simParty = aecPartyToSimParty[party];
			if (!simPartyIsTpp(simParty)) seat.isTpp = false;
			if (simParty == 1 || simParty == -4) {
				if (coalitionPartyPresent) seat.isTpp = false;
				else coalitionPartyPresent = true;
			}
		}

		for (auto [party, percent] : seatOrdinaryTcpPercent[seatId]) {
			float boothSwing = seatOrdinaryTcpSwing[seatId][party];
			//auto const& simSeat = project.seats().viewByIndex(aecSeatToSimSeat[seatId]);
			float decVoteBasis = seatDecVoteSwingBasis[seatId];
			// *** this might be better replaced by a type-by-type count, but it'll do for now
			float decVoteSwing = mix(boothSwing, seatDecVoteTcpSwing[seatId][party], std::sqrt(decVoteBasis));
			seat.tcpSwing[party] = mix(boothSwing, decVoteSwing, seatDecVoteProjectedProportion[seatId]);
			seat.tcpPercent[party] = mix(seatOrdinaryTcpPercent[seatId][party], seatDecVotePercent[seatId][party], seatDecVoteProjectedProportion[seatId]);
			//if (seat.isTpp && seatDecVoteSwingBasis[seatId] && seatPostCountTcpEstimate.contains(seatId) && seatPostCountTcpEstimate[seatId].contains(party)) {
			//	// *** DEFINITELY need to change this for VIC counting of dec votes on the night
			//	float impliedPrevious = aecPartyToSimParty[party] == 0 ? 50.0f + simSeat.tppMargin : 50.0f - simSeat.tppMargin;
			//	seat.tcpPercent[party] = seatPostCountTcpEstimate[seatId][party];
			//	seat.tcpSwing[party] = seat.tcpPercent[party] - impliedPrevious;
			//}
		}
	}
	for (auto const& [id, seat] : currentElection.seats) {
		logger << "Seat: " << seat.name << "\n";
		logger << " Fp progress: " << seat.fpProgress << "\n";
		logger << " Tcp progress: " << seat.tcpProgress << "\n";
		logger << " Tcp swing progress: " << seat.tcpSwingProgress << "\n";
		logger << " Tcp swing basis: " << seat.tcpSwingBasis << "\n";
		logger << " Seat is TPP: " << seat.isTpp << "\n";
		if (seat.tcpPercent.size()) {
			logger << " Tcp votes: \n";
			for (auto [party, percent] : seat.tcpPercent) {
				logger << "  " << currentElection.parties.at(party).name <<
					": " << percent << "%, (" << formatFloat(seat.tcpSwing.at(party), 2, true) << "%)\n";
			}
		}
		if (seat.fpSwing.size()) {
			logger << " Fp votes: \n";
			for (auto [candidate, swing] : seat.fpSwing) {
				logger << "  " << currentElection.candidates.at(candidate).name <<
					" (" << currentElection.parties[currentElection.candidates.at(candidate).party].name <<
					"): " << seat.fpPercent.at(candidate) << "% (" << formatFloat(swing, 2, true) << ") (transformed " <<
					formatFloat(seat.fpTransformedSwing.at(candidate), 2, true) << ")\n";
			}
		}
		for (auto boothId : seat.booths) {
			auto const& booth = currentElection.booths.at(boothId);
			if (booth.fpPercent.size() || booth.tcpPercent.size()) {
				logger << " Booth: " << currentElection.booths.at(boothId).name << " - " << booth.totalVotesFp() << " fp votes\n";
			}
			if (booth.tcpPercent.size()) {
				logger << "  Tcp results: " << currentElection.booths.at(boothId).name << "\n";
				for (auto [party, percent] : booth.tcpPercent) {
					logger << "   Party: " << currentElection.parties.at(party).name <<
						": " << percent << "%, (" << formatFloat(booth.tcpSwing.at(party), 2, true) << "%)\n";
				}
			}
			else if (booth.tcpEstimate.size()) {
				logger << "  Tcp estimates: " << currentElection.booths.at(boothId).name << "\n";
				for (auto [party, percent] : booth.tcpEstimate) {
					logger << "   Party: " << currentElection.parties.at(party).name <<
						": " << percent << "%, (" << formatFloat(getAt(booth.tcpEstimateSwing, party, NaN), 2, true) << "%)\n";
				}
			}
			if (booth.fpSwing.size()) {
				logger << "  Fp votes: " << currentElection.booths.at(boothId).name << "\n";
				for (auto [candidate, swing] : booth.fpSwing) {
					logger << "   " << currentElection.candidates.at(candidate).name <<
						" (" << currentElection.parties[currentElection.candidates.at(candidate).party].name <<
						"): " << booth.fpPercent.at(candidate) << "% (" << formatFloat(swing, 2, true) << ") (transformed " <<
						formatFloat(booth.fpTransformedSwing.at(candidate), 2, true) << ")\n";
				}
			}
		}
	}
}

void LivePreparation::projectOrdinaryVoteTotals()
{
	// current/estimated vote totals
	for (auto const& [seatId, seat] : currentElection.seats) {
		int countedOrdinaryVotesFp = 0;
		int countedOrdinaryVotesTcp = 0;
		int matchedOrdinaryVotes = 0;
		int matchedPreviousOrdinaryVotes = 0;
		int uncountedPreviousOrdinaryVotes = 0;
		int mysteryVotes = 0;
		for (auto boothId : seat.booths) {
			auto const& booth = currentElection.booths[boothId];
			int totalVotes = booth.totalVotesFp();
			countedOrdinaryVotesFp += totalVotes;
			countedOrdinaryVotesTcp += booth.totalVotesTcp();
			if (totalVotes && previousElection.booths.contains(boothId)) {
				matchedOrdinaryVotes += totalVotes;
				matchedPreviousOrdinaryVotes += previousElection.booths[boothId].totalVotesFp();
			}
			else if (!totalVotes && previousElection.booths.contains(boothId)) {
				uncountedPreviousOrdinaryVotes += previousElection.booths[boothId].totalVotesFp();
			}
			// *** adjust this to match vic situation?
			else if (!totalVotes) {
				if (booth.type == Results2::Booth::Type::Ppvc) {
					mysteryVotes += 3000;
				}
				else if (booth.type == Results2::Booth::Type::Remote) {
					mysteryVotes += 700;
				}
				else if (booth.type == Results2::Booth::Type::Other) {
					mysteryVotes += 50;
				}
				else if (booth.type == Results2::Booth::Type::Hospital) {
					mysteryVotes += 100;
				}
				else if (booth.type == Results2::Booth::Type::Prison) {
					mysteryVotes += 100;
				}
				else {
					mysteryVotes += 800;
				}
			}
		}
		seatOrdinaryVotesCountedFp[seatId] = countedOrdinaryVotesFp;
		seatOrdinaryVotesCountedTcp[seatId] = countedOrdinaryVotesTcp;
		float boothSizeChange = matchedPreviousOrdinaryVotes ? float(matchedOrdinaryVotes) / float(matchedPreviousOrdinaryVotes) : 1.0f;
		float minExpectedVotes = float(countedOrdinaryVotesFp) + float(uncountedPreviousOrdinaryVotes) * boothSizeChange * 0.8f +
			mysteryVotes * 0.4f;
		float maxExpectedVotes = float(countedOrdinaryVotesFp) + float(uncountedPreviousOrdinaryVotes) * boothSizeChange * 1.2f +
			mysteryVotes * 2.0f;
		float averageExpectedVotes = (minExpectedVotes + maxExpectedVotes) * 0.5f;
		seatOrdinaryVotesProjection[seatId] = averageExpectedVotes;
	}

	// fp swings
	for (auto& [seatId, seat] : currentElection.seats) {
		std::unordered_map<int, double> weightedSwing;
		std::unordered_map<int, double> weightedTransformedSwing;
		std::unordered_map<int, double> weightedPercent;
		std::unordered_map<int, double> weightSwingSum;
		std::unordered_map<int, double> weightPercentSum;
		for (auto boothId : seat.booths) {
			auto const& booth = currentElection.booths[boothId];
			for (auto [candidate, swing] : booth.fpSwing) {
				double total = double(booth.totalVotesFp());
				weightedPercent[candidate] += double(booth.fpPercent.at(candidate)) * total;
				weightPercentSum[candidate] += total;
				if (std::isnan(swing)) continue;
				weightedSwing[candidate] += double(swing) * total;
				weightedTransformedSwing[candidate] += double(booth.fpTransformedSwing.at(candidate)) * total;
				weightSwingSum[candidate] += total;
			}
		}
		// *** add VEC "early" votes here. And maybe dec votes too?
		for (auto [party, percent] : weightedPercent) {
			seat.fpPercent[party] = float(percent / weightPercentSum[party]);
			if (!weightedSwing.contains(party) || !weightSwingSum[party]) {
				seat.fpSwing[party] = NaN;
				seat.fpTransformedSwing[party] = NaN;
			}
			else {
				seat.fpSwing[party] = float(weightedSwing[party] / weightSwingSum[party]);
				seat.fpTransformedSwing[party] = float(weightedTransformedSwing[party] / weightSwingSum[party]);
			}
		}
	}

	// tcp swings
	for (auto& [seatId, seat] : currentElection.seats) {
		std::unordered_map<int, double> weightedSwing;
		std::unordered_map<int, double> weightedPercent;
		std::unordered_map<int, double> weightSwingSum;
		std::unordered_map<int, double> weightPercentSum;
		for (auto boothId : seat.booths) {
			auto const& booth = currentElection.booths[boothId];
			if (booth.tcpPercent.size()) {
				for (auto [party, percent] : booth.tcpPercent) {
					weightedPercent[party] += double(booth.tcpPercent.at(party)) * double(booth.totalVotesTcp());
					weightPercentSum[party] += double(booth.totalVotesTcp());
					if (!booth.tcpSwing.contains(party)) continue;
					if (std::isnan(booth.tcpSwing.at(party))) continue;
					weightedSwing[party] += double(booth.tcpSwing.at(party)) * double(booth.totalVotesTcp());
					weightSwingSum[party] += double(booth.totalVotesTcp());
				}
			}
			else if (booth.tcpEstimate.size()) {
				for (auto [party, percent] : booth.tcpEstimate) {
					weightedPercent[party] += double(booth.tcpEstimate.at(party)) * double(booth.totalVotesFp()) * 0.5;
					weightPercentSum[party] += double(booth.totalVotesFp()) * 0.5;
					if (!booth.tcpEstimateSwing.contains(party)) continue;
					if (std::isnan(booth.tcpEstimateSwing.at(party))) continue;
					weightedSwing[party] += double(booth.tcpEstimateSwing.at(party)) * double(booth.totalVotesFp()) * 0.5;
					weightSwingSum[party] += double(booth.totalVotesFp()) * 0.5;
				}
			}
		}
		for (auto [party, _] : weightedSwing) {
			seatOrdinaryTcpPercent[seatId][party] = float(weightedPercent[party] / weightPercentSum[party]);
			seatOrdinaryTcpSwing[seatId][party] = float(weightedSwing[party] / weightSwingSum[party]);
		}
		if (weightSwingSum.size() && weightSwingSum.begin()->second) {
			seat.tcpSwingBasis = weightSwingSum.begin()->second * 100.0f / float(seat.enrolment);
		}
		else if (weightPercentSum.size() && weightPercentSum.begin()->second) {
			seat.tcpSwingBasis = weightPercentSum.begin()->second * 50.0f / float(seat.enrolment);
		}
	}
}

Results2::Seat const& LivePreparation::findBestMatchingPreviousSeat(int currentSeatId)
{
	if (previousElection.seats.contains(currentSeatId)) return previousElection.seats.at(currentSeatId);
	std::string const& currentName = currentElection.seats.at(currentSeatId).name;
	for (auto const& [prevSeatId, seat] : previousElection.seats) {
		if (seat.name == currentName) return seat;
	}
	std::string const& previousName = project.seats().viewByIndex(aecSeatToSimSeat[currentSeatId]).previousName;
	for (auto const& [prevSeatId, seat] : previousElection.seats) {
		if (seat.name == previousName) return seat;
	}
	std::string const& useFpResultsName = project.seats().viewByIndex(aecSeatToSimSeat[currentSeatId]).useFpResults;
	for (auto const& [prevSeatId, seat] : previousElection.seats) {
		if (seat.name == useFpResultsName) return seat;
	}
	throw std::out_of_range("Could not find any satisfactory matching previous seat");
}

void LivePreparation::projectDeclarationVotes()
{
	for (auto const& [seatId, seat] : currentElection.seats) {

		typedef std::map<Results2::VoteType, int> VotesByVoteType;
		auto getTcpsByVoteType = [&](Results2::Seat const& seat, VotesByVoteType& totalTcpVotesByType)
			-> std::map<int, std::map<Results2::VoteType, float>>
		{

			std::map<int, std::map<Results2::VoteType, float>> tcpByVoteType;

			for (auto const& [partyId, votes] : seat.tcpVotes) {
				for (auto [voteType, voteCount] : votes) {
					if (voteType == Results2::VoteType::Invalid) continue;
					if (voteCount) totalTcpVotesByType[voteType] += voteCount;
				}
			}

			for (auto const& [partyId, votes] : seat.tcpVotes) {
				for (auto [voteType, voteCount] : votes) {
					if (voteType == Results2::VoteType::Invalid) continue;
					if (!voteCount) continue;
					tcpByVoteType[partyId][voteType] = totalTcpVotesByType[voteType] ?
						float(voteCount) / float(totalTcpVotesByType[voteType]) * 100.0f : 0.0f;
				}
			}
			return tcpByVoteType;
		};

		VotesByVoteType currentTcpVotesByType;
		VotesByVoteType previousTcpVotesByType;
		logger << seat.name << "\n";
		PA_LOG_VAR(seat.tcpVotes);
		auto const currentTcpPercentByVoteType = getTcpsByVoteType(seat, currentTcpVotesByType);

		std::map<int, float> overallTcpPercentDecVotes; // by party
		for (auto const& [partyId, votes] : currentTcpPercentByVoteType) {
			logger << " " << partyId << ": " << currentElection.parties[partyId].name << "\n";
			float weightedPercentSum = 0.0f;
			float weightSum = 0.0f;
			for (auto [voteType, votePercent] : votes) {
				if (voteType == Results2::VoteType::Ordinary) continue;
				float weight = float(seat.tcpVotes.at(partyId).at(voteType));
				weightedPercentSum += votePercent * weight;
				weightSum += weight;
			}

			overallTcpPercentDecVotes[partyId] = weightSum > 0.0f ? weightedPercentSum / weightSum : 0.0f;
		}

		// *** We need:
		//  - First calculate overall average swing deviations for declaration votes
		//  - Regulate this based on the size/diversity of votes received
		//  - Then use this for the base estimate for dec vote swings (instead of ordinaries)

		auto const& previousSeat = findBestMatchingPreviousSeat(seatId);
		auto matchedParties = findMatchedParties(previousSeat, seat);
		if (matchedParties.size() != 2) continue;

		auto previousTcpPercentByVoteType = getTcpsByVoteType(previousSeat, previousTcpVotesByType);

		seatDecVoteSwingBasis[seatId] = 0;
		seatDecVotePercent[seatId] = overallTcpPercentDecVotes;
		float totalDecVotes = float(seat.totalVotesTcp({ Results2::VoteType::Ordinary}));
		float totalTcpVotes = seat.totalVotesTcp({});
		seatDecVotePercentOfCounted[seatId] = totalTcpVotes ? totalDecVotes / totalTcpVotes : 0.0f;
		logger << "FINAL DEC VOTE PERCENTAGES\n";
		logger << " dec vote percent: " << seatDecVotePercent[seatId] << "\n";
		logger << " dec vote percent of counted: " << seatDecVotePercentOfCounted[seatId] << "\n";
		if (!previousElection.seats.contains(seatId)) continue;

		std::map<int, std::map<Results2::VoteType, float>> currentDecVoteSwing;

		std::map<int, float> overallDecVoteSwing;
		float totalProjectedDecVotes = 0.0f;
		float totalCountedDecVotes = 0.0f;
		auto const& simSeat = project.seats().viewByIndex(aecSeatToSimSeat[seatId]);

		std::map<Results2::VoteType, float> baseExpectedVotes;
		for (auto [voteType, votePercent] : (*previousTcpPercentByVoteType.begin()).second) {
			if (voteType == Results2::VoteType::Ordinary) continue;
			float expectedVotes = float(previousTcpVotesByType[voteType]);
			if (voteType == Results2::VoteType::Absent && simSeat.knownAbsentCount) expectedVotes = simSeat.knownAbsentCount * 0.96f;
			if (voteType == Results2::VoteType::Provisional && simSeat.knownProvisionalCount) expectedVotes = simSeat.knownProvisionalCount * 0.3f;
			if (voteType == Results2::VoteType::PrePoll && simSeat.knownDecPrepollCount) expectedVotes = simSeat.knownDecPrepollCount * 0.96f;
			if (voteType == Results2::VoteType::Early && simSeat.knownDecPrepollCount) expectedVotes = simSeat.knownDecPrepollCount * 0.96f;
			if (voteType == Results2::VoteType::Postal && simSeat.knownPostalCount) expectedVotes = simSeat.knownPostalCount * 0.923f;
			if (voteType == Results2::VoteType::Postal && simSeat.knownPostalPercent && !simSeat.knownPostalCount) {
				// Factor of 1.05f accounts for differences between formal votes and enrolment.
				// Factor of 0.72f accounts for not all applied postals being returned along with some being rejected.
				expectedVotes = simSeat.knownPostalPercent * 0.01f * 1.05f * 0.72f * float(seat.enrolment);
			}
			if (voteType == Results2::VoteType::Early && simSeat.knownPrepollPercent && !simSeat.knownDecPrepollCount) {
				// Factor of 1.05f accounts for differences between formal votes and enrolment.
				expectedVotes = simSeat.knownPrepollPercent * 0.01f * 1.05f * float(seat.enrolment);
			}
			baseExpectedVotes[voteType] = expectedVotes;
			totalProjectedDecVotes += expectedVotes;
		}

		for (auto const& [partyId, votes] : seat.tcpVotes) {
			float weightedSwingSum = 0.0f;
			float weightSum = 0.0f;
			// *** wrong! this needs to be booth-matched
			float ordinariesSwing = seatOrdinaryTcpSwing[seatId][partyId];
			logger << "  ordinaries swing: " << ordinariesSwing << "\n";
			float fullProjection = seatOrdinaryTcpPercent[seatId][partyId] * currentTcpVotesByType[Results2::VoteType::Ordinary] * 0.01f;
			for (auto [voteType, votePercent] : previousTcpPercentByVoteType[matchedParties[partyId]]) {
				if (voteType == Results2::VoteType::Ordinary) continue;
				if (!currentTcpPercentByVoteType.contains(partyId)) continue;
				float expectedVotes = baseExpectedVotes[voteType];
				float currentVotes = currentTcpVotesByType.contains(voteType) ? float(currentTcpVotesByType[voteType]) : 0.0f;
				expectedVotes = std::max(expectedVotes, currentVotes);
				float expectedSwing = ordinariesSwing;
				if (currentTcpPercentByVoteType.at(partyId).contains(voteType)) {
					expectedSwing = currentTcpPercentByVoteType.at(partyId).at(voteType) - previousTcpPercentByVoteType[matchedParties[partyId]][voteType];
				}

				// smoothly transition from original projection to one based on actual count
				// absent & dec pre-poll votes more uneven than other vote types
				float votesDenom = voteType == Results2::VoteType::Absent || voteType == Results2::VoteType::PrePoll ? 2000.0f : 500.0f;
				votesDenom = std::min(votesDenom, expectedVotes);
				float mixFactor = std::clamp(currentTcpVotesByType[voteType] / votesDenom, 0.0f, 1.0f);
				float mixedProjectionSwing = mix(ordinariesSwing, expectedSwing, mixFactor);
				float mixedProjectionPercent = std::clamp(previousTcpPercentByVoteType[partyId][voteType] + mixedProjectionSwing, 0.1f, 99.9f);
				float projectedVotes = mixedProjectionPercent * expectedVotes * 0.01f; // projected vote FOR THIS PARTY in tcp
				float safeCurrentDecVotePercent = currentTcpPercentByVoteType.at(partyId).contains(voteType) ? currentTcpPercentByVoteType.at(partyId).at(voteType) : 0.0f;
				fullProjection += projectedVotes;
				weightSum += expectedVotes;
				logger << "  " << voteTypeName(voteType) << " percentage: " << formatFloat(safeCurrentDecVotePercent, 2) <<
					", previous percentage: " << formatFloat(previousTcpPercentByVoteType[matchedParties[partyId]][voteType], 2) <<
					", expected votes: " << expectedVotes << ", current votes: " << currentVotes << ", projected votes: " << projectedVotes <<
					", expected swing: " << expectedSwing << "\n";
				currentDecVoteSwing[partyId][voteType] = expectedSwing;
				weightedSwingSum += expectedSwing * expectedVotes;
				totalCountedDecVotes += float(seat.tcpVotes.at(partyId).at(voteType));
			}

			float totalExpectedVotes = weightSum + currentTcpVotesByType[Results2::VoteType::Ordinary];
			logger << " Full projection: " << fullProjection / totalExpectedVotes * 100.0f << "\n";
			seatPostCountTcpEstimate[seatId][partyId] = fullProjection / totalExpectedVotes * 100.0f;

			overallDecVoteSwing[partyId] = weightedSwingSum / weightSum;
		}
		float estimatedRemainingDecVotes = std::max((totalProjectedDecVotes - totalCountedDecVotes), 400.0f);
		float projectedTotalVotes = seatOrdinaryVotesProjection[seatId] + totalProjectedDecVotes;
		float estimatedPercentRemaining = estimatedRemainingDecVotes / projectedTotalVotes * 100.0f;
		run.liveEstDecVoteRemaining[aecSeatToSimSeat[seatId]] = estimatedPercentRemaining;
		seatDecVoteSwingBasis[seatId] = totalCountedDecVotes / totalProjectedDecVotes;
		seatDecVoteTcpSwing[seatId] = overallDecVoteSwing;
		seatDecVoteProjectedProportion[seatId] = totalProjectedDecVotes / projectedTotalVotes;
		logger << "FINAL DEC VOTE SWINGS\n";
		logger << " expected ordinaries: " << seatOrdinaryVotesProjection[seatId] << "\n";
		logger << " expected dec votes: " << totalProjectedDecVotes << "\n";
		logger << " dec vote swing basis: " << seatDecVoteSwingBasis[seatId] << "\n";
		logger << " dec vote swing: " << seatDecVoteTcpSwing[seatId] << "\n";
		logger << " dec vote swing weight: " << seatDecVoteProjectedProportion[seatId] << "\n";
		logger << " estimated dec votes remaining: " << estimatedRemainingDecVotes << "\n";
	}
}

// *** EARLY VOTES: Need to do something like this
void LivePreparation::determinePpvcBiasSensitivity()
{
	for (auto const& [seatId, seat] : currentElection.seats) {
		if (!seat.isTpp) {
			run.liveSeatPpvcSensitivity[aecSeatToSimSeat[seatId]] = 0.0f;
			continue;
		}
		double totalTcpOrdinaries = 0;
		double totalTcpPpvc = 0;
		for (auto boothId : seat.booths) {
			auto const& booth = currentElection.booths.at(boothId);
			if (booth.type == Results2::Booth::Type::Ppvc) {
				totalTcpPpvc += double(booth.totalVotesTcp());
			}
			else {
				totalTcpOrdinaries += double(booth.totalVotesTcp());
			}
		}
		double formalProportionExpected = 0.9189;
		double totalFormalVotesExpected = formalProportionExpected * double(seat.enrolment);
		float ordinaryVotesExpected = 0.545 * totalFormalVotesExpected;
		float ppvcVotesExpected = 0.282 * totalFormalVotesExpected;
		if (project.seats().viewByIndex(aecSeatToSimSeat[seatId]).knownPrepollPercent) {
			// Factor of 1.05f accounts for differences between formal votes and enrolment.
			ppvcVotesExpected = totalFormalVotesExpected * project.seats().viewByIndex(aecSeatToSimSeat[seatId]).knownPrepollPercent * 0.01f * 1.05f;
		}
		float ordinaryCounted = std::clamp(float(totalTcpOrdinaries / ordinaryVotesExpected), 0.0f, 1.0f);
		float prepollCounted = std::clamp(float(totalTcpPpvc / ppvcVotesExpected), 0.0f, 1.0f);
		float a = ordinaryCounted; // as proportion of ordinaries
		float c = ordinaryVotesExpected; // as proportion of total votes
		float u = ppvcVotesExpected; // as proportion of total votes
		float z = prepollCounted; // as proportion of prepolls
		if (a == 0 && z == 0) {
			run.liveSeatPpvcSensitivity[aecSeatToSimSeat[seatId]] = 0.0f;
			continue;
		}
		float sensitivity = (a * c) / (a * c + u * z) - c / (c + u); // thanks to algebra
		run.liveSeatPpvcSensitivity[aecSeatToSimSeat[seatId]] = sensitivity;
	}
}

void LivePreparation::determineDecVoteSensitivity()
{
	for (auto const& [seatId, seat] : currentElection.seats) {
		if (!seat.isTpp) {
			run.liveSeatDecVoteSensitivity[aecSeatToSimSeat[seatId]] = 0.0f;
			continue;
		}
		if (!previousElection.seats.contains(seatId)) {
			run.liveSeatDecVoteSensitivity[aecSeatToSimSeat[seatId]] = 0.0f;
			continue;
		}
		std::map<int, int> matchedAffiliation;
		auto& previousSeat = previousElection.seats.at(seatId);
		double totalVote = previousSeat.totalVotesTcp({});
		double totalDecVote = previousSeat.totalVotesTcp({ Results2::VoteType::Ordinary }); // despite what it looks like this EXCLUDES ordinary votes
		if (!totalDecVote) {
			run.liveSeatDecVoteSensitivity[aecSeatToSimSeat[seatId]] = 0.0f;
			continue;
		}
		run.liveSeatDecVoteSensitivity[aecSeatToSimSeat[seatId]] = totalDecVote / (totalDecVote + totalVote);
		if (project.seats().viewByIndex(aecSeatToSimSeat[seatId]).knownPostalPercent) {
			// Factor of 1.05f accounts for differences between formal votes and enrolment.
			// Factor of 0.8f accounts for not all applied postals being returned along with some being rejected.
			run.liveSeatDecVoteSensitivity[aecSeatToSimSeat[seatId]] = project.seats().viewByIndex(aecSeatToSimSeat[seatId]).knownPostalPercent * 0.01f * 1.05f * 0.8f;

		}
		run.liveSeatDecVoteSensitivity[aecSeatToSimSeat[seatId]] = std::clamp(run.liveSeatDecVoteSensitivity[aecSeatToSimSeat[seatId]] - seatDecVotePercentOfCounted[seatId], 0.0f, 1.0f);
	}
}

void LivePreparation::determinePartyIdConversions()
{
	for (auto const& [_, aecParty] : currentElection.parties) {
		for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
			auto const& simParty = project.parties().viewByIndex(partyIndex);
			if (contains(simParty.officialCodes, aecParty.shortCode)) {
				if (partyIndex == 1 && aecParty.shortCode[0] == 'N') {
					aecPartyToSimParty[aecParty.id] = CoalitionPartnerIndex;
				}
				else {
					aecPartyToSimParty[aecParty.id] = partyIndex;
				}
				break;
			}
		}
		if (!aecPartyToSimParty.contains(aecParty.id)) {
			logger << "No party conversion found for " << aecParty.name << " (" << aecParty.shortCode << ") - check this is ok\n";
			aecPartyToSimParty[aecParty.id] = -1;
		}
	}
	for (auto const& [_, aecParty] : previousElection.parties) {
		if (aecPartyToSimParty.contains(aecParty.id)) continue;
		for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
			auto const& simParty = project.parties().viewByIndex(partyIndex);
			if (contains(simParty.officialCodes, aecParty.shortCode)) {
				if (partyIndex == 1 && aecParty.shortCode[0] == 'N') {
					aecPartyToSimParty[aecParty.id] = CoalitionPartnerIndex;
				}
				else {
					aecPartyToSimParty[aecParty.id] = partyIndex;
				}
				break;
			}
		}
		if (!aecPartyToSimParty.contains(aecParty.id)) {
			logger << "No party conversion found for " << aecParty.name << " (" << aecParty.shortCode << ") - check this is ok\n";
			aecPartyToSimParty[aecParty.id] = -1;
		}
	}
}

void LivePreparation::determineSeatIdConversions()
{
	for (auto const& [_, aecSeat] : currentElection.seats) {
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			auto const& simSeat = project.seats().viewByIndex(seatIndex);
			if (simSeat.name == aecSeat.name) {
				aecSeatToSimSeat[aecSeat.id] = seatIndex;
				break;
			}
		}
		if (!aecSeatToSimSeat.contains(aecSeat.id)) {
			logger << "No seat conversion found for " << aecSeat.name << " - needs to be fixed\n";
			aecSeatToSimSeat[aecSeat.id] = -1;
		}
	}
}

void LivePreparation::prepareLiveTppSwings()
{
	for (auto const& [id, seat] : currentElection.seats) {
		int seatIndex = aecSeatToSimSeat[seat.id];
		run.liveSeatTppSwing[seatIndex] = NaN;
		if (seat.tcpSwing.size() != 2) continue;
		if (!seat.isTpp) continue;
		for (auto [party, swing] : seat.tcpSwing) {
			if (aecPartyToSimParty[party] == 0) {
				run.liveSeatTppSwing[seatIndex] = swing;
				run.liveSeatTcpCounted[seatIndex] = seat.tcpSwingProgress;
				run.liveSeatTcpBasis[seatIndex] = seat.tcpSwingBasis;
				int seatId = project.seats().indexToId(seatIndex);
				project.outcomes().add(Outcome(seatId, swing, seat.tcpSwingProgress, 0, 0));
				break;
			}
		}
	}
}

void LivePreparation::prepareLiveTcpSwings()
{
	for (auto const& [id, seat] : currentElection.seats) {
		if (seat.tcpPercent.size() != 2) continue;
		if (seat.isTpp) continue;
		auto firstCandidate = seat.tcpSwing.begin();
		int seatIndex = aecSeatToSimSeat[seat.id];
		run.liveSeatTcpParties[seatIndex] = { aecPartyToSimParty[firstCandidate->first], aecPartyToSimParty[std::next(firstCandidate)->first] };
		run.liveSeatTcpSwing[seatIndex] = firstCandidate->second;
		run.liveSeatTcpPercent[seatIndex] = seat.tcpPercent.at(firstCandidate->first);
		run.liveSeatTcpCounted[seatIndex] = seat.tcpSwingProgress;
		run.liveSeatTcpBasis[seatIndex] = seat.tcpSwingBasis;
	}
}

void LivePreparation::prepareLiveFpSwings()
{
	for (auto const& [seatId, seat] : currentElection.seats) {
		int seatIndex = aecSeatToSimSeat[seat.id];
		run.liveSeatFpCounted[seatIndex] = seat.fpSwingProgress;
		float coalitionMainPercent = 0.0f;
		float coalitionPartnerPercent = 0.0f;
		std::priority_queue<std::tuple<float, float, float>> indFps; // percent, swing, transformed
		for (auto [candidate, swing] : seat.fpSwing) {
			int partyIndex = aecPartyToSimParty[currentElection.candidates[candidate].party];
			// in this section handle only parties that are unambiguously representable
			if (partyIndex == OthersIndex) {
				run.liveSeatFpSwing[seatIndex][partyIndex] += seat.fpSwing.at(candidate);
				run.liveSeatFpTransformedSwing[seatIndex][partyIndex] += seat.fpTransformedSwing.at(candidate);
				run.liveSeatFpPercent[seatIndex][partyIndex] += seat.fpPercent.at(candidate);
			}
			else if (partyIndex == run.indPartyIndex) {
				indFps.push({ seat.fpPercent.at(candidate), swing, seat.fpTransformedSwing.at(candidate) });
			}
			else {
				if (partyIndex == CoalitionPartnerIndex) coalitionPartnerPercent = seat.fpPercent.at(candidate);
				if (partyIndex == 1) coalitionMainPercent = seat.fpPercent.at(candidate);
				run.liveSeatFpSwing[seatIndex][partyIndex] = swing;
				run.liveSeatFpTransformedSwing[seatIndex][partyIndex] += seat.fpTransformedSwing.at(candidate);
				run.liveSeatFpPercent[seatIndex][partyIndex] += seat.fpPercent.at(candidate);
			}
		}
		if (indFps.size()) {
			auto highestInd = indFps.top();
			float voteShare = std::get<0>(highestInd);
			float swing = std::get<1>(highestInd);
			float transformedSwing = std::get<2>(highestInd);
			if (swing < voteShare - 0.1f) { // implies candidate is matched
				run.liveSeatFpSwing[seatIndex][run.indPartyIndex] = swing;
				run.liveSeatFpTransformedSwing[seatIndex][run.indPartyIndex] = transformedSwing;
				run.liveSeatFpPercent[seatIndex][run.indPartyIndex] = voteShare;
			}
			else if (voteShare > 8.0f) {
				run.liveSeatFpSwing[seatIndex][run.indPartyIndex] = swing;
				run.liveSeatFpTransformedSwing[seatIndex][run.indPartyIndex] = transformedSwing;
				run.liveSeatFpPercent[seatIndex][run.indPartyIndex] = voteShare;
			}
			else {
				run.liveSeatFpSwing[seatIndex][OthersIndex] += swing;
				run.liveSeatFpTransformedSwing[seatIndex][OthersIndex] += transformedSwing;
				run.liveSeatFpPercent[seatIndex][OthersIndex] += voteShare;
			}
			indFps.pop();
		}
		if (indFps.size()) {
			auto secondInd = indFps.top();
			float voteShare = std::get<0>(secondInd);
			float swing = std::get<1>(secondInd);
			float transformedSwing = std::get<2>(secondInd);
			if (voteShare > 8.0f) {
				run.liveSeatFpSwing[seatIndex][EmergingIndIndex] = swing;
				run.liveSeatFpTransformedSwing[seatIndex][EmergingIndIndex] = transformedSwing;
				run.liveSeatFpPercent[seatIndex][EmergingIndIndex] = voteShare;
			}
			else {
				run.liveSeatFpSwing[seatIndex][OthersIndex] += swing;
				run.liveSeatFpTransformedSwing[seatIndex][OthersIndex] += transformedSwing;
				run.liveSeatFpPercent[seatIndex][OthersIndex] += voteShare;
			}
			indFps.pop();
		}
		while (indFps.size()) {
			auto otherInd = indFps.top();
			float voteShare = std::get<0>(otherInd);
			float swing = std::get<1>(otherInd);
			float transformedSwing = std::get<2>(otherInd);
			run.liveSeatFpSwing[seatIndex][OthersIndex] += swing;
			run.liveSeatFpTransformedSwing[seatIndex][OthersIndex] += transformedSwing;
			run.liveSeatFpPercent[seatIndex][OthersIndex] += voteShare;
			indFps.pop();
		}
		// All this code ensures that (a) the main Coalition candidate (with highest current primary vote)
		// in the seat is in index 1 and represented as a swing (or vote share only if didn't contest last time)
		// (b) the coalition partner (with lower current primary vote), if any,
		// is in index CoalitionPartnerIndex (-4) and represented as a raw vote share.
		if (run.liveSeatFpSwing[seatIndex].contains(CoalitionPartnerIndex) &&
			!run.liveSeatFpSwing[seatIndex].contains(1)) {
			run.liveSeatFpSwing[seatIndex][1] = run.liveSeatFpSwing[seatIndex][CoalitionPartnerIndex];
			run.liveSeatFpTransformedSwing[seatIndex][1] = run.liveSeatFpTransformedSwing[seatIndex][CoalitionPartnerIndex];
			run.liveSeatFpPercent[seatIndex][1] = run.liveSeatFpPercent[seatIndex][CoalitionPartnerIndex];
			run.liveSeatFpSwing[seatIndex].erase(CoalitionPartnerIndex);
			run.liveSeatFpTransformedSwing[seatIndex].erase(CoalitionPartnerIndex);
			run.liveSeatFpPercent[seatIndex].erase(CoalitionPartnerIndex);
		}
		if (run.liveSeatFpSwing[seatIndex].contains(CoalitionPartnerIndex) &&
			run.liveSeatFpSwing[seatIndex].contains(1) &&
			coalitionPartnerPercent > coalitionMainPercent) {
			run.liveSeatFpSwing[seatIndex][1] = run.liveSeatFpSwing[seatIndex][CoalitionPartnerIndex];
			run.liveSeatFpTransformedSwing[seatIndex][1] = run.liveSeatFpTransformedSwing[seatIndex][CoalitionPartnerIndex];
			run.liveSeatFpPercent[seatIndex][1] = run.liveSeatFpPercent[seatIndex][CoalitionPartnerIndex];
			run.liveSeatFpSwing[seatIndex][CoalitionPartnerIndex] = NaN;
			run.liveSeatFpTransformedSwing[seatIndex][CoalitionPartnerIndex] = NaN;
			run.liveSeatFpPercent[seatIndex][CoalitionPartnerIndex] = coalitionMainPercent;
		}
		else if (run.liveSeatFpSwing[seatIndex].contains(CoalitionPartnerIndex)) {
			run.liveSeatFpSwing[seatIndex][CoalitionPartnerIndex] = NaN;
			run.liveSeatFpTransformedSwing[seatIndex][CoalitionPartnerIndex] = NaN;
			run.liveSeatFpPercent[seatIndex][CoalitionPartnerIndex] = coalitionPartnerPercent;
		}
	}
}

void LivePreparation::determinePpvcBias()
{
	float totalBiasSum = 0.0f;
	float totalBiasWeight = 0.0f;
	for (auto const& [seatId, seat] : currentElection.seats) {
		if (seat.tcpVotes.size() != 2) continue;
		int aecFirstParty = seat.tcpVotes.begin()->first;
		int aecSecondParty = std::next(seat.tcpVotes.begin())->first;
		int firstParty = aecPartyToSimParty[aecFirstParty];
		int secondParty = aecPartyToSimParty[aecSecondParty];
		if (!simPartyIsTpp(firstParty) || !simPartyIsTpp(secondParty)) continue;
		if (firstParty + secondParty == -3) continue; // means they are both coalition parties
		int aecAlp = firstParty == 0 ? aecFirstParty : aecSecondParty;
		float ordinarySwingSum = 0.0f;
		float ordinarySwingWeight = 0.0f;
		float ppvcSwingSum = 0.0f;
		float ppvcSwingWeight = 0.0f;
		for (auto const& boothId : seat.booths) {
			auto const& booth = currentElection.booths[boothId];
			if (booth.tcpSwing.size() != 2 || !booth.tcpSwing.contains(aecAlp)) continue;
			float swing = booth.tcpSwing.at(aecAlp);
			if (std::isnan(swing)) continue;
			float weight = float(booth.totalVotesTcp());
			if (booth.type == Results2::Booth::Type::Ppvc) {
				ppvcSwingSum += swing * weight;
				ppvcSwingWeight += weight;
			}
			else {
				ordinarySwingSum += swing * weight;
				ordinarySwingWeight += weight;
			}
		}
		if (!ordinarySwingWeight) continue;
		if (!ppvcSwingWeight) continue;
		float averageOrdinarySwing = ordinarySwingSum / ordinarySwingWeight;
		float averagePPVCSwing = ppvcSwingSum / ppvcSwingWeight;
		float bias = averagePPVCSwing - averageOrdinarySwing;
		totalBiasSum += bias * ppvcSwingWeight;
		totalBiasWeight += ppvcSwingWeight;
	}
	if (!totalBiasWeight) {
		run.ppvcBiasObserved = 0.0f;
		run.ppvcBiasConfidence = 0.0f;
	}
	else {
		float finalBias = totalBiasSum / totalBiasWeight;
		run.ppvcBiasObserved = finalBias;
		run.ppvcBiasConfidence = totalBiasWeight;
	}
	PA_LOG_VAR(run.ppvcBiasObserved);
	PA_LOG_VAR(run.ppvcBiasConfidence);
}

void LivePreparation::prepareOverallLiveFpSwings()
{
	for (auto [partyId, party] : project.parties()) {
		int partyIndex = project.parties().idToIndex(partyId);
		if (partyIndex < 2) continue;
		int swingSeatsTurnout = 0;
		int swingSeatsVotes = 0;
		int totalPastTurnout = 0;
		int newSeats = 0;
		for (auto const& [seatId, seat] : currentElection.seats) {
			int seatIndex = aecSeatToSimSeat[seat.id];
			int pastTurnout = run.pastSeatResults[seatIndex].turnoutCount;
			totalPastTurnout += pastTurnout;
			for (auto [candidateId, votes] : seat.fpVotes) {
				if (aecPartyToSimParty[currentElection.candidates[candidateId].party] == partyIndex) {
					// the party is contesting this seat!
					if (run.pastSeatResults[seatIndex].fpVoteCount.contains(partyIndex)) {
						swingSeatsVotes += run.pastSeatResults[seatIndex].fpVoteCount[partyIndex];
						swingSeatsTurnout += pastTurnout;
					}
					else {
						++newSeats;
					}
				}
			}
		}
		float swingSeatsPastPercent = float(swingSeatsVotes) / float(swingSeatsTurnout) * 100.0f;
		float swingVoteExpected = swingSeatsPastPercent > 0 ?
			detransformVoteShare(transformVoteShare(swingSeatsPastPercent) + run.liveOverallFpSwing[partyIndex]) : 0.0f;
		swingVoteExpected *= float(swingSeatsTurnout) / float(totalPastTurnout);
		float newVoteExpected = newSeats > 0 ? float(newSeats) * run.liveOverallFpNew[partyIndex] / float(project.seats().count()) : 0.0f;
		run.liveOverallFpTarget[partyIndex] = swingVoteExpected + newVoteExpected;
	}
}

std::string LivePreparation::getTermCode()
{
	return run.yearCode + run.regionCode;
}

std::map<int, int> LivePreparation::findMatchedParties(Results2::Seat const& previousSeat, Results2::Seat const& currentSeat)
{
	std::map<int, int> matchedAffiliation;
	for (auto [affiliation, votes] : currentSeat.tcpVotes) {
		for (auto [prevAffiliation, prevVotes] : previousSeat.tcpVotes) {
			if (aecPartyToSimParty[affiliation] == aecPartyToSimParty[prevAffiliation] && aecPartyToSimParty[affiliation] != -1) {
				matchedAffiliation[affiliation] = prevAffiliation;
				break;
			}
		}
	}
	return matchedAffiliation;
}
