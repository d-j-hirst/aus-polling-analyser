#include "StanModel.h"

#include "General.h"

#include <fstream>
#include <sstream>

constexpr float OneSigma = 0.6827f;

StanModel::StanModel(std::string name, std::string termCode, std::string partyCodes,
	std::string meanAdjustments, std::string deviationAdjustments)
	
	: name(name), termCode(termCode), partyCodes(partyCodes)
{
}

void StanModel::loadData(std::function<void(std::string)> feedback)
{
	auto partyCodeVec = splitString(partyCodes, ",");
	if (!partyCodeVec.size() || (partyCodeVec.size() == 1 && !partyCodeVec[0].size())) {
		feedback("No party codes found!");
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
			feedback("Could not load file: " + filename);
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
	}
	// *** create series with adjustments
	updateAdjustedData(feedback);

	lastUpdatedDate = wxDateTime::Now();
	feedback("Finished loading models");
}

int StanModel::seriesCount() const
{
	return int(partySupport.size());
}

std::string StanModel::getTextReport() const
{
	std::stringstream ss;
	ss << "Raw party support, assuming only sampling error:\n";
	for (auto [key, series] : this->partySupport) {
		ss << key << "\n";
		ss << "1%: " << series.timePoint.back().values[1] << "\n";
		ss << "10%: " << series.timePoint.back().values[10] << "\n";
		ss << "50%: " << series.timePoint.back().values[50] << "\n";
		ss << "90%: " << series.timePoint.back().values[90] << "\n";
		ss << "99%: " << series.timePoint.back().values[99] << "\n";
	}
	ss << ";";
	ss << "Adjusted party support, accounting for possible systemic bias and variability:\n";
	for (auto [key, series] : this->adjustedSupport) {
		ss << key << "\n";
		ss << "1%: " << series.timePoint.back().values[1] << "\n";
		ss << "10%: " << series.timePoint.back().values[10] << "\n";
		ss << "50%: " << series.timePoint.back().values[50] << "\n";
		ss << "90%: " << series.timePoint.back().values[90] << "\n";
		ss << "99%: " << series.timePoint.back().values[99] << "\n";
	}
	return ss.str();
}

StanModel::Series const& StanModel::viewRawSeries(std::string partyCode) const
{
	return partySupport.at(partyCode);
}

StanModel::Series const& StanModel::viewRawSeriesByIndex(int index) const
{

	return std::next(partySupport.begin(), index)->second;
}

StanModel::Series const& StanModel::viewAdjustedSeries(std::string partyCode) const
{
	return adjustedSupport.at(partyCode);
}

StanModel::Series const& StanModel::viewAdjustedSeriesByIndex(int index) const
{

	return std::next(adjustedSupport.begin(), index)->second;
}

std::string StanModel::partyCodeByIndex(int index) const
{
	return std::next(partySupport.begin(), index)->first;
}

void StanModel::updateAdjustedData(std::function<void(std::string)> feedback)
{
	auto meanAdjustmentsVec = splitString(meanAdjustments, ",");
	auto deviationAdjustmentsVec = splitString(deviationAdjustments, ",");
	auto partyCodeVec = splitString(partyCodes, ",");
	bool validSizes = meanAdjustmentsVec.size() == partyCodeVec.size() &&
		(deviationAdjustmentsVec.size() == partyCodeVec.size());
	if (!validSizes) {
		feedback("Warning: Mean and/or deviation adjustments not valid, skipping adjustment phase");
		return;
	}

	for (int partyIndex = 0; partyIndex < int(partyCodeVec.size()); ++partyIndex) {
		std::string partyName = partyCodeVec[partyIndex];
		if (partySupport.count(partyName)) {
			adjustedSupport[partyName] = partySupport[partyName];
			float adjustMean = 0.0f;
			float adjustDeviation = 1.0f;
			try {
				adjustMean = std::stof(meanAdjustmentsVec[partyIndex]);
				adjustDeviation = std::stof(deviationAdjustmentsVec[partyIndex]);
			}
			catch (std::invalid_argument) {
				feedback("Warning: Invalid mean or deviation value for party " + partyName + ", aborting adjustment phase");
				adjustedSupport.clear();
				break;
			}
			for (auto& timePoint : adjustedSupport[partyName].timePoint) {
				// Transform party support values
				for (auto& value : timePoint.values) {
					value = transformVoteShare(value);
				}
				// Adjust transformed party support values by a set amount
				// and also according to its distance from the mean
				float meanValue = timePoint.values[timePoint.Size / 2];
				for (auto& value : timePoint.values) {
					float deviation = (value - meanValue);
					value = meanValue + adjustMean + deviation * adjustDeviation;
				}
				// Transform back into a regular vote share
				for (auto& value : timePoint.values) {
					value = detransformVoteShare(value);
				}
			}
		}
	}
}

StanModel::Series& StanModel::addSeries(std::string partyCode)
{
	return partySupport.insert({ partyCode, Series() }).first->second;
}
