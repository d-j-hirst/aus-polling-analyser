#include "StanModel.h"

#include "General.h"

#include <fstream>

StanModel::StanModel(std::string name, std::string termCode, std::string partyCodes)
	: name(name), termCode(termCode), partyCodes(partyCodes)
{
}

void StanModel::loadData()
{
	auto partyCodeVec = splitString(partyCodes, ",");
	if (!partyCodeVec.size() || (partyCodeVec.size() == 1 && !partyCodeVec[0].size())) {
		wxMessageBox("No party codes found!");
	}
	for (auto partyCode : partyCodeVec) {
		auto& series = partySupport[partyCode];
		std::string filename = "python/Outputs/fp_trend_"
			+ termCode + "_" + partyCode + " FP.csv";
		auto file = std::ifstream(filename);
		if (!file) {
			wxMessageBox("Could not load file: " + filename);
			continue;
		}
		std::string line;
		std::getline(file, line); // first line is just a legend, skip it
		std::getline(file, line);
		if (!startDate.IsValid()) {
			auto dateVals = splitString(line, ",");
			startDate = wxDateTime(std::stoi(dateVals[0]),
				wxDateTime::Month(std::stoi(dateVals[1]) - 1), std::stoi(dateVals[2]));
			wxMessageBox(startDate.FormatISODate());
		}
		std::getline(file, line); // this line is just a legend, skip it
		std::vector<float> trend;
		do {
			std::getline(file, line);
			if (!file) break;
			auto trendVals = splitString(line, ",");
			series.timePoint.push_back(Spread());
			for (int percentile = 0; percentile < Spread::Size; ++percentile) {
				series.timePoint.back().values[percentile]
					= std::stof(trendVals[percentile + 2]);
			}
		} while (true);
		wxMessageBox("Loaded model output from " + filename);
	}
	lastUpdatedDate = wxDateTime::Now();
	wxMessageBox("Finished loading models");
}

StanModel::Series const& StanModel::viewSeries(std::string partyCode) const
{
	return partySupport.at(partyCode);
}

StanModel::Series& StanModel::addSeries(std::string partyCode)
{
	return partySupport.insert({ partyCode, Series() }).first->second;
}
