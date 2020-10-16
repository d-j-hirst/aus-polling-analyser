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
		constexpr static size_t Size = 101;
		std::array<float, Size> values;
	};

	struct Series {
		std::vector<StanModel::Spread> timePoint;
	};

	typedef std::map<std::string, Series> PartySupport;

	StanModel(std::string name = "", std::string termCode = "", std::string partyCodes = "");

	std::string getName() const { return name; }

	std::string getTermCode() const { return termCode; }

	std::string getPartyCodes() const { return partyCodes; }

	wxDateTime getStartDate() const { return startDate; }

	wxDateTime getLastUpdatedDate() const { return lastUpdatedDate; }

	void loadData();

	int seriesCount() const;

	std::string getTextReport() const;

	// Views data for a series in the model corresponding to the given party
	Series const& viewSeries(std::string partyCode) const;

	Series const& viewSeriesByIndex(int index) const;

	std::string partyCodeByIndex(int index) const;
private:

	// Adds a series to the model for the given party name and returns a reference to it
	Series& addSeries(std::string partyCode);

	PartySupport partySupport;
	std::string name;
	std::string termCode;
	std::string partyCodes;
	wxDateTime startDate = wxInvalidDateTime;
	wxDateTime lastUpdatedDate = wxInvalidDateTime;
};