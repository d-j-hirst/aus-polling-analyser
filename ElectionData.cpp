#include "ElectionData.h"

#include "General.h"
#include "Log.h"

Results2::Election::Election(tinyxml2::XMLDocument const& xml)
{
	update(xml);
}

void Results2::Election::update(tinyxml2::XMLDocument const& xml)
{
	auto results = xml.FirstChildElement("MediaFeed")->FirstChildElement("Results");
	auto electionEl = results->FirstChildElement("eml:EventIdentifier");
	id = electionEl->FindAttribute("Id")->IntValue();
	name = electionEl->FirstChildElement("eml:EventName")->GetText();
	auto contests = results->FirstChildElement("Election")->FirstChildElement("House")->FirstChildElement("Contests");
	auto currentContest = contests->FirstChildElement("Contest");
	while (currentContest) {
		auto contestIdEl = currentContest->FirstChildElement("eml:ContestIdentifier");
		Seat seat;
		seat.id = contestIdEl->FindAttribute("Id")->IntValue();
		if (seats.contains(seat.id)) seat = seats[seat.id]; // maintain already existing data
		seat.name = contestIdEl->FirstChildElement("eml:ContestName")->GetText();
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
					}
				}
				else candidate.party = Candidate::Independent;
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
				fpVoteType = fpVoteType->NextSiblingElement("Votes");
			}

			currentCandidate = currentCandidate->NextSiblingElement("Candidate");
		}

		auto tcps = currentContest->FirstChildElement("TwoCandidatePreferred");
		currentCandidate = tcps->FirstChildElement("Candidate");
		while (currentCandidate) {
			auto candidateIdEl = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
			int candidateId = -1;
			candidateId = candidateIdEl->FindAttribute("Id")->IntValue();
			auto votesByType = currentCandidate->FirstChildElement("VotesByType");
			auto tcpVoteType = votesByType->FirstChildElement("Votes");
			while (tcpVoteType) {
				std::string typeName = tcpVoteType->FindAttribute("Type")->Value();
				int tcpCount = std::stoi(tcpVoteType->GetText());
				if (typeName == "Ordinary") seat.tcpVotes[candidateId][VoteType::Ordinary] = tcpCount;
				else if (typeName == "Absent") seat.tcpVotes[candidateId][VoteType::Absent] = tcpCount;
				else if (typeName == "Provisional") seat.tcpVotes[candidateId][VoteType::Provisional] = tcpCount;
				else if (typeName == "PrePoll") seat.tcpVotes[candidateId][VoteType::PrePoll] = tcpCount;
				else if (typeName == "Postal") seat.tcpVotes[candidateId][VoteType::Postal] = tcpCount;
				tcpVoteType = tcpVoteType->NextSiblingElement("Votes");
			}

			currentCandidate = currentCandidate->NextSiblingElement("Candidate");
		}

		auto tpps = currentContest->FirstChildElement("TwoPartyPreferred");
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

		auto boothsEl = currentContest->FirstChildElement("PollingPlaces");
		auto currentBooth = boothsEl->FirstChildElement("PollingPlace");
		while (currentBooth) {
			auto boothIdEl = currentBooth->FirstChildElement("PollingPlaceIdentifier");
			Booth booth;
			booth.id = boothIdEl->FindAttribute("Id")->IntValue();
			if (booths.contains(booth.id)) booth = booths[booth.id]; // maintain already existing data
			if (boothIdEl->FindAttribute("Name")) {
				booth.name = boothIdEl->FindAttribute("Name")->Value();
				auto classifierEl = boothIdEl->FindAttribute("Classification");
				if (classifierEl) {
					std::string classifier = classifierEl->Value();
					if (classifier == "PrePollVotingCentre") booth.type = Booth::Type::Ppvc;
					if (classifier == "PrisonMobile") booth.type = Booth::Type::Prison;
					if (classifier == "SpecialHospital") booth.type = Booth::Type::Hospital;
					if (classifier == "RemoteMobile") booth.type = Booth::Type::Remote;
				}
				seat.booths.push_back(booth.id);
			}

			auto fps = currentBooth->FirstChildElement("FirstPreferences");
			currentCandidate = fps->FirstChildElement("Candidate");
			while (currentCandidate) {
				auto candidateIdEl = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
				int candidateId = candidateIdEl->FindAttribute("Id")->IntValue();
				int votes = currentCandidate->FirstChildElement("Votes")->IntText();
				booth.votesFp[candidateId] = votes;

				currentCandidate = currentCandidate->NextSiblingElement("Candidate");
			}

			tcps = currentBooth->FirstChildElement("TwoCandidatePreferred");
			currentCandidate = tcps->FirstChildElement("Candidate");
			while (currentCandidate) {
				auto candidateIdEl = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
				int candidateId = candidateIdEl->FindAttribute("Id")->IntValue();
				int votes = currentCandidate->FirstChildElement("Votes")->IntText();
				booth.votesTcp[candidateId] = votes;

				currentCandidate = currentCandidate->NextSiblingElement("Candidate");
			}

			booths[booth.id] = booth;

			currentBooth = currentBooth->NextSiblingElement("PollingPlace");
		}

		seats[seat.id] = seat;

		currentContest = currentContest->NextSiblingElement("Contest");
	}
	logger << "==SEATS==\n";
	for (auto const& [seatId, seat] : seats) {
		PA_LOG_VAR(seat.name);
		PA_LOG_VAR(seat.id);
		PA_LOG_VAR(seat.enrolment);
		PA_LOG_VAR(seat.fpVotes);
		PA_LOG_VAR(seat.tcpVotes);
		PA_LOG_VAR(seat.tppVotes);
		for (int boothId : seat.booths) {
			auto const& booth = booths.at(boothId);
			PA_LOG_VAR(booth.id);
			PA_LOG_VAR(booth.name);
			PA_LOG_VAR(booth.votesFp);
			PA_LOG_VAR(booth.votesTcp);
			PA_LOG_VAR(booth.type);
		}
	}
	logger << "==CANDIDATES==\n";
	for (auto const& candidate : candidates) {
		PA_LOG_VAR(candidate.second.id);
		PA_LOG_VAR(candidate.second.name);
		if (candidate.second.party != -1) {
			PA_LOG_VAR(parties[candidate.second.party].name);
		}
		else {
			logger << "Independent\n";
		}
	}
	logger << "==PARTIES==\n";
	for (auto const& party : parties) {
		PA_LOG_VAR(party.second.id);
		PA_LOG_VAR(party.second.name);
		PA_LOG_VAR(party.second.shortCode);
	}
	logger << "==COALITIONS==\n";
	for (auto const& coalition : coalitions) {
		PA_LOG_VAR(coalition.second.id);
		PA_LOG_VAR(coalition.second.name);
		PA_LOG_VAR(coalition.second.shortCode);
	}
	logger << "==ELECTION==\n";
	PA_LOG_VAR(name);
	PA_LOG_VAR(id);
}
