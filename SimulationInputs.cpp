// Capture the non-live inputs which determine reusable simulation calculations.
// This does not watch files or support editing an active run. It checks once at
// the next run boundary, then supplies those same file contents to preparation.

#include "SimulationInputs.h"

#include "PollingProject.h"
#include "Simulation.h"
#include "json.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
// Absence is meaningful for optional regional files: creating or removing one
// changes the forecast's fallback assumptions. Other read failures must not be
// mistaken for an absent optional file.
std::optional<std::string> readInput(std::string const& path)
{
	if (!std::filesystem::exists(path)) return std::nullopt;
	std::ifstream file(path, std::ios::binary);
	if (!file) throw std::runtime_error("Cannot read simulation input: " + path);
	std::ostringstream text;
	text << file.rdbuf();
	if (file.bad() || text.bad()) {
		throw std::runtime_error("Cannot finish reading simulation input: " + path);
	}
	return text.str();
}

// Select settings that affect calibration or the forecast before counts arrive.
// In particular, live source URLs, replay selection, output directories, manual
// count overrides and display colours are not inputs to those calculations.
// A nowcast's date matters because it changes the sampled voting intention;
// the wall-clock date does not change an election-date baseline during replay.
std::string calculationSettings(
	PollingProject const& project, Simulation const& simulation, Date nowcastDate)
{
	using nlohmann::json;
	auto const& sim = simulation.getSettings();
	auto const& projection = project.projections().view(sim.baseProjection);
	auto const& projected = projection.getSettings();
	auto const& model = projection.getBaseModel(project.models());
	json result;
	result["simulation"] = {
		{"projection", sim.baseProjection}, {"iterations", sim.numIterations},
		{"previousElection", sim.prevTermCodes.empty() ? "" : sim.prevTermCodes.front()},
		{"previousTpp", sim.prevElection2pp}, {"federalElectionDate", sim.fedElectionDate.formatIso()},
		{"nowcast", simulation.isNowcast()}
	};
	if (sim.forceTpp) result["simulation"]["forcedTpp"] = *sim.forceTpp;
	if (simulation.isNowcast()) result["simulation"]["date"] = nowcastDate.formatIso();
	result["projection"] = {
		{"model", projected.baseModel}, {"iterations", projected.numIterations},
		{"dates", projected.possibleDates}, {"end", projected.endDate.formatIso()}
	};
	result["model"] = {
		{"term", model.getTermCode()}, {"parties", model.getPartyCodes()},
		{"preferenceDeviation", model.getPreferenceDeviation()},
		{"preferenceSamples", model.getPreferenceSamples()},
		{"start", model.getStartDate().formatIso()},
		{"majorPartyCodes", StanModel::getMajorPartyCodes()}
	};
	result["others"] = {project.parties().getOthersPreferenceFlow(), project.parties().getOthersExhaustRate()};
	result["parties"] = json::array();
	for (auto const& [id, party] : project.parties()) {
		result["parties"].push_back({
			{"id", id}, {"name", party.name}, {"abbreviation", party.abbreviation},
			{"codes", party.officialCodes}, {"preferences", party.p1PreferenceFlow},
			{"exhaustion", party.exhaustRate}, {"nonClassicPreferences", party.ncPreferenceFlow},
			{"home", party.homeRegion}, {"seatTarget", party.seatTarget},
			{"countAs", int(party.countAsParty)}, {"supports", int(party.supportsParty)},
			{"ideology", party.ideology}, {"consistency", party.consistency},
			{"others", party.includeInOthers}, {"relation", int(party.relationType)},
			{"relationTarget", party.relationTarget}
		});
	}
	result["regions"] = json::array();
	for (auto const& [id, region] : project.regions()) {
		result["regions"].push_back({
			{"id", id}, {"name", region.name}, {"population", region.population},
			{"lastTpp", region.lastElection2pp}, {"sampleTpp", region.sample2pp},
			{"code", region.analysisCode}, {"swingDeviation", region.swingDeviation},
			{"homeModifier", region.homeRegionMod}
		});
	}
	return result.dump();
}
}

