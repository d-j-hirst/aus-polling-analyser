#include "PreviousElectionDataRetriever.h"

#include "General.h"
#include "Log.h"

#include "tinyxml2.h"

#include <fstream>
#include <map>

const std::string PreviousElectionDataRetriever::UnzippedFileName = "downloads/previous_results.xml";

void loadFirstPreferencesForSeat(Results2::Election& election, Results2::Seat& thisSeat, tinyxml2::XMLElement* currentCandidate) {
	auto isIndependent = (currentCandidate->FindAttribute("Independent") != nullptr ||
		currentCandidate->FindAttribute("NoAffiliation") != nullptr);
	auto candidateIdElement = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
	auto candidateName = candidateIdElement->FirstChildElement("eml:CandidateName")->GetText();
	auto candidateId = candidateIdElement->FindAttribute("Id")->IntValue();
	int affiliationId = -1;
	if (!isIndependent) {
		auto affiliationElement = currentCandidate->FirstChildElement("eml:AffiliationIdentifier");
		std::string affiliationName = affiliationElement->FirstChildElement("eml:RegisteredName")->GetText();
		affiliationId = affiliationElement->FindAttribute("Id")->IntValue();
		std::string affiliationShortCode = affiliationElement->FindAttribute("ShortCode")->Value();
		if (!election.parties.count(affiliationId)) {
			Results2::Party thisParty;
			thisParty.id = affiliationId;
			thisParty.name = affiliationName;
			thisParty.shortCode = affiliationShortCode;
			election.parties[affiliationId] = thisParty;
		}
	}
	if (!election.candidates.count(candidateId)) {
		Results2::Candidate thisCandidate;
		thisCandidate.id = candidateId;
		thisCandidate.name = candidateName;
		thisCandidate.party = affiliationId;
		election.candidates[candidateId] = thisCandidate;
	}
	auto currentVoteType = currentCandidate->FirstChildElement("VotesByType")->FirstChildElement("Votes");
	int ordinaryVotes = 0;
	int absentVotes = 0;
	int provisionalVotes = 0;
	int prepollVotes = 0;
	int postalVotes = 0;
	do {
		std::string voteType = currentVoteType->FindAttribute("Type")->Value();
		auto thisVoteCount = currentVoteType->IntText();
		if (voteType == "Ordinary") ordinaryVotes = thisVoteCount;
		else if (voteType == "Absent") absentVotes = thisVoteCount;
		else if (voteType == "Provisional") provisionalVotes = thisVoteCount;
		else if (voteType == "PrePoll") prepollVotes = thisVoteCount;
		else if (voteType == "Postal") postalVotes = thisVoteCount;
		currentVoteType = currentVoteType->NextSiblingElement("Votes");
	} while (currentVoteType);
	thisSeat.ordinaryVotesFp[candidateId] = ordinaryVotes;
	thisSeat.absentVotesFp[candidateId] = absentVotes;
	thisSeat.provisionalVotesFp[candidateId] = provisionalVotes;
	thisSeat.prepollVotesFp[candidateId] = prepollVotes;
	thisSeat.postalVotesFp[candidateId] = postalVotes;
}

void load2cpForSeat(Results2::Seat& thisSeat, tinyxml2::XMLElement* currentCandidate) {
	auto candidateIdElement = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
	auto candidateId = candidateIdElement->FindAttribute("Id")->IntValue();
	auto currentVoteType = currentCandidate->FirstChildElement("VotesByType")->FirstChildElement("Votes");
	int ordinaryVotes = 0;
	int absentVotes = 0;
	int provisionalVotes = 0;
	int prepollVotes = 0;
	int postalVotes = 0;
	do {
		std::string voteType = currentVoteType->FindAttribute("Type")->Value();
		auto thisVoteCount = currentVoteType->IntText();
		if (voteType == "Ordinary") ordinaryVotes = thisVoteCount;
		else if (voteType == "Absent") absentVotes = thisVoteCount;
		else if (voteType == "Provisional") provisionalVotes = thisVoteCount;
		else if (voteType == "PrePoll") prepollVotes = thisVoteCount;
		else if (voteType == "Postal") postalVotes = thisVoteCount;
		currentVoteType = currentVoteType->NextSiblingElement("Votes");
	} while (currentVoteType);
	thisSeat.ordinaryVotesTcp[candidateId] = ordinaryVotes;
	thisSeat.absentVotesTcp[candidateId] = absentVotes;
	thisSeat.provisionalVotesTcp[candidateId] = provisionalVotes;
	thisSeat.prepollVotesTcp[candidateId] = prepollVotes;
	thisSeat.postalVotesTcp[candidateId] = postalVotes;
}

