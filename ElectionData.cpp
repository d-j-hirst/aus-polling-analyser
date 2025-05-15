#include "ElectionData.h"

#include "General.h"
#include "Log.h"

#include <set>

std::map<std::string, Results2::VoteType> typeNameToVoteType = {
	{"Ordinary", Results2::VoteType::Ordinary},
	{"PP", Results2::VoteType::Ordinary},
	{"PR", Results2::VoteType::Ordinary},
	{"Absent", Results2::VoteType::Absent},
	{"Provisional", Results2::VoteType::Provisional},
	{"Enrolment/Provisional", Results2::VoteType::Provisional},
	{"PrePoll", Results2::VoteType::PrePoll},
	{"Postal", Results2::VoteType::Postal},
	{"Early", Results2::VoteType::Early},
	{"iVote", Results2::VoteType::IVote}
};

// WAEC doesn't give us any numerical IDs for the booths/seats, so we need to generate our own
// Create a simple hash function as a named lambda
int hashName(const std::string& name) {
	std::size_t hash = 0;
	for (char c : name) {
		hash = hash * 31 + c; // Multiply by prime number and add character value
	}
	return static_cast<int>(hash & 0x7FFFFFFF); // Ensure positive value by masking
};

Results2::Election Results2::Election::createAec(tinyxml2::XMLDocument const& xml, std::string const& termCode)
{
	Election election(termCode);
	election.update(xml);
	return election;
}

Results2::Election Results2::Election::createVec(nlohmann::json const& results, tinyxml2::XMLDocument const& input_candidates, tinyxml2::XMLDocument const& input_booths, std::string const& termCode)
{
	Election election(termCode);
	election.update2022VicPrev(results, input_candidates, input_booths);
	return election;
}

Results2::Election Results2::Election::createVec(tinyxml2::XMLDocument const& input_candidates, tinyxml2::XMLDocument const& input_booths, std::string const& termCode)
{
	Election election(termCode);	
	election.preload2022Vic(input_candidates, input_booths, true);
	return election;
}

Results2::Election Results2::Election::createNswec(nlohmann::json const& results, tinyxml2::XMLDocument const& zeros, std::string const& termCode)
{
	Election election(termCode);
	election.preloadNswec(results, zeros, true);
	return election;
}

Results2::Election Results2::Election::createQec(nlohmann::json const& results, tinyxml2::XMLDocument const& zeros, std::string const& termCode)
{
	Election election(termCode);
	election.preloadQec(results, zeros);
	return election;
}

Results2::Election Results2::Election::createWaec(tinyxml2::XMLDocument const& candidatesXml, tinyxml2::XMLDocument const& resultsXml, std::string const& termCode)
{
	Election election(termCode);
	election.preloadWaec(candidatesXml, resultsXml);
	return election;
}

void Results2::Election::preload2022Vic(tinyxml2::XMLDocument const& input_candidates, tinyxml2::XMLDocument const& input_booths, bool includeSeatBooths)
{
	const std::map<std::string, std::string> shortCodes = {
		{"The Australian Greens - Victoria", "GRN"},
		{"Australian Labor Party - Victorian Branch", "ALP"},
		{"Liberal Party of Australia - Victorian Division", "LNP"},
		{"National Party of Australia - Victoria", "LNP"}
	};
	auto candidateList = input_candidates.FirstChildElement("EML")->FirstChildElement("CandidateList")->FirstChildElement("Election");
	auto currentContest = candidateList->FirstChildElement("Contest");
	while (currentContest) {
		Seat seat;
		auto contestIdentifier = currentContest->FirstChildElement("PollingDistrictIdentifier");
		seat.id = contestIdentifier->FindAttribute("Id")->IntValue();
		if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data
		std::string contestName = currentContest->FirstChildElement("ContestIdentifier")->FirstChildElement("ContestName")->GetText();
		seat.name = contestName.substr(0, contestName.length() - 9);
		seat.enrolment = currentContest->FirstChildElement("Enrolment")->IntText();
		auto currentCandidate = currentContest->FirstChildElement("Candidate");
		while (currentCandidate) {
			Candidate candidate;
			auto candidateIdentifier = currentCandidate->FirstChildElement("CandidateIdentifier");
			candidate.id = candidateIdentifier->FindAttribute("Id")->IntValue();
			candidate.name = candidateIdentifier->FirstChildElement("CandidateName")->GetText();
			auto affiliationEl = currentCandidate->FirstChildElement("Affiliation");
			if (affiliationEl) {
				auto affiliationIdentifier = affiliationEl->FirstChildElement("AffiliationIdentifier");
				candidate.party = affiliationIdentifier->FindAttribute("Id")->IntValue();
				if (!parties.contains(candidate.party)) {
					parties[candidate.party] = Party();
					parties[candidate.party].id = candidate.party;
					parties[candidate.party].name =
						affiliationIdentifier->FirstChildElement("RegisteredName")->GetText();
					if (shortCodes.contains(parties[candidate.party].name)) {
						parties[candidate.party].shortCode = shortCodes.at(parties[candidate.party].name);
					}
				}
			}
			else {
				candidate.party = Candidate::Independent;
				parties[Candidate::Independent].id = Candidate::Independent;
				parties[Candidate::Independent].shortCode = "IND";
				parties[Candidate::Independent].name = "Independent";
			}
			candidates[candidate.id] = candidate;

			currentCandidate = currentCandidate->NextSiblingElement("Candidate");
		}

		seats[seat.id] = seat;
		currentContest = currentContest->NextSiblingElement("Contest");
	}
	auto currentPollingDistrict = input_booths.FirstChildElement("PollingDistrictList")->FirstChildElement("PollingDistrict");
	while (currentPollingDistrict) {
		int seatId = currentPollingDistrict->FirstChildElement("PollingDistrictIdentifier")->IntAttribute("Id");
		auto currentBooth = currentPollingDistrict->FirstChildElement("PollingPlaces")->FirstChildElement("PollingPlace");
		while (currentBooth) {
			Booth booth;
			booth.id = currentBooth->FirstChildElement("PollingPlaceIdentifier")->IntAttribute("Id");
			booth.name = currentBooth->FirstChildElement("PollingPlaceIdentifier")->Attribute("Name");
			booth.parentSeat = seatId;
			booths[booth.id] = booth;
			currentBooth = currentBooth->NextSiblingElement("PollingPlace");
			if (includeSeatBooths) {
				seats[seatId].booths.push_back(booth.id);
			}
		}
		currentPollingDistrict = currentPollingDistrict->NextSiblingElement("PollingDistrict");
	}
}

