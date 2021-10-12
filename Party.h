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

	enum class RelationType : unsigned char {
		None,
		Supports,
		Coalition,
		IsPartOf,
		IsMajor
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
	std::string homeRegion = "";
	CountAsParty countAsParty = CountAsParty::None;
	SupportsParty supportsParty = SupportsParty::None;
	int ideology = 2; // 0 = strong left, 4 = strong right
	int consistency = 1; // 0 = weak flow, 1 = normal flow, 2 = tight flow
	bool includeInOthers = false;
	RelationType relationType = RelationType::None;
	Id relationTarget = 0;
	Colour colour = { 255, 255, 255 };
	float boothColourMult = 1.6f;
	bool countsAsMajor() const { return !(countAsParty == CountAsParty::None); }
	bool countsAsOne() const { return countAsParty == CountAsParty::CountsAsPartyOne || countAsParty == CountAsParty::IsPartyOne; }
	bool countsAsTwo() const { return countAsParty == CountAsParty::CountsAsPartyTwo || countAsParty == CountAsParty::IsPartyTwo; }

	std::string relationString() const {
		return std::to_string(relationTarget);
	}

	std::string relationTypeString() const {
		switch (relationType) {
		case RelationType::IsMajor: return "Is major party";
		case RelationType::IsPartOf: return "Is part of";
		case RelationType::Coalition: return "Is in coalition with";
		case RelationType::Supports: return "Supports";
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
		report << " Relation to Party: " << relationString() << "\n";
		report << " Relation Type: " << relationTypeString() << "\n";
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