#pragma once

#include "ElectionData.h"
#include "RegexNavigation.h"

#include <array>
#include <unordered_map>
#include <vector>

class PreviousElectionDataRetriever {
public:

	const static std::string UnzippedFileName;

	// collects booth data etc. from the downloaded results file.
	// Make sure this has been downloaded by ResultsDownloader first.
	Results2::Election collectData();

private:
};