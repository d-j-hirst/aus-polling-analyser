#include "StanModel.h"

#include "General.h"

#include <fstream>
#include <sstream>
#include <numeric>

constexpr float OneSigma = 0.6827f;

constexpr int MedianSpreadValue = StanModel::Spread::Size / 2;

RandomGenerator StanModel::rng = RandomGenerator();

StanModel::MajorPartyCodes StanModel::majorPartyCodes = 
	{ "ALP", "LNP", "LIB", "NAT", "GRN", "OTH", "OTHx" };

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
	rawSupport.clear();
	for (auto partyCode : partyCodeVec) {
		auto& series = rawSupport[partyCode];
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

int StanModel::rawSeriesCount() const
{
	return int(rawSupport.size());
}

int StanModel::adjustedSeriesCount() const
{
	return int(adjustedSupport.size());
}

std::string StanModel::getTextReport() const
{
	std::stringstream ss;
	ss << "Raw party support, assuming only sampling error:\n";
	for (auto [key, series] : this->rawSupport) {
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
	ss << ";";
	ss << "Vote share sample:\n";
	auto sample = generateSupportSample();
	for (auto [key, vote] : sample) {
		ss << key << ": " << vote << "\n";
	}
	return ss.str();
}

StanModel::Series const& StanModel::viewRawSeries(std::string partyCode) const
{
	return rawSupport.at(partyCode);
}

StanModel::Series const& StanModel::viewRawSeriesByIndex(int index) const
{

	return std::next(rawSupport.begin(), index)->second;
}

StanModel::Series const& StanModel::viewAdjustedSeries(std::string partyCode) const
{
	return adjustedSupport.at(partyCode);
}

StanModel::Series const& StanModel::viewAdjustedSeriesByIndex(int index) const
{
	return std::next(adjustedSupport.begin(), index)->second;
}

StanModel::SupportSample StanModel::generateSupportSample(wxDateTime date) const
{
	if (!adjustedSupport.size()) return SupportSample();
	int seriesLength = adjustedSupport.begin()->second.timePoint.size();
	if (!seriesLength) return SupportSample();
	int dayOffset = adjustedSupport.begin()->second.timePoint.size() - 1;
	if (date.IsValid()) dayOffset = std::min(dayOffset, (date - startDate).GetDays());
	if (dayOffset < 0) dayOffset = 0;
	SupportSample sample;
	for (auto [key, support] : adjustedSupport) {
		if (key == "OTH") continue; // only include the "OTHx" unnamed 
		float uniform = rng.uniform(0.0, 1.0);
		int lowerBucket = int(floor(uniform * float(Spread::Size - 1)));
		float upperMix = std::fmod(uniform * float(Spread::Size - 1), 1.0f);
		float lowerVote = support.timePoint[dayOffset].values[lowerBucket];
		float upperVote = support.timePoint[dayOffset].values[lowerBucket + 1];
		float sampledVote = upperVote * upperMix + lowerVote * (1.0f - upperMix);
		sample.insert({ key, sampledVote });
	}
	float sampleSum = std::accumulate(sample.begin(), sample.end(), 0.0f,
		[](float a, decltype(sample)::value_type b) {return a + b.second; });
	float sampleAdjust = 100.0f / sampleSum;
	for (auto& vote : sample) {
		vote.second *= sampleAdjust;
	}
	return sample;
}

std::string StanModel::partyCodeByIndex(int index) const
{
	return std::next(rawSupport.begin(), index)->first;
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
		if (rawSupport.count(partyName)) {
			adjustedSupport[partyName] = rawSupport[partyName];
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
				float meanValue = timePoint.values[MedianSpreadValue];
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

	// Create a series for estimated others, excluding
	if (adjustedSupport.count("OTH")) {
		adjustedSupport["OTHx"] = adjustedSupport["OTH"];
		for (auto const& [key, series] : adjustedSupport) {
			if (key != "OTH" && key != "OTHx" && !majorPartyCodes.count(key)) {
				for (int time = 0; time < int(series.timePoint.size()); ++time) {
					float partyMedian = series.timePoint[time].values[MedianSpreadValue];
					for (float& value : adjustedSupport["OTHx"].timePoint[time].values) {
						// Should remain with at least a small, ascending sequence
						value = std::max(value - partyMedian, 0.1f + value * 0.01f);
					}
				}
			}
		}
	}
}

StanModel::Series& StanModel::addSeries(std::string partyCode)
{
	return rawSupport.insert({ partyCode, Series() }).first->second;
}