void Results2::Election::update2022VicPrev(nlohmann::json const& results, tinyxml2::XMLDocument const& input_candidates, tinyxml2::XMLDocument const& input_booths)
{
	preload2022Vic(input_candidates, input_booths);
	std::map<std::string, int> seatNameToId;
	for (auto [seatId, seat] : seats) {
		if (seatNameToId.contains(seat.name)) {
			logger << "Warning: duplicate name for " << seat.name << "!\n";
		}
		else {
			seatNameToId[seat.name] = seatId;
		}
	}
	for (auto [candidateId, candidate] : candidates) {
		if (candidateNameToId.contains(candidate.name)) {
			logger << "Warning: duplicate name for " << candidate.name << "!\n";
		}
		else {
			candidateNameToId[candidate.name] = candidateId;
		}
	}
	std::set<std::string> seenBooths;
	std::set<std::string> ambiguousBooths;
	for (auto [boothId, booth] : booths) {
		if (seenBooths.contains(booth.name)) {
			ambiguousBooths.emplace(booth.name);
		}
		else {
			seenBooths.emplace(booth.name);
		}
	}
	std::map<std::string, int> boothNameToId;
	std::map<std::pair<std::string, std::string>, int> ambiguousBoothNameToId;
	for (auto [boothId, booth] : booths) {
		if (ambiguousBooths.contains(booth.name)) {
			ambiguousBoothNameToId[{booth.name, seats[booth.parentSeat].name}] = boothId;
		}
		else {
			boothNameToId[booth.name] = boothId;
		}
	}
	//PA_LOG_VAR(boothNameToId);
	//PA_LOG_VAR(ambiguousBoothNameToId);
	int dummyCandidateId = -100000; // Low numbers that will never be mistaken for an official id
	int dummyBoothId = -100000; // Low numbers that will never be mistaken for an official id
	// Not worth soft-coding this
	const std::map<std::string, int> partyIds = {
		{"ANIMAL JUSTICE PARTY",  25},
		{"LIBERAL", 8},
		{"FIONA PATTEN'S REASON PARTY", 20},
		{"SUSTAINABLE AUSTRALIA", 91},
		{"AUSTRALIAN GREENS", 4},
		{"AUSTRALIAN LABOR PARTY", 5},
		{"LABOUR DLP", 7},
		{"SHOOTERS, FISHERS & FARMERS VIC", 28},
		{"VICTORIAN SOCIALISTS", 90},
		{"THE NATIONALS", 9},
		{"DERRYN HINCH'S JUSTICE PARTY", 89},
		{"LIBERAL DEMOCRATS", 30},
		{"TRANSPORT MATTERS", 87},
		{"AUSSIE BATTLER PARTY", -1},
		{"AUSTRALIAN COUNTRY PARTY", -1},
		{"AUSTRALIAN LIBERTY ALLIANCE", -1},
		{"INDEPENDENT", -1}
	};
	std::set<int> matchedIds;
	for (auto const& [seatName, seatValue] : results.items()) {
		int seatId = -1;
		if (seatNameToId.contains(seatName)) {
			seatId = seatNameToId[seatName];
		}
		std::map<int, int> indexToId;
		for (auto const& [candIndex, candValue] : seatValue["candidates"].items()) {
			int candIndexI = std::stoi(candIndex);
			auto candidateName = candValue["name"];
			auto party = candValue["party"];
			if (candidateNameToId.contains(candidateName)) {
				indexToId[candIndexI] = candidateNameToId[candidateName];
			}
			else {
				indexToId[candIndexI] = dummyCandidateId;
				Candidate candidate;
				candidate.id = dummyCandidateId;
				candidate.name = candidateName;
				candidate.party = partyIds.at(party);
				candidates[candidate.id] = candidate;
				--dummyCandidateId;
			}
		}
		for (auto const& [boothName, boothValue] : seatValue["booths"].items()) {
			if (boothName.find("Votes") != std::string::npos) {
				VoteType voteType = VoteType::Invalid;
				if (boothName == "Postal Votes") voteType = VoteType::Postal;
				if (boothName == "Absent Votes") voteType = VoteType::Absent;
				if (boothName == "Early Votes") voteType = VoteType::Early;
				if (boothName == "Provisional Votes") voteType = VoteType::Provisional;
				if (voteType == VoteType::Invalid) continue;
				auto fps = boothValue["fp"];
				for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
					int fpCandIndexI = std::stoi(fpCandIndex);
					int fpCandId = indexToId[fpCandIndexI];
					seats[seatId].fpVotes[fpCandId][voteType] += fpVotes;
				}
				auto tcps = boothValue["tcp"];
				for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
					int tcpCandIndexI = std::stoi(tcpCandIndex);
					int tcpCandId = indexToId[tcpCandIndexI];
					int tcpAffiliation = candidates[tcpCandId].party;
					seats[seatId].tcpVotes[tcpAffiliation][voteType] += tcpVotes;
				}
				continue;
			}
			int boothId = dummyBoothId;
			if (boothNameToId.contains(boothName)) {
				boothId = boothNameToId[boothName];
			}
			else if (ambiguousBoothNameToId.contains({ boothName, seatName })) {
				boothId = ambiguousBoothNameToId[{ boothName, seatName }];
			}
			else {
				Booth booth;
				booth.name = boothName;
				booth.id = dummyBoothId;
				booths[booth.id] = booth;
				--dummyBoothId;
			}
			if (matchedIds.contains(boothId)) {
				// If two "old" booths match to one "new" booth then we don't know
				// which "old" booth to actually compare to
				// so ... make new booths that won't match to either
				if (booths.contains(boothId)) {
					auto boothData = booths[boothId];
					boothData.id = dummyBoothId;
					booths.erase(boothId);
					booths[dummyBoothId] = boothData;
					auto& parent = seats[booths[dummyBoothId].parentSeat];
					//PA_LOG_VAR(parent.name);
					//PA_LOG_VAR(parent.booths);
					//PA_LOG_VAR(boothId);
					//PA_LOG_VAR(dummyBoothId);
					auto foundBoothId = std::find(parent.booths.begin(), parent.booths.end(), boothId);
					if (foundBoothId != parent.booths.end()) *foundBoothId = dummyBoothId;
					//PA_LOG_VAR(parent.booths);
					//logger << "Detached old ambiguous booth: " << booths[dummyBoothId].name << "\n";
					--dummyBoothId;
				}
				boothId = dummyBoothId;
				//logger << "Detached ambiguous booth: " << boothName << "\n";
				--dummyBoothId;
			}
			matchedIds.insert(boothId);
			Booth& booth = booths[boothId];
			// the parent seat refers to the seat this booth is in for the new election,
			// but it should the the one it is in for this (old) election
			booth.parentSeat = seatId;
			booth.id = boothId;
			booth.name = boothName;
			auto fps = boothValue["fp"];
			for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
				int fpCandIndexI = std::stoi(fpCandIndex);
				int fpCandId = indexToId[fpCandIndexI];
				booth.fpVotes[fpCandId] = fpVotes;
				if (seatId > 0) seats[seatId].fpVotes[fpCandId][VoteType::Ordinary] += fpVotes;
			}
			auto tcps = boothValue["tcp"];
			for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
				int tcpCandIndexI = std::stoi(tcpCandIndex);
				int tcpCandId = indexToId[tcpCandIndexI];
				int tcpAffiliation = candidates[tcpCandId].party;
				booth.tcpVotes[tcpAffiliation] = tcpVotes;
				if (seatId > 0) seats[seatId].tcpVotes[tcpAffiliation][VoteType::Ordinary] += tcpVotes;
			}
			if (seatId > 0) {
				seats[seatId].booths.push_back(booth.id);
			}
		}
	}

	//logger << "==SEATS==\n";
	//for (auto const& [seatId, seat] : seats) {
	//	PA_LOG_VAR(seat.name);
	//	PA_LOG_VAR(seat.id);
	//	PA_LOG_VAR(seat.enrolment);
	//	PA_LOG_VAR(seat.fpVotes);
	//	PA_LOG_VAR(seat.tcpVotes);
	//	PA_LOG_VAR(seat.tppVotes);
	//	for (int boothId : seat.booths) {
	//		PA_LOG_VAR(boothId);
	//		auto const& booth = booths.at(boothId);
	//		PA_LOG_VAR(booth.id);
	//		PA_LOG_VAR(booth.name);
	//		PA_LOG_VAR(booth.fpVotes);
	//		PA_LOG_VAR(booth.tcpVotes);
	//		PA_LOG_VAR(booth.type);
	//	}
	//}
	//logger << "==CANDIDATES==\n";
	//for (auto const& candidate : candidates) {
	//	PA_LOG_VAR(candidate.second.id);
	//	PA_LOG_VAR(candidate.second.name);
	//	if (candidate.second.party != -1) {
	//		PA_LOG_VAR(parties[candidate.second.party].name);
	//	}
	//	else {
	//		logger << "Independent\n";
	//	}
	//}
	//logger << "==PARTIES==\n";
	//for (auto const& party : parties) {
	//	PA_LOG_VAR(party.second.id);
	//	PA_LOG_VAR(party.second.name);
	//	PA_LOG_VAR(party.second.shortCode);
	//}
	logger << "Appeared to successfully load past election data\n";
}

