#pragma once

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