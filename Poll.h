#pragma once

#undef max
#undef min

#include <vector>
#include <string>
#include <algorithm>
#include "Pollster.h"
#include "Party.h" // needed for MAX_PARTIES constant
#include "General.h"
#include <wx/datetime.h>

class Poll {
public:
	Pollster const* pollster;
	wxDateTime date;
	float reported2pp;
	float respondent2pp;
	float calc2pp;
	float primary[16]; // others is always index 15.
	Poll(Pollster* pollster, wxDateTime date, float reported2pp, float respondent2pp, float calc2pp)
		: pollster(pollster), date(date), reported2pp(reported2pp), respondent2pp(respondent2pp), calc2pp(calc2pp) {
		resetPrimaries();
	}
	Poll() : pollster(nullptr), date(wxDateTime(0.0)), reported2pp(50), respondent2pp(-1), calc2pp(-1) {
		resetPrimaries();
	}
	std::string removeTrailingZeroes(std::string str) const {
		std::string newStr = str;
		newStr.erase(newStr.find_last_not_of('0') + 1, std::string::npos); // remove all trailing zeroes
		newStr.erase(newStr.find_last_not_of('.') + 1, std::string::npos); // remove the decimal point if it's in last place
		return newStr;
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

	// use an argument of 15 to get the "Others" vote
	std::string getPrimaryString(int partyIndex) const {
		if (primary[partyIndex] >= 0) return formatFloat(primary[partyIndex], 1);
		else return "";
	}
	void resetPrimaries() {
		for (int i = 0; i < 16; i++) primary[i] = -1;
	}
};