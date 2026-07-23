#include "../ForecastSpecificationIO.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
	void writeFile(std::filesystem::path const& path, std::string const& contents)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary);
		assert(output);
		output << contents;
		assert(output);
	}

	bool hasDiagnostic(ForecastSpecificationLoadResult const& result,
		std::string const& text)
	{
		for (auto const& diagnostic : result.diagnostics) {
			if (diagnostic.location.find(text) != std::string::npos ||
				diagnostic.message.find(text) != std::string::npos) return true;
		}
		return false;
	}

	std::string validManifest(bool includeUnknownField = false)
	{
		return std::string(R"({
  "schema_version": 1,
  "id": "federal-2028",
  "election_name": "2028 Australian federal election",
  "election_code": "2028fed",
  "files": {
    "parties": "parties.csv",
    "party_official_codes": "party-official-codes.csv",
    "nonclassic_preferences": "nonclassic-preferences.csv",
    "regions": "regions.csv"
  },
  "data_sources": {
    "seats": "analysis/seats/2028fed.txt"
  },
  "party_settings": {
    "major_party_one_id": "labor",
    "major_party_two_id": "coalition"
  },
  "election_settings": {
    "previous_term_codes": ["2025fed"],
    "previous_election_tpp": 55.2
  },
  "models": [{
    "id": "standard-model",
    "name": "Standard model",
    "term_code": "2028fed",
    "parties": [
      {"code": "ALP", "preference_deviation": 0, "preference_samples": 0},
      {"code": "LNP", "preference_deviation": 0, "preference_samples": 0},
      {"code": "OTH", "preference_deviation": 5, "preference_samples": 10},
      {"code": "xOTH", "preference_deviation": 5, "preference_samples": 10},
      {"code": "eOTH", "preference_deviation": 5, "preference_samples": 10}
    ]
  }],
  "projections": [{
    "id": "standard-projection",
    "name": "Standard projection",
    "base_model_id": "standard-model",
    "num_iterations": 5000,
    "end_date": "2028-05-20",
    "possible_dates": [{"date": "2028-05-20", "weight": 1}]
  }],
  "simulations": [{
    "id": "standard-simulation",
    "name": "Standard simulation",
    "base_projection_id": "standard-projection",
    "num_iterations": 10000,
    "mode": "live_automatic",
    "report_mode": "regular_forecast"
  }])") + (includeUnknownField ? ",\n  \"unexpected\": true\n}" : "\n}");
	}

	void writeValidPackage(std::filesystem::path const& package,
		std::filesystem::path const& workspace)
	{
		writeFile(package / "forecast.json", validManifest());
		writeFile(package / "parties.csv",
			"index,id,name,abbreviation,home_region_id,seat_target,relation_type,relation_target_party_id,ideology,preference_consistency\n"
			"0,labor,\"Labor, Australian\",ALP,,10000,,,moderate_left,high\n"
			"1,coalition,Coalition,LNP,,10000,,,moderate_right,high\n");
		writeFile(package / "party-official-codes.csv",
			"party_id,official_code\n"
			"labor,ALP\n"
			"coalition,LNP\n");
		writeFile(package / "nonclassic-preferences.csv",
			"source_party_id,first_target_code,second_target_code,preference_to_first\n");
		writeFile(package / "regions.csv",
			"index,id,name,population,analysis_code,previous_election_tpp\n"
			"0,national,National,1000000,all,50.5\n");
		writeFile(workspace / "analysis/seats/2028fed.txt", "#Example\n");
	}
}

int main(int argc, char const* argv[])
{
	if (argc > 2) {
		auto const workspace = std::filesystem::path(argv[1]);
		bool valid = true;
		for (int argument = 2; argument < argc; ++argument) {
			auto const result = loadForecastSpecification(argv[argument], workspace);
			for (auto const& diagnostic : result.diagnostics) {
				std::cerr << argv[argument] << ": " << diagnostic.location <<
					": " << diagnostic.message << '\n';
			}
			valid = valid && result.valid();
		}
		return valid ? 0 : 1;
	}

	auto const root = std::filesystem::temp_directory_path() /
		"polling-analyser-forecast-specification-tests";
	std::filesystem::remove_all(root);
	auto const package = root / "package";
	auto const workspace = root / "workspace";
	writeValidPackage(package, workspace);

	auto valid = loadForecastSpecification(package / "forecast.json", workspace);
	assert(valid.valid());
	assert(valid.diagnostics.empty());
	assert(valid.specification.parties.size() == 2);
	assert(valid.specification.parties[0].name == "Labor, Australian");
	assert(valid.specification.projections[0].endDate.formatIso() == "2028-05-20");
	assert(valid.specification.electionSettings.previousTermCodes[0] == "2025fed");

	auto invalidManifest = validManifest(true);
	auto const projectionReference = invalidManifest.find(
		"\"base_projection_id\": \"standard-projection\"");
	assert(projectionReference != std::string::npos);
	invalidManifest.replace(projectionReference,
		std::string("\"base_projection_id\": \"standard-projection\"").size(),
		"\"base_projection_id\": \"missing-projection\"");
	writeFile(package / "forecast.json", invalidManifest);
	writeFile(package / "parties.csv",
		"index,id,name,abbreviation,home_region_id,seat_target,relation_type,relation_target_party_id,ideology,preference_consistency\n"
		"0,labor,Labor,ALP,,not-a-number,,,moderate_left,high\n"
		"1,coalition,Coalition,LNP,,10000,,,moderate_right,high\n");
	writeFile(package / "party-official-codes.csv",
		"party_id,official_code\n"
		"labor,ALP\n"
		"coalition,ALP\n");
	auto invalid = loadForecastSpecification(package / "forecast.json", workspace);
	assert(!invalid.valid());
	assert(hasDiagnostic(invalid, "unknown field"));
	assert(hasDiagnostic(invalid, "invalid numeric value"));
	assert(hasDiagnostic(invalid, "unknown projection ID"));
	assert(hasDiagnostic(invalid, "assigned to more than one party"));

	writeFile(package / "forecast.json", validManifest());
	writeFile(package / "parties.csv",
		"index,id,name,abbreviation,home_region_id,seat_target,relation_type,relation_target_party_id,ideology,preference_consistency\n"
		"0,labor,\"unterminated,Labor,ALP,,10000,,,moderate_left,high\n");
	auto malformedCsv = loadForecastSpecification(package / "forecast.json", workspace);
	assert(!malformedCsv.valid());
	assert(hasDiagnostic(malformedCsv, "unterminated quoted field"));

	std::filesystem::remove_all(root);
}