void loadBoothForSeat(Results2::Election& election, Results2::Seat& thisSeat, tinyxml2::XMLElement* currentPollingPlace) {
	Results2::Booth thisBooth;
	auto pollingPlaceIdentifier = currentPollingPlace->FirstChildElement("PollingPlaceIdentifier");
	auto pollingPlaceName = pollingPlaceIdentifier->FindAttribute("Name")->Value();
	auto pollingPlaceClass = pollingPlaceIdentifier->FindAttribute("Classification");
	std::string pollingPlaceClassString = (pollingPlaceClass ? pollingPlaceClass->Value() : "");
	auto pollingPlaceId = pollingPlaceIdentifier->FindAttribute("Id")->IntValue();
	thisBooth.id = pollingPlaceId;
	thisBooth.name = pollingPlaceName;
	if (pollingPlaceClassString == "PrePollVotingCentre") thisBooth.type = Results2::Booth::Type::Ppvc;
	else if (pollingPlaceClassString == "SpecialHospital") thisBooth.type = Results2::Booth::Type::Hospital;
	else if (pollingPlaceClassString == "RemoteMobile") thisBooth.type = Results2::Booth::Type::Remote;
	else if (pollingPlaceClassString == "PrisonMobile") thisBooth.type = Results2::Booth::Type::Prison;
	auto currentCandidate = currentPollingPlace->FirstChildElement("FirstPreferences")->FirstChildElement("Candidate");
	do {
		auto candidateIdElement = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
		auto candidateId = candidateIdElement->FindAttribute("Id")->IntValue();
		auto votes = currentCandidate->FirstChildElement("Votes")->IntText();
		thisBooth.fpVotes[candidateId] = votes;
		currentCandidate = currentCandidate->NextSiblingElement("Candidate");
	} while (currentCandidate);
	currentCandidate = currentPollingPlace->FirstChildElement("TwoCandidatePreferred")->FirstChildElement("Candidate");
	do {
		auto candidateIdElement = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
		auto candidateId = candidateIdElement->FindAttribute("Id")->IntValue();
		auto votes = currentCandidate->FirstChildElement("Votes")->IntText();
		thisBooth.tcpVotes[candidateId] = votes;
		currentCandidate = currentCandidate->NextSiblingElement("Candidate");
	} while (currentCandidate);
	election.booths[pollingPlaceId] = thisBooth;
	thisSeat.booths.push_back(pollingPlaceId);
}

void loadSeat(Results2::Election& election, tinyxml2::XMLElement* currentContest) {
	Results2::Seat thisSeat;
	auto districtElement = currentContest->FirstChildElement("PollingDistrictIdentifier");
	thisSeat.id = districtElement->FindAttribute("Id")->IntValue();
	thisSeat.name = districtElement->FirstChildElement("Name")->GetText();
	thisSeat.enrolment = currentContest->FirstChildElement("Enrolment")->IntText();
	auto currentCandidate = currentContest->FirstChildElement("FirstPreferences")->FirstChildElement("Candidate");
	do {
		loadFirstPreferencesForSeat(election, thisSeat, currentCandidate);
		currentCandidate = currentCandidate->NextSiblingElement("Candidate");
	} while (currentCandidate);
	currentCandidate = currentContest->FirstChildElement("TwoCandidatePreferred")->FirstChildElement("Candidate");
	do {
		load2cpForSeat(thisSeat, currentCandidate);
		currentCandidate = currentCandidate->NextSiblingElement("Candidate");
	} while (currentCandidate);
	auto currentPollingPlace = currentContest->FirstChildElement("PollingPlaces")->FirstChildElement("PollingPlace");
	do {
		loadBoothForSeat(election, thisSeat, currentPollingPlace);
		currentPollingPlace = currentPollingPlace->NextSiblingElement("PollingPlace");
	} while (currentPollingPlace);
	election.seats[thisSeat.id] = thisSeat;
}

void consolidateParties(Results2::Election& election) {
	std::unordered_map<std::string, int> codeToId;
	for (auto const& [key, party] : election.parties) {
		auto mapIt = codeToId.find(party.shortCode);
		if (mapIt == codeToId.end()) {
			codeToId.insert({ party.shortCode, party.id });
		}
		else {
			mapIt->second = std::min(mapIt->second, party.id);
		}
	}
	for (auto& [key, candidate] : election.candidates) {
		candidate.party = codeToId[election.parties[candidate.party].shortCode];
	}
	std::vector<decltype(election.parties)::iterator> partiesToDelete;
	for (auto partyIt = election.parties.begin(); partyIt != election.parties.end(); ++partyIt) {
		if (codeToId[partyIt->second.shortCode] != partyIt->second.id) {
			partiesToDelete.push_back(partyIt);
		}
	}
	for (auto partyIt : partiesToDelete) {
		election.parties.erase(partyIt);
	}
}

