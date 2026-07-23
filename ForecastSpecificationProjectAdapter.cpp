#include "ForecastSpecificationProjectAdapter.h"

#include "PollingProject.h"
#include "WorkspacePaths.h"

#include <algorithm>
#include <cmath>
#include <exception>
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

	std::vector<std::string> splitValues(std::string const& text)
	{
		std::vector<std::string> values;
		std::size_t start = 0;
		while (start <= text.size()) {
			auto const separator = text.find(',', start);
			auto const end =
				separator == std::string::npos ? text.size() : separator;
			auto const first = text.find_first_not_of(" \t", start);
			if (first == std::string::npos || first >= end) {
				values.emplace_back();
			}
			else {
				auto const last = text.find_last_not_of(" \t", end - 1);
				values.push_back(text.substr(first, last - first + 1));
			}
			if (separator == std::string::npos) break;
			start = separator + 1;
		}
		return values;
	}

	std::optional<std::vector<float>> parseFloatValues(
		std::string const& text)
	{
		std::vector<float> values;
		for (auto const& value : splitValues(text)) {
			try {
				std::size_t consumed = 0;
				float const parsed = std::stof(value, &consumed);
				if (consumed != value.size() || !std::isfinite(parsed)) {
					return std::nullopt;
				}
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
		return first.name == second.name &&
			first.baseModel == second.baseModel &&
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
			first.live == second.live &&
			first.reportMode == second.reportMode &&
			first.baseProjection == second.baseProjection &&
			first.fedElectionDate == second.fedElectionDate &&
			first.prevElection2pp == second.prevElection2pp &&
			first.forceTpp == second.forceTpp &&
			first.previousResultsUrl == second.previousResultsUrl &&
			first.preloadUrl == second.preloadUrl &&
			first.currentTestUrl == second.currentTestUrl &&
			first.currentRealUrl == second.currentRealUrl;
	}
}

bool ForecastSpecificationProjectApplyResult::valid() const
{
	return !hasErrors(diagnostics);
}

std::string ForecastSpecificationProjectApplyResult::errorMessage() const
{
	return diagnosticsText(diagnostics);
}

ForecastSpecificationProjectConstructionResult::
ForecastSpecificationProjectConstructionResult() = default;

ForecastSpecificationProjectConstructionResult::
~ForecastSpecificationProjectConstructionResult() = default;

ForecastSpecificationProjectConstructionResult::
ForecastSpecificationProjectConstructionResult(
	ForecastSpecificationProjectConstructionResult&&) noexcept = default;

ForecastSpecificationProjectConstructionResult&
ForecastSpecificationProjectConstructionResult::operator=(
	ForecastSpecificationProjectConstructionResult&&) noexcept = default;

bool ForecastSpecificationProjectConstructionResult::valid() const
{
	return project && !hasErrors(diagnostics);
}

std::string ForecastSpecificationProjectConstructionResult::errorMessage() const
{
	return diagnosticsText(diagnostics);
}

ForecastSpecificationProjectApplyResult
ForecastSpecificationProjectAdapter::apply(
	PollingProject& project,
	ForecastSpecification const& specification)
{
	ForecastSpecificationProjectApplyResult result;
	auto requireCount = [&](std::string const& collection, int projectCount,
		std::size_t specificationCount) {
		if (projectCount != int(specificationCount)) {
			addError(result.diagnostics, collection,
				"the project contains " + std::to_string(projectCount) +
				" entries but the forecast specification contains " +
				std::to_string(specificationCount));
		}
	};
	requireCount("parties.csv", project.parties().count(),
		specification.parties.size());
	requireCount("regions.csv", project.regions().count(),
		specification.regions.size());
	requireCount("forecast.json.models", project.models().count(),
		specification.models.size());
	requireCount("forecast.json.projections", project.projections().count(),
		specification.projections.size());
	requireCount("forecast.json.simulations", project.simulations().count(),
		specification.simulations.size());
	if (hasErrors(result.diagnostics)) return result;

	for (auto const& party : specification.parties) {
		result.runtimeIds.parties[party.id] =
			project.parties().indexToId(party.index);
	}
	for (auto const& region : specification.regions) {
		result.runtimeIds.regions[region.id] =
			project.regions().indexToId(region.index);
	}
	for (std::size_t index = 0; index < specification.models.size(); ++index) {
		result.runtimeIds.models[specification.models[index].id] =
			project.models().indexToId(int(index));
	}
	for (std::size_t index = 0;
		index < specification.projections.size(); ++index) {
		result.runtimeIds.projections[specification.projections[index].id] =
			project.projections().indexToId(int(index));
	}
	for (std::size_t index = 0;
		index < specification.simulations.size(); ++index) {
		result.runtimeIds.simulations[specification.simulations[index].id] =
			project.simulations().indexToId(int(index));
	}

	// Reference validation is performed while loading the specification. Once
	// these maps exist, malformed input cannot leave a partially configured
	// project.
	project.setElectionName(specification.electionName);
	bool simulationInputsChanged = false;
	for (auto const& configured : specification.regions) {
		auto& region = project.regions().access(
			result.runtimeIds.regions.at(configured.id));
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
		auto const partyId = result.runtimeIds.parties.at(configured.id);
		auto party = project.parties().view(partyId);
		auto const configuredHomeRegion = configured.homeRegionId ?
			project.regions().view(
				result.runtimeIds.regions.at(*configured.homeRegionId)).name :
			"";
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
				relationType(configured.relation->type) :
				Party::RelationType::None;
			auto const configuredRelationTarget = configured.relation ?
				result.runtimeIds.parties.at(
					configured.relation->targetPartyId) : 0;
			simulationInputsChanged = simulationInputsChanged ||
				party.relationType != configuredRelationType ||
				party.relationTarget != configuredRelationTarget;
		}
		party.name = configured.name;
		party.abbreviation = configured.abbreviation;
		party.homeRegion = configuredHomeRegion;
		party.seatTarget = configured.seatTarget;
		party.ideology = static_cast<int>(configured.ideology);
		party.consistency =
			static_cast<int>(configured.preferenceConsistency);
		party.officialCodes = officialCodes[configured.id];
		party.ncPreferenceFlow = preferences[configured.id];
		if (configured.relation) {
			party.relationType = relationType(configured.relation->type);
			party.relationTarget = result.runtimeIds.parties.at(
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
		auto const modelId = result.runtimeIds.models.at(configured.id);
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
	for (auto const& configured : specification.projections) {
		auto const projectionId =
			result.runtimeIds.projections.at(configured.id);
		auto& projection = project.projections().access(projectionId);
		Projection::Settings settings;
		settings.name = configured.name;
		settings.baseModel =
			result.runtimeIds.models.at(configured.baseModelId);
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

	for (auto const& configured : specification.simulations) {
		auto& simulation = project.simulations().access(
			result.runtimeIds.simulations.at(configured.id));
		auto settings = simulation.getSettings();
		settings.name = configured.name;
		settings.prevTermCodes =
			specification.electionSettings.previousTermCodes;
		settings.numIterations = configured.numIterations;
		settings.live = simulationMode(configured.mode);
		settings.reportMode = reportMode(configured.reportMode);
		settings.baseProjection =
			result.runtimeIds.projections.at(configured.baseProjectionId);
		settings.fedElectionDate =
			specification.electionSettings.federalElectionDate.value_or(Date());
		settings.prevElection2pp =
			specification.electionSettings.previousElectionTpp;
		if (specification.electionSettings.liveSources) {
			auto const& sources =
				*specification.electionSettings.liveSources;
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

	project.seats().configureImportSource(
		specification.dataSources.seats, true);
	result.applied = true;
	return result;
}

ForecastSpecificationProjectConstructionResult
ForecastSpecificationProjectAdapter::construct(
	ForecastSpecification const& specification,
	WorkspacePaths const& workspacePaths)
{
	ForecastSpecificationProjectConstructionResult result;
	try {
		// Populate placeholders so construction and legacy overlay share exactly
		// the same conversion and reference-mapping path.
		auto project = std::unique_ptr<PollingProject>(
			new PollingProject(workspacePaths));
		for (std::size_t index = 0;
			index < specification.parties.size(); ++index) {
			project->parties().add(Party());
		}
		for (std::size_t index = 0;
			index < specification.regions.size(); ++index) {
			// RegionCollection recalculates weighted values after every add.
			// A positive placeholder population keeps intermediate values finite.
			project->regions().add(Region("", 1, 50.0f, 50.0f));
		}
		for (std::size_t index = 0;
			index < specification.models.size(); ++index) {
			project->models().add(StanModel());
		}
		for (std::size_t index = 0;
			index < specification.projections.size(); ++index) {
			project->projections().add(Projection());
		}
		for (std::size_t index = 0;
			index < specification.simulations.size(); ++index) {
			project->simulations().add(Simulation());
		}

		auto applied = apply(*project, specification);
		result.runtimeIds = std::move(applied.runtimeIds);
		result.diagnostics = std::move(applied.diagnostics);
		if (!applied.valid()) return result;

		project->name = specification.id;
		project->finalizeFileLoading();
		project->seats().importInfo();
		project->valid = true;
		result.project = std::move(project);
	}
	catch (SeatImportException const& exception) {
		addError(result.diagnostics, "forecast.json.data_sources.seats",
			exception.what());
	}
	catch (std::exception const& exception) {
		addError(result.diagnostics, "forecast specification",
			"could not construct the project: " +
			std::string(exception.what()));
	}
	return result;
}
