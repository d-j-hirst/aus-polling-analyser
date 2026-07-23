#include "ForecastSpecificationIO.h"

#include "json.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace {
	using Diagnostic = ForecastSpecificationDiagnostic;
	using Json = nlohmann::json;

	struct CsvRow {
		std::size_t line = 1;
		std::vector<std::string> fields;
	};

	void addError(std::vector<Diagnostic>& diagnostics,
		std::string location, std::string message)
	{
		diagnostics.push_back({
			Diagnostic::Severity::Error,
			std::move(location), std::move(message) });
	}

	std::string pathString(std::filesystem::path const& path)
	{
		return path.lexically_normal().generic_string();
	}

	std::string csvLocation(std::filesystem::path const& path,
		std::size_t line, std::string_view column = {})
	{
		std::string location = pathString(path) + ":" + std::to_string(line);
		if (!column.empty()) location += ":" + std::string(column);
		return location;
	}

	bool checkObject(Json const& value, std::string const& location,
		std::initializer_list<std::string_view> allowed,
		std::initializer_list<std::string_view> required,
		std::vector<Diagnostic>& diagnostics)
	{
		if (!value.is_object()) {
			addError(diagnostics, location, "expected an object");
			return false;
		}
		for (auto const& item : value.items()) {
			bool const known = std::any_of(allowed.begin(), allowed.end(),
				[&](std::string_view key) { return key == item.key(); });
			if (!known) {
				addError(diagnostics, location + "." + item.key(),
					"unknown field");
			}
		}
		for (std::string_view key : required) {
			if (value.find(std::string(key)) == value.end()) {
				addError(diagnostics, location + "." + std::string(key),
					"missing required field");
			}
		}
		return true;
	}

	Json const* findMember(Json const& object, std::string_view key)
	{
		if (!object.is_object()) return nullptr;
		auto const found = object.find(std::string(key));
		return found == object.end() ? nullptr : &*found;
	}

	std::optional<std::string> readJsonString(Json const& object,
		std::string_view key, std::string const& location,
		std::vector<Diagnostic>& diagnostics)
	{
		auto const* value = findMember(object, key);
		if (!value) return std::nullopt;
		if (!value->is_string()) {
			addError(diagnostics, location + "." + std::string(key),
				"expected a string");
			return std::nullopt;
		}
		return value->get<std::string>();
	}

	std::optional<float> readJsonFloat(Json const& object,
		std::string_view key, std::string const& location,
		std::vector<Diagnostic>& diagnostics)
	{
		auto const* value = findMember(object, key);
		if (!value) return std::nullopt;
		if (!value->is_number()) {
			addError(diagnostics, location + "." + std::string(key),
				"expected a number");
			return std::nullopt;
		}
		try {
			double const number = value->get<double>();
			if (!std::isfinite(number) ||
				std::abs(number) > std::numeric_limits<float>::max()) {
				addError(diagnostics, location + "." + std::string(key),
					"number is not a finite float");
				return std::nullopt;
			}
			return static_cast<float>(number);
		}
		catch (Json::exception const&) {
			addError(diagnostics, location + "." + std::string(key),
				"number is out of range");
			return std::nullopt;
		}
	}

	std::optional<int> readJsonInt(Json const& object,
		std::string_view key, std::string const& location,
		std::vector<Diagnostic>& diagnostics)
	{
		auto const* value = findMember(object, key);
		if (!value) return std::nullopt;
		if (!value->is_number_integer()) {
			addError(diagnostics, location + "." + std::string(key),
				"expected an integer");
			return std::nullopt;
		}
		try {
			auto const number = value->get<long long>();
			if (number < std::numeric_limits<int>::min() ||
				number > std::numeric_limits<int>::max()) {
				addError(diagnostics, location + "." + std::string(key),
					"integer is out of range");
				return std::nullopt;
			}
			return static_cast<int>(number);
		}
		catch (Json::exception const&) {
			addError(diagnostics, location + "." + std::string(key),
				"integer is out of range");
			return std::nullopt;
		}
	}

	std::optional<Date> readJsonDate(Json const& object,
		std::string_view key, std::string const& location,
		std::vector<Diagnostic>& diagnostics)
	{
		auto const text = readJsonString(object, key, location, diagnostics);
		if (!text) return std::nullopt;
		auto const date = Date::parseIso(*text);
		if (!date) {
			addError(diagnostics, location + "." + std::string(key),
				"expected a valid ISO date (YYYY-MM-DD)");
		}
		return date;
	}

	std::optional<std::vector<CsvRow>> readCsv(
		std::filesystem::path const& path,
		std::vector<Diagnostic>& diagnostics)
	{
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			std::error_code error;
			if (std::filesystem::exists(path, error)) {
				addError(diagnostics, pathString(path), "could not read file");
			}
			return std::nullopt;
		}
		std::ostringstream buffer;
		buffer << input.rdbuf();
		std::string const text = buffer.str();

		std::vector<CsvRow> rows;
		CsvRow row;
		std::string field;
		std::size_t line = 1;
		row.line = line;
		bool inQuotes = false;
		bool afterQuote = false;
		bool fieldStarted = false;

		auto finishField = [&]() {
			row.fields.push_back(std::move(field));
			field.clear();
			fieldStarted = false;
			afterQuote = false;
		};
		auto finishRow = [&]() {
			finishField();
			rows.push_back(std::move(row));
			row = CsvRow();
			row.line = line + 1;
		};

		for (std::size_t index = 0; index < text.size(); ++index) {
			char const character = text[index];
			if (inQuotes) {
				if (character == '"') {
					if (index + 1 < text.size() && text[index + 1] == '"') {
						field.push_back('"');
						++index;
					}
					else {
						inQuotes = false;
						afterQuote = true;
					}
				}
				else if (character == '\r' || character == '\n') {
					if (character == '\r') {
						if (index + 1 >= text.size() || text[index + 1] != '\n') {
							addError(diagnostics, csvLocation(path, line),
								"bare carriage return in quoted field");
							return std::nullopt;
						}
						++index;
					}
					field.push_back('\n');
					++line;
				}
				else {
					field.push_back(character);
				}
				continue;
			}

			if (afterQuote) {
				if (character == ',') {
					finishField();
					continue;
				}
				if (character != '\r' && character != '\n') {
					addError(diagnostics, csvLocation(path, line),
						"unexpected character after closing quote");
					return std::nullopt;
				}
			}

			if (character == ',') {
				finishField();
			}
			else if (character == '"') {
				if (fieldStarted) {
					addError(diagnostics, csvLocation(path, line),
						"quote in unquoted field");
					return std::nullopt;
				}
				fieldStarted = true;
				inQuotes = true;
			}
			else if (character == '\r' || character == '\n') {
				if (character == '\r') {
					if (index + 1 >= text.size() || text[index + 1] != '\n') {
						addError(diagnostics, csvLocation(path, line),
							"bare carriage return");
						return std::nullopt;
					}
					++index;
				}
				finishRow();
				++line;
				row.line = line;
			}
			else {
				fieldStarted = true;
				field.push_back(character);
			}
		}

		if (inQuotes) {
			addError(diagnostics, csvLocation(path, line),
				"unterminated quoted field");
			return std::nullopt;
		}
		if (fieldStarted || afterQuote || !field.empty() || !row.fields.empty()) {
			finishField();
			rows.push_back(std::move(row));
		}
		if (rows.empty()) {
			addError(diagnostics, pathString(path), "CSV file is empty");
			return std::nullopt;
		}
		if (!rows[0].fields.empty() && rows[0].fields[0].starts_with("\xEF\xBB\xBF")) {
			rows[0].fields[0].erase(0, 3);
		}
		return rows;
	}

	std::optional<std::vector<CsvRow>> readTable(
		std::filesystem::path const& path,
		std::vector<std::string> const& expectedHeader,
		std::vector<Diagnostic>& diagnostics)
	{
		auto rows = readCsv(path, diagnostics);
		if (!rows) return std::nullopt;
		if ((*rows)[0].fields != expectedHeader) {
			addError(diagnostics, csvLocation(path, (*rows)[0].line),
				"header does not match forecast schema version 1");
			return std::nullopt;
		}
		rows->erase(rows->begin());
		for (auto const& row : *rows) {
			if (row.fields.size() != expectedHeader.size()) {
				addError(diagnostics, csvLocation(path, row.line),
					"expected " + std::to_string(expectedHeader.size()) +
					" columns, found " + std::to_string(row.fields.size()));
			}
		}
		return rows;
	}

	template <typename Number>
	std::optional<Number> parseCsvNumber(std::string const& text,
		std::filesystem::path const& path, CsvRow const& row,
		std::string_view column, std::vector<Diagnostic>& diagnostics)
	{
		Number value{};
		bool valid = !text.empty() && std::none_of(text.begin(), text.end(),
			[](unsigned char character) { return std::isspace(character); });
		if constexpr (std::is_floating_point_v<Number>) {
			char* end = nullptr;
			errno = 0;
			value = std::strtof(text.c_str(), &end);
			valid = valid && errno != ERANGE && end == text.c_str() + text.size() &&
				std::isfinite(value);
		}
		else {
			auto const parsed = std::from_chars(
				text.data(), text.data() + text.size(), value);
			valid = valid && parsed.ec == std::errc() &&
				parsed.ptr == text.data() + text.size();
		}
		if (!valid) {
			addError(diagnostics, csvLocation(path, row.line, column),
				"invalid numeric value");
			return std::nullopt;
		}
		return value;
	}

	std::filesystem::path packagePath(
		std::filesystem::path const& manifestDirectory,
		std::string const& configuredPath)
	{
		return (manifestDirectory / configuredPath).lexically_normal();
	}

	void readManifest(Json const& root, ForecastSpecification& specification,
		std::vector<Diagnostic>& diagnostics)
	{
		std::string const location = "forecast.json";
		if (!checkObject(root, location,
			{ "schema_version", "id", "election_name",
			  "election_code", "files", "data_sources", "party_settings", "election_settings",
			  "models", "projections", "simulations" },
			{ "schema_version", "id", "election_name",
			  "election_code", "files", "data_sources", "party_settings", "election_settings",
			  "models", "projections", "simulations" }, diagnostics)) return;

		if (auto value = readJsonInt(root, "schema_version", location, diagnostics))
			specification.schemaVersion = *value;
		if (auto value = readJsonString(root, "id", location, diagnostics))
			specification.id = *value;
		if (auto value = readJsonString(root, "election_name", location, diagnostics))
			specification.electionName = *value;
		if (auto value = readJsonString(root, "election_code", location, diagnostics))
			specification.electionCode = *value;

		if (auto const* files = findMember(root, "files")) {
			std::string const itemLocation = location + ".files";
			if (checkObject(*files, itemLocation,
				{ "parties", "party_official_codes",
				  "nonclassic_preferences", "regions" },
				{ "parties", "party_official_codes",
				  "nonclassic_preferences", "regions" }, diagnostics)) {
				if (auto value = readJsonString(*files, "parties", itemLocation, diagnostics))
					specification.files.parties = *value;
				if (auto value = readJsonString(*files, "party_official_codes", itemLocation, diagnostics))
					specification.files.partyOfficialCodes = *value;
				if (auto value = readJsonString(*files, "nonclassic_preferences", itemLocation, diagnostics))
					specification.files.nonClassicPreferences = *value;
				if (auto value = readJsonString(*files, "regions", itemLocation, diagnostics))
					specification.files.regions = *value;
			}
		}

		if (auto const* sources = findMember(root, "data_sources")) {
			std::string const itemLocation = location + ".data_sources";
			if (checkObject(*sources, itemLocation, { "seats" }, { "seats" }, diagnostics)) {
				if (auto value = readJsonString(*sources, "seats", itemLocation, diagnostics))
					specification.dataSources.seats = *value;
			}
		}

		if (auto const* settings = findMember(root, "party_settings")) {
			std::string const itemLocation = location + ".party_settings";
			if (checkObject(*settings, itemLocation,
				{ "major_party_one_id", "major_party_two_id" },
				{ "major_party_one_id", "major_party_two_id" }, diagnostics)) {
				if (auto value = readJsonString(*settings, "major_party_one_id", itemLocation, diagnostics))
					specification.partySettings.majorPartyOneId = *value;
				if (auto value = readJsonString(*settings, "major_party_two_id", itemLocation, diagnostics))
					specification.partySettings.majorPartyTwoId = *value;
			}
		}

		if (auto const* settings = findMember(root, "election_settings")) {
			std::string const itemLocation = location + ".election_settings";
			if (checkObject(*settings, itemLocation,
				{ "previous_term_codes", "previous_election_tpp",
				  "federal_election_date", "live_sources" },
				{ "previous_term_codes", "previous_election_tpp" }, diagnostics)) {
				if (auto const* terms = findMember(*settings, "previous_term_codes")) {
					if (!terms->is_array()) addError(diagnostics, itemLocation + ".previous_term_codes", "expected an array");
					else for (std::size_t termIndex = 0; termIndex < terms->size(); ++termIndex) {
						if (!(*terms)[termIndex].is_string()) addError(diagnostics, itemLocation + ".previous_term_codes[" + std::to_string(termIndex) + "]", "expected a string");
						else specification.electionSettings.previousTermCodes.push_back((*terms)[termIndex].get<std::string>());
					}
				}
				if (auto value = readJsonFloat(*settings, "previous_election_tpp", itemLocation, diagnostics)) specification.electionSettings.previousElectionTpp = *value;
				if (findMember(*settings, "federal_election_date")) {
					if (auto value = readJsonDate(*settings, "federal_election_date", itemLocation, diagnostics)) specification.electionSettings.federalElectionDate = *value;
				}
				if (auto const* live = findMember(*settings, "live_sources")) {
					std::string const liveLocation = itemLocation + ".live_sources";
					if (checkObject(*live, liveLocation,
						{ "previous_results_url", "preload_url", "current_test_url", "current_real_url" },
						{ "previous_results_url", "preload_url", "current_test_url", "current_real_url" }, diagnostics)) {
						ForecastSpecification::LiveSources sources;
						if (auto value = readJsonString(*live, "previous_results_url", liveLocation, diagnostics)) sources.previousResultsUrl = *value;
						if (auto value = readJsonString(*live, "preload_url", liveLocation, diagnostics)) sources.preloadUrl = *value;
						if (auto value = readJsonString(*live, "current_test_url", liveLocation, diagnostics)) sources.currentTestUrl = *value;
						if (auto value = readJsonString(*live, "current_real_url", liveLocation, diagnostics)) sources.currentRealUrl = *value;
						specification.electionSettings.liveSources = std::move(sources);
					}
				}
			}
		}

		auto const* models = findMember(root, "models");
		if (models && !models->is_array()) {
			addError(diagnostics, location + ".models", "expected an array");
		}
		else if (models) {
			for (std::size_t index = 0; index < models->size(); ++index) {
				auto const& item = (*models)[index];
				std::string const itemLocation = location + ".models[" + std::to_string(index) + "]";
				if (!checkObject(item, itemLocation,
					{ "id", "name", "term_code", "parties" },
					{ "id", "name", "term_code", "parties" }, diagnostics)) continue;
				ForecastSpecification::Model model;
				if (auto value = readJsonString(item, "id", itemLocation, diagnostics)) model.id = *value;
				if (auto value = readJsonString(item, "name", itemLocation, diagnostics)) model.name = *value;
				if (auto value = readJsonString(item, "term_code", itemLocation, diagnostics)) model.termCode = *value;
				auto const* parties = findMember(item, "parties");
				if (parties && !parties->is_array()) {
					addError(diagnostics, itemLocation + ".parties", "expected an array");
				}
				else if (parties) {
					for (std::size_t partyIndex = 0; partyIndex < parties->size(); ++partyIndex) {
						auto const& partyItem = (*parties)[partyIndex];
						std::string const partyLocation = itemLocation + ".parties[" + std::to_string(partyIndex) + "]";
						if (!checkObject(partyItem, partyLocation,
							{ "code", "preference_deviation", "preference_samples" },
							{ "code", "preference_deviation", "preference_samples" }, diagnostics)) continue;
						ForecastSpecification::ModelPartyParameters parameters;
						if (auto value = readJsonString(partyItem, "code", partyLocation, diagnostics)) parameters.code = *value;
						if (auto value = readJsonFloat(partyItem, "preference_deviation", partyLocation, diagnostics)) parameters.preferenceDeviation = *value;
						if (auto value = readJsonFloat(partyItem, "preference_samples", partyLocation, diagnostics)) parameters.preferenceSamples = *value;
						model.parties.push_back(std::move(parameters));
					}
				}
				specification.models.push_back(std::move(model));
			}
		}

		auto const* projections = findMember(root, "projections");
		if (projections && !projections->is_array()) {
			addError(diagnostics, location + ".projections", "expected an array");
		}
		else if (projections) {
			for (std::size_t index = 0; index < projections->size(); ++index) {
				auto const& item = (*projections)[index];
				std::string const itemLocation = location + ".projections[" + std::to_string(index) + "]";
				if (!checkObject(item, itemLocation,
					{ "id", "name", "base_model_id", "num_iterations",
					  "end_date", "possible_dates" },
					{ "id", "name", "base_model_id", "num_iterations",
					  "end_date", "possible_dates" }, diagnostics)) continue;
				ForecastSpecification::Projection projection;
				if (auto value = readJsonString(item, "id", itemLocation, diagnostics)) projection.id = *value;
				if (auto value = readJsonString(item, "name", itemLocation, diagnostics)) projection.name = *value;
				if (auto value = readJsonString(item, "base_model_id", itemLocation, diagnostics)) projection.baseModelId = *value;
				if (auto value = readJsonInt(item, "num_iterations", itemLocation, diagnostics)) projection.numIterations = *value;
				if (auto value = readJsonDate(item, "end_date", itemLocation, diagnostics)) projection.endDate = *value;
				auto const* dates = findMember(item, "possible_dates");
				if (dates && !dates->is_array()) {
					addError(diagnostics, itemLocation + ".possible_dates", "expected an array");
				}
				else if (dates) {
					for (std::size_t dateIndex = 0; dateIndex < dates->size(); ++dateIndex) {
						auto const& dateItem = (*dates)[dateIndex];
						std::string const dateLocation = itemLocation + ".possible_dates[" + std::to_string(dateIndex) + "]";
						if (!checkObject(dateItem, dateLocation,
							{ "date", "weight" }, { "date", "weight" }, diagnostics)) continue;
						ForecastSpecification::PossibleDate possibleDate;
						if (auto value = readJsonDate(dateItem, "date", dateLocation, diagnostics)) possibleDate.date = *value;
						if (auto value = readJsonFloat(dateItem, "weight", dateLocation, diagnostics)) possibleDate.weight = *value;
						projection.possibleDates.push_back(std::move(possibleDate));
					}
				}
				specification.projections.push_back(std::move(projection));
			}
		}

		auto const* simulations = findMember(root, "simulations");
		if (simulations && !simulations->is_array()) {
			addError(diagnostics, location + ".simulations", "expected an array");
		}
		else if (simulations) {
			for (std::size_t index = 0; index < simulations->size(); ++index) {
				auto const& item = (*simulations)[index];
				std::string const itemLocation = location + ".simulations[" + std::to_string(index) + "]";
				if (!checkObject(item, itemLocation,
					{ "id", "name", "base_projection_id", "num_iterations",
					  "mode", "report_mode" },
					{ "id", "name", "base_projection_id", "num_iterations",
					  "mode", "report_mode" }, diagnostics)) continue;
				ForecastSpecification::Simulation simulation;
				if (auto value = readJsonString(item, "id", itemLocation, diagnostics)) simulation.id = *value;
				if (auto value = readJsonString(item, "name", itemLocation, diagnostics)) simulation.name = *value;
				if (auto value = readJsonString(item, "base_projection_id", itemLocation, diagnostics)) simulation.baseProjectionId = *value;
				if (auto value = readJsonInt(item, "num_iterations", itemLocation, diagnostics)) simulation.numIterations = *value;
				if (auto value = readJsonString(item, "mode", itemLocation, diagnostics)) {
					if (*value == "projection") simulation.mode = ForecastSpecification::SimulationMode::Projection;
					else if (*value == "live_manual") simulation.mode = ForecastSpecification::SimulationMode::LiveManual;
					else if (*value == "live_automatic") simulation.mode = ForecastSpecification::SimulationMode::LiveAutomatic;
					else addError(diagnostics, itemLocation + ".mode", "unknown simulation mode");
				}
				if (auto value = readJsonString(item, "report_mode", itemLocation, diagnostics)) {
					if (*value == "regular_forecast") simulation.reportMode = ForecastSpecification::ReportMode::RegularForecast;
					else if (*value == "live_forecast") simulation.reportMode = ForecastSpecification::ReportMode::LiveForecast;
					else if (*value == "nowcast") simulation.reportMode = ForecastSpecification::ReportMode::Nowcast;
					else addError(diagnostics, itemLocation + ".report_mode", "unknown report mode");
				}
				specification.simulations.push_back(std::move(simulation));
			}
		}
	}

	void readParties(ForecastSpecification& specification,
		std::filesystem::path const& path, std::vector<Diagnostic>& diagnostics)
	{
		std::vector<std::string> const header = {
			"index", "id", "name", "abbreviation", "home_region_id",
			"seat_target", "relation_type", "relation_target_party_id",
			"ideology", "preference_consistency" };
		auto rows = readTable(path, header, diagnostics);
		if (!rows) return;
		for (auto const& row : *rows) {
			if (row.fields.size() != header.size()) continue;
			ForecastSpecification::Party party;
			if (auto value = parseCsvNumber<int>(row.fields[0], path, row, header[0], diagnostics)) party.index = *value;
			party.id = row.fields[1];
			party.name = row.fields[2];
			party.abbreviation = row.fields[3];
			if (!row.fields[4].empty()) party.homeRegionId = row.fields[4];
			if (auto value = parseCsvNumber<float>(row.fields[5], path, row, header[5], diagnostics)) party.seatTarget = *value;
			if (!row.fields[6].empty()) {
				ForecastSpecification::PartyRelation relation;
				if (row.fields[6] == "supports") relation.type = ForecastSpecification::PartyRelationType::Supports;
				else if (row.fields[6] == "coalition") relation.type = ForecastSpecification::PartyRelationType::Coalition;
				else if (row.fields[6] == "is_part_of") relation.type = ForecastSpecification::PartyRelationType::IsPartOf;
				else addError(diagnostics, csvLocation(path, row.line, header[6]), "unknown relation type");
				relation.targetPartyId = row.fields[7];
				party.relation = std::move(relation);
			}
			else if (!row.fields[7].empty()) {
				addError(diagnostics, csvLocation(path, row.line, header[7]), "relation target requires a relation type");
			}
			if (row.fields[8] == "strong_left") party.ideology = ForecastSpecification::Ideology::StrongLeft;
			else if (row.fields[8] == "moderate_left") party.ideology = ForecastSpecification::Ideology::ModerateLeft;
			else if (row.fields[8] == "centrist") party.ideology = ForecastSpecification::Ideology::Centrist;
			else if (row.fields[8] == "moderate_right") party.ideology = ForecastSpecification::Ideology::ModerateRight;
			else if (row.fields[8] == "strong_right") party.ideology = ForecastSpecification::Ideology::StrongRight;
			else addError(diagnostics, csvLocation(path, row.line, header[8]), "unknown ideology");
			if (row.fields[9] == "low") party.preferenceConsistency = ForecastSpecification::PreferenceConsistency::Low;
			else if (row.fields[9] == "moderate") party.preferenceConsistency = ForecastSpecification::PreferenceConsistency::Moderate;
			else if (row.fields[9] == "high") party.preferenceConsistency = ForecastSpecification::PreferenceConsistency::High;
			else addError(diagnostics, csvLocation(path, row.line, header[9]), "unknown preference consistency");
			specification.parties.push_back(std::move(party));
		}
	}

	void readPartyCodes(ForecastSpecification& specification,
		std::filesystem::path const& path, std::vector<Diagnostic>& diagnostics)
	{
		std::vector<std::string> const header = { "party_id", "official_code" };
		auto rows = readTable(path, header, diagnostics);
		if (!rows) return;
		for (auto const& row : *rows) {
			if (row.fields.size() != header.size()) continue;
			specification.partyOfficialCodes.push_back({ row.fields[0], row.fields[1] });
		}
	}

	void readNonClassicPreferences(ForecastSpecification& specification,
		std::filesystem::path const& path, std::vector<Diagnostic>& diagnostics)
	{
		std::vector<std::string> const header = {
			"source_party_id", "first_target_code", "second_target_code",
			"preference_to_first" };
		auto rows = readTable(path, header, diagnostics);
		if (!rows) return;
		for (auto const& row : *rows) {
			if (row.fields.size() != header.size()) continue;
			ForecastSpecification::NonClassicPreference preference;
			preference.sourcePartyId = row.fields[0];
			preference.firstTargetCode = row.fields[1];
			preference.secondTargetCode = row.fields[2];
			if (auto value = parseCsvNumber<float>(row.fields[3], path, row, header[3], diagnostics)) preference.preferenceToFirst = *value;
			specification.nonClassicPreferences.push_back(std::move(preference));
		}
	}

	void readRegions(ForecastSpecification& specification,
		std::filesystem::path const& path, std::vector<Diagnostic>& diagnostics)
	{
		std::vector<std::string> const header = {
			"index", "id", "name", "population", "analysis_code",
			"previous_election_tpp" };
		auto rows = readTable(path, header, diagnostics);
		if (!rows) return;
		for (auto const& row : *rows) {
			if (row.fields.size() != header.size()) continue;
			ForecastSpecification::Region region;
			if (auto value = parseCsvNumber<int>(row.fields[0], path, row, header[0], diagnostics)) region.index = *value;
			region.id = row.fields[1];
			region.name = row.fields[2];
			if (auto value = parseCsvNumber<int>(row.fields[3], path, row, header[3], diagnostics)) region.population = *value;
			region.analysisCode = row.fields[4];
			if (auto value = parseCsvNumber<float>(row.fields[5], path, row, header[5], diagnostics)) region.previousElectionTpp = *value;
			specification.regions.push_back(std::move(region));
		}
	}

	bool validIdentifier(std::string const& value)
	{
		if (value.empty() || value[0] < 'a' || value[0] > 'z') return false;
		return std::all_of(std::next(value.begin()), value.end(), [](char character) {
			return (character >= 'a' && character <= 'z') ||
				(character >= '0' && character <= '9') ||
				character == '_' || character == '-';
		});
	}

	bool validTermCode(std::string const& value)
	{
		if (value.size() < 5) return false;
		if (!std::all_of(value.begin(), value.begin() + 4,
			[](char c) { return c >= '0' && c <= '9'; })) return false;
		if (value[4] < 'a' || value[4] > 'z') return false;
		return std::all_of(value.begin() + 5, value.end(), [](char character) {
			return (character >= 'a' && character <= 'z') ||
				(character >= '0' && character <= '9') ||
				character == '_' || character == '-';
		});
	}

	bool safeRelativePath(std::string const& configuredPath)
	{
		if (configuredPath.empty()) return false;
		std::filesystem::path const path(configuredPath);
		if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
		return std::none_of(path.begin(), path.end(),
			[](auto const& component) { return component == ".."; });
	}

	template <typename Value>
	bool inRange(Value value, Value minimum, Value maximum)
	{
		return std::isfinite(value) && value >= minimum && value <= maximum;
	}

	void validateId(std::string const& value, std::string const& location,
		std::vector<Diagnostic>& diagnostics)
	{
		if (!validIdentifier(value)) {
			addError(diagnostics, location,
				"identifier must match ^[a-z][a-z0-9_-]*$");
		}
	}

	void validatePath(std::string const& configuredPath,
		std::filesystem::path const& base, std::string const& location,
		std::vector<Diagnostic>& diagnostics, bool requireExistingFile = true)
	{
		if (!safeRelativePath(configuredPath)) {
			addError(diagnostics, location,
				"path must be a non-empty relative path without '..'");
			return;
		}
		if (!requireExistingFile) return;
		auto const resolved = (base / configuredPath).lexically_normal();
		std::error_code error;
		if (!std::filesystem::is_regular_file(resolved, error)) {
			addError(diagnostics, location,
				(error ? "could not inspect file: " : "file does not exist: ") +
				pathString(resolved));
		}
	}
}

