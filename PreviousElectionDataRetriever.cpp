#include "PreviousElectionDataRetriever.h"

#include "General.h"
#include "Log.h"

#include "tinyxml2.h"

#include <fstream>
#include <map>

const std::string PreviousElectionDataRetriever::UnzippedFileName = "downloads/previous_results.xml";

Results2::Election PreviousElectionDataRetriever::collectData()
{
	std::ifstream file(UnzippedFileName);
	std::string xmlString;
	transferFileToString(file, xmlString);
	file.close(); // don't prevent the file from being used for longer than necessary

	std::map<int, std::string> candidateIdToName;
	std::map<int, int> candidateToAffiliation;
	std::map<int, std::string> affiliationCode;
	affiliationCode[-1] = "IND";

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
		Results2::Seat thisSeat;
		auto districtElement = currentContest->FirstChildElement("PollingDistrictIdentifier");
		thisSeat.id = districtElement->FindAttribute("Id")->IntValue();
		thisSeat.name = districtElement->FirstChildElement("Name")->GetText();
		thisSeat.enrolment = currentContest->FirstChildElement("Enrolment")->IntText();
		logger << " First preferences:\n";
		auto currentCandidate = currentContest->FirstChildElement("FirstPreferences")->FirstChildElement("Candidate");
		do {
			auto isIndependent = (currentCandidate->FindAttribute("Independent") != nullptr || 
				currentCandidate->FindAttribute("NoAffiliation") != nullptr);
			auto candidateIdElement = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
			auto candidateName = candidateIdElement->FirstChildElement("eml:CandidateName")->GetText();
			auto candidateId = candidateIdElement->FindAttribute("Id")->IntValue();
			candidateIdToName[candidateId] = candidateName;
			int affiliationId = -1;
			if (!isIndependent) {
				auto affiliationElement = currentCandidate->FirstChildElement("eml:AffiliationIdentifier");
				std::string affiliationName = affiliationElement->FirstChildElement("eml:RegisteredName")->GetText();
				affiliationId = affiliationElement->FindAttribute("Id")->IntValue();
				std::string affiliationShortCode = affiliationElement->FindAttribute("ShortCode")->Value();
				candidateToAffiliation[candidateId] = affiliationId;
				affiliationCode[affiliationId] = affiliationShortCode;
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
			currentCandidate = currentCandidate->NextSiblingElement("Candidate");
		} while (currentCandidate);
		currentCandidate = currentContest->FirstChildElement("TwoCandidatePreferred")->FirstChildElement("Candidate");
		logger << " Two candidate preferred:\n";
		do {
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
			thisSeat.ordinaryVotes2cp[candidateId] = ordinaryVotes;
			thisSeat.absentVotes2cp[candidateId] = absentVotes;
			thisSeat.provisionalVotes2cp[candidateId] = provisionalVotes;
			thisSeat.prepollVotes2cp[candidateId] = prepollVotes;
			thisSeat.postalVotes2cp[candidateId] = postalVotes;
			currentCandidate = currentCandidate->NextSiblingElement("Candidate");
		} while (currentCandidate);
		auto currentPollingPlace = currentContest->FirstChildElement("PollingPlaces")->FirstChildElement("PollingPlace");
		do {
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
			currentCandidate = currentPollingPlace->FirstChildElement("FirstPreferences")->FirstChildElement("Candidate");
			do {
				auto candidateIdElement = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
				auto candidateId = candidateIdElement->FindAttribute("Id")->IntValue();
				auto votes = currentCandidate->FirstChildElement("Votes")->IntText();
				thisBooth.votesFp[candidateId] = votes;
				currentCandidate = currentCandidate->NextSiblingElement("Candidate");
			} while (currentCandidate);
			currentCandidate = currentPollingPlace->FirstChildElement("TwoCandidatePreferred")->FirstChildElement("Candidate");
			do {
				auto candidateIdElement = currentCandidate->FirstChildElement("eml:CandidateIdentifier");
				auto candidateId = candidateIdElement->FindAttribute("Id")->IntValue();
				auto votes = currentCandidate->FirstChildElement("Votes")->IntText();
				thisBooth.votes2cp[candidateId] = votes;
				currentCandidate = currentCandidate->NextSiblingElement("Candidate");
			} while (currentCandidate);
			election.booths[pollingPlaceId] = thisBooth;
			thisSeat.booths.push_back(pollingPlaceId);
			currentPollingPlace = currentPollingPlace->NextSiblingElement("PollingPlace");
		} while (currentPollingPlace);
		election.seats[thisSeat.id] = thisSeat;
		currentContest = currentContest->NextSiblingElement("Contest");
	} while (currentContest);
	return election;
}
