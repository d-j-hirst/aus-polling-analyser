#pragma once

#include "Date.h"

#include <optional>
#include <string>
#include <vector>

// Portable configuration required to run the core forecast pipeline. This is
// deliberately separate from PollingProject, which also owns GUI state,
// generated output, live overrides, and legacy analysis data.
struct ForecastSpecification {
	constexpr static int CurrentSchemaVersion = 1;

	enum class PartyRelationType {
		Supports,
		Coalition,
		IsPartOf,
	};

	enum class Ideology {
		StrongLeft,
		ModerateLeft,
		Centrist,
		ModerateRight,
		StrongRight,
	};

	enum class PreferenceConsistency {
		Low,
		Moderate,
		High,
	};

	enum class SimulationMode {
		Projection,
		LiveManual,
		LiveAutomatic,
	};

	enum class ReportMode {
		RegularForecast,
		LiveForecast,
		Nowcast,
	};

	struct ConfigurationFiles {
		std::string parties;
		std::string partyOfficialCodes;
		std::string nonClassicPreferences;
		std::string regions;
	};

	struct DataSources {
		// Workspace-relative canonical seat data. The specification references
		// this file rather than duplicating the legacy seat configuration.
		std::string seats;
	};

	struct PartySettings {
		std::string majorPartyOneId;
		std::string majorPartyTwoId;
	};

	struct PartyRelation {
		PartyRelationType type = PartyRelationType::Supports;
		std::string targetPartyId;
	};

	struct Party {
		// Explicit order is retained because several simulation categories use
		// party position. The two major parties are also named in PartySettings.
		int index = 0;
		std::string id;
		std::string name;
		std::string abbreviation;
		std::optional<std::string> homeRegionId;
		float seatTarget = 10000.0f;
		std::optional<PartyRelation> relation;
		Ideology ideology = Ideology::Centrist;
		PreferenceConsistency preferenceConsistency =
			PreferenceConsistency::Moderate;
	};

	struct PartyOfficialCode {
		std::string partyId;
		std::string officialCode;
	};

	struct NonClassicPreference {
		std::string sourcePartyId;
		std::string firstTargetCode;
		std::string secondTargetCode;
		float preferenceToFirst = 50.0f;
	};

	struct Region {
		// Region order remains explicit because current regional model output is
		// converted to project regions partly by numeric position.
		int index = 0;
		std::string id;
		std::string name;
		int population = 0;
		std::string analysisCode;
		float previousElectionTpp = 50.0f;
	};

	struct ModelPartyParameters {
		std::string code;
		float preferenceDeviation = 0.0f;
		float preferenceSamples = 0.0f;
	};

	struct Model {
		std::string id;
		std::string name;
		std::string termCode;
		// Order is significant: the first two entries define the TPP pair.
		std::vector<ModelPartyParameters> parties;
	};

	struct PossibleDate {
		Date date;
		float weight = 0.0f;
	};

	struct Projection {
		std::string id;
		std::string name;
		std::string baseModelId;
		int numIterations = 5000;
		Date endDate;
		std::vector<PossibleDate> possibleDates;
	};

	struct LiveSources {
		std::string previousResultsUrl;
		std::string preloadUrl;
		std::string currentTestUrl;
		std::string currentRealUrl;
	};

	struct ElectionSettings {
		std::vector<std::string> previousTermCodes;
		float previousElectionTpp = 50.0f;
		std::optional<Date> federalElectionDate;
		std::optional<LiveSources> liveSources;
	};

	struct Simulation {
		std::string id;
		std::string name;
		std::string baseProjectionId;
		int numIterations = 10000;
		SimulationMode mode = SimulationMode::Projection;
		ReportMode reportMode = ReportMode::RegularForecast;
	};

	int schemaVersion = CurrentSchemaVersion;
	std::string id;
	std::string electionName;
	std::string electionCode;
	ConfigurationFiles files;
	DataSources dataSources;
	PartySettings partySettings;
	ElectionSettings electionSettings;
	std::vector<Party> parties;
	std::vector<PartyOfficialCode> partyOfficialCodes;
	std::vector<NonClassicPreference> nonClassicPreferences;
	std::vector<Region> regions;
	std::vector<Model> models;
	std::vector<Projection> projections;
	std::vector<Simulation> simulations;
};
