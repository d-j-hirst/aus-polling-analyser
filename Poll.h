#pragma once

#include "Pollster.h"
#include "PartyCollection.h"
#include "PollsterCollection.h"
#include "General.h"

#include <wx/datetime.h>

#include <algorithm>
#include <array>
#include <vector>
#include <string>

class Poll {
public:
	typedef int Id;
	constexpr static Id InvalidId = -1;
	constexpr static float NullValue = -1.0f;

	Pollster::Id pollster = Pollster::DefaultId;
	wxDateTime date = wxDateTime::Now();
	float reported2pp = 50.0f;
	float respondent2pp = NullValue;
	float calc2pp = NullValue;
	std::array<float, PartyCollection::MaxParties + 1> primary; // others is always the final index.

	Poll(Pollster::Id pollster, wxDateTime date, float reported2pp = NullValue, float respondent2pp = NullValue, float calc2pp = NullValue)
		: pollster(pollster), date(date), reported2pp(reported2pp), respondent2pp(respondent2pp), calc2pp(calc2pp) {
		resetPrimaries();
	}

	Poll() {
		resetPrimaries();
	}

	std::string getReported2ppString() const {
		if (reported2pp >= 0) return formatFloat(reported2pp, 1);
		else return "";
	}

	std::string getRespondent2ppString() const {
		if (respondent2pp >= 0) return formatFloat(respondent2pp, 1);
		else return "";
	}

	std::string getCalc2ppString() const {
		if (calc2pp >= 0) return formatFloat(calc2pp, 2);
		else return "";
	}

	float getBest2pp() const {
		// Note: because of differences in calculating previous-election preferences based on treatment
		// of One Nation between Newspoll and others, 2pps are always calculated from primaries for now until
		// we get a new consensus on this
		float used2pp = 0.0f;
		//if (reported2pp > 0.1f) {
		//	if (calc2pp > 0.1f) {
		//		used2pp = std::max(std::min((reported2pp + calc2pp) * 0.5f, reported2pp + 0.5f), reported2pp - 0.5f);
		//	}
		//	else used2pp = reported2pp;
		//}
		/*else*/ if (calc2pp > 0.1f) used2pp = calc2pp;
		else if (reported2pp > 0.1f) used2pp = reported2pp;
		else if (respondent2pp > 0.1f) used2pp = respondent2pp;
		return used2pp;
	}

	// use an argument of PartyCollection::MaxParties to get the "Others" vote
	std::string getPrimaryString(int partyIndex) const {
		if (primary[partyIndex] >= 0) return formatFloat(primary[partyIndex], 1);
		else return "";
	}

	void resetPrimaries() {
		for (int i = 0; i <= PartyCollection::MaxParties; i++) primary[i] = NullValue;
	}

	std::string textReport(PartyCollection const& parties, PollsterCollection const& pollsters) const {
		std::stringstream report;
		report << std::boolalpha;
		report << "Reporting Poll: \n";
		report << " Pollster: " << pollsters.view(pollster).name << "\n";
		report << " Date: " << date.FormatDate() << "\n";
		report << " Reported 2pp: " << getReported2ppString() << "\n";
		report << " Respondent 2pp: " << getRespondent2ppString() << "\n";
		report << " Calculated 2pp: " << getCalc2ppString() << "\n";
		report << " Primaries: \n";
		for (int partyIndex = 0; partyIndex < std::min(parties.count(), int(primary.size()) - 1); ++partyIndex) {
			report << "  " << parties.viewByIndex(partyIndex).name
				<< ": " << getPrimaryString(partyIndex) << "\n";
		}
		report << "  Others: " << primary.back() << "\n";
		return report.str();
	}
};