void Results2::Election::preloadNswec([[maybe_unused]] nlohmann::json const& results, tinyxml2::XMLDocument const& zeros, bool includeSeatBooths)
{
	const std::map<std::string, std::string> shortCodes = {
		{"Shooters, Fishers and Farmers Party (NSW) Incorporated", "SFF"},
		{"The Greens NSW", "GRN"},
		{"Australian Labor Party (NSW Branch)", "ALP"},
		{"Country Labor Party", "ALP"},
		{"The Liberal Party of Australia, New South Wales Division", "LNP"},
		{"National Party of Australia - NSW", "LNP"}
	};
	auto contests = zeros.FirstChildElement("MediaFeed")
		->FirstChildElement("Election")
		->FirstChildElement("House")
		->FirstChildElement("Contests");
	auto currentContest = contests->FirstChildElement("Contest");
	int seatIdCounter = 1;
	int candidateIdCounter = 1;
	while (currentContest) {
		Seat seat;
		auto seatIdentifier = currentContest->FirstChildElement("PollingDistrictIdentifier");
		seat.name = seatIdentifier->FindAttribute("Id")->Value();
		seat.id = seatIdCounter;
		++seatIdCounter;
		if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data
		seat.enrolment = currentContest->FirstChildElement("Enrolment")->IntText();
		auto currentCandidate = currentContest->FirstChildElement("FirstPreferences")->FirstChildElement("Candidate");
		while (currentCandidate) {
			Candidate candidate;
			auto candidateIdentifier = currentCandidate->FirstChildElement("CandidateIdentifier");
			candidate.name = candidateIdentifier->FindAttribute("Id")->Value();
			candidate.id = candidateIdCounter;
			++candidateIdCounter;
			auto affiliationEl = currentCandidate->FirstChildElement("Affiliation");
			auto affiliationIdentifier = affiliationEl->FirstChildElement("AffiliationIdentifier");
			auto affiliationIdEl = affiliationIdentifier->FindAttribute("Id");
			if (affiliationIdEl) {
				candidate.party = std::stoi(affiliationIdEl->Value());
				if (!parties.contains(candidate.party)) {
					parties[candidate.party] = Party();
					parties[candidate.party].id = candidate.party;
					parties[candidate.party].name =
						affiliationIdentifier->FirstChildElement("RegisteredName")->GetText();
					if (shortCodes.contains(parties[candidate.party].name)) {
						parties[candidate.party].shortCode = shortCodes.at(parties[candidate.party].name);
					}
				}
			}
			else {
				candidate.party = Candidate::Independent;
				parties[Candidate::Independent].id = Candidate::Independent;
				parties[Candidate::Independent].shortCode = "IND";
				parties[Candidate::Independent].name = "Independent";
			}
			candidates[candidate.id] = candidate;

			currentCandidate = currentCandidate->NextSiblingElement("Candidate");
		}

		auto currentBooth = currentContest->FirstChildElement("PollingPlaces")->FirstChildElement("PollingPlace");
		while (currentBooth) {
			Booth booth;
			booth.id = currentBooth->FirstChildElement("PollingPlaceIdentifier")->IntAttribute("Id");
			booth.id += seat.id * 100000; // create unique booth ID for booths with the same name in different seats
			booth.name = currentBooth->FirstChildElement("PollingPlaceIdentifier")->Attribute("Name");
			booth.parentSeat = seat.id;
			booths[booth.id] = booth;
			currentBooth = currentBooth->NextSiblingElement("PollingPlace");
			if (includeSeatBooths) {
				seats[seat.id].booths.push_back(booth.id);
			}
		}

		seats[seat.id] = seat;
		currentContest = currentContest->NextSiblingElement("Contest");
	}
	if (!results.is_null()) {
		// A lot of this is just a straight up copy of the Victorian Election
		// In summary, the purpose is to match past booth data to current booth data
		// The length of the code is primarily to deal with various types of ambiguities
		// that can result from booths with the same name under redistributions
		// (and can therefore take on vastly different character depening on the
		// boundary changes) so these are largely removed unless the booth
		// changed cleanly from one seat to another.
		std::map<std::string, int> seatNameToId;
		for (auto [seatId, seat] : seats) {
			if (seatNameToId.contains(seat.name)) {
				logger << "Warning: duplicate name for " << seat.name << "!\n";
			}
			else {
				seatNameToId[seat.name] = seatId;
			}
		}
		for (auto [candidateId, candidate] : candidates) {
			if (candidateNameToId.contains(candidate.name)) {
				logger << "Warning: duplicate name for " << candidate.name << "!\n";
			}
			else {
				candidateNameToId[candidate.name] = candidateId;
			}
		}
		std::set<std::string> seenBooths;
		std::set<std::string> ambiguousBooths;
		for (auto [boothId, booth] : booths) {
			if (seenBooths.contains(booth.name)) {
				ambiguousBooths.emplace(booth.name);
			}
			else {
				seenBooths.emplace(booth.name);
			}
		}
		std::map<std::string, int> boothNameToId;
		std::map<std::pair<std::string, std::string>, int> ambiguousBoothNameToId;
		for (auto [boothId, booth] : booths) {
			if (ambiguousBooths.contains(booth.name)) {
				ambiguousBoothNameToId[{booth.name, seats[booth.parentSeat].name}] = boothId;
			}
			else {
				boothNameToId[booth.name] = boothId;
			}
		}
		//PA_LOG_VAR(boothNameToId);
		//PA_LOG_VAR(ambiguousBoothNameToId);
		int dummyCandidateId = -100000; // Low numbers that will never be mistaken for an official id
		int dummyBoothId = -100000; // Low numbers that will never be mistaken for an official id
		// Not worth soft-coding this
		const std::map<std::string, int> partyIds = {
			{"LAB", 2},
			{"CDP", 4},
			{"LIB", 7},
			{"GRN", 16},
			{"CLP", 5},
			{"NAT", 8},
			{"NP", 8},
			{"SFF", 17},
			{"SA", 14},
			{"KSO", 979},
			{"SAP", 857},
			{"AJP", 376},
			{"LDP", 310},
			{"AC", 837},
			{"PHON", 937},
			{"SBP", 957},
			{"UP", -1},
			{"ORP", -1},
			{"NLT", -1},
			{"ACP", -1},
			{"FLUX", -1},
			{"IND", -1}
		};
		std::set<int> matchedIds;
		for (auto const& [seatName, seatValue] : results.items()) {
			int seatId = -1;
			if (seatNameToId.contains(seatName)) {
				seatId = seatNameToId[seatName];
			}
			std::map<int, int> indexToId;
			for (auto const& [candIndex, candValue] : seatValue["candidates"].items()) {
				int candIndexI = std::stoi(candIndex);
				auto candidateName = candValue["name"];
				auto party = candValue["party"];
				// *** json file only contains the surname, need to fix
				// (DoP file has the full name)
				if (candidateNameToId.contains(candidateName)) {
					indexToId[candIndexI] = candidateNameToId[candidateName];
				}
				else {
					indexToId[candIndexI] = dummyCandidateId;
					Candidate candidate;
					candidate.id = dummyCandidateId;
					candidate.name = candidateName;
					if (!partyIds.contains(party)) PA_LOG_VAR(seatName);
					if (!partyIds.contains(party)) PA_LOG_VAR(party);
					candidate.party = partyIds.at(party);
					candidates[candidate.id] = candidate;
					--dummyCandidateId;
				}
			}
			for (auto const& [boothName, boothValue] : seatValue["booths"].items()) {
				if (boothName.find("Votes") != std::string::npos) {
					VoteType voteType = VoteType::Invalid;
					if (boothName == "Postal Votes") voteType = VoteType::Postal;
					if (boothName == "Absent Votes") voteType = VoteType::Absent;
					if (boothName == "Provisional Votes") voteType = VoteType::Provisional;
					if (boothName == "iVote Votes") voteType = VoteType::IVote;
					if (voteType == VoteType::Invalid) continue;
					auto fps = boothValue["fp"];
					for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
						int fpCandIndexI = std::stoi(fpCandIndex);
						int fpCandId = indexToId[fpCandIndexI];
						seats[seatId].fpVotes[fpCandId][voteType] += fpVotes;
					}
					auto tcps = boothValue["tcp"];
					for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
						int tcpCandIndexI = std::stoi(tcpCandIndex);
						int tcpCandId = indexToId[tcpCandIndexI];
						int tcpAffiliation = candidates[tcpCandId].party;
						seats[seatId].tcpVotes[tcpAffiliation][voteType] += tcpVotes;
					}
					continue;
				}
				int boothId = dummyBoothId;
				if (boothNameToId.contains(boothName)) {
					boothId = boothNameToId[boothName];
				}
				else if (ambiguousBoothNameToId.contains({ boothName, seatName })) {
					boothId = ambiguousBoothNameToId[{ boothName, seatName }];
				}
				else {
					Booth booth;
					booth.name = boothName;
					booth.id = dummyBoothId;
					booths[booth.id] = booth;
					--dummyBoothId;
				}
				if (matchedIds.contains(boothId)) {
					// If two "old" booths match to one "new" booth then we don't know
					// which "old" booth to actually compare to
					// so ... make new booths that won't match to either
					if (booths.contains(boothId)) {
						auto boothData = booths[boothId];
						boothData.id = dummyBoothId;
						booths.erase(boothId);
						booths[dummyBoothId] = boothData;
						auto& parent = seats[booths[dummyBoothId].parentSeat];
						//PA_LOG_VAR(parent.name);
						//PA_LOG_VAR(parent.booths);
						//PA_LOG_VAR(boothId);
						//PA_LOG_VAR(dummyBoothId);
						auto foundBoothId = std::find(parent.booths.begin(), parent.booths.end(), boothId);
						if (foundBoothId != parent.booths.end()) *foundBoothId = dummyBoothId;
						//PA_LOG_VAR(parent.booths);
						//logger << "Detached old ambiguous booth: " << booths[dummyBoothId].name << "\n";
						--dummyBoothId;
					}
					boothId = dummyBoothId;
					//logger << "Detached ambiguous booth: " << boothName << "\n";
					--dummyBoothId;
				}
				matchedIds.insert(boothId);
				Booth& booth = booths[boothId];
				// the parent seat refers to the seat this booth is in for the new election,
				// but it should be the one it is in for this (old) election
				booth.parentSeat = seatId;
				booth.id = boothId;
				booth.name = boothName;
				auto fps = boothValue["fp"];
				for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
					int fpCandIndexI = std::stoi(fpCandIndex);
					int fpCandId = indexToId[fpCandIndexI];
					booth.fpVotes[fpCandId] = fpVotes;
					if (seatId > 0) seats[seatId].fpVotes[fpCandId][VoteType::Ordinary] += fpVotes;
				}
				auto tcps = boothValue["tcp"];
				for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
					int tcpCandIndexI = std::stoi(tcpCandIndex);
					int tcpCandId = indexToId[tcpCandIndexI];
					int tcpAffiliation = candidates[tcpCandId].party;
					booth.tcpVotes[tcpAffiliation] = tcpVotes;
					if (seatId > 0) seats[seatId].tcpVotes[tcpAffiliation][VoteType::Ordinary] += tcpVotes;
				}
				auto tpps = boothValue["tpp"];
				for (auto const& [tppCandIndex, tppVotes] : tpps.items()) {
					int tppCandIndexI = std::stoi(tppCandIndex);
					int tppCandId = indexToId[tppCandIndexI];
					int tppAffiliation = candidates[tppCandId].party;
					booth.tppVotes[tppAffiliation] = tppVotes;
					if (seatId > 0) seats[seatId].tppVotes[tppAffiliation][VoteType::Ordinary] += tppVotes;
				}
				if (seatId > 0) {
					seats[seatId].booths.push_back(booth.id);
				}
			}
		}

		// logger << "==SEATS==\n";
		// for (auto const& [seatId, seat] : seats) {
		// 	PA_LOG_VAR(seat.name);
		// 	PA_LOG_VAR(seat.id);
		// 	PA_LOG_VAR(seat.enrolment);
		// 	PA_LOG_VAR(seat.fpVotes);
		// 	PA_LOG_VAR(seat.tcpVotes);
		// 	PA_LOG_VAR(seat.tppVotes);
		// 	for (int boothId : seat.booths) {
		// 		PA_LOG_VAR(boothId);
		// 		auto const& booth = booths.at(boothId);
		// 		PA_LOG_VAR(booth.id);
		// 		PA_LOG_VAR(booth.name);
		// 		PA_LOG_VAR(booth.fpVotes);
		// 		PA_LOG_VAR(booth.tcpVotes);
		// 		PA_LOG_VAR(booth.type);
		// 	}
		// }
		// logger << "==CANDIDATES==\n";
		// for (auto const& candidate : candidates) {
		// 	PA_LOG_VAR(candidate.second.id);
		// 	PA_LOG_VAR(candidate.second.name);
		// 	if (candidate.second.party != -1) {
		// 		PA_LOG_VAR(parties[candidate.second.party].name);
		// 	}
		// 	else {
		// 		logger << "Independent\n";
		// 	}
		// }
		// logger << "==PARTIES==\n";
		// for (auto const& party : parties) {
		// 	PA_LOG_VAR(party.second.id);
		// 	PA_LOG_VAR(party.second.name);
		// 	PA_LOG_VAR(party.second.shortCode);
		// }
		logger << "Appeared to successfully load past election data\n";
	}
}

