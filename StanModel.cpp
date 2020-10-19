#include "StanModel.h"

#include "General.h"

#include <fstream>
#include <sstream>

StanModel::StanModel(std::string name, std::string termCode, std::string partyCodes,
	std::string meanAdjustments, std::string stdevAdjustments)
	
	: name(name), termCode(termCode), partyCodes(partyCodes)
{
}

void StanModel::loadData()
{
	auto partyCodeVec = splitString(partyCodes, ",");
	if (!partyCodeVec.size() || (partyCodeVec.size() == 1 && !partyCodeVec[0].size())) {
		wxMessageBox("No party codes found!");
		return;
	}
	startDate = wxInvalidDateTime;
	partySupport.clear();
	for (auto partyCode : partyCodeVec) {
		auto& series = partySupport[partyCode];
		std::string filename = "python/Outputs/fp_trend_"
			+ termCode + "_" + partyCode + " FP.csv";
		auto file = std::ifstream(filename);
		if (!file) {
			wxMessageBox("Could not load file: " + filename);
			continue;
		}
		series.timePoint.clear();
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
		//wxMessageBox("Loaded model output from " + filename);
	}
	lastUpdatedDate = wxDateTime::Now();
	wxMessageBox("Finished loading models");
}

int StanModel::seriesCount() const
{
	return int(partySupport.size());
}

std::string StanModel::getTextReport() const
{
	std::stringstream ss;
	for (auto [key, series] : this->partySupport) {
		ss << key << "\n";
		ss << "1%: " << series.timePoint.back().values[1] << "\n";
		ss << "10%: " << series.timePoint.back().values[10] << "\n";
		ss << "50%: " << series.timePoint.back().values[50] << "\n";
		ss << "90%: " << series.timePoint.back().values[90] << "\n";
		ss << "99%: " << series.timePoint.back().values[99] << "\n";
	}
	return ss.str();
}

StanModel::Series const& StanModel::viewSeries(std::string partyCode) const
{
	return partySupport.at(partyCode);
}

StanModel::Series const& StanModel::viewSeriesByIndex(int index) const
{

	return std::next(partySupport.begin(), index)->second;
}

std::string StanModel::partyCodeByIndex(int index) const
{
	return std::next(partySupport.begin(), index)->first;
}

StanModel::Series& StanModel::addSeries(std::string partyCode)
{
	return partySupport.insert({ partyCode, Series() }).first->second;
}
