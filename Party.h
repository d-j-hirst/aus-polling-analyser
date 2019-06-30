#pragma once

#include <string>
#include <vector>

const int PA_MAX_PARTIES = 15;

struct Party {
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
	static bool oppositeMajors(Party const& party1, Party const& party2) {
		return (party1.countsAsOne() && party2.countsAsTwo()) || (party1.countsAsTwo() && party2.countsAsOne());
	}
};