void Results2::Election::preloadQec([[maybe_unused]] nlohmann::json const& results, tinyxml2::XMLDocument const& zeros)
{
	const std::map<std::string, std::string> shortCodes = {
		{"Queensland Greens", "GRN"},
		{"Australian Labor Party (State of Queensland)", "ALP"},
		{"Liberal National Party of Queensland", "LNP"},
		{"Pauline Hanson's One Nation Queensland Division", "ON"},
		{"Katter's Australian Party (KAP)", "KAP"},
		{"Family First Queensland", "FF"},
		{"Independent", "IND"}
	};
	auto districts = zeros.FirstChildElement("ecq")
		->FirstChildElement("election")
		->FirstChildElement("districts");
	auto currentDistrict = districts->FirstChildElement("district");
	int candidateIdCounter = 1;
	int partyIdCounter = 1;
	std::map<std::string, int> partyNameToPartyId;
	partyNameToPartyId["Independent"] = -1;
	while (currentDistrict) {
		Seat seat;
		seat.name = currentDistrict->Attribute("districtName");
		seat.id = currentDistrict->IntAttribute("number");
		if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data
		seat.enrolment = currentDistrict->IntAttribute("enrolment");
		auto currentCandidate = currentDistrict->FirstChildElement("candidates")->FirstChildElement("candidate");
		while (currentCandidate) {
			Candidate candidate;
			candidate.name = currentCandidate->Attribute("ballotName");
			candidate.id = candidateIdCounter;
			++candidateIdCounter;
			auto partyName = currentCandidate->Attribute("party");
			if (!partyNameToPartyId.contains(partyName)) {
				partyNameToPartyId[partyName] = partyIdCounter;
				++partyIdCounter;
			}
			candidate.party = partyNameToPartyId[partyName];
			if (!parties.contains(candidate.party)) {
				parties[candidate.party] = Party();
				parties[candidate.party].id = candidate.party;
				parties[candidate.party].name = partyName;
				if (shortCodes.contains(parties[candidate.party].name)) {
					parties[candidate.party].shortCode = shortCodes.at(parties[candidate.party].name);
				}
			}
			candidates[candidate.id] = candidate;
			if (candidateNameToId.contains(candidate.name)) {
				logger << "WARNING: Identical candidate names found: " << candidate.name << "\n";
			}
			candidateNameToId[candidate.name] = candidate.id;

			currentCandidate = currentCandidate->NextSiblingElement("candidate");
		}

		auto currentBooth = currentDistrict->FirstChildElement("countRound")->FirstChildElement("booths")->FirstChildElement("booth");
		while (currentBooth) {
			Booth booth;
			booth.id = currentBooth->IntAttribute("id");
			booth.id += seat.id * 100000; // create unique booth ID for booths with the same name in different seats
			booth.name = currentBooth->Attribute("name");
			if (booth.name.find("Returning Officer") != std::string::npos ||
				booth.name.find("Early Voting Centre") != std::string::npos) {
				booth.type = Booth::Type::Ppvc;
			}
			booth.parentSeat = seat.id;
			booths[booth.id] = booth;
			currentBooth = currentBooth->NextSiblingElement("booth");
			if (results.is_null()) {
				seat.booths.push_back(booth.id);
			}
		}

		seats[seat.id] = seat;
		currentDistrict = currentDistrict->NextSiblingElement("district");
	}

	if (!results.is_null()) {
		// A lot of this is just a straight up copy of the Victorian Election
		// In summary, the purpose is to match past booth data to currents booth data
		// The length of the code is primarily to deal with various types of ambiguities
		// that can result from booths with the same name under redistributions
		// (and can therefore take on vastly different character depending on the
		// boundary changes) so these are largely removed unless the booth
		// changed cleanly from one seat to another.
		std::map<std::string, int> seatNameToId;
		for (auto [seatId, seat] : seats) {
			if (seatNameToId.contains(seat.name)) {
				logger << "Warning: duplicate name for " << seat.name << "!\n";
			}
			else {
				seatNameToId[seat.name] = seatId;
			}
		}
		std::set<std::string> seenBooths;
		std::set<std::string> ambiguousBooths;
		for (auto [boothId, booth] : booths) {
			if (seenBooths.contains(booth.name)) {
				ambiguousBooths.emplace(booth.name);
			}
			else {
				seenBooths.emplace(booth.name);
			}
		}
		std::map<std::string, int> boothNameToId;
		std::map<std::pair<std::string, std::string>, int> ambiguousBoothNameToId;
		for (auto [boothId, booth] : booths) {
			if (ambiguousBooths.contains(booth.name)) {
				ambiguousBoothNameToId[{booth.name, seats[booth.parentSeat].name}] = boothId;
			}
			else {
				boothNameToId[booth.name] = boothId;
			}
		}
		int dummyCandidateId = -100000; // Low numbers that will never be mistaken for an official id
		int dummyBoothId = -100000; // Low numbers that will never be mistaken for an official id
		//PA_LOG_VAR(boothNameToId);
		//PA_LOG_VAR(ambiguousBoothNameToId);
		std::set<int> matchedIds;
		for (auto const& [seatName, seatValue] : results.items()) {
			int seatId = -1;
			if (seatNameToId.contains(seatName)) {
				seatId = seatNameToId[seatName];
			}
			std::map<int, int> indexToId;
			for (auto const& [candIndex, candValue] : seatValue["candidates"].items()) {
				int candIndexI = std::stoi(candIndex);
				auto candidateName = candValue["name"];
				auto party = candValue["party"];
				if (candidateNameToId.contains(candidateName)) {
					indexToId[candIndexI] = candidateNameToId[candidateName];
				}
				else {
					indexToId[candIndexI] = dummyCandidateId;
					Candidate candidate;
					candidate.id = dummyCandidateId;
					candidate.name = candidateName;
					if (partyNameToPartyId.contains(party)) {
						candidate.party = partyNameToPartyId[party];
					}
					else {
						candidate.party = -1;
					}
					candidates[candidate.id] = candidate;
					--dummyCandidateId;
				}
			}
			for (auto const& [boothName, boothValue] : seatValue["booths"].items()) {
				VoteType voteType = VoteType::Ordinary;
				if (boothName == "Absent Early Voting") voteType = VoteType::Absent;
				if (boothName == "Absent Election Day") voteType = VoteType::Absent;
				if (boothName == "Postal Declaration Votes") voteType = VoteType::Postal;
				if (boothName == "In Person Declaration Votes") voteType = VoteType::Provisional;
				if (boothName == "Telephone Voting") voteType = VoteType::Telephone;
				if (boothName == "Telephone Voting - Early Voting") voteType = VoteType::Telephone;
				if (voteType != VoteType::Ordinary) {
					auto fps = boothValue["fp"];
					for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
						int fpCandIndexI = std::stoi(fpCandIndex);
						int fpCandId = indexToId[fpCandIndexI];
						seats[seatId].fpVotes[fpCandId][voteType] += fpVotes;
					}
					auto tcps = boothValue["tcp"];
					for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
						int tcpCandIndexI = std::stoi(tcpCandIndex);
						int tcpCandId = indexToId[tcpCandIndexI];
						int tcpAffiliation = candidates[tcpCandId].party;
						seats[seatId].tcpVotes[tcpAffiliation][voteType] += tcpVotes;
					}
					continue;
				}
				int boothId = dummyBoothId;
				if (boothNameToId.contains(boothName)) {
					boothId = boothNameToId[boothName];
				}
				else if (ambiguousBoothNameToId.contains({ boothName, seatName })) {
					boothId = ambiguousBoothNameToId[{ boothName, seatName }];
				}
				else {
					Booth booth;
					booth.name = boothName;
					booth.id = dummyBoothId;
					booths[booth.id] = booth;
					--dummyBoothId;
				}
				if (matchedIds.contains(boothId)) {
					// If two "old" booths match to one "new" booth then we don't know
					// which "old" booth to actually compare to
					// so ... make new booths that won't match to either
					if (booths.contains(boothId)) {
						auto boothData = booths[boothId];
						boothData.id = dummyBoothId;
						booths.erase(boothId);
						booths[dummyBoothId] = boothData;
						auto& parent = seats[booths[dummyBoothId].parentSeat];
						//PA_LOG_VAR(parent.name);
						//PA_LOG_VAR(parent.booths);
						//PA_LOG_VAR(boothId);
						//PA_LOG_VAR(dummyBoothId);
						auto foundBoothId = std::find(parent.booths.begin(), parent.booths.end(), boothId);
						if (foundBoothId != parent.booths.end()) *foundBoothId = dummyBoothId;
						//PA_LOG_VAR(parent.booths);
						logger << "Detached old ambiguous booth: " << booths[dummyBoothId].name << "\n";
						--dummyBoothId;
					}
					boothId = dummyBoothId;
					logger << "Detached ambiguous booth: " << boothName << "\n";
					--dummyBoothId;
				}
				matchedIds.insert(boothId);
				Booth& booth = booths[boothId];
				// the parent seat refers to the seat this booth is in for the new election,
				// but it should be the one it is in for this (old) election
				booth.parentSeat = seatId;
				booth.id = boothId;
				booth.name = boothName;
				auto fps = boothValue["fp"];
				for (auto const& [fpCandIndex, fpVotes] : fps.items()) {
					int fpCandIndexI = std::stoi(fpCandIndex);
					int fpCandId = indexToId[fpCandIndexI];
					booth.fpVotes[fpCandId] = fpVotes;
					if (seatId > 0) seats[seatId].fpVotes[fpCandId][VoteType::Ordinary] += fpVotes;
				}
				auto tcps = boothValue["tcp"];
				for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
					int tcpCandIndexI = std::stoi(tcpCandIndex);
					int tcpCandId = indexToId[tcpCandIndexI];
					int tcpAffiliation = candidates[tcpCandId].party;
					booth.tcpVotes[tcpAffiliation] = tcpVotes;
					if (seatId > 0) seats[seatId].tcpVotes[tcpAffiliation][VoteType::Ordinary] += tcpVotes;
				}
				if (seatId > 0) {
					seats[seatId].booths.push_back(booth.id);
				}
			}
		}
	}

	// logger << "Qec Preload\n";
	// PA_LOG_VAR(booths.size());
	// logger << "==SEATS==\n";
	// for (auto const& [seatId, seat] : seats) {
	// 	PA_LOG_VAR(seat.name);
	// 	PA_LOG_VAR(seat.id);
	// 	PA_LOG_VAR(seat.enrolment);
	// 	PA_LOG_VAR(seat.fpVotes);
	// 	PA_LOG_VAR(seat.tcpVotes);
	// 	PA_LOG_VAR(seat.tppVotes);
	// 	PA_LOG_VAR(seat.booths);
	// 	for (int boothId : seat.booths) {
	// 		PA_LOG_VAR(boothId);
	// 		auto const& booth = booths.at(boothId);
	// 		PA_LOG_VAR(booth.id);
	// 		PA_LOG_VAR(booth.name);
	// 		PA_LOG_VAR(booth.fpVotes);
	// 		PA_LOG_VAR(booth.tcpVotes);
	// 		PA_LOG_VAR(booth.type);
	// 	}
	// }
	// logger << "==CANDIDATES==\n";
	// for (auto const& candidate : candidates) {
	// 	PA_LOG_VAR(candidate.second.id);
	// 	PA_LOG_VAR(candidate.second.name);
	// 	if (candidate.second.party != -1) {
	// 		PA_LOG_VAR(parties[candidate.second.party].name);
	// 	}
	// 	else {
	// 		logger << "Independent\n";
	// 	}
	// }
	// logger << "==PARTIES==\n";
	// for (auto const& party : parties) {
	// 	PA_LOG_VAR(party.second.id);
	// 	PA_LOG_VAR(party.second.name);
	// 	PA_LOG_VAR(party.second.shortCode);
	// }
	logger << "Appeared to successfully load past election data\n";
}

