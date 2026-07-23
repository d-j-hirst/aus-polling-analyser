#include "ForecastSpecificationExport.h"

#include "PollingProject.h"
#include "json.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace {
	using Diagnostic = ForecastSpecificationDiagnostic;
	using Json = nlohmann::json;

	void addError(std::vector<Diagnostic>& diagnostics,
		std::string location, std::string message)
	{
		diagnostics.push_back({ Diagnostic::Severity::Error,
			std::move(location), std::move(message) });
	}

	bool hasErrors(std::vector<Diagnostic> const& diagnostics)
	{
		return std::any_of(diagnostics.begin(), diagnostics.end(),
			[](auto const& diagnostic) {
				return diagnostic.severity == Diagnostic::Severity::Error;
			});
	}

	std::string trim(std::string_view value)
	{
		auto first = value.begin();
		while (first != value.end() &&
			std::isspace(static_cast<unsigned char>(*first))) ++first;
		auto last = value.end();
		while (last != first &&
			std::isspace(static_cast<unsigned char>(*std::prev(last)))) --last;
		return std::string(first, last);
	}

	std::vector<std::string> splitCommaSeparated(std::string_view value)
	{
		std::vector<std::string> parts;
		std::size_t begin = 0;
		while (begin <= value.size()) {
			auto const end = value.find(',', begin);
			parts.push_back(trim(value.substr(begin,
				end == std::string_view::npos ? value.size() - begin : end - begin)));
			if (end == std::string_view::npos) break;
			begin = end + 1;
		}
		return parts;
	}

	std::optional<float> parseFloat(std::string const& text)
	{
		if (text.empty() || std::isspace(static_cast<unsigned char>(text.front())) ||
			std::isspace(static_cast<unsigned char>(text.back()))) return std::nullopt;
		char* end = nullptr;
		errno = 0;
		float const value = std::strtof(text.c_str(), &end);
		if (end != text.c_str() + text.size() || errno == ERANGE ||
			!std::isfinite(value)) return std::nullopt;
		return value;
	}

	std::string identifierBase(std::string_view value, std::string_view fallback)
	{
		std::string result;
		bool separatorPending = false;
		for (unsigned char character : value) {
			if (std::isalnum(character)) {
				if (separatorPending && !result.empty()) result.push_back('-');
				result.push_back(static_cast<char>(std::tolower(character)));
				separatorPending = false;
			}
			else if (!result.empty()) {
				separatorPending = true;
			}
		}
		if (result.empty()) result = std::string(fallback);
		if (!std::isalpha(static_cast<unsigned char>(result.front()))) {
			result = std::string(fallback) + "-" + result;
		}
		return result;
	}

	std::string uniqueIdentifier(std::string_view value,
		std::string_view fallback, std::set<std::string>& used)
	{
		auto const base = identifierBase(value, fallback);
		auto candidate = base;
		for (int suffix = 2; !used.insert(candidate).second; ++suffix) {
			candidate = base + "-" + std::to_string(suffix);
		}
		return candidate;
	}

	std::string csvField(std::string const& value)
	{
		if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
		std::string escaped = "\"";
		for (char character : value) {
			if (character == '\"') escaped += "\"\"";
			else escaped += character;
		}
		escaped += '\"';
		return escaped;
	}

	template <typename Value>
	void writeCsvValue(std::ostream& output, Value const& value)
	{
		output << value;
	}

	void writeCsvValue(std::ostream& output, std::string const& value)
	{
		output << csvField(value);
	}

	template <typename... Values>
	void writeCsvRow(std::ostream& output, Values const&... values)
	{
		bool first = true;
		auto writeValue = [&](auto const& value) {
			if (!first) output << ',';
			first = false;
			writeCsvValue(output, value);
		};
		(writeValue(values), ...);
		output << '\n';
	}

	std::optional<std::ofstream> openOutput(
		std::filesystem::path const& path, std::vector<Diagnostic>& diagnostics)
	{
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!output) {
			addError(diagnostics, path.generic_string(), "could not create output file");
			return std::nullopt;
		}
		output << std::setprecision(std::numeric_limits<float>::max_digits10);
		return std::optional<std::ofstream>(std::move(output));
	}

	std::string relationType(ForecastSpecification::PartyRelationType type)
	{
		switch (type) {
		case ForecastSpecification::PartyRelationType::Supports: return "supports";
		case ForecastSpecification::PartyRelationType::Coalition: return "coalition";
		case ForecastSpecification::PartyRelationType::IsPartOf: return "is_part_of";
		}
		return {};
	}

	std::string ideology(ForecastSpecification::Ideology value)
	{
		switch (value) {
		case ForecastSpecification::Ideology::StrongLeft: return "strong_left";
		case ForecastSpecification::Ideology::ModerateLeft: return "moderate_left";
		case ForecastSpecification::Ideology::Centrist: return "centrist";
		case ForecastSpecification::Ideology::ModerateRight: return "moderate_right";
		case ForecastSpecification::Ideology::StrongRight: return "strong_right";
		}
		return {};

	}

	std::string consistency(ForecastSpecification::PreferenceConsistency value)
	{
		switch (value) {
		case ForecastSpecification::PreferenceConsistency::Low: return "low";
		case ForecastSpecification::PreferenceConsistency::Moderate: return "moderate";
		case ForecastSpecification::PreferenceConsistency::High: return "high";
		}
		return {};
	}

	std::string simulationMode(ForecastSpecification::SimulationMode mode)
	{
		switch (mode) {
		case ForecastSpecification::SimulationMode::Projection: return "projection";
		case ForecastSpecification::SimulationMode::LiveManual: return "live_manual";
		case ForecastSpecification::SimulationMode::LiveAutomatic: return "live_automatic";
		}
		return {};
	}

	std::string reportMode(ForecastSpecification::ReportMode mode)
	{
		switch (mode) {
		case ForecastSpecification::ReportMode::RegularForecast: return "regular_forecast";
		case ForecastSpecification::ReportMode::LiveForecast: return "live_forecast";
		case ForecastSpecification::ReportMode::Nowcast: return "nowcast";
		}
		return {};
	}

	template <typename Value>
	void mergeElectionSetting(std::optional<Value>& merged,
		Value const& value, bool present, std::string const& field,
		std::string const& simulationName,
		std::vector<Diagnostic>& diagnostics)
	{
		if (!present) return;
		if (!merged) {
			merged = value;
			return;
		}
		if (*merged != value) {
			addError(diagnostics, "simulation " + simulationName,
				field + " conflicts with another non-empty simulation value");
		}
	}

	ForecastSpecification buildSpecification(PollingProject const& project,
		std::vector<Diagnostic>& diagnostics)
	{
		ForecastSpecification specification;
		specification.electionName = project.getElectionName();
		specification.files = { "parties.csv", "party-official-codes.csv",
			"nonclassic-preferences.csv", "regions.csv" };

		if (project.models().count() > 0) {
			specification.electionCode = project.models().viewByIndex(0).getTermCode();
		}
		specification.id = identifierBase(
			"forecast-" + specification.electionCode, "forecast");
		specification.dataSources.seats =
			"analysis/seats/" + specification.electionCode + ".txt";

		std::set<std::string> usedPartyIds;
		std::map<Party::Id, std::string> partyIds;
		std::map<std::string, std::string> regionIdsByName;
		std::set<std::string> usedRegionIds;
		for (int index = 0; index < project.regions().count(); ++index) {
			auto const& source = project.regions().viewByIndex(index);
			auto id = uniqueIdentifier(source.name, "region", usedRegionIds);
			regionIdsByName[source.name] = id;
			ForecastSpecification::Region region;
			region.index = index;
			region.id = std::move(id);
			region.name = source.name;
			region.population = source.population;
			region.analysisCode = source.analysisCode;
			region.previousElectionTpp = source.lastElection2pp;
			specification.regions.push_back(std::move(region));
		}

		for (int index = 0; index < project.parties().count(); ++index) {
			auto const id = project.parties().indexToId(index);
			auto const& source = project.parties().view(id);
			partyIds[id] = uniqueIdentifier(source.name, "party", usedPartyIds);
		}
		if (project.parties().count() >= 2) {
			specification.partySettings.majorPartyOneId =
				partyIds.at(project.parties().indexToId(0));
			specification.partySettings.majorPartyTwoId =
				partyIds.at(project.parties().indexToId(1));
		}

		for (int index = 0; index < project.parties().count(); ++index) {
			auto const sourceId = project.parties().indexToId(index);
			auto const& source = project.parties().view(sourceId);
			ForecastSpecification::Party party;
			party.index = index;
			party.id = partyIds.at(sourceId);
			party.name = source.name;
			party.abbreviation = source.abbreviation;
			party.seatTarget = source.seatTarget;
			if (!source.homeRegion.empty()) {
				auto const found = regionIdsByName.find(source.homeRegion);
				if (found == regionIdsByName.end()) {
					addError(diagnostics, "party " + source.name,
						"unknown home region " + source.homeRegion);
				}
				else party.homeRegionId = found->second;
			}
			if (source.ideology < 0 || source.ideology > 4) {
				addError(diagnostics, "party " + source.name, "invalid ideology");
			}
			else party.ideology = static_cast<ForecastSpecification::Ideology>(source.ideology);
			if (source.consistency < 0 || source.consistency > 2) {
				addError(diagnostics, "party " + source.name,
					"invalid preference consistency");
			}
			else party.preferenceConsistency =
				static_cast<ForecastSpecification::PreferenceConsistency>(source.consistency);

			if (source.relationType != Party::RelationType::None &&
				source.relationType != Party::RelationType::IsMajor) {
				auto const target = partyIds.find(source.relationTarget);
				if (target == partyIds.end()) {
					addError(diagnostics, "party " + source.name,
						"relation refers to an unknown party");
				}
				else {
					ForecastSpecification::PartyRelation relation;
					relation.targetPartyId = target->second;
					switch (source.relationType) {
					case Party::RelationType::Supports:
						relation.type = ForecastSpecification::PartyRelationType::Supports;
						break;
					case Party::RelationType::Coalition:
						relation.type = ForecastSpecification::PartyRelationType::Coalition;
						break;
					case Party::RelationType::IsPartOf:
						relation.type = ForecastSpecification::PartyRelationType::IsPartOf;
						break;
					default:
						addError(diagnostics, "party " + source.name,
							"unknown relation type");
						break;
					}
					party.relation = std::move(relation);
				}
			}

			for (auto const& officialCode : source.officialCodes) {
				specification.partyOfficialCodes.push_back(
					{ party.id, officialCode });
			}
			for (auto const& preference : source.ncPreferenceFlow) {
				specification.nonClassicPreferences.push_back({ party.id,
					preference.first.first, preference.first.second,
					preference.second });
			}
			specification.parties.push_back(std::move(party));
		}

		std::set<std::string> usedModelIds;
		std::map<ModelCollection::Id, std::string> modelIds;
		for (auto const& [id, source] : project.models()) {
			ForecastSpecification::Model model;
			model.id = uniqueIdentifier(source.getName(), "model", usedModelIds);
			modelIds[id] = model.id;
			model.name = source.getName();
			model.termCode = source.getTermCode();
			auto const codes = splitCommaSeparated(source.getPartyCodes());
			auto const deviations = splitCommaSeparated(source.getPreferenceDeviation());
			auto const samples = splitCommaSeparated(source.getPreferenceSamples());
			if (codes.size() != deviations.size() || codes.size() != samples.size()) {
				addError(diagnostics, "model " + source.getName(),
					"party codes, preference deviations, and preference samples have different lengths");
			}
			auto const count = std::min({ codes.size(), deviations.size(), samples.size() });
			for (std::size_t index = 0; index < count; ++index) {
				auto deviation = parseFloat(deviations[index]);
				auto sampleCount = parseFloat(samples[index]);
				if (!deviation || !sampleCount) {
					addError(diagnostics, "model " + source.getName(),
						"invalid preference parameter at party position " +
						std::to_string(index));
					continue;
				}
				model.parties.push_back({ codes[index], *deviation, *sampleCount });
			}
			specification.models.push_back(std::move(model));
		}

		std::set<std::string> usedProjectionIds;
		std::map<Projection::Id, std::string> projectionIds;
		for (auto const& [id, source] : project.projections()) {
			auto const& settings = source.getSettings();
			ForecastSpecification::Projection projection;
			projection.id = uniqueIdentifier(settings.name, "projection", usedProjectionIds);
			projectionIds[id] = projection.id;
			projection.name = settings.name;
			auto const model = modelIds.find(settings.baseModel);
			if (model == modelIds.end()) {
				addError(diagnostics, "projection " + settings.name,
					"base model does not exist");
			}
			else projection.baseModelId = model->second;
			projection.numIterations = settings.numIterations;
			projection.endDate = settings.endDate;
			for (auto const& [dateText, weight] : settings.possibleDates) {
				auto date = Date::parseIso(dateText);
				if (!date) {
					addError(diagnostics, "projection " + settings.name,
						"invalid possible date " + dateText);
					continue;
				}
				projection.possibleDates.push_back({ *date, weight });
			}
			specification.projections.push_back(std::move(projection));
		}

		std::optional<std::vector<std::string>> previousTermCodes;
		std::optional<float> previousElectionTpp;
		std::optional<Date> federalElectionDate;
		std::optional<std::string> previousResultsUrl;
		std::optional<std::string> preloadUrl;
		std::optional<std::string> currentTestUrl;
		std::optional<std::string> currentRealUrl;
		for (auto const& [id, simulation] : project.simulations()) {
			auto const& settings = simulation.getSettings();
			mergeElectionSetting(previousTermCodes, settings.prevTermCodes,
				!settings.prevTermCodes.empty(), "previous term codes",
				settings.name, diagnostics);
			if (!std::isfinite(settings.prevElection2pp)) {
				addError(diagnostics, "simulation " + settings.name,
					"previous-election TPP is not finite");
			}
			else {
				// The legacy float has no optional state; zero is its only usable
				// representation of an omitted previous-election TPP.
				mergeElectionSetting(previousElectionTpp, settings.prevElection2pp,
					settings.prevElection2pp != 0.0f, "previous-election TPP",
					settings.name, diagnostics);
			}
			mergeElectionSetting(federalElectionDate, settings.fedElectionDate,
				settings.fedElectionDate.isValid(), "federal election date",
				settings.name, diagnostics);
			mergeElectionSetting(previousResultsUrl, settings.previousResultsUrl,
				!settings.previousResultsUrl.empty(), "previous-results URL",
				settings.name, diagnostics);
			mergeElectionSetting(preloadUrl, settings.preloadUrl,
				!settings.preloadUrl.empty(), "preload URL",
				settings.name, diagnostics);
			mergeElectionSetting(currentTestUrl, settings.currentTestUrl,
				!settings.currentTestUrl.empty(), "current test URL",
				settings.name, diagnostics);
			mergeElectionSetting(currentRealUrl, settings.currentRealUrl,
				!settings.currentRealUrl.empty(), "current real URL",
				settings.name, diagnostics);
		}
		if (!previousTermCodes) {
			addError(diagnostics, "simulations",
				"no simulation supplies previous term codes");
		}
		else specification.electionSettings.previousTermCodes = *previousTermCodes;
		if (!previousElectionTpp) {
			addError(diagnostics, "simulations",
				"no simulation supplies a previous-election TPP");
		}
		else specification.electionSettings.previousElectionTpp = *previousElectionTpp;
		if (federalElectionDate) {
			specification.electionSettings.federalElectionDate = *federalElectionDate;
		}
		if (previousResultsUrl || preloadUrl || currentTestUrl || currentRealUrl) {
			specification.electionSettings.liveSources =
				ForecastSpecification::LiveSources{
					previousResultsUrl.value_or(""), preloadUrl.value_or(""),
					currentTestUrl.value_or(""), currentRealUrl.value_or("") };
		}

		std::set<std::string> usedSimulationIds;
		for (auto const& [id, source] : project.simulations()) {
			auto const& settings = source.getSettings();
			ForecastSpecification::Simulation simulation;
			simulation.id = uniqueIdentifier(settings.name, "simulation", usedSimulationIds);
			simulation.name = settings.name;
			auto const projection = projectionIds.find(settings.baseProjection);
			if (projection == projectionIds.end()) {
				addError(diagnostics, "simulation " + settings.name,
					"base projection does not exist");
			}
			else simulation.baseProjectionId = projection->second;
			simulation.numIterations = settings.numIterations;
			simulation.mode = static_cast<ForecastSpecification::SimulationMode>(settings.live);
			simulation.reportMode = static_cast<ForecastSpecification::ReportMode>(settings.reportMode);
			specification.simulations.push_back(std::move(simulation));
		}
		return specification;
	}

	Json manifestJson(ForecastSpecification const& specification)
	{
		Json root = {
			{ "schema_version", specification.schemaVersion },
			{ "id", specification.id },
			{ "election_name", specification.electionName },
			{ "election_code", specification.electionCode },
			{ "files", {
				{ "parties", specification.files.parties },
				{ "party_official_codes", specification.files.partyOfficialCodes },
				{ "nonclassic_preferences", specification.files.nonClassicPreferences },
				{ "regions", specification.files.regions } } },
			{ "data_sources", { { "seats", specification.dataSources.seats } } },
			{ "party_settings", {
				{ "major_party_one_id", specification.partySettings.majorPartyOneId },
				{ "major_party_two_id", specification.partySettings.majorPartyTwoId } } },
			{ "election_settings", {
				{ "previous_term_codes", specification.electionSettings.previousTermCodes },
				{ "previous_election_tpp", specification.electionSettings.previousElectionTpp } } }
		};
		if (specification.electionSettings.federalElectionDate) {
			root["election_settings"]["federal_election_date"] =
				specification.electionSettings.federalElectionDate->formatIso();
		}
		if (specification.electionSettings.liveSources) {
			auto const& sources = *specification.electionSettings.liveSources;
			root["election_settings"]["live_sources"] = {
				{ "previous_results_url", sources.previousResultsUrl },
				{ "preload_url", sources.preloadUrl },
				{ "current_test_url", sources.currentTestUrl },
				{ "current_real_url", sources.currentRealUrl } };
		}
		root["models"] = Json::array();
		for (auto const& model : specification.models) {
			Json item = { { "id", model.id }, { "name", model.name },
				{ "term_code", model.termCode }, { "parties", Json::array() } };
			for (auto const& party : model.parties) {
				item["parties"].push_back({ { "code", party.code },
					{ "preference_deviation", party.preferenceDeviation },
					{ "preference_samples", party.preferenceSamples } });
			}
			root["models"].push_back(std::move(item));
		}
		root["projections"] = Json::array();
		for (auto const& projection : specification.projections) {
			Json item = { { "id", projection.id }, { "name", projection.name },
				{ "base_model_id", projection.baseModelId },
				{ "num_iterations", projection.numIterations },
				{ "end_date", projection.endDate.formatIso() },
				{ "possible_dates", Json::array() } };
			for (auto const& date : projection.possibleDates) {
				item["possible_dates"].push_back({
					{ "date", date.date.formatIso() }, { "weight", date.weight } });
			}
			root["projections"].push_back(std::move(item));
		}
		root["simulations"] = Json::array();
		for (auto const& simulation : specification.simulations) {
			Json item = { { "id", simulation.id }, { "name", simulation.name },
				{ "base_projection_id", simulation.baseProjectionId },
				{ "num_iterations", simulation.numIterations },
				{ "mode", simulationMode(simulation.mode) },
				{ "report_mode", reportMode(simulation.reportMode) } };
			root["simulations"].push_back(std::move(item));
		}
		return root;
	}

	void writePackage(ForecastSpecification const& specification,
		std::filesystem::path const& directory,
		std::vector<Diagnostic>& diagnostics)
	{
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		if (error) {
			addError(diagnostics, directory.generic_string(),
				"could not create package directory: " + error.message());
			return;
		}

		if (auto output = openOutput(directory / "forecast.json", diagnostics)) {
			*output << manifestJson(specification).dump(2) << '\n';
		}
		if (auto output = openOutput(directory / specification.files.parties, diagnostics)) {
			writeCsvRow(*output, "index", "id", "name", "abbreviation",
				"home_region_id",
				"seat_target", "relation_type", "relation_target_party_id",
				"ideology", "preference_consistency");
			for (auto const& party : specification.parties) {
				writeCsvRow(*output, party.index, party.id, party.name,
					party.abbreviation, party.homeRegionId.value_or(""), party.seatTarget,
					party.relation ? relationType(party.relation->type) : "",
					party.relation ? party.relation->targetPartyId : "",
					ideology(party.ideology), consistency(party.preferenceConsistency));
			}
		}
		if (auto output = openOutput(directory / specification.files.partyOfficialCodes, diagnostics)) {
			writeCsvRow(*output, "party_id", "official_code");
			for (auto const& code : specification.partyOfficialCodes) {
				writeCsvRow(*output, code.partyId, code.officialCode);
			}
		}
		if (auto output = openOutput(directory / specification.files.nonClassicPreferences, diagnostics)) {
			writeCsvRow(*output, "source_party_id", "first_target_code",
				"second_target_code", "preference_to_first");
			for (auto const& preference : specification.nonClassicPreferences) {
				writeCsvRow(*output, preference.sourcePartyId,
					preference.firstTargetCode, preference.secondTargetCode,
					preference.preferenceToFirst);
			}
		}
		if (auto output = openOutput(directory / specification.files.regions, diagnostics)) {
			writeCsvRow(*output, "index", "id", "name", "population",
				"analysis_code", "previous_election_tpp");
			for (auto const& region : specification.regions) {
				writeCsvRow(*output, region.index, region.id, region.name,
					region.population, region.analysisCode,
					region.previousElectionTpp);
			}
		}
	}

	std::vector<std::filesystem::path> packageFiles(
		ForecastSpecification const& specification)
	{
		return {
			"forecast.json",
			specification.files.parties,
			specification.files.partyOfficialCodes,
			specification.files.nonClassicPreferences,
			specification.files.regions,
		};
	}

	void publishPackage(ForecastSpecification const& specification,
		std::filesystem::path const& stagingDirectory,
		std::filesystem::path const& packageDirectory,
		std::vector<Diagnostic>& diagnostics)
	{
		std::error_code error;
		std::filesystem::create_directories(packageDirectory, error);
		if (error) {
			addError(diagnostics, packageDirectory.generic_string(),
				"could not create package directory: " + error.message());
			return;
		}

		for (auto const& relativePath : packageFiles(specification)) {
			auto const destination = packageDirectory / relativePath;
			std::filesystem::create_directories(destination.parent_path(), error);
			if (error) {
				addError(diagnostics, destination.generic_string(),
					"could not create parent directory: " + error.message());
				return;
			}
			std::filesystem::copy_file(stagingDirectory / relativePath,
				destination, std::filesystem::copy_options::overwrite_existing,
				error);
			if (error) {
				addError(diagnostics, destination.generic_string(),
					"could not publish file: " + error.message());
				return;
			}
		}
	}
}