Results2::Election PreviousElectionDataRetriever::collectData()
{
	std::ifstream file(UnzippedFileName);
	std::string xmlString;
	transferFileToString(file, xmlString);
	file.close(); // don't prevent the file from being used for longer than necessary

	Results2::Election election;

	tinyxml2::XMLDocument doc;
	doc.LoadFile(UnzippedFileName.c_str());
	auto eventIdentifier = doc.FirstChildElement("MediaFeed")->FirstChildElement("Results")->FirstChildElement("eml:EventIdentifier");
	election.name = eventIdentifier->FirstChildElement("eml:EventName")->GetText();
	election.id = eventIdentifier->FindAttribute("Id")->IntValue();
	auto contests = doc.FirstChildElement("MediaFeed")->FirstChildElement("Results")->FirstChildElement("Election")
		->FirstChildElement("House")->FirstChildElement("Contests");
	auto currentContest = contests->FirstChildElement("Contest");
	do {
		loadSeat(election, currentContest);
		currentContest = currentContest->NextSiblingElement("Contest");
	} while (currentContest);
	consolidateParties(election);
	return election;
}

Results2::Election PreviousElectionDataRetriever::load2004Tcp(std::string filename)
{
	logger << "Loading results from " << filename << " using 2004 format\n";
	Results2::Election election;
	std::ifstream file(filename);
	std::string firstLine; std::string secondLine;
	std::getline(file, firstLine);
	std::getline(file, secondLine);
	logger << firstLine << "\n";
	auto splitFirstLine = splitString(firstLine, " ");
	auto electionWord = std::find(splitFirstLine.begin(), splitFirstLine.end(), "Election");
	if (electionWord != splitFirstLine.end()) ++electionWord;
	election.name = std::accumulate(splitFirstLine.begin(), electionWord, std::string(),
		[](std::string existing, std::string addition) {return existing + addition + " "; });
	election.name = election.name.substr(0, election.name.size() - 1); // remove trailing space
	int year = std::stoi(filename.substr(filename.find("/") + 1, 4));
	election.id = year;

	std::unordered_map<std::string, int> partyCodeToId;
	partyCodeToId.insert({ "IND", -1 });
	int partyIndex = 0;
	do {
		std::string candidateLine;
		std::getline(file, candidateLine);
		if (!file) break;
		auto splitCandidateLine = splitString(candidateLine, ",");
		int seatId = std::stoi(splitCandidateLine[1]);
		std::string seatName = splitCandidateLine[2];
		int boothId = std::stoi(splitCandidateLine[3]);
		std::string boothName = splitCandidateLine[4];
		int candidateId = std::stoi(splitCandidateLine[5]);
		std::string candidateName = splitCandidateLine[6] + ", " + splitCandidateLine[7];
		std::string partyCode = splitCandidateLine[10];
		int votes = std::stoi(splitCandidateLine[12]);
		if (!election.seats.count(seatId)) {
			Results2::Seat seat;
			seat.id = seatId;
			seat.name = seatName;
			election.seats.insert({ seatId, seat });
		}
		auto& seat = election.seats.find(seatId)->second;
		if (!election.booths.count(boothId)) {
			Results2::Booth booth;
			booth.id = boothId;
			booth.name = boothName;
			election.booths.insert({ boothId, booth });
		}
		if (!std::count(seat.booths.begin(), seat.booths.end(), boothId)) seat.booths.push_back(boothId);
		auto& booth = election.booths.find(boothId)->second;
		booth.tcpVotes.insert({ candidateId, votes });
		if (!partyCodeToId.count(partyCode)) {
			partyCodeToId[partyCode] = partyIndex;
			Results2::Party party;
			party.id = partyCodeToId[partyCode];
			party.shortCode = partyCode;
			election.parties.insert({ party.id, party });
			partyIndex++;
		}
		if (!election.seats.count(candidateId)) {
			Results2::Candidate candidate;
			candidate.id = candidateId;
			candidate.name = candidateName;
			candidate.party = partyCodeToId[partyCode];
			election.candidates.insert({ candidateId, candidate });
		}
	} while (true);

	consolidateParties(election);
	logger << "Election name: " << election.name << "\n";
	return election;
}

struct BoothAndSeat { std::string boothName; std::string seatName; };
struct BoothSeatComp {
	bool operator()(BoothAndSeat const& lhs, BoothAndSeat const& rhs) const {
		if (lhs.boothName < rhs.boothName) return true;
		if (lhs.boothName > rhs.boothName) return false;
		if (lhs.seatName < rhs.seatName) return true;
		if (lhs.seatName > rhs.seatName) return false;
		return false;
	};

};

Results2::Election PreviousElectionDataRetriever::loadPre2004Tcp(Results2::Election const& templateElection, std::string filename)
{
	logger << "Loading results from " << filename << " using pre-2004 format\n";
	Results2::Election election;

	// This section looks for duplicate booth names in the file, which are later used to
	// avoid matching the wrong booths if there are two with the same name in different seats
	std::ifstream firstRead(filename);
	std::string firstLine;
	std::getline(firstRead, firstLine); // skip first line
	std::map<std::string, int> fileUniqueBoothNames; // stores booth id if unique and -1 if not
	do {
		std::string candidateLine;
		std::getline(firstRead, candidateLine);
		if (!firstRead) break;
		auto splitCandidateLine = splitString(candidateLine, ";");
		std::string boothName = splitCandidateLine[1];
		if (!fileUniqueBoothNames.count(boothName)) {
			fileUniqueBoothNames.insert({ boothName, 1 });
		}
		else {
			fileUniqueBoothNames[boothName] = -1;
		}
	} while (true);

	std::ifstream file(filename);
	int year = std::stoi(filename.substr(filename.find("/") + 1, 4));
	election.id = year;
	election.name = std::to_string(year) + " Federal Election";
	std::getline(file, firstLine); // skip first line

	// Now go through the previous election data and look for duplicate booths
	// We only want to match booths that either have a unique name in both elections,
	// or which also have their seat matched.
	std::unordered_map<std::string, int> seatNameToId;
	std::map<BoothAndSeat, int, BoothSeatComp> boothNameToId;
	std::map<std::string, int> uniqueBoothNames; // stores booth id if unique and -1 if not
	for (auto const& [key, seat] : templateElection.seats) {
		seatNameToId.insert({ seat.name, seat.id });
		for (auto const& booth : seat.booths) {
			if (templateElection.booths.count(booth)) {
				std::string boothName = templateElection.booths.at(booth).name;
				boothNameToId.insert({ {boothName, seat.name}, booth });
				if (!uniqueBoothNames.count(boothName)) {
					uniqueBoothNames.insert({ boothName, booth });
				}
				else {
					uniqueBoothNames[boothName] = -1;
				}
				if (fileUniqueBoothNames[boothName] == -1) uniqueBoothNames[boothName] = -1;
			}
		}
	}
	Results2::Party alp;
	alp.shortCode = "ALP";
	alp.id = 1;
	Results2::Party lp;
	lp.shortCode = "LP";
	lp.id = 2;
	election.parties.insert({ 1, alp });
	election.parties.insert({ 2, lp });
	int seatIndex = -1;
	int boothIndex = -1;
	int candidateIndex = 0;
	do {
		std::string candidateLine;
		std::getline(file, candidateLine);
		if (!file) break;
		auto splitCandidateLine = splitString(candidateLine, ";");
		std::string seatName = splitCandidateLine[0];
		std::string boothName = splitCandidateLine[1];
		int alpVotes = std::stoi(splitCandidateLine[2]);
		int lpVotes = std::stoi(splitCandidateLine[5]);
		int seatId = (seatNameToId.count(seatName) ? seatNameToId[seatName] : seatIndex--);
		int boothId = -100000000;
		if (boothNameToId.count({ boothName, seatName })) boothId = boothNameToId.at({ boothName, seatName });
		else if (uniqueBoothNames.count(boothName) && uniqueBoothNames.at(boothName) != -1) boothId = uniqueBoothNames.at(boothName);
		else boothId = boothIndex--;
		if (!election.seats.count(seatId)) {
			Results2::Seat seat;
			seat.id = seatId;
			seat.name = seatName;
			election.seats.insert({ seatId, seat });
			seatNameToId[seatName] = seatId;
		}
		auto& seat = election.seats.find(seatId)->second;
		if (!election.booths.count(boothId)) {
			Results2::Booth booth;
			booth.id = boothId;
			booth.name = boothName;
			election.booths.insert({ boothId, booth });
			uniqueBoothNames[boothName] = -1;
		}
		Results2::Booth booth;
		booth.id = boothId;
		booth.name = boothName;
		election.booths.insert({ boothId, booth });
		if (!std::count(seat.booths.begin(), seat.booths.end(), boothId)) seat.booths.push_back(boothId);
		auto& thisBooth = election.booths.find(boothId)->second;
		Results2::Candidate alpCandidate;
		alpCandidate.id = candidateIndex;
		alpCandidate.party = 1;
		election.candidates.insert({ candidateIndex, alpCandidate });
		thisBooth.tcpVotes.insert({ candidateIndex, alpVotes });
		++candidateIndex;
		Results2::Candidate lpCandidate;
		lpCandidate.id = candidateIndex;
		lpCandidate.party = 2;
		election.candidates.insert({ candidateIndex, lpCandidate });
		thisBooth.tcpVotes.insert({ candidateIndex, lpVotes });
		++candidateIndex;
	} while (true);

	logger << "Election name: " << election.name << "\n";
	return election;
}