void Results2::Election::preloadWaec(tinyxml2::XMLDocument const& candidatesXml, tinyxml2::XMLDocument const& boothsXml)
{
	auto currentRegion = boothsXml.FirstChildElement()->FirstChildElement("ElectionRegion");
	while (currentRegion) {

		auto currentDistrict = currentRegion->FirstChildElement("ElectionDistrict");
		while (currentDistrict) {

			Seat seat;
			seat.name = currentDistrict->Attribute("Name");
			seat.id = hashName(seat.name);
			if (seats.contains(seat.id)) seat = seats[seat.id];
			seat.enrolment = currentDistrict->IntAttribute("Enrolment");

			auto currentBooth = currentDistrict->FirstChildElement("OrdinaryPollingPlace");
			while (currentBooth) {
				Booth booth;
				booth.id = hashName(currentBooth->Attribute("VenueId"));
				if (booths.contains(booth.id)) booth = booths[booth.id];
				booth.name = currentBooth->Attribute("Name");
				booth.parentSeat = seat.id;
				booths[booth.id] = booth;
				seat.booths.push_back(booth.id);

				currentBooth = currentBooth->NextSiblingElement("OrdinaryPollingPlace");
			}

			seats[seat.id] = seat;
			currentDistrict = currentDistrict->NextSiblingElement("ElectionDistrict");
		}

		currentRegion = currentRegion->NextSiblingElement("ElectionRegion");
	}

	int candidateIdCounter = 1;
	int partyIdCounter = 1;
	std::map<std::string, int> partyShortCodeToPartyId;

	currentRegion = candidatesXml.FirstChildElement()->FirstChildElement("ElectionRegion");
	while (currentRegion) {
		auto currentDistrict = currentRegion->FirstChildElement("ElectionDistrict");
		while (currentDistrict) {
			auto seatId = hashName(currentDistrict->Attribute("Name"));
			Seat& seat = seats.at(seatId);

			auto currentCandidate = currentDistrict->FirstChildElement("LA")->FirstChildElement("Candidate");
			while (currentCandidate) {
				Candidate candidate;
				candidate.name = currentCandidate->Attribute("BallotPaperName");
				candidate.id = candidateIdCounter;
				++candidateIdCounter;
				auto partyShortCode = currentCandidate->Attribute("RegisteredPartyAbbreviation");
				if (!partyShortCodeToPartyId.contains(partyShortCode)) {
					partyShortCodeToPartyId[partyShortCode] = partyIdCounter;
					++partyIdCounter;
				}
				candidate.party = partyShortCodeToPartyId[partyShortCode];
				if (!parties.contains(candidate.party)) {
					parties[candidate.party] = Party();
					parties[candidate.party].id = candidate.party;
					parties[candidate.party].name = currentCandidate->Attribute("RegisteredPartyBallotPaperName");
					parties[candidate.party].shortCode = partyShortCode;
				}
				candidates[candidate.id] = candidate;
				if (candidateNameToId.contains(candidate.name)) {
					// If this triggers, we'll just need to add the seat name to the candidate name
					logger << "WARNING: Identical candidate names found: " << candidate.name << "\n";
				}
				candidateNameToId[candidate.name] = candidate.id;

				// Initialise the votes for this candidate in all booths in the seat
				for (auto const& boothId : seat.booths) {
					booths[boothId].fpVotes[candidate.id] = 0;
				}
			
				currentCandidate = currentCandidate->NextSiblingElement("Candidate");
			}

			currentDistrict = currentDistrict->NextSiblingElement("ElectionDistrict");
		}

		currentRegion = currentRegion->NextSiblingElement("ElectionRegion");
	}

	// logger << "Waec Preload\n";
	// PA_LOG_VAR(booths.size());
	// logger << "==SEATS==\n";
	// for (auto const& [seatId, seat] : seats) {
	// 	PA_LOG_VAR(seat.name);
	// 	PA_LOG_VAR(seat.id);
	// 	PA_LOG_VAR(seat.enrolment);
	// 	PA_LOG_VAR(seat.booths);
	// 	for (int boothId : seat.booths) {
	// 		PA_LOG_VAR(boothId);
	// 		auto const& booth = booths.at(boothId);
	// 		PA_LOG_VAR(booth.id);
	// 		PA_LOG_VAR(booth.name);
	// 		PA_LOG_VAR(booth.type);
	// 	}
	// }
	// logger << "==CANDIDATES==\n";
	// for (auto const& candidate : candidates) {
	// 	PA_LOG_VAR(candidate.second.id);
	// 	PA_LOG_VAR(candidate.second.name);
	// 	if (candidate.second.party != -1) {
	// 		PA_LOG_VAR(parties[candidate.second.party].name);
	// 	}
	// 	else {
	// 		logger << "Independent\n";
	// 	}
	// }
	// logger << "==PARTIES==\n";
	// for (auto const& party : parties) {
	// 	PA_LOG_VAR(party.second.id);
	// 	PA_LOG_VAR(party.second.name);
	// 	PA_LOG_VAR(party.second.shortCode);
	// }
	logger << "Appeared to successfully load past election data\n";
}