bool ForecastSpecificationExportResult::valid() const
{
	return !hasErrors(diagnostics);
}

std::string ForecastSpecificationExportResult::errorMessage() const
{
	std::string message;
	for (auto const& diagnostic : diagnostics) {
		if (diagnostic.severity != Diagnostic::Severity::Error) continue;
		if (!message.empty()) message += '\n';
		message += diagnostic.location + ": " + diagnostic.message;
	}
	return message;
}

ForecastSpecificationExportResult exportForecastSpecification(
	PollingProject const& project,
	std::filesystem::path const& packageDirectory)
{
	ForecastSpecificationExportResult result;
	result.specification = buildSpecification(project, result.diagnostics);
	if (hasErrors(result.diagnostics)) return result;

	// Validate the complete DTO before creating package files. Package-relative
	// file checks are deferred because the CSV tables exist only in memory here.
	auto validation = validateForecastSpecification(result.specification,
		packageDirectory, project.paths().root(), false);
	result.diagnostics.insert(result.diagnostics.end(),
		std::make_move_iterator(validation.begin()),
		std::make_move_iterator(validation.end()));
	if (hasErrors(result.diagnostics)) return result;

	auto const stagingDirectory = std::filesystem::temp_directory_path() /
		("polling-analyser-forecast-export-" + std::to_string(
			std::chrono::steady_clock::now().time_since_epoch().count()));
	writePackage(result.specification, stagingDirectory, result.diagnostics);
	if (hasErrors(result.diagnostics)) {
		std::error_code ignored;
		std::filesystem::remove_all(stagingDirectory, ignored);
		return result;
	}

	// Reload the staged package through the public path. Serialization mistakes
	// therefore cannot leave invalid files in the selected destination.
	auto verified = loadForecastSpecification(
		stagingDirectory / "forecast.json", project.paths().root());
	result.diagnostics.insert(result.diagnostics.end(),
		verified.diagnostics.begin(), verified.diagnostics.end());
	if (!hasErrors(result.diagnostics)) {
		publishPackage(result.specification, stagingDirectory,
			packageDirectory, result.diagnostics);
	}
	std::error_code ignored;
	std::filesystem::remove_all(stagingDirectory, ignored);
	return result;
}
