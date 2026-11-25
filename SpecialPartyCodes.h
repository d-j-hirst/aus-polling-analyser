#pragma once

#include <string>

const std::string OthersCode = "OTH";
const std::string UnnamedOthersCode = "xOTH";
const std::string EmergingOthersCode = "eOTH";
const std::string TppCode = "@TPP";

inline bool isOthersCode(std::string code) {
	return code == OthersCode || code == UnnamedOthersCode|| code == EmergingOthersCode;
}

const int OthersIndex = -1;
const int EmergingIndIndex = -2;
const int EmergingPartyIndex = -3;
const int CoalitionPartnerIndex = -4; // Placeholder for live-automatic 
const int InvalidPartyIndex = -100000; // for situations where we want to explicitly say this shouldn't match any index

// A prominent independent or a named centrist candidate can draw tactical
// primary votes from one major party. Several small candidates can together
// exceed the same FP threshold without any one of them attracting tactical
// votes. Keep their grouped Others flow, even if classified as centrist.
// The emerging independent represents one candidate; an emerging party has
// its own sampled preference flow.
inline bool canAttractTacticalPreferences(int partyIndex, int ideology)
{
	return partyIndex == EmergingIndIndex || (partyIndex >= 0 && ideology == 2);
}
