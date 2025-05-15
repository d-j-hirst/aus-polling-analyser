#pragma once

#include "ElectionData.h"
#include "RegexNavigation.h"

#include "tinyxml2.h"

#include <array>
#include <unordered_map>
#include <vector>

class PreviousElectionDataRetriever {
public:

	const static std::string UnzippedFileName;

	// collects booth data etc. from the downloaded results file.
	// Make sure this has been downloaded by ResultsDownloader first.
	Results2::Election collectData(std::string const& termCode);

	Results2::Election load2004Tcp(std::string filename);

	Results2::Election loadPre2004Tcp(Results2::Election const& templateElection, std::string filename, std::string const& termCode);

private:
};