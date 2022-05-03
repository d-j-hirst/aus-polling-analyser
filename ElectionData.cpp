#include "ElectionData.h"

#include "Log.h"

Results2::Election::Election(tinyxml2::XMLDocument const& xml)
{
	auto results = xml.FirstChildElement("MediaFeed")->FirstChildElement("Results");
	auto electionEl = results->FirstChildElement("eml:EventIdentifier");
	electionEl->FindAttribute("Id")->QueryIntValue(&id);
	name = electionEl->FirstChildElement("eml:EventName")->GetText();
	PA_LOG_VAR(name);
	PA_LOG_VAR(id);
	auto contests = results->FirstChildElement("Election")->FirstChildElement("House")->FirstChildElement("Contests");
	auto currentContest = contests->FirstChildElement("Contest");
	while (currentContest) {
		auto contestIdEl = currentContest->FirstChildElement("PollingDistrictIdentifier");
		Seat seat;
		contestIdEl->FindAttribute("Id")->QueryIntValue(&seat.id);
		seat.name = contestIdEl->FirstChildElement("Name")->GetText();
		seat.enrolment = std::stoi(currentContest->FirstChildElement("Enrolment")->GetText());
		PA_LOG_VAR(seat.name);
		PA_LOG_VAR(seat.id);
		PA_LOG_VAR(seat.enrolment);
		auto firstPrefs = currentContest->FirstChildElement("FirstPreferences");
		auto currentCandidate = firstPrefs->FirstChildElement("Candidate");
		while (currentCandidate) {
			Candidate candidate;
			auto candidateIdEl = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
			candidateIdEl->FindAttribute("Id")->QueryIntValue(&candidate.id);
			candidate.name = candidateIdEl->FirstChildElement("eml:CandidateName")->GetText();
			auto affiliationEl = currentCandidate->FirstChildElement("eml:AffiliationIdentifier");
			if (affiliationEl) {
				affiliationEl->FindAttribute("Id")->QueryIntValue(&candidate.party);
				if (!parties.contains(candidate.party)) {
					parties[candidate.party] = Party();
					parties[candidate.party].id = candidate.party;
					parties[candidate.party].name =
						currentCandidate->FirstChildElement("eml:AffiliationIdentifier")->FirstChildElement("eml:RegisteredName")->GetText();
				}
			}
			else candidate.party = -1;
			candidates[candidate.id] = candidate;
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
			candidateIdEl->FindAttribute("Id")->QueryIntValue(&candidateId);
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

		PA_LOG_VAR(seat.fpVotes);
		PA_LOG_VAR(seat.tcpVotes);
		currentContest = currentContest->NextSiblingElement("Contest");
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
}