std::shared_ptr<SimulationInputs const> SimulationInputs::capture(
	PollingProject const& project, Simulation const& simulation,
	std::optional<std::string> seatContents, Date nowcastDate)
{
	auto inputs = std::make_shared<SimulationInputs>();
	inputs->seatContents = std::move(seatContents);
	auto const& sim = simulation.getSettings();
	auto const& projection = project.projections().view(sim.baseProjection);
	auto const& model = projection.getBaseModel(project.models());
	auto const term = model.getTermCode();
	auto const previous = sim.prevTermCodes.empty() ? "" : sim.prevTermCodes.front();

	// These small tables describe historical results and expected electoral
	// behaviour. Count feeds and turnout/history files are deliberately absent:
	// live preparation reads them separately, after the reusable non-live work.
	std::map<File, std::string> files = {
		{File::Preferences, "analysis/Data/preference-estimates.csv"},
		{File::PriorResults, "analysis/Data/prior-results.csv"},
		{File::SeatTypes, "analysis/Data/seat-types.csv"},
		{File::PastSeatResults, "analysis/elections/results_" + previous + ".csv"},
		{File::TppSwingFactors, "analysis/Seat Statistics/tpp-swing-factors.csv"},
		{File::GreensStatistics, "analysis/Seat Statistics/statistics_GRN.csv"},
		{File::IndependentStatistics, "analysis/Seat Statistics/statistics_IND.csv"},
		{File::OthersStatistics, "analysis/Seat Statistics/statistics_OTH.csv"},
		{File::GreensIndependentCorrelation, "analysis/Seat Statistics/GRN_IND_correlation.csv"},
		{File::IndependentEmergence, "analysis/Seat Statistics/statistics_emerging_IND.csv"},
		{File::PopulistStatistics, "analysis/Seat Statistics/statistics_populist.csv"},
		{File::PopulistModifiers, "analysis/Seat Statistics/modifiers_populist.csv"},
		{File::CentristStatistics, "analysis/Seat Statistics/statistics_centrist.csv"},
		{File::CentristModifiers, "analysis/Seat Statistics/modifiers_centrist.csv"},
		{File::IndividualSeats, "analysis/Seat Statistics/individual-seat-factors/" + term + ".csv"},
		{File::NationalsParameters, "analysis/Nationals/" + term + "_stats.csv"},
		{File::NationalsSeats, "analysis/Nationals/" + term + "_seats.csv"},
		{File::RegionBase, "analysis/Regional/" + term + "-regions-base.csv"},
		{File::RegionPolls, "analysis/Regional/" + term + "-regions-polled.csv"},
		{File::RegionMix, "analysis/Regional/" + term + "-mix-regions.csv"},
		{File::RegionMixParameters, "analysis/Regional/" + term + "-mix-parameters.csv"},
		{File::RegionTppDeviations, "analysis/Regional/" + term + "-swing-deviations.csv"}
	};
	// The ranking count is enabled through an election's optional preference file.
	// Record even a missing file. Changing the mode or the assumptions must
	// invalidate both the betting-odds calibration and the pre-count baseline.
	if (term == "2026vic" || term == "2026sa") {
		files[File::PreferenceModel] = "analysis/Preferences/" + term + ".json";
	}
	if (term == "2026vic") {
		// Historical scrutiny counts also affect the ordinary FP forecast and
		// its supporting runs, independently of the preference model mode.
		files[File::CoalitionPreferenceHistory] =
			"analysis/Data/coalition-preference-history-" + previous + ".csv";
		// The trial changes the district priors in every supporting run. Record
		// absence too, so enabling or disabling it refreshes reusable results.
		files[File::OneNationDistrictFp] =
			"analysis/Federal-State/2026vic-one-nation-fp.json";
	}
	if (project.parties().indexByShortCode("ON") != PartyCollection::InvalidIndex) {
		// Record both spellings. Appearance of the preferred one must invalidate
		// results previously calculated using the fallback (or without either).
		files[File::RegionOnDeviations] = "analysis/Regional/" + term + "-swing-deviations-ON.csv";
		files[File::RegionOnDeviationsLowercase] = "analysis/Regional/" + term + "-swing-deviations-on.csv";
	}
	for (auto const& [file, relative] : files) {
		auto path = project.paths().resolveString(relative);
		inputs->paths[file] = path;
		inputs->contents[path] = readInput(path);
	}
	inputs->settings = calculationSettings(project, simulation, nowcastDate);
	inputs->modelRevision = model.preparedDataRevision();
	inputs->projectionRevision = projection.preparedDataRevision();
	return inputs;
}

bool SimulationInputs::reusableWith(SimulationInputs const& other) const
{
	// A failed/non-strict seat import cannot establish that the retained work
	// matches the configured seats, even if it also failed on the previous run.
	return seatContents && other.seatContents && seatContents == other.seatContents &&
		settings == other.settings && paths == other.paths && contents == other.contents &&
		modelRevision == other.modelRevision && projectionRevision == other.projectionRevision;
}

std::string const& SimulationInputs::path(File file) const
{
	return paths.at(file);
}

std::istringstream SimulationInputs::open(std::string const& path) const
{
	auto const& stored = contents.at(path);
	if (!stored) {
		std::istringstream missing;
		missing.setstate(std::ios::failbit);
		return missing;
	}
	std::string text = *stored;
#ifdef _WIN32
	// Match the old Windows text-mode ifstream behaviour. Compare raw bytes for
	// invalidation, but do not introduce carriage returns into parsed CSV fields.
	std::string translated;
	translated.reserve(text.size());
	for (std::size_t i = 0; i < text.size(); ++i) {
		if (text[i] == '\x1a') break;
		if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
		translated.push_back(text[i]);
	}
	text = std::move(translated);
#endif
	return std::istringstream(std::move(text));
}
