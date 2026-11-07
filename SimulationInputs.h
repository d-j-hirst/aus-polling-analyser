#pragma once

// Inputs used by preparation, betting-odds calibration and the pre-count live
// baseline. One immutable copy is shared by all supporting runs of a forecast.
// Comparing it with the previous run prevents reuse of results made from different
// inputs, without treating a new live count or export location as a change.

#include "Date.h"

#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

class PollingProject;
class Simulation;

class SimulationInputs {
public:
	// Each entry identifies a direct file input to SimulationPreparation. Keep
	// the filenames in capture() alongside this list; preparation uses these
	// entries rather than maintaining an independent list of files to open.
	enum class File {
		Preferences, PriorResults, SeatTypes, PastSeatResults, TppSwingFactors,
		GreensStatistics, IndependentStatistics, OthersStatistics,
		GreensIndependentCorrelation, IndependentEmergence,
		PopulistStatistics, PopulistModifiers, CentristStatistics, CentristModifiers,
		IndividualSeats, NationalsParameters, NationalsSeats,
		RegionBase, RegionPolls, RegionMix, RegionMixParameters,
		RegionTppDeviations, RegionOnDeviations, RegionOnDeviationsLowercase
	};

	// seatContents is the text already consumed by seat import, not another read.
	// nowcastDate is the fixed date used by this run, ignored for ordinary/live
	// election forecasts. Missing files are recorded, then each loader retains
	// its normal required-file error or optional-file fallback.
	static std::shared_ptr<SimulationInputs const> capture(
		PollingProject const& project, Simulation const& simulation,
		std::optional<std::string> seatContents, Date nowcastDate);

	bool reusableWith(SimulationInputs const& other) const;
	std::string const& path(File file) const;
	std::istringstream open(std::string const& path) const;

private:
	std::optional<std::string> seatContents;
	std::map<File, std::string> paths;
	std::map<std::string, std::optional<std::string>> contents;
	std::string settings;
	// Identity markers distinguish newly prepared model/projection data, even if
	// an update preserves its timestamp. Copies into a live worker share them.
	std::shared_ptr<int const> modelRevision, projectionRevision;
};
