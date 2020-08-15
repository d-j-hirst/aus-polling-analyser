#pragma once

#include <string>
#include <sstream>
#include <vector>

struct Party {
	typedef int Id;
	constexpr static Id InvalidId = -1;

	enum class CountAsParty : unsigned char {
		None,
		IsPartyOne,
		IsPartyTwo,
		CountsAsPartyOne,
		CountsAsPartyTwo
	};

	enum class SupportsParty : unsigned char {
		None,
		One,
		Two
	};

	struct Colour {
		int r;
		int g;
		int b;
	};

	Party(std::string name, float preferenceShare, float exhaustRate, std::string abbreviation, CountAsParty countAsParty)
		: name(name), preferenceShare(preferenceShare), exhaustRate(exhaustRate), abbreviation(abbreviation), countAsParty(countAsParty) {}
	Party() {}
	std::string name = "";
	float preferenceShare = 50.0f;
	float exhaustRate = 0.0f;
	std::string abbreviation = "";
	std::vector<std::string> officialCodes; // official codes that match to this party according to the electoral commission
	CountAsParty countAsParty = CountAsParty::None;
	SupportsParty supportsParty = SupportsParty::None;
	int ideology = 2; // 0 = strong left, 4 = strong right
	int consistency = 1; // 0 = weak flow, 1 = normal flow, 2 = tight flow
	Colour colour = { 255, 255, 255 };
	float boothColourMult = 1.6f;
	bool countsAsMajor() const { return !(countAsParty == CountAsParty::None); }
	bool countsAsOne() const { return countAsParty == CountAsParty::CountsAsPartyOne || countAsParty == CountAsParty::IsPartyOne; }
	bool countsAsTwo() const { return countAsParty == CountAsParty::CountsAsPartyTwo || countAsParty == CountAsParty::IsPartyTwo; }

	std::string countsAsPartyString() const {
		switch (countAsParty) {
		case CountAsParty::CountsAsPartyOne: return "Counts as Party One";
		case CountAsParty::CountsAsPartyTwo: return "Counts as Party Two";
		case CountAsParty::IsPartyOne: return "Is Party One";
		case CountAsParty::IsPartyTwo: return "Is Party Two";
		default: return "None";
		}
	}

	std::string supportsPartyString() const {
		switch (supportsParty) {
		case SupportsParty::One: return "Supports Party One";
		case SupportsParty::Two: return "Supports Party Two";
		default: return "None";
		}
	}

	std::string textReport() const {
		std::stringstream report;
		report << "Reporting Party: \n";
		report << " Name: " << name << "\n";
		report << " Preference Share: " << preferenceShare << "\n";
		report << " Exhaust Rate: " << exhaustRate << "\n";
		report << " Abbreviation: " << abbreviation << "\n";
		report << " Counts As Party: " << countsAsPartyString() << "\n";
		report << " Preference Share: " << supportsPartyString() << "\n";
		report << " Booth Colour Multiplier: " << boothColourMult << "\n";
		report << " Ideology: " << ideology << "\n";
		report << " Consistency: " << consistency << "\n";
		report << " Official Codes:";
		for (std::string officialCode : officialCodes) {
			report << " " << officialCode;
		}
		report << "\n";
		report << " Colour: " << colour.r << ", " << colour.g << ", " << colour.b << "\n";
		return report.str();
	}
};