bool ForecastSpecificationLoadResult::valid() const
{
	return std::none_of(diagnostics.begin(), diagnostics.end(),
		[](auto const& diagnostic) {
			return diagnostic.severity == Diagnostic::Severity::Error;
		});
}

ForecastSpecificationLoadResult loadForecastSpecification(
	std::filesystem::path const& manifestPath,
	std::filesystem::path const& workspaceRoot)
{
	ForecastSpecificationLoadResult result;
	std::ifstream manifest(manifestPath);
	if (!manifest) {
		addError(result.diagnostics, pathString(manifestPath),
			"could not read manifest");
		return result;
	}
	try {
		Json const root = Json::parse(manifest);
		readManifest(root, result.specification, result.diagnostics);
	}
	catch (Json::parse_error const& error) {
		addError(result.diagnostics, pathString(manifestPath),
			"invalid JSON: " + std::string(error.what()));
		return result;
	}
	catch (Json::exception const& error) {
		addError(result.diagnostics, pathString(manifestPath),
			"could not read JSON value: " + std::string(error.what()));
		return result;
	}

	auto const manifestDirectory = manifestPath.parent_path();
	readParties(result.specification,
		packagePath(manifestDirectory, result.specification.files.parties),
		result.diagnostics);
	readPartyCodes(result.specification,
		packagePath(manifestDirectory, result.specification.files.partyOfficialCodes),
		result.diagnostics);
	readNonClassicPreferences(result.specification,
		packagePath(manifestDirectory, result.specification.files.nonClassicPreferences),
		result.diagnostics);
	readRegions(result.specification,
		packagePath(manifestDirectory, result.specification.files.regions),
		result.diagnostics);

	auto validation = validateForecastSpecification(
		result.specification, manifestDirectory, workspaceRoot);
	result.diagnostics.insert(result.diagnostics.end(),
		std::make_move_iterator(validation.begin()),
		std::make_move_iterator(validation.end()));
	return result;
}

