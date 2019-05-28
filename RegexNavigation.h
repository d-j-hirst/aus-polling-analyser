#pragma once

#include <regex>
#include <string>

#include <regex>
#include <string>

typedef std::string::const_iterator SearchIterator;

inline int extractInt(std::string const& xmlString, std::string const& regexString, SearchIterator& searchIt, int matchNum = 1) {
	std::regex thisRegex(regexString);
	std::smatch match;
	std::regex_search(searchIt, xmlString.cend(), match, thisRegex);
	searchIt = match.suffix().first;
	return std::stoi(match.str(matchNum));
}

inline float extractFloat(std::string const& xmlString, std::string const& regexString, SearchIterator& searchIt, int matchNum = 1) {
	std::regex thisRegex(regexString);
	std::smatch match;
	std::regex_search(searchIt, xmlString.cend(), match, thisRegex);
	searchIt = match.suffix().first;
	return std::stof(match.str(matchNum));
}

inline std::string extractString(std::string const& xmlString, std::string const& regexString, SearchIterator& searchIt, int matchNum = 1) {
	std::regex thisRegex(regexString);
	std::smatch match;
	std::regex_search(searchIt, xmlString.cend(), match, thisRegex);
	searchIt = match.suffix().first;
	return match.str(matchNum);
}

inline bool extractBool(std::string const& xmlString, std::string const& regexString, SearchIterator& searchIt, int matchNum = 1) {
	std::regex thisRegex(regexString);
	std::smatch match;
	std::regex_search(searchIt, xmlString.cend(), match, thisRegex);
	searchIt = match.suffix().first;
	return match[matchNum].matched;
}

inline void seekTo(std::string const& xmlString, std::string const& regexString, SearchIterator& searchIt) {
	std::regex thisRegex(regexString);
	std::smatch match;
	std::regex_search(searchIt, xmlString.cend(), match, thisRegex);
	searchIt = match.suffix().first;
}

// returns true if the first instance of firstString comes before the first instance of secondString
inline bool comesBefore(std::string const& xmlString, std::string const& firstString, std::string const& secondString, SearchIterator const& searchIt) {
	std::regex firstRegex(firstString);
	std::smatch match;
	std::regex_search(searchIt, xmlString.cend(), match, firstRegex);
	if (!match[0].matched) {
		return false; // didn't find this at all
	}
	std::regex secondRegex(secondString);
	// only want to search up to where the previous match was made
	std::regex_search(searchIt, match[0].second, match, secondRegex);
	return !match[0].matched;
}