void Results2::Election::update(tinyxml2::XMLDocument const& xml, Format format)
{
	if (format == Format::QEC) {
		// QEC format is different enough to just have a separate procedure
		updateQec(xml);
		return;
	}
	else if (format == Format::WAEC) {
		updateWaec(xml);
		return;
	}
	PA_LOG_VAR(xml.FirstChildElement()->Name());
	auto resultsFinder = [&]() {
		switch (format) {
		case Format::AEC: return xml.FirstChildElement("MediaFeed")->FirstChildElement("Results");
		case Format::VEC: return xml.FirstChildElement("MediaFeed");
		case Format::NSWEC: return xml.FirstChildElement("MediaFeed");
		default: return xml.FirstChildElement("MediaFeed")->FirstChildElement("Results");
		}
	};
	auto results = resultsFinder();
	auto getElectionInfo = [&]() {
		switch (format) {
		case Format::NSWEC:
			{
				auto electionEl = results->FirstChildElement("EventIdentifier");
				id = std::stoi(std::string(electionEl->FindAttribute("Id")->Value()).substr(2));
				name = electionEl->FirstChildElement("EventName")->GetText();
				break;
			}
		default:
			{
				auto electionEl = results->FirstChildElement("eml:EventIdentifier");
				id = electionEl->FindAttribute("Id")->IntValue();
				name = electionEl->FirstChildElement("eml:EventName")->GetText();
			}
		}
	};
	getElectionInfo();
	auto contests = results->FirstChildElement("Election")->FirstChildElement("House")->FirstChildElement("Contests");
	auto currentContest = contests->FirstChildElement("Contest");

	// For elections (like NSW) where seats and candidates aren't given their own id numbers
	int seatIdCounter = 1;
	int candidateIdCounter = 1;
	std::string candidateIdElName = format == Format::NSWEC ? "CandidateIdentifier" : "eml:CandidateIdentifier";

	std::set<int> boothIdsPresent;

	while (currentContest) {
		Seat seat;
		auto contestIdFinder = [&]() {
			switch (format) {
			case Format::AEC: return currentContest->FirstChildElement("eml:ContestIdentifier");
			case Format::VEC: return currentContest->FirstChildElement("PollingDistrictIdentifier");
			case Format::NSWEC: return currentContest->FirstChildElement("PollingDistrictIdentifier");
			default: return currentContest->FirstChildElement("eml:ContestIdentifier");
			}
		};

		auto contestIdEl = contestIdFinder();
		if (format == Format::NSWEC) {
			seat.id = seatIdCounter;
			++seatIdCounter;
		}
		else {
			seat.id = contestIdEl->FindAttribute("Id")->IntValue();
		}
		if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data

		auto nameFinder = [&]() -> std::string {
			switch (format) {
			case Format::AEC: return contestIdEl->FirstChildElement("eml:ContestName")->GetText();
			case Format::VEC: {
				std::string prelimText = contestIdEl->FirstChildElement("Name")->GetText();
				return prelimText.substr(0, prelimText.length() - 9);
			}
			case Format::NSWEC: return contestIdEl->FindAttribute("Id")->Value();
			default: return contestIdEl->FirstChildElement("eml:ContestName")->GetText();
			}
		};

		seat.name = nameFinder();
		seat.enrolment = std::stoi(currentContest->FirstChildElement("Enrolment")->GetText());

		auto firstPrefs = currentContest->FirstChildElement("FirstPreferences");
		auto currentCandidate = firstPrefs->FirstChildElement("Candidate");
		while (currentCandidate) {
			Candidate candidate;
			auto candidateIdEl = currentCandidate->FirstChildElement(candidateIdElName.c_str());

			if (format == Format::NSWEC) {
				candidate.id = candidateIdCounter;
				std::string candidateName = candidateIdEl->FirstChildElement("CandidateName")->GetText();
				candidateNameToId[candidateName] = candidate.id;
				++candidateIdCounter;
			} else {
				candidate.id = candidateIdEl->FindAttribute("Id")->IntValue();
			}

			// Any candidate/party data should already be preloaded for NSWEC
			if (format != Format::NSWEC && candidateIdEl->FirstChildElement("eml:CandidateName")) {
				candidate.name = candidateIdEl->FirstChildElement("eml:CandidateName")->GetText();
				auto affiliationEl = currentCandidate->FirstChildElement("eml:AffiliationIdentifier");
				if (affiliationEl) {
					candidate.party = affiliationEl->FindAttribute("Id")->IntValue();
					if (!parties.contains(candidate.party)) {
						parties[candidate.party] = Party();
						parties[candidate.party].id = candidate.party;
						parties[candidate.party].name =
							currentCandidate->FirstChildElement("eml:AffiliationIdentifier")->FirstChildElement("eml:RegisteredName")->GetText();
						parties[candidate.party].shortCode = affiliationEl->FindAttribute("ShortCode")->Value();
					}
				}
				else candidate.party = Candidate::Independent;
				parties[Candidate::Independent].id = Candidate::Independent;
				parties[Candidate::Independent].shortCode = "IND";
				parties[Candidate::Independent].name = "Independent";
				candidates[candidate.id] = candidate;
			}
			auto votesByType = currentCandidate->FirstChildElement("VotesByType");
			auto fpVoteType = votesByType->FirstChildElement("Votes");
			while (fpVoteType) {
				std::string typeName = fpVoteType->FindAttribute("Type")->Value();
				int fpCount = fpVoteType->GetText() ? std::stoi(fpVoteType->GetText()) : 0;
				if (typeNameToVoteType.contains(typeName)) seat.fpVotes[candidate.id][typeNameToVoteType[typeName]] += fpCount;
				fpVoteType = fpVoteType->NextSiblingElement("Votes");
			}

			currentCandidate = currentCandidate->NextSiblingElement("Candidate");
		}

		auto candidateIdFinder = [&]() {
			switch (format) {
			case Format::NSWEC:
			{
				auto candidateIdEl = currentCandidate->FirstChildElement("CandidateIdentifier");
				return candidateNameToId[candidateIdEl->FindAttribute("Id")->Value()];
			}
			default:
			{
				auto candidateIdEl = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
				return candidateIdEl->FindAttribute("Id")->IntValue();
			}
			}
		};

		auto tcps = currentContest->FirstChildElement("TwoCandidatePreferred");
		if (tcps) {
			currentCandidate = tcps->FirstChildElement("Candidate");
			while (currentCandidate) {
				
				auto candidateId = candidateIdFinder();
				int partyId = candidates.at(candidateId).party;
				auto votesByType = currentCandidate->FirstChildElement("VotesByType");
				auto tcpVoteType = votesByType->FirstChildElement("Votes");
				while (tcpVoteType) {
					std::string typeName = tcpVoteType->FindAttribute("Type")->Value();
					int tcpCount = tcpVoteType->GetText() ? std::stoi(tcpVoteType->GetText()) : 0;
					// Note: for NSWEC the PP/PR categories are always zero - will need to extract them from the booth data
					if (typeNameToVoteType.contains(typeName)) seat.tcpVotes[partyId][typeNameToVoteType[typeName]] = tcpCount;
					if (typeNameToVoteType.contains(typeName)) seat.tcpVotesCandidate[candidateId][typeNameToVoteType[typeName]] = tcpCount;

					tcpVoteType = tcpVoteType->NextSiblingElement("Votes");
				}

				currentCandidate = currentCandidate->NextSiblingElement("Candidate");
			}
		}

		auto boothsEl = currentContest->FirstChildElement("PollingPlaces");
		auto currentBooth = boothsEl->FirstChildElement("PollingPlace");
		while (currentBooth) {
			auto boothIdEl = currentBooth->FirstChildElement("PollingPlaceIdentifier");
			Booth booth;
			booth.id = boothIdEl->FindAttribute("Id")->IntValue();
			boothIdsPresent.emplace(booth.id);
			if (format == Format::NSWEC) booth.id += seat.id * 100000; // create unique booth ID for booths with the same name in different seats
			if (booths.contains(booth.id)) booth = booths[booth.id]; // maintain already existing data
			if (boothIdEl->FindAttribute("Name")) {
				booth.parentSeat = seat.id;
				booth.name = boothIdEl->FindAttribute("Name")->Value();
				if (booth.name == "Absent" || booth.name == "Enrolment/Provisional" ||
					booth.name == "Postal" || booth.name == "iVote")
				{
					currentBooth = currentBooth->NextSiblingElement("PollingPlace");
					continue;
				}
				if (booth.name == "Declared Facility" || booth.name == "Sydney Town Hall") {
					booth.name += " (" + seat.name + ")";
				}

				auto classifierEl = boothIdEl->FindAttribute("Classification");
				if (classifierEl) {
					std::string classifier = classifierEl->Value();
					if (classifier == "PrePollVotingCentre") booth.type = Booth::Type::Ppvc;
					if (classifier == "PrisonMobile") booth.type = Booth::Type::Prison;
					if (classifier == "SpecialHospital") booth.type = Booth::Type::Hospital;
					if (classifier == "RemoteMobile") booth.type = Booth::Type::Remote;
					// These are technically PPVCs but they're very volatile and shouldn't be used as a proxy for normal PPVCs.
					if (booth.name.find("Divisional Office") != std::string::npos) booth.type = Booth::Type::Other;
					if (booth.name.find("BLV") != std::string::npos) booth.type = Booth::Type::Other;
				}

				// Don't add the same booth multiple times
				if (std::find(seat.booths.begin(), seat.booths.end(), booth.id) == seat.booths.end()) {
					seat.booths.push_back(booth.id);
				}
			}

			auto fps = currentBooth->FirstChildElement("FirstPreferences");
			currentCandidate = fps->FirstChildElement("Candidate");
			while (currentCandidate) {
				int candidateId = candidateIdFinder();
				int votes = currentCandidate->FirstChildElement("Votes")->IntText();
				booth.fpVotes[candidateId] = votes;

				currentCandidate = currentCandidate->NextSiblingElement("Candidate");
			}

			tcps = currentBooth->FirstChildElement("TwoCandidatePreferred");
			if (tcps) {
				currentCandidate = tcps->FirstChildElement("Candidate");
				while (currentCandidate) {
					int candidateId = candidateIdFinder();
					int partyId = candidates.at(candidateId).party;
					int votes = currentCandidate->FirstChildElement("Votes")->IntText();
					booth.tcpVotes[partyId] = votes;
					booth.tcpVotesCandidate[candidateId] = votes;

					currentCandidate = currentCandidate->NextSiblingElement("Candidate");
				}
			}

			booths[booth.id] = booth;

			currentBooth = currentBooth->NextSiblingElement("PollingPlace");
		}

		seats[seat.id] = seat;

		currentContest = currentContest->NextSiblingElement("Contest");
	}

	for (auto& [seatId, seat] : seats) {
		std::vector<int> boothsToErase;
		for (int boothId : seat.booths) {
			if (!boothIdsPresent.contains(boothId)) {
				auto const& booth = booths.at(boothId);
				logger << "Removing booth " << booth.name << " (" << booth.id << ") from " << seat.name << " as it is not present.\n";
				boothsToErase.push_back(boothId);
			}
		}
		for (int boothToErase : boothsToErase) {
			seat.booths.erase(std::remove(seat.booths.begin(), seat.booths.end(), boothToErase), seat.booths.end());
		}
	}

	applyResultOverrides();

	// logger << "==SEATS==\n";
	//   for (auto const& [seatId, seat] : seats) {
	// 	PA_LOG_VAR(seat.name);
	// 	PA_LOG_VAR(seat.id);
	// 	PA_LOG_VAR(seat.enrolment);
	// 	PA_LOG_VAR(seat.fpVotes);
	// 	PA_LOG_VAR(seat.tcpVotes);
	// 	PA_LOG_VAR(seat.tppVotes);
	// 	PA_LOG_VAR(seat.booths);
	//  	for (int boothId : seat.booths) {
	//  		auto const& booth = booths.at(boothId);
	//  		PA_LOG_VAR(booth.id);
	//  		PA_LOG_VAR(booth.name);
	// 		PA_LOG_VAR(booth.fpVotes);
	// 		PA_LOG_VAR(booth.tcpVotes);
	// 		PA_LOG_VAR(booth.tcpVotesCandidate);
	// 		PA_LOG_VAR(booth.tppVotes);
	// 		PA_LOG_VAR(booth.type);
	// 	}
	// }
	// logger << "==CANDIDATES==\n";
	// for (auto const& candidate : candidates) {
	// 	PA_LOG_VAR(candidate.second.id);
	// 	PA_LOG_VAR(candidate.second.name);
	// 	if (candidate.second.party != -1) {
	// 		PA_LOG_VAR(parties[candidate.second.party].name);
	// 	}
	// 	else {
	// 		logger << "Independent\n";
	// 	}
	// }
	// logger << "==PARTIES==\n";
	// for (auto const& party : parties) {
	// 	PA_LOG_VAR(party.second.id);
	// 	PA_LOG_VAR(party.second.name);
	// 	PA_LOG_VAR(party.second.shortCode);
	// }
	// logger << "==COALITIONS==\n";
	// for (auto const& coalition : coalitions) {
	// 	PA_LOG_VAR(coalition.second.id);
	// 	PA_LOG_VAR(coalition.second.name);
	// 	PA_LOG_VAR(coalition.second.shortCode);
	// }
	// logger << "==ELECTION==\n";
	// PA_LOG_VAR(name);
	// PA_LOG_VAR(id);
}

void Results2::Election::updateQec(tinyxml2::XMLDocument const& xml)
{
	// This is a pure update, assumes you've already used the preload
	const std::map<std::string, std::string> shortCodes = {
		{"Queensland Greens", "GRN"},
		{"Australian Labor Party (State of Queensland)", "ALP"},
		{"The Liberal Party of Australia, New South Wales Division", "LNP"},
		{"Pauline Hanson's One Nation Queensland Division", "ON"},
		{"Katter's Australian Party (KAP)", "KAP"},
		{"Family First Queensland", "FF"},
		{"Independent", "IND"}
	};

	id = xml.FirstChildElement("ecq")->FirstChildElement("election")->IntAttribute("id");
	name = xml.FirstChildElement("ecq")->FirstChildElement("election")->IntAttribute("electionName");
	auto districts = xml.FirstChildElement("ecq")
		->FirstChildElement("election")
		->FirstChildElement("districts");
	auto currentDistrict = districts->FirstChildElement("district");
	std::map<std::string, int> partyNameToPartyId;
	while (currentDistrict) {
		Seat seat;
		seat.id = currentDistrict->IntAttribute("number");
		if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data
		seat.enrolment = currentDistrict->IntAttribute("enrolment");

		// For now it's assumed that the first countRound has the fp votes
		auto fps = currentDistrict->FirstChildElement("countRound");

		auto getBoothVoteType = [](std::string boothName) {
			if (boothName == "Mobile Polling") return VoteType::PrePoll;
			if (boothName == "Telephone Voting") return VoteType::Telephone;
			if (boothName == "Telephone Voting - Early Voting") return VoteType::Telephone;
			if (boothName == "Postal Declaration Votes") return VoteType::Postal;
			if (boothName == "In Person Declaration Votes") return VoteType::Provisional;
			if (boothName == "Absent Election Day") return VoteType::Absent;
			if (boothName == "Absent Early Voting") return VoteType::PrePoll;
			return VoteType::Ordinary;
		};

		// Extract booth fp results
		auto fpBooths = fps->FirstChildElement("booths");
		if (fpBooths && std::string(fps->Attribute("countName")) == "Unofficial Preliminary Count") {
			auto currentFpBooth = fpBooths->FirstChildElement("booth");
			for (; currentFpBooth; currentFpBooth = currentFpBooth->NextSiblingElement("booth")) {
				if (!currentFpBooth->FirstChildElement("primaryVoteResults")) continue;
				auto boothId = currentFpBooth->IntAttribute("id") + seat.id * 100000;
				if (!booths.contains(boothId)) continue; // ignore booths not in preload
				Booth& booth = booths[boothId];
				auto currentCandidate = currentFpBooth->FirstChildElement("primaryVoteResults")->FirstChildElement("candidate");
				for (; currentCandidate; currentCandidate = currentCandidate->NextSiblingElement("candidate")) {
					auto candidateName = currentCandidate->Attribute("ballotName");
					if (!candidateNameToId.contains(candidateName)) continue;
					auto candidateId = candidateNameToId[candidateName];
					auto votes = currentCandidate->FirstChildElement("count")->IntText();
					booth.fpVotes[candidateId] = votes;
					seat.fpVotes[candidateId][getBoothVoteType(booth.name)] += votes;
				}
			}
		}

		// For now it's assumed that the second countRound has the fp votes
		auto tcps = fps->NextSiblingElement("countRound");

		// Extract booth tcp results
		auto tcpBooths = tcps->FirstChildElement("booths");
		if (tcpBooths && std::string(tcps->Attribute("countName")) == "Unofficial Indicative Count") {
			auto currentTcpBooth = tcpBooths->FirstChildElement("booth");
			for (; currentTcpBooth; currentTcpBooth = currentTcpBooth->NextSiblingElement("booth")) {
				if (!currentTcpBooth->FirstChildElement("twoCandidateVotes")) continue;
				auto boothId = currentTcpBooth->IntAttribute("id") + seat.id * 100000;;
				if (!booths.contains(boothId)) continue; // ignore booths not in preload
				Booth& booth = booths[boothId];
				auto currentCandidate = currentTcpBooth->FirstChildElement("twoCandidateVotes")->FirstChildElement("candidate");
				for (; currentCandidate; currentCandidate = currentCandidate->NextSiblingElement("candidate")) {
					auto candidateName = currentCandidate->Attribute("ballotName");
					if (!candidateNameToId.contains(candidateName)) continue;
					auto candidateId = candidateNameToId[candidateName];
					int partyId = candidates.at(candidateId).party;
					auto votes = currentCandidate->FirstChildElement("count")->IntText();
					booth.tcpVotes[partyId] = votes;
					seat.tcpVotes[partyId][getBoothVoteType(booth.name)] += votes;
				}
			}
		}

		seats[seat.id] = seat;
		currentDistrict = currentDistrict->NextSiblingElement("district");
	}

	// logger << "Qec Update\n";
	// PA_LOG_VAR(booths.size());
	// logger << "==SEATS==\n";
	// for (auto const& [seatId, seat] : seats) {
	// 	PA_LOG_VAR(seat.name);
	// 	PA_LOG_VAR(seat.id);
	// 	PA_LOG_VAR(seat.enrolment);
	// 	PA_LOG_VAR(seat.fpVotes);
	// 	PA_LOG_VAR(seat.tcpVotes);
	// 	PA_LOG_VAR(seat.tppVotes);
	// 	PA_LOG_VAR(seat.booths);
	// 	for (int boothId : seat.booths) {
	// 		PA_LOG_VAR(boothId);
	// 		auto const& booth = booths.at(boothId);
	// 		PA_LOG_VAR(booth.id);
	// 		PA_LOG_VAR(booth.name);
	// 		PA_LOG_VAR(booth.fpVotes);
	// 		PA_LOG_VAR(booth.tcpVotes);
	// 		PA_LOG_VAR(booth.type);
	// 	}
	// }
	// logger << "==CANDIDATES==\n";
	// for (auto const& candidate : candidates) {
	// 	PA_LOG_VAR(candidate.second.id);
	// 	PA_LOG_VAR(candidate.second.name);
	// 	if (candidate.second.party != -1) {
	// 		PA_LOG_VAR(parties[candidate.second.party].name);
	// 	}
	// 	else {
	// 		logger << "Independent\n";
	// 	}
	// }
	// logger << "==PARTIES==\n";
	// for (auto const& party : parties) {
	// 	PA_LOG_VAR(party.second.id);
	// 	PA_LOG_VAR(party.second.name);
	// 	PA_LOG_VAR(party.second.shortCode);
	// }
	logger << "Appeared to successfully load election data update\n";
}

