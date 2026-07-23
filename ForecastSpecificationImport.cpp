#include "ForecastSpecificationImport.h"

#include "PollingProject.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace {
	using Diagnostic = ForecastSpecificationDiagnostic;

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

	std::vector<std::string> splitValues(std::string const& text)
	{
		std::vector<std::string> values;
		std::size_t start = 0;
		while (start <= text.size()) {
			auto const separator = text.find(',', start);
			auto const end = separator == std::string::npos ? text.size() : separator;
			auto first = text.find_first_not_of(" \t", start);
			if (first == std::string::npos || first >= end) values.emplace_back();
			else {
				auto const last = text.find_last_not_of(" \t", end - 1);
				values.push_back(text.substr(first, last - first + 1));
			}
			if (separator == std::string::npos) break;
			start = separator + 1;
		}
		return values;
	}

	std::optional<std::vector<float>> parseFloatValues(std::string const& text)
	{
		std::vector<float> values;
		for (auto const& value : splitValues(text)) {
			try {
				std::size_t consumed = 0;
				float const parsed = std::stof(value, &consumed);
				if (consumed != value.size() || !std::isfinite(parsed)) return std::nullopt;
				values.push_back(parsed);
			}
			catch (std::exception const&) {
				return std::nullopt;
			}
		}
		return values;
	}

	std::string joinStrings(std::vector<std::string> const& values)
	{
		std::ostringstream output;
		for (std::size_t index = 0; index < values.size(); ++index) {
			if (index) output << ',';
			output << values[index];
		}
		return output.str();
	}

	std::string joinFloats(std::vector<float> const& values)
	{
		std::ostringstream output;
		output << std::setprecision(std::numeric_limits<float>::max_digits10);
		for (std::size_t index = 0; index < values.size(); ++index) {
			if (index) output << ',';
			output << values[index];
		}
		return output.str();
	}

	bool sameFloatValues(std::string const& current,
		std::vector<float> const& configured)
	{
		auto const parsed = parseFloatValues(current);
		return parsed && *parsed == configured;
	}

	Party::RelationType relationType(
		ForecastSpecification::PartyRelationType type)
	{
		switch (type) {
		case ForecastSpecification::PartyRelationType::Supports:
			return Party::RelationType::Supports;
		case ForecastSpecification::PartyRelationType::Coalition:
			return Party::RelationType::Coalition;
		case ForecastSpecification::PartyRelationType::IsPartOf:
			return Party::RelationType::IsPartOf;
		}
		return Party::RelationType::None;
	}

	Simulation::Settings::Mode simulationMode(
		ForecastSpecification::SimulationMode mode)
	{
		return static_cast<Simulation::Settings::Mode>(mode);
	}

	Simulation::Settings::ReportMode reportMode(
		ForecastSpecification::ReportMode mode)
	{
		return static_cast<Simulation::Settings::ReportMode>(mode);
	}

	bool sameProjectionSettings(Projection::Settings const& first,
		Projection::Settings const& second)
	{
		return first.name == second.name && first.baseModel == second.baseModel &&
			first.numIterations == second.numIterations &&
			first.possibleDates == second.possibleDates &&
			first.endDate == second.endDate;
	}

	bool sameSimulationSettings(Simulation::Settings const& first,
		Simulation::Settings const& second)
	{
		return first.name == second.name &&
			first.prevTermCodes == second.prevTermCodes &&
			first.numIterations == second.numIterations &&
			first.live == second.live && first.reportMode == second.reportMode &&
			first.baseProjection == second.baseProjection &&
			first.fedElectionDate == second.fedElectionDate &&
			first.prevElection2pp == second.prevElection2pp &&
			first.forceTpp == second.forceTpp &&
			first.previousResultsUrl == second.previousResultsUrl &&
			first.preloadUrl == second.preloadUrl &&
			first.currentTestUrl == second.currentTestUrl &&
			first.currentRealUrl == second.currentRealUrl;
	}

	std::string diagnosticsText(std::vector<Diagnostic> const& diagnostics)
	{
		std::string message;
		for (auto const& diagnostic : diagnostics) {
			if (diagnostic.severity != Diagnostic::Severity::Error) continue;
			if (!message.empty()) message += '\n';
			message += diagnostic.location + ": " + diagnostic.message;
		}
		return message;
	}
}

bool ForecastSpecificationImportResult::valid() const
{
	return !hasErrors(diagnostics);
}

std::string ForecastSpecificationImportResult::errorMessage() const
{
	return diagnosticsText(diagnostics);
}

