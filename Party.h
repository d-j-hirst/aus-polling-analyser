#pragma once

#include <string>

const int PA_MAX_PARTIES = 15;

struct Party {
	enum class CountAsParty : unsigned char {
		None,
		IsPartyOne,
		IsPartyTwo,
		CountsAsPartyOne,
		CountsAsPartyTwo
	};
	Party(std::string name, float preferenceShare, float exhaustRate, std::string abbreviation, CountAsParty countAsParty)
		: name(name), preferenceShare(preferenceShare), exhaustRate(exhaustRate), abbreviation(abbreviation), countAsParty(countAsParty) {}
	Party() {}
	std::string name = "";
	float preferenceShare = 50.0f;
	float exhaustRate = 0.0f;
	std::string abbreviation = "";
	CountAsParty countAsParty = CountAsParty::None;
};