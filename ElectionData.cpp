#include "ElectionData.h"

#include "General.h"
#include "Log.h"

#include <set>

Results2::Election::Election(tinyxml2::XMLDocument const& xml)
{
	update(xml);
}

Results2::Election::Election(nlohmann::json const& results, tinyxml2::XMLDocument const& input_candidates, tinyxml2::XMLDocument const& input_booths)
{
	update2022VicPrev(results, input_candidates, input_booths);
}

Results2::Election::Election(tinyxml2::XMLDocument const& input_candidates, tinyxml2::XMLDocument const& input_booths)
{
	preload2022Vic(input_candidates, input_booths, true);
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
				auto affiliationIndentifier = affiliationEl->FirstChildElement("AffiliationIdentifier");
				candidate.party = affiliationIndentifier->FindAttribute("Id")->IntValue();
				if (!parties.contains(candidate.party)) {
					parties[candidate.party] = Party();
					parties[candidate.party].id = candidate.party;
					parties[candidate.party].name =
						affiliationIndentifier->FirstChildElement("RegisteredName")->GetText();
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
	std::map<std::string, int> candidateNameToId;
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
					seats[seatId].fpVotes[fpCandId][voteType] = fpVotes;
				}
				auto tcps = boothValue["tcp"];
				for (auto const& [tcpCandIndex, tcpVotes] : tcps.items()) {
					int tcpCandIndexI = std::stoi(tcpCandIndex);
					int tcpCandId = indexToId[tcpCandIndexI];
					int tcpAffiliation = candidates[tcpCandId].party;
					seats[seatId].tcpVotes[tcpAffiliation][voteType] = tcpVotes;
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

void Results2::Election::update(tinyxml2::XMLDocument const& xml, Format format)
{
	PA_LOG_VAR(xml.FirstChildElement()->Name());
	auto resultsFinder = [&]() {
		switch (format) {
		case Format::AEC: return xml.FirstChildElement("MediaFeed")->FirstChildElement("Results");
		case Format::VEC: return xml.FirstChildElement("MediaFeed");
		default: return xml.FirstChildElement("MediaFeed")->FirstChildElement("Results");
		}
	};
	auto results = resultsFinder();
	auto electionEl = results->FirstChildElement("eml:EventIdentifier");
	id = electionEl->FindAttribute("Id")->IntValue();
	name = electionEl->FirstChildElement("eml:EventName")->GetText();
	auto contests = results->FirstChildElement("Election")->FirstChildElement("House")->FirstChildElement("Contests");
	auto currentContest = contests->FirstChildElement("Contest");
	while (currentContest) {
		auto contestIdFinder = [&]() {
			switch (format) {
			case Format::AEC: return currentContest->FirstChildElement("eml:ContestIdentifier");
			case Format::VEC: return currentContest->FirstChildElement("PollingDistrictIdentifier");
			default: return currentContest->FirstChildElement("eml:ContestIdentifier");
			}
		};
		auto contestIdEl = contestIdFinder();
		Seat seat;
		seat.id = contestIdEl->FindAttribute("Id")->IntValue();
		if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data
		auto nameFinder = [&]() {
			switch (format) {
			case Format::AEC: return contestIdEl->FirstChildElement("eml:ContestName");
			case Format::VEC: return contestIdEl->FirstChildElement("Name");
			default: return contestIdEl->FirstChildElement("eml:ContestName");
			}
		};
		seat.name = nameFinder()->GetText();
		if (format == Format::VEC) seat.name = seat.name.substr(0, seat.name.length() - 9);
		seat.enrolment = std::stoi(currentContest->FirstChildElement("Enrolment")->GetText());

		auto firstPrefs = currentContest->FirstChildElement("FirstPreferences");
		auto currentCandidate = firstPrefs->FirstChildElement("Candidate");
		while (currentCandidate) {
			Candidate candidate;
			auto candidateIdEl = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
			candidate.id = candidateIdEl->FindAttribute("Id")->IntValue();
			if (candidateIdEl->FirstChildElement("eml:CandidateName")) {
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
				int fpCount = std::stoi(fpVoteType->GetText());
				if (typeName == "Ordinary") seat.fpVotes[candidate.id][VoteType::Ordinary] = fpCount;
				else if (typeName == "Absent") seat.fpVotes[candidate.id][VoteType::Absent] = fpCount;
				else if (typeName == "Provisional") seat.fpVotes[candidate.id][VoteType::Provisional] = fpCount;
				else if (typeName == "PrePoll") seat.fpVotes[candidate.id][VoteType::PrePoll] = fpCount;
				else if (typeName == "Postal") seat.fpVotes[candidate.id][VoteType::Postal] = fpCount;
				else if (typeName == "Early") seat.fpVotes[candidate.id][VoteType::Early] = fpCount;
				fpVoteType = fpVoteType->NextSiblingElement("Votes");
			}

			currentCandidate = currentCandidate->NextSiblingElement("Candidate");
		}

		auto tcps = currentContest->FirstChildElement("TwoCandidatePreferred");
		if (tcps) {
			currentCandidate = tcps->FirstChildElement("Candidate");
			while (currentCandidate) {
				auto candidateIdEl = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
				int candidateId = -1;
				candidateId = candidateIdEl->FindAttribute("Id")->IntValue();
				int partyId = candidates.at(candidateId).party;
				auto votesByType = currentCandidate->FirstChildElement("VotesByType");
				auto tcpVoteType = votesByType->FirstChildElement("Votes");
				while (tcpVoteType) {
					std::string typeName = tcpVoteType->FindAttribute("Type")->Value();
					int tcpCount = std::stoi(tcpVoteType->GetText());
					if (typeName == "Ordinary") seat.tcpVotes[partyId][VoteType::Ordinary] = tcpCount;
					else if (typeName == "Absent") seat.tcpVotes[partyId][VoteType::Absent] = tcpCount;
					else if (typeName == "Provisional") seat.tcpVotes[partyId][VoteType::Provisional] = tcpCount;
					else if (typeName == "PrePoll") seat.tcpVotes[partyId][VoteType::PrePoll] = tcpCount;
					else if (typeName == "Postal") seat.tcpVotes[partyId][VoteType::Postal] = tcpCount;
					else if (typeName == "Early") seat.tcpVotes[partyId][VoteType::Early] = tcpCount;
					tcpVoteType = tcpVoteType->NextSiblingElement("Votes");
				}

				currentCandidate = currentCandidate->NextSiblingElement("Candidate");
			}
		}

		auto tpps = currentContest->FirstChildElement("TwoPartyPreferred");
		if (tpps) {
			auto currentCoalition = tpps->FirstChildElement("Coalition");
			while (currentCoalition) {
				auto coalitionIdEl = currentCoalition->FirstChildElement("CoalitionIdentifier");
				if (!coalitionIdEl) break;
				Coalition coalition;
				int coalitionId = coalitionIdEl->FindAttribute("Id")->IntValue();
				if (!coalitions.contains(coalitionId)) {
					coalitions[coalitionId].id = coalitionId;
					coalitions[coalitionId].shortCode = coalitionIdEl->FindAttribute("ShortCode")->Value();
					coalitions[coalitionId].name =
						coalitionIdEl->FirstChildElement("CoalitionName")->GetText();
				}
				int tppVotes = currentCoalition->FirstChildElement("Votes")->IntText();
				seat.tppVotes[coalitionId] = tppVotes;

				currentCoalition = currentCoalition->NextSiblingElement("Coalition");
			}
		}

		auto boothsEl = currentContest->FirstChildElement("PollingPlaces");
		auto currentBooth = boothsEl->FirstChildElement("PollingPlace");
		while (currentBooth) {
			auto boothIdEl = currentBooth->FirstChildElement("PollingPlaceIdentifier");
			Booth booth;
			booth.id = boothIdEl->FindAttribute("Id")->IntValue();
			if (booths.contains(booth.id)) booth = booths[booth.id]; // maintain already existing data
			if (boothIdEl->FindAttribute("Name")) {
				booth.parentSeat = seat.id;
				booth.name = boothIdEl->FindAttribute("Name")->Value();
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
				seat.booths.push_back(booth.id);
			}

			auto fps = currentBooth->FirstChildElement("FirstPreferences");
			currentCandidate = fps->FirstChildElement("Candidate");
			while (currentCandidate) {
				auto candidateIdEl = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
				int candidateId = candidateIdEl->FindAttribute("Id")->IntValue();
				int votes = currentCandidate->FirstChildElement("Votes")->IntText();
				booth.fpVotes[candidateId] = votes;

				currentCandidate = currentCandidate->NextSiblingElement("Candidate");
			}

			tcps = currentBooth->FirstChildElement("TwoCandidatePreferred");
			if (tcps) {
				currentCandidate = tcps->FirstChildElement("Candidate");
				while (currentCandidate) {
					auto candidateIdEl = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
					int candidateId = candidateIdEl->FindAttribute("Id")->IntValue();
					int partyId = candidates.at(candidateId).party;
					int votes = currentCandidate->FirstChildElement("Votes")->IntText();
					booth.tcpVotes[partyId] = votes;

					currentCandidate = currentCandidate->NextSiblingElement("Candidate");
				}
			}

			booths[booth.id] = booth;

			currentBooth = currentBooth->NextSiblingElement("PollingPlace");
		}

		seats[seat.id] = seat;

		currentContest = currentContest->NextSiblingElement("Contest");
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
	//logger << "==COALITIONS==\n";
	//for (auto const& coalition : coalitions) {
	//	PA_LOG_VAR(coalition.second.id);
	//	PA_LOG_VAR(coalition.second.name);
	//	PA_LOG_VAR(coalition.second.shortCode);
	//}
	//logger << "==ELECTION==\n";
	//PA_LOG_VAR(name);
	//PA_LOG_VAR(id);
}