void Results2::Election::updateWaec(tinyxml2::XMLDocument const& xml)
{
	// This is a pure update, assumes you've already used the preload

	auto VoteTypeCategory = [](std::string boothName) {
		if (boothName == "Special Institutions,  Hospitals & Remotes") return VoteType::SIR;
		if (boothName == "Absent Votes") return VoteType::Absent;
		if (boothName == "Early Votes (by Post)") return VoteType::Postal;
		if (boothName == "Early Votes (In Person)") return VoteType::PrePoll;
		if (boothName == "Provisional Votes") return VoteType::Provisional;
		return VoteType::Invalid;
	};

	auto currentRegion = xml.FirstChildElement()->FirstChildElement("ElectionRegion");
	while (currentRegion) {
		auto currentDistrict = currentRegion->FirstChildElement("ElectionDistrict");
		while (currentDistrict) {
			int seatId = hashName(currentDistrict->Attribute("Name"));
			Seat& seat = seats.at(seatId);

			auto districtVotes = currentDistrict->FirstChildElement("LA")->FirstChildElement("DistrictVotes");

			// We need to know all the candidate ids for this seat to establish zeros for other vote types
			std::vector<int> candidateIds;
			auto currentCandidate = districtVotes->FirstChildElement("CandidateVotes");
			while (currentCandidate) {
				auto candidateName = currentCandidate->Attribute("CandidateBallotPaperName");
				if (!candidateNameToId.contains(candidateName)) {
					currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
					continue;
				}
				candidateIds.push_back(candidateNameToId[candidateName]);
				currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
			}

			auto currentBooth = districtVotes->FirstChildElement("OrdinaryPollingPlaceVotes");
			while (currentBooth) {

				int boothId = currentBooth->IntAttribute("VenueId");
				if (booths.contains(boothId)) {
					Booth& booth = booths[boothId];
					currentCandidate = currentBooth->FirstChildElement("CandidateVotes");
					while (currentCandidate) {
						auto candidateName = currentCandidate->Attribute("CandidateBallotPaperName");
						if (!candidateNameToId.contains(candidateName)) {
							currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
							continue;
						}
						auto candidateId = candidateNameToId[candidateName];
						auto votes = currentCandidate->IntAttribute("Votes");
						booth.fpVotes[candidateId] = votes;
						currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
					}
				}

				currentBooth = currentBooth->NextSiblingElement("OrdinaryPollingPlaceVotes");
			}

			auto currentCategory = districtVotes->FirstChildElement("CategoryVotes");
			while (currentCategory) {
				auto categoryName = currentCategory->Attribute("CategoryName");
				auto categoryType = VoteTypeCategory(categoryName);
				if (categoryType == VoteType::Invalid) {
					currentCategory = currentCategory->NextSiblingElement("CategoryVotes");
					continue;
				}
				currentCandidate = currentCategory->FirstChildElement("CandidateVotes");
				while (currentCandidate) {
					auto candidateName = currentCandidate->Attribute("CandidateBallotPaperName");
					if (!candidateNameToId.contains(candidateName)) {
						currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
						continue;
					}
					auto candidateId = candidateNameToId[candidateName];
					auto votes = currentCandidate->IntAttribute("Votes");
					seat.fpVotes[candidateId][categoryType] = votes;
					currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
				}
				currentCategory = currentCategory->NextSiblingElement("CategoryVotes");
			}
			districtVotes = districtVotes->NextSiblingElement("DistrictVotes");

			if (districtVotes) {

				currentBooth = districtVotes->FirstChildElement("OrdinaryPollingPlaceVotes");
				while (currentBooth) {

					int boothId = currentBooth->IntAttribute("VenueId");
					if (booths.contains(boothId)) {
						Booth& booth = booths[boothId];
						currentCandidate = currentBooth->FirstChildElement("CandidateVotes");
						while (currentCandidate) {
							auto candidateName = currentCandidate->Attribute("CandidateBallotPaperName");
							if (!candidateNameToId.contains(candidateName)) {
								currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
								continue;
							}
							auto candidateId = candidateNameToId[candidateName];
							auto votes = currentCandidate->IntAttribute("Votes");
							booth.tcpVotesCandidate[candidateId] = votes;
							auto partyId = candidates.at(candidateId).party;
							booth.tcpVotes[partyId] = votes;
							currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
						}
					}
					currentBooth = currentBooth->NextSiblingElement("OrdinaryPollingPlaceVotes");
				}

				currentCategory = districtVotes->FirstChildElement("CategoryVotes");
				while (currentCategory) {
					auto categoryName = currentCategory->Attribute("CategoryName");
					auto categoryType = VoteTypeCategory(categoryName);
					if (categoryType == VoteType::Invalid) {
						currentCategory = currentCategory->NextSiblingElement("CategoryVotes");
						continue;
					}
					currentCandidate = currentCategory->FirstChildElement("CandidateVotes");
					while (currentCandidate) {
						auto candidateName = currentCandidate->Attribute("CandidateBallotPaperName");
						if (!candidateNameToId.contains(candidateName)) {
							currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
							continue;
						}
						auto candidateId = candidateNameToId[candidateName];
						auto votes = currentCandidate->IntAttribute("Votes");
						seat.tcpVotesCandidate[candidateId][categoryType] = votes;
						auto partyId = candidates.at(candidateId).party;
						seat.tcpVotes[partyId][categoryType] = votes;
						currentCandidate = currentCandidate->NextSiblingElement("CandidateVotes");
					}
					currentCategory = currentCategory->NextSiblingElement("CategoryVotes");
				}
			}

			std::vector expectedVoteTypes = {
				VoteType::SIR, 
				VoteType::Absent,
				VoteType::Postal,
				VoteType::PrePoll,
				VoteType::Provisional
			};
			for (auto voteType : expectedVoteTypes) {
				for (auto candidateId : candidateIds) {
					if (seat.fpVotes.find(candidateId) == seat.fpVotes.end()) {
						seat.fpVotes[candidateId] = {};
					}
					if (seat.tcpVotes.find(candidateId) == seat.tcpVotes.end()) {
						seat.tcpVotes[candidateId] = {};
					}
					if (seat.tcpVotesCandidate.find(candidateId) == seat.tcpVotesCandidate.end()) {
						seat.tcpVotesCandidate[candidateId] = {};
					}
					if (seat.fpVotes[candidateId].find(voteType) == seat.fpVotes[candidateId].end()) {
						seat.fpVotes[candidateId][voteType] = 0;
					}
					if (seat.tcpVotes[candidateId].find(voteType) == seat.tcpVotes[candidateId].end()) {
						seat.tcpVotes[candidateId][voteType] = 0;
					}
					if (seat.tcpVotesCandidate[candidateId].find(voteType) == seat.tcpVotesCandidate[candidateId].end()) {
						seat.tcpVotesCandidate[candidateId][voteType] = 0;
					}
				}
			}
			currentDistrict = currentDistrict->NextSiblingElement("ElectionDistrict");
		}

		currentRegion = currentRegion->NextSiblingElement("ElectionRegion");
	}

	// logger << "Waec Update\n";
	// PA_LOG_VAR(booths.size());
	// logger << "==SEATS==\n";
	// for (auto const& [seatId, seat] : seats) {
	// 	PA_LOG_VAR(seat.name);
	// 	PA_LOG_VAR(seat.id);
	// 	PA_LOG_VAR(seat.enrolment);
	// 	PA_LOG_VAR(seat.fpVotes);
	// 	PA_LOG_VAR(seat.tcpVotes);
	// 	PA_LOG_VAR(seat.tcpVotesCandidate);
	// 	PA_LOG_VAR(seat.tppVotes);
	// 	PA_LOG_VAR(seat.booths);
	// 	for (int boothId : seat.booths) {
	// 		PA_LOG_VAR(boothId);
	// 		auto const& booth = booths.at(boothId);
	// 		PA_LOG_VAR(booth.id);
	// 		PA_LOG_VAR(booth.name);
	// 		PA_LOG_VAR(booth.fpVotes);
	// 		PA_LOG_VAR(booth.tcpVotes);
	// 		PA_LOG_VAR(booth.type);
	// 	}
	// }
	// logger << "==CANDIDATES==\n";
	// for (auto const& candidate : candidates) {
	// 	PA_LOG_VAR(candidate.second.id);
	// 	PA_LOG_VAR(candidate.second.name);
	// 	if (candidate.second.party != -1) {
	// 		PA_LOG_VAR(parties[candidate.second.party].name);
	// 	}
	// 	else {
	// 		logger << "Independent\n";
	// 	}
	// }
	// logger << "==PARTIES==\n";
	// for (auto const& party : parties) {
	// 	PA_LOG_VAR(party.second.id);
	// 	PA_LOG_VAR(party.second.name);
	// 	PA_LOG_VAR(party.second.shortCode);
	// }
	logger << "Appeared to successfully load election data update\n";
}

void Results2::Election::applyResultOverrides() {
	std::string fileName = "analysis/Live Overrides/" + termCode + ".csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		logger << "Info: Could not find file " + fileName + " - original results will be used\n";
		return;
	}
	do {
		std::string line;
		std::getline(file, line);
		if (!file) break;
		auto values = splitString(line, ",");
		if (values.size() <= 1) break;
		if (values[0] == "tcp") {
      if (values.size() < 7) {
        logger << "Warning: Invalid line in " + fileName + ": " + line + "\n";
        continue;
      }
      std::string seatName = values[1];
      std::string boothName = values[2];
      std::string partyOneCode = values[3];
      std::string partyTwoCode = values[4];
      int partyOneVotes = std::stoi(values[5]);
      int partyTwoVotes = std::stoi(values[6]);
      auto booth = std::find_if(booths.begin(), booths.end(), [this, &seatName, &boothName](decltype(booths)::value_type const& b) {
        return b.second.name == boothName && seats.at(b.second.parentSeat).name == seatName;
      });
      if (booth == booths.end()) {
        logger << "Warning: Could not find booth " + boothName + " in seat " + seatName + " in " + fileName + "\n";

				std::vector<std::string> boothNames;
				for (auto const& b : booths) {
					if (seats.at(b.second.parentSeat).name == seatName) {
						boothNames.push_back(b.second.name);
					}
				}
        continue;	
      }
			auto candidateOne = std::find_if(booth->second.fpVotes.begin(), booth->second.fpVotes.end(),
				[this, &partyOneCode](decltype(booth->second.fpVotes)::value_type const& c) {
					return parties.at(candidates.at(c.first).party).shortCode == partyOneCode;
				}
			);
			auto candidateTwo = std::find_if(booth->second.fpVotes.begin(), booth->second.fpVotes.end(),
				[this, &partyTwoCode](decltype(booth->second.fpVotes)::value_type const& c) {
					return parties.at(candidates.at(c.first).party).shortCode == partyTwoCode;
				}
			);
			if (candidateOne == booth->second.fpVotes.end() || candidateTwo == booth->second.fpVotes.end()) {
				logger << "Warning: Could not find candidates for " + partyOneCode + " or " + partyTwoCode + " in " + fileName + "\n";
				continue;
			}
			booths.at(booth->first).tcpVotes[candidates.at(candidateOne->first).party] = partyOneVotes;
      booths.at(booth->first).tcpVotes[candidates.at(candidateTwo->first).party] = partyTwoVotes;
			booths.at(booth->first).tcpVotesCandidate[candidateOne->first] = partyOneVotes;
      booths.at(booth->first).tcpVotesCandidate[candidateTwo->first] = partyTwoVotes;
      logger << "Applied override for " + seatName + " " + boothName + " - " + partyOneCode + " "  + std::to_string(partyOneVotes) + " " + partyTwoCode + " " + std::to_string(partyTwoVotes) + "\n";
    }
	} while (true);
}