std::vector<ForecastSpecificationDiagnostic> validateForecastSpecification(
	ForecastSpecification const& specification,
	std::filesystem::path const& manifestDirectory,
	std::filesystem::path const& workspaceRoot,
	bool validatePackageFiles)
{
	std::vector<Diagnostic> diagnostics;
	if (specification.schemaVersion != ForecastSpecification::CurrentSchemaVersion) {
		addError(diagnostics, "forecast.json.schema_version",
			"unsupported schema version " + std::to_string(specification.schemaVersion));
	}
	validateId(specification.id, "forecast.json.id", diagnostics);
	if (specification.electionName.empty()) addError(diagnostics, "forecast.json.election_name", "value must not be empty");
	if (!validTermCode(specification.electionCode)) addError(diagnostics, "forecast.json.election_code", "invalid election code");

	validatePath(specification.files.parties, manifestDirectory,
		"forecast.json.files.parties", diagnostics, validatePackageFiles);
	validatePath(specification.files.partyOfficialCodes, manifestDirectory,
		"forecast.json.files.party_official_codes", diagnostics,
		validatePackageFiles);
	validatePath(specification.files.nonClassicPreferences, manifestDirectory,
		"forecast.json.files.nonclassic_preferences", diagnostics,
		validatePackageFiles);
	validatePath(specification.files.regions, manifestDirectory,
		"forecast.json.files.regions", diagnostics, validatePackageFiles);
	validatePath(specification.dataSources.seats, workspaceRoot,
		"forecast.json.data_sources.seats", diagnostics);

	if (specification.electionSettings.previousTermCodes.empty()) {
		addError(diagnostics, "forecast.json.election_settings.previous_term_codes",
			"at least one previous election code is required");
	}
	std::set<std::string> previousTerms;
	for (std::size_t index = 0;
		index < specification.electionSettings.previousTermCodes.size(); ++index) {
		auto const& term = specification.electionSettings.previousTermCodes[index];
		auto const location = "forecast.json.election_settings.previous_term_codes[" +
			std::to_string(index) + "]";
		if (!validTermCode(term)) addError(diagnostics, location, "invalid election code");
		else if (!previousTerms.insert(term).second) addError(diagnostics, location, "duplicate previous election code");
	}
	if (!std::isfinite(specification.electionSettings.previousElectionTpp) ||
		specification.electionSettings.previousElectionTpp <= 0.0f ||
		specification.electionSettings.previousElectionTpp >= 100.0f) {
		addError(diagnostics, "forecast.json.election_settings.previous_election_tpp",
			"value must be strictly between 0 and 100");
	}
	if (specification.electionSettings.federalElectionDate &&
		!specification.electionSettings.federalElectionDate->isValid()) {
		addError(diagnostics, "forecast.json.election_settings.federal_election_date",
			"date is invalid");
	}

	if (specification.parties.size() < 2) {
		addError(diagnostics, "parties.csv", "at least two parties are required");
	}
	std::map<std::string, ForecastSpecification::Party const*> partiesById;
	std::set<std::string> partyNames;
	std::set<std::string> abbreviations;
	std::set<int> partyIndices;
	for (std::size_t index = 0; index < specification.parties.size(); ++index) {
		auto const& party = specification.parties[index];
		std::string const location = "parties.csv[" + std::to_string(index) + "]";
		validateId(party.id, location + ".id", diagnostics);
		if (!partiesById.emplace(party.id, &party).second)
			addError(diagnostics, location + ".id", "duplicate party ID");
		if (party.name.empty()) addError(diagnostics, location + ".name", "value must not be empty");
		else if (!partyNames.insert(party.name).second) addError(diagnostics, location + ".name", "duplicate party name");
		if (party.abbreviation.empty()) addError(diagnostics, location + ".abbreviation", "value must not be empty");
		else if (!abbreviations.insert(party.abbreviation).second) addError(diagnostics, location + ".abbreviation", "duplicate party abbreviation");
		if (!partyIndices.insert(party.index).second) addError(diagnostics, location + ".index", "duplicate party index");
		if (!std::isfinite(party.seatTarget) || party.seatTarget < 0.0f) addError(diagnostics, location + ".seat_target", "value must be finite and non-negative");
	}
	for (int index = 0; index < int(specification.parties.size()); ++index) {
		if (!partyIndices.contains(index)) addError(diagnostics, "parties.csv", "party indices must be contiguous from zero");
	}

	auto const partyOne = partiesById.find(specification.partySettings.majorPartyOneId);
	auto const partyTwo = partiesById.find(specification.partySettings.majorPartyTwoId);
	if (partyOne == partiesById.end()) addError(diagnostics, "forecast.json.party_settings.major_party_one_id", "unknown party ID");
	else if (partyOne->second->index != 0) addError(diagnostics, "forecast.json.party_settings.major_party_one_id", "major party one must have index zero");
	if (partyTwo == partiesById.end()) addError(diagnostics, "forecast.json.party_settings.major_party_two_id", "unknown party ID");
	else if (partyTwo->second->index != 1) addError(diagnostics, "forecast.json.party_settings.major_party_two_id", "major party two must have index one");
	if (!specification.partySettings.majorPartyOneId.empty() &&
		specification.partySettings.majorPartyOneId == specification.partySettings.majorPartyTwoId) {
		addError(diagnostics, "forecast.json.party_settings", "major parties must be different");
	}

	std::map<std::string, ForecastSpecification::Region const*> regionsById;
	std::set<std::string> regionNames;
	std::set<std::string> analysisCodes;
	std::set<int> regionIndices;
	if (specification.regions.empty()) addError(diagnostics, "regions.csv", "at least one region is required");
	for (std::size_t index = 0; index < specification.regions.size(); ++index) {
		auto const& region = specification.regions[index];
		std::string const location = "regions.csv[" + std::to_string(index) + "]";
		validateId(region.id, location + ".id", diagnostics);
		if (!regionsById.emplace(region.id, &region).second) addError(diagnostics, location + ".id", "duplicate region ID");
		if (region.name.empty()) addError(diagnostics, location + ".name", "value must not be empty");
		else if (!regionNames.insert(region.name).second) addError(diagnostics, location + ".name", "duplicate region name");
		if (region.population <= 0) addError(diagnostics, location + ".population", "value must be positive");
		if (!region.analysisCode.empty() && !analysisCodes.insert(region.analysisCode).second) addError(diagnostics, location + ".analysis_code", "duplicate analysis code");
		if (!regionIndices.insert(region.index).second) addError(diagnostics, location + ".index", "duplicate region index");
		if (!std::isfinite(region.previousElectionTpp) || region.previousElectionTpp <= 0.0f || region.previousElectionTpp >= 100.0f) addError(diagnostics, location + ".previous_election_tpp", "value must be strictly between 0 and 100");
	}
	for (int index = 0; index < int(specification.regions.size()); ++index) {
		if (!regionIndices.contains(index)) addError(diagnostics, "regions.csv", "region indices must be contiguous from zero");
	}
	for (std::size_t index = 0; index < specification.parties.size(); ++index) {
		auto const& party = specification.parties[index];
		std::string const location = "parties.csv[" + std::to_string(index) + "]";
		if (party.homeRegionId && !regionsById.contains(*party.homeRegionId)) addError(diagnostics, location + ".home_region_id", "unknown region ID");
		if (party.relation) {
			if (!partiesById.contains(party.relation->targetPartyId)) addError(diagnostics, location + ".relation_target_party_id", "unknown party ID");
			else if (party.relation->targetPartyId == party.id) addError(diagnostics, location + ".relation_target_party_id", "party cannot relate to itself");
		}
	}

	std::map<std::string, std::string> codeToParty;
	std::set<std::pair<std::string, std::string>> partyCodes;
	for (std::size_t index = 0; index < specification.partyOfficialCodes.size(); ++index) {
		auto const& code = specification.partyOfficialCodes[index];
		std::string const location = "party-official-codes.csv[" + std::to_string(index) + "]";
		if (!partiesById.contains(code.partyId)) addError(diagnostics, location + ".party_id", "unknown party ID");
		if (code.officialCode.empty()) addError(diagnostics, location + ".official_code", "value must not be empty");
		if (!partyCodes.emplace(code.partyId, code.officialCode).second) addError(diagnostics, location, "duplicate party/code row");
		auto const found = codeToParty.find(code.officialCode);
		if (found != codeToParty.end() && found->second != code.partyId) addError(diagnostics, location + ".official_code", "official code is assigned to more than one party");
		else if (!code.officialCode.empty()) codeToParty[code.officialCode] = code.partyId;
	}

	std::set<std::tuple<std::string, std::string, std::string>> nonClassicKeys;
	for (std::size_t index = 0; index < specification.nonClassicPreferences.size(); ++index) {
		auto const& preference = specification.nonClassicPreferences[index];
		std::string const location = "nonclassic-preferences.csv[" + std::to_string(index) + "]";
		if (!partiesById.contains(preference.sourcePartyId)) addError(diagnostics, location + ".source_party_id", "unknown party ID");
		if (!codeToParty.contains(preference.firstTargetCode)) addError(diagnostics, location + ".first_target_code", "unknown official party code");
		if (!codeToParty.contains(preference.secondTargetCode)) addError(diagnostics, location + ".second_target_code", "unknown official party code");
		if (preference.firstTargetCode == preference.secondTargetCode) addError(diagnostics, location, "target parties must be different");
		if (!std::isfinite(preference.preferenceToFirst) || preference.preferenceToFirst <= 0.0f || preference.preferenceToFirst >= 100.0f) addError(diagnostics, location + ".preference_to_first", "value must be strictly between 0 and 100");
		auto first = preference.firstTargetCode;
		auto second = preference.secondTargetCode;
		if (second < first) std::swap(first, second);
		if (!nonClassicKeys.emplace(preference.sourcePartyId, first, second).second) addError(diagnostics, location, "duplicate source party and target pair");
	}

	std::set<std::string> modelIds;
	if (specification.models.empty()) addError(diagnostics, "forecast.json.models", "at least one model is required");
	for (std::size_t index = 0; index < specification.models.size(); ++index) {
		auto const& model = specification.models[index];
		std::string const location = "forecast.json.models[" + std::to_string(index) + "]";
		validateId(model.id, location + ".id", diagnostics);
		if (!modelIds.insert(model.id).second) addError(diagnostics, location + ".id", "duplicate model ID");
		if (model.name.empty()) addError(diagnostics, location + ".name", "value must not be empty");
		if (!validTermCode(model.termCode)) addError(diagnostics, location + ".term_code", "invalid election code");
		else if (model.termCode != specification.electionCode) addError(diagnostics, location + ".term_code", "must match top-level election code");
		if (model.parties.size() < 2) addError(diagnostics, location + ".parties", "at least two party parameters are required");
		std::set<std::string> codes;
		for (std::size_t partyIndex = 0; partyIndex < model.parties.size(); ++partyIndex) {
			auto const& parameters = model.parties[partyIndex];
			std::string const partyLocation = location + ".parties[" + std::to_string(partyIndex) + "]";
			if (parameters.code.empty()) addError(diagnostics, partyLocation + ".code", "value must not be empty");
			else if (!codes.insert(parameters.code).second) addError(diagnostics, partyLocation + ".code", "duplicate model party code");
			if (!std::isfinite(parameters.preferenceDeviation) || parameters.preferenceDeviation < 0.0f) addError(diagnostics, partyLocation + ".preference_deviation", "value must be finite and non-negative");
			if (!std::isfinite(parameters.preferenceSamples) || parameters.preferenceSamples < 0.0f) addError(diagnostics, partyLocation + ".preference_samples", "value must be finite and non-negative");
		}
		for (std::string const required : { "OTH", "xOTH", "eOTH" }) {
			if (!codes.contains(required)) addError(diagnostics, location + ".parties", "missing required model party code " + required);
		}
	}

	std::set<std::string> projectionIds;
	if (specification.projections.empty()) addError(diagnostics, "forecast.json.projections", "at least one projection is required");
	for (std::size_t index = 0; index < specification.projections.size(); ++index) {
		auto const& projection = specification.projections[index];
		std::string const location = "forecast.json.projections[" + std::to_string(index) + "]";
		validateId(projection.id, location + ".id", diagnostics);
		if (!projectionIds.insert(projection.id).second) addError(diagnostics, location + ".id", "duplicate projection ID");
		if (projection.name.empty()) addError(diagnostics, location + ".name", "value must not be empty");
		if (!modelIds.contains(projection.baseModelId)) addError(diagnostics, location + ".base_model_id", "unknown model ID");
		if (projection.numIterations <= 0) addError(diagnostics, location + ".num_iterations", "value must be positive");
		if (!projection.endDate.isValid()) addError(diagnostics, location + ".end_date", "date is invalid");
		float totalWeight = 0.0f;
		std::set<int> dates;
		for (std::size_t dateIndex = 0; dateIndex < projection.possibleDates.size(); ++dateIndex) {
			auto const& possibleDate = projection.possibleDates[dateIndex];
			std::string const dateLocation = location + ".possible_dates[" + std::to_string(dateIndex) + "]";
			if (!possibleDate.date.isValid()) addError(diagnostics, dateLocation + ".date", "date is invalid");
			else if (!dates.insert(possibleDate.date.modifiedJulianDay()).second) addError(diagnostics, dateLocation + ".date", "duplicate possible date");
			if (!std::isfinite(possibleDate.weight) || possibleDate.weight < 0.0f) addError(diagnostics, dateLocation + ".weight", "value must be finite and non-negative");
			else totalWeight += possibleDate.weight;
		}
		if (!projection.possibleDates.empty() && totalWeight <= 0.0f) addError(diagnostics, location + ".possible_dates", "at least one weight must be positive");
	}

	std::set<std::string> simulationIds;
	if (specification.simulations.empty()) addError(diagnostics, "forecast.json.simulations", "at least one simulation is required");
	for (std::size_t index = 0; index < specification.simulations.size(); ++index) {
		auto const& simulation = specification.simulations[index];
		std::string const location = "forecast.json.simulations[" + std::to_string(index) + "]";
		validateId(simulation.id, location + ".id", diagnostics);
		if (!simulationIds.insert(simulation.id).second) addError(diagnostics, location + ".id", "duplicate simulation ID");
		if (simulation.name.empty()) addError(diagnostics, location + ".name", "value must not be empty");
		if (!projectionIds.contains(simulation.baseProjectionId)) addError(diagnostics, location + ".base_projection_id", "unknown projection ID");
		if (simulation.numIterations <= 0) addError(diagnostics, location + ".num_iterations", "value must be positive");
	}
	return diagnostics;
}