ForecastSpecificationImportResult
ForecastSpecificationImporter::importForProject(PollingProject& project)
{
	ForecastSpecificationImportResult result;
	std::set<std::string> matchingTermCodes;
	for (auto const& modelEntry : project.models()) {
		auto const& model = modelEntry.second;
		if (model.getTermCode().empty()) continue;
		auto const manifest = project.paths().resolve(
			std::filesystem::path("forecasts") / model.getTermCode() /
			"forecast.json");
		std::error_code error;
		if (std::filesystem::is_regular_file(manifest, error) && !error) {
			matchingTermCodes.insert(model.getTermCode());
		}
	}
	if (matchingTermCodes.empty()) return result;
	if (matchingTermCodes.size() != 1) {
		addError(result.diagnostics, "forecasts",
			"the project refers to more than one available forecast package");
		return result;
	}

	auto const termCode = *matchingTermCodes.begin();
	auto const manifest = project.paths().resolve(
		std::filesystem::path("forecasts") / termCode / "forecast.json");
	auto loaded = loadForecastSpecification(manifest, project.paths().root());
	result.diagnostics = std::move(loaded.diagnostics);
	if (!loaded.valid()) return result;
	auto const& specification = loaded.specification;
	if (specification.electionCode != termCode) {
		addError(result.diagnostics, manifest.generic_string(),
			"election_code does not match the forecast package directory");
		return result;
	}

	auto requireCount = [&](std::string const& collection, int legacyCount,
		std::size_t specificationCount) {
		if (legacyCount != int(specificationCount)) {
			addError(result.diagnostics, collection,
				"the .pol2 file contains " + std::to_string(legacyCount) +
				" entries but the forecast specification contains " +
				std::to_string(specificationCount));
		}
	};
	requireCount("parties.csv", project.parties().count(), specification.parties.size());
	requireCount("regions.csv", project.regions().count(), specification.regions.size());
	requireCount("forecast.json.models", project.models().count(), specification.models.size());
	requireCount("forecast.json.projections", project.projections().count(), specification.projections.size());
	requireCount("forecast.json.simulations", project.simulations().count(), specification.simulations.size());
	if (hasErrors(result.diagnostics)) return result;

	std::map<std::string, Party::Id> partyIds;
	std::map<std::string, Region::Id> regionIds;
	for (auto const& party : specification.parties) {
		partyIds[party.id] = project.parties().indexToId(party.index);
	}
	for (auto const& region : specification.regions) {
		regionIds[region.id] = project.regions().indexToId(region.index);
	}
	std::map<std::string, ModelCollection::Id> modelIds;
	for (std::size_t index = 0; index < specification.models.size(); ++index) {
		modelIds[specification.models[index].id] =
			project.models().indexToId(int(index));
	}
	std::map<std::string, Projection::Id> projectionIds;
	for (std::size_t index = 0; index < specification.projections.size(); ++index) {
		projectionIds[specification.projections[index].id] =
			project.projections().indexToId(int(index));
	}

	// All validation and reference mapping is complete. Mutations below cannot
	// leave a partially imported configuration due to malformed input.
	project.setElectionName(specification.electionName);
	bool simulationInputsChanged = false;
	for (auto const& configured : specification.regions) {
		auto& region = project.regions().access(regionIds.at(configured.id));
		simulationInputsChanged = simulationInputsChanged ||
			region.name != configured.name ||
			region.population != configured.population ||
			region.analysisCode != configured.analysisCode ||
			region.lastElection2pp != configured.previousElectionTpp;
		region.name = configured.name;
		region.population = configured.population;
		region.analysisCode = configured.analysisCode;
		region.lastElection2pp = configured.previousElectionTpp;
	}

	std::map<std::string, std::vector<std::string>> officialCodes;
	for (auto const& code : specification.partyOfficialCodes) {
		officialCodes[code.partyId].push_back(code.officialCode);
	}
	std::map<std::string, std::vector<Party::NcPreferenceFlow>> preferences;
	for (auto const& preference : specification.nonClassicPreferences) {
		preferences[preference.sourcePartyId].push_back({
			{ preference.firstTargetCode, preference.secondTargetCode },
			preference.preferenceToFirst });
	}
	for (auto const& configured : specification.parties) {
		auto const partyId = partyIds.at(configured.id);
		auto party = project.parties().view(partyId);
		auto const configuredHomeRegion = configured.homeRegionId ?
			project.regions().view(regionIds.at(*configured.homeRegionId)).name : "";
		simulationInputsChanged = simulationInputsChanged ||
			party.name != configured.name ||
			party.abbreviation != configured.abbreviation ||
			party.homeRegion != configuredHomeRegion ||
			party.seatTarget != configured.seatTarget ||
			party.ideology != static_cast<int>(configured.ideology) ||
			party.consistency !=
				static_cast<int>(configured.preferenceConsistency) ||
			party.officialCodes != officialCodes[configured.id] ||
			party.ncPreferenceFlow != preferences[configured.id];
		if (configured.index >= PartyCollection::NumMajorParties) {
			auto const configuredRelationType = configured.relation ?
				relationType(configured.relation->type) : Party::RelationType::None;
			auto const configuredRelationTarget = configured.relation ?
				partyIds.at(configured.relation->targetPartyId) : 0;
			simulationInputsChanged = simulationInputsChanged ||
				party.relationType != configuredRelationType ||
				party.relationTarget != configuredRelationTarget;
		}
		party.name = configured.name;
		party.abbreviation = configured.abbreviation;
		party.homeRegion = configuredHomeRegion;
		party.seatTarget = configured.seatTarget;
		party.ideology = static_cast<int>(configured.ideology);
		party.consistency = static_cast<int>(configured.preferenceConsistency);
		party.officialCodes = officialCodes[configured.id];
		party.ncPreferenceFlow = preferences[configured.id];
		if (configured.relation) {
			party.relationType = relationType(configured.relation->type);
			party.relationTarget = partyIds.at(
				configured.relation->targetPartyId);
		}
		else {
			party.relationType = Party::RelationType::None;
			party.relationTarget = 0;
		}
		project.parties().replace(partyId, std::move(party));
	}

	std::set<ModelCollection::Id> changedModels;
	for (std::size_t index = 0; index < specification.models.size(); ++index) {
		auto const& configured = specification.models[index];
		auto const modelId = project.models().indexToId(int(index));
		auto& model = project.models().access(modelId);
		std::vector<std::string> codes;
		std::vector<float> deviations;
		std::vector<float> samples;
		for (auto const& party : configured.parties) {
			codes.push_back(party.code);
			deviations.push_back(party.preferenceDeviation);
			samples.push_back(party.preferenceSamples);
		}
		bool const behaviourChanged = model.termCode != configured.termCode ||
			splitValues(model.partyCodes) != codes ||
			!sameFloatValues(model.preferenceDeviation, deviations) ||
			!sameFloatValues(model.preferenceSamples, samples);
		if (behaviourChanged) {
			auto const legacyPreferenceFlow = model.preferenceFlow;
			model = StanModel(configured.name, configured.termCode,
				joinStrings(codes));
			model.preferenceFlow = legacyPreferenceFlow;
			model.preferenceDeviation = joinFloats(deviations);
			model.preferenceSamples = joinFloats(samples);
			changedModels.insert(modelId);
		}
		else {
			model.name = configured.name;
			model.termCode = configured.termCode;
			model.partyCodes = joinStrings(codes);
			model.preferenceDeviation = joinFloats(deviations);
			model.preferenceSamples = joinFloats(samples);
		}
	}

	std::set<Projection::Id> changedProjections;
	for (std::size_t index = 0; index < specification.projections.size(); ++index) {
		auto const& configured = specification.projections[index];
		auto const projectionId = project.projections().indexToId(int(index));
		auto& projection = project.projections().access(projectionId);
		Projection::Settings settings;
		settings.name = configured.name;
		settings.baseModel = modelIds.at(configured.baseModelId);
		settings.numIterations = configured.numIterations;
		settings.endDate = configured.endDate;
		for (auto const& possibleDate : configured.possibleDates) {
			settings.possibleDates.push_back(
				{ possibleDate.date.formatIso(), possibleDate.weight });
		}
		if (!sameProjectionSettings(projection.getSettings(), settings)) {
			projection.replaceSettings(std::move(settings));
			changedProjections.insert(projectionId);
		}
		else if (changedModels.contains(settings.baseModel)) {
			projection.invalidate();
			changedProjections.insert(projectionId);
		}
	}

	for (std::size_t index = 0; index < specification.simulations.size(); ++index) {
		auto const& configured = specification.simulations[index];
		auto& simulation = project.simulations().access(
			project.simulations().indexToId(int(index)));
		auto settings = simulation.getSettings();
		settings.name = configured.name;
		settings.prevTermCodes = specification.electionSettings.previousTermCodes;
		settings.numIterations = configured.numIterations;
		settings.live = simulationMode(configured.mode);
		settings.reportMode = reportMode(configured.reportMode);
		settings.baseProjection = projectionIds.at(configured.baseProjectionId);
		settings.fedElectionDate = specification.electionSettings.federalElectionDate
			.value_or(Date());
		settings.prevElection2pp =
			specification.electionSettings.previousElectionTpp;
		if (specification.electionSettings.liveSources) {
			auto const& sources = *specification.electionSettings.liveSources;
			settings.previousResultsUrl = sources.previousResultsUrl;
			settings.preloadUrl = sources.preloadUrl;
			settings.currentTestUrl = sources.currentTestUrl;
			settings.currentRealUrl = sources.currentRealUrl;
		}
		else {
			settings.previousResultsUrl.clear();
			settings.preloadUrl.clear();
			settings.currentTestUrl.clear();
			settings.currentRealUrl.clear();
		}
		if (!sameSimulationSettings(simulation.getSettings(), settings) ||
			changedProjections.contains(settings.baseProjection) ||
			simulationInputsChanged) {
			simulation.replaceSettings(std::move(settings));
		}
	}

	result.applied = true;
	return result;
}
