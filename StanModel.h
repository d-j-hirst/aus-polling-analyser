#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include <wx/datetime.h>

class StanModel {
public:
	friend class ModelCollection;
	friend class EditModelFrame;
	friend class ProjectFiler;

	struct Spread {
		constexpr static size_t Size = 101; // must be odd so that there is a single median value
		std::array<float, Size> values;
	};

	struct Series {
		std::vector<StanModel::Spread> timePoint;
	};

	typedef std::map<std::string, Series> PartySupport;

	typedef std::map<std::string, float> SupportAdjustments;

	StanModel(std::string name = "", std::string termCode = "", std::string partyCodes = "",
		std::string meanAdjustments = "", std::string stdevAdjustments = "");

	std::string getName() const { return name; }

	std::string getTermCode() const { return termCode; }

	std::string getPartyCodes() const { return partyCodes; }

	wxDateTime getStartDate() const { return startDate; }

	wxDateTime getLastUpdatedDate() const { return lastUpdatedDate; }

	void loadData(std::function<void(std::string)> feedback = [](std::string) {});

	int seriesCount() const;

	std::string getTextReport() const;

	// Views data for a series in the model corresponding to the given party
	Series const& viewRawSeries(std::string partyCode) const;

	Series const& viewRawSeriesByIndex(int index) const;

	Series const& viewAdjustedSeries(std::string partyCode) const;

	Series const& viewAdjustedSeriesByIndex(int index) const;

	std::string partyCodeByIndex(int index) const;
private:

	void updateAdjustedData(std::function<void(std::string)> feedback);

	// Adds a series to the model for the given party name and returns a reference to it
	Series& addSeries(std::string partyCode);

	PartySupport partySupport;
	PartySupport adjustedSupport;
	std::string meanAdjustments;
	std::string deviationAdjustments;
	std::string name;
	std::string termCode;
	std::string partyCodes;
	wxDateTime startDate = wxInvalidDateTime;
	wxDateTime lastUpdatedDate = wxInvalidDateTime;
};