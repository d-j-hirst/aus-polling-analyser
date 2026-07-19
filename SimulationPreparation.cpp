#include "SimulationPreparation.h"

#include "LivePreparation.h"
#include "PollingProject.h"
#include "Simulation.h"
#include "SimulationRun.h"
#include "SpecialPartyCodes.h"

#include <cctype>
#include <cmath>
#include <limits>

using Mp = Simulation::MajorParty;

namespace {
	float parseFiniteFloat(
		std::string const& value,
		std::string const& fileName,
		std::string const& description)
	{
		try {
			std::size_t processed = 0;
			float const parsed = std::stof(value, &processed);
			bool const trailingText = std::any_of(
				std::next(value.begin(), processed), value.end(),
				[](unsigned char character) {
					return !std::isspace(character);
				});
			if (!std::isfinite(parsed)) {
				throw SimulationPreparation::Exception(
					fileName + " contains a non-finite " + description + ".");
			}
			if (trailingText) {
				throw SimulationPreparation::Exception(
					fileName + " contains trailing text in " +
					description + ".");
			}
			return parsed;
		}
		catch (SimulationPreparation::Exception const&) {
			throw;
		}
		catch (std::exception const& error) {
			throw SimulationPreparation::Exception(
				"Could not parse " + description + " in " + fileName +
				": " + error.what());
		}
	}

	int parseInt(
		std::string const& value,
		std::string const& fileName,
		std::string const& description)
	{
		try {
			std::size_t processed = 0;
			int const parsed = std::stoi(value, &processed);
			bool const trailingText = std::any_of(
				std::next(value.begin(), processed), value.end(),
				[](unsigned char character) {
					return !std::isspace(character);
				});
			if (trailingText) {
				throw SimulationPreparation::Exception(
					fileName + " contains trailing text in " +
					description + ".");
			}
			return parsed;
		}
		catch (SimulationPreparation::Exception const&) {
			throw;
		}
		catch (std::exception const& error) {
			throw SimulationPreparation::Exception(
				"Could not parse " + description + " in " + fileName +
				": " + error.what());
		}
	}

	void requireColumns(
		std::vector<std::string> const& values,
		std::size_t required,
		std::string const& fileName,
		std::string const& description)
	{
		if (values.size() < required) {
			throw SimulationPreparation::Exception(
				fileName + " has " + description + " with " +
				std::to_string(values.size()) + " columns; expected at least " +
				std::to_string(required) + ".");
		}
	}

	std::vector<float> loadRequiredFloatLines(
		std::string const& fileName,
		std::size_t required)
	{
		auto file = std::ifstream(fileName);
		if (!file) {
			throw SimulationPreparation::Exception(
				"Could not find file " + fileName + "!");
		}

		std::vector<float> values;
		values.reserve(required);
		std::string line;
		while (values.size() < required && std::getline(file, line)) {
			values.push_back(parseFiniteFloat(
				line, fileName,
				"value " + std::to_string(values.size() + 1)));
		}
		if (values.size() != required) {
			throw SimulationPreparation::Exception(
				fileName + " contains " + std::to_string(values.size()) +
				" values; expected " + std::to_string(required) + ".");
		}
		return values;
	}

	int seatPartyIndex(
		PollingProject const& project,
		Seat const& seat,
		std::string const& partyCode,
		std::string const& inputName)
	{
		if (partyCode == OthersCode) return OthersIndex;

		int const partyIndex =
			project.parties().indexByShortCode(partyCode);
		if (partyIndex == PartyCollection::InvalidIndex) {
			throw SimulationPreparation::Exception(
				"Seat " + seat.name + " has an unknown party code (" +
				partyCode + ") in " + inputName + ".");
		}
		return partyIndex;
	}

	void loadSeatStatisticsFile(
		std::string const& fileName,
		SimulationRun::SeatStatistics& statistics)
	{
		auto file = std::ifstream(fileName);
		if (!file) {
			throw SimulationPreparation::Exception(
				"Could not find file " + fileName + "!");
		}

		try {
			std::string line;
			if (!std::getline(file, line)) {
				throw SimulationPreparation::Exception(
					"Seat-statistics file " + fileName + " is empty.");
			}

			auto scaleStrings = splitString(line, ",");
			if (scaleStrings.size() < 2) {
				throw SimulationPreparation::Exception(
					"Seat-statistics file " + fileName +
					" must contain at least two scale points.");
			}

			std::vector<float> scale;
			scale.reserve(scaleStrings.size());
			for (auto const& value : scaleStrings) {
				scale.push_back(parseFiniteFloat(
					value, fileName, "scale value"));
			}

			float const scaleStep = scale[1] - scale[0];
			if (!(scaleStep > 0.0f) || !std::isfinite(scaleStep)) {
				throw SimulationPreparation::Exception(
					"Seat-statistics file " + fileName +
					" must use a positive scale step.");
			}
			for (std::size_t index = 2; index < scale.size(); ++index) {
				float const expected = scale[0] + scaleStep * float(index);
				float const tolerance =
					0.0001f * std::max(1.0f, std::abs(expected));
				if (std::abs(scale[index] - expected) > tolerance) {
					throw SimulationPreparation::Exception(
						"Seat-statistics file " + fileName +
						" must use evenly spaced scale points.");
				}
			}

			statistics = SimulationRun::SeatStatistics();
			statistics.scaleLow = scale.front();
			statistics.scaleStep = scaleStep;
			statistics.scaleHigh = scale.back();

			int const trendTypeCount =
				int(SimulationRun::SeatStatistics::TrendType::Num);
			for (int trendType = 0; trendType < trendTypeCount; ++trendType) {
				if (!std::getline(file, line)) {
					throw SimulationPreparation::Exception(
						"Seat-statistics file " + fileName +
						" does not contain all required statistic rows.");
				}
				auto values = splitString(line, ",");
				if (values.size() != scale.size()) {
					throw SimulationPreparation::Exception(
						"Seat-statistics file " + fileName +
						" has a statistic row with " +
						std::to_string(values.size()) +
						" values; expected " +
						std::to_string(scale.size()) + ".");
				}
				for (auto const& value : values) {
					float const statistic =
						parseFiniteFloat(value, fileName, "statistic");
					auto const type =
						SimulationRun::SeatStatistics::TrendType(trendType);
					if ((type ==
						SimulationRun::SeatStatistics::TrendType::LowerRmse ||
						type ==
						SimulationRun::SeatStatistics::TrendType::UpperRmse) &&
						statistic < 0.0f) {
						throw SimulationPreparation::Exception(
							"Seat-statistics file " + fileName +
							" contains a negative RMSE.");
					}
					statistics.trend[trendType].push_back(statistic);
				}
			}
		}
		catch (SimulationPreparation::Exception const&) {
			throw;
		}
		catch (std::exception const& error) {
			throw SimulationPreparation::Exception(
				"Could not parse seat-statistics file " + fileName +
				": " + error.what());
		}
	}

	void loadSeatModifierFile(
		PollingProject const& project,
		std::string const& regionCode,
		std::string const& fileName,
		std::vector<float>& modifiers)
	{
		modifiers.assign(
			project.seats().count(),
			std::numeric_limits<float>::quiet_NaN());
		std::vector<int> matchPriority(project.seats().count(), 0);
		auto file = std::ifstream(fileName);
		if (!file) {
			throw SimulationPreparation::Exception(
				"Could not find file " + fileName + "!");
		}

		std::string line;
		while (std::getline(file, line)) {
			if (line.empty()) continue;
			auto const values = splitString(line, ",");
			requireColumns(values, 3, fileName, "seat-modifier row");
			if (values[1] != regionCode) continue;

			float const modifier =
				parseFiniteFloat(values[2], fileName, "seat modifier");
			if (modifier <= 0.0f) {
				throw SimulationPreparation::Exception(
					fileName + " has a non-positive modifier for " +
					values[0] + ".");
			}
			for (int seatIndex = 0;
				seatIndex < project.seats().count(); ++seatIndex) {
				auto const& seat = project.seats().viewByIndex(seatIndex);
				int priority = 0;
				if (values[0] == seat.name) {
					priority = 2;
				}
				else if (
					(!seat.previousName.empty() &&
						values[0] == seat.previousName) ||
					(!seat.useFpResults.empty() &&
						values[0] == seat.useFpResults)) {
					priority = 1;
				}
				if (priority >= matchPriority[seatIndex] && priority > 0) {
					modifiers[seatIndex] = modifier;
					matchPriority[seatIndex] = priority;
				}
			}
		}

		for (int seatIndex = 0;
			seatIndex < project.seats().count(); ++seatIndex) {
			if (!std::isfinite(modifiers[seatIndex])) {
				throw SimulationPreparation::Exception(
					fileName + " has no modifier for seat " +
					project.seats().viewByIndex(seatIndex).name + ".");
			}
		}
	}
}

SimulationPreparation::SimulationPreparation(PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), sim(sim), run(run)
{
}

void SimulationPreparation::prepareForIterations()
{
	// Validate project structure before loading derived model inputs. This
	// keeps bad configuration out of the heavily threaded iteration stage.
	resetLatestReport();

	storeTermCode();
	determineSpecificPartyIndices();
	validateIterationInputs();

	// Reset all output accumulated by a prior preparation or iteration pass.
	resetRegionSpecificOutput();
	resetSeatSpecificOutput();
	resetOtherOutput();

	// Load election-wide and seat-level model inputs.
	loadTppSwingFactors();
	loadPreviousPreferenceFlows();
	loadNcPreferenceFlows();
	loadPreviousElectionBaselineVotes();
	loadSeatTypes();
	calculateRegionalProportion();
	loadGreensSeatStatistics();
	loadIndSeatStatistics();
	loadIndEmergence();
	loadPopulistSeatStatistics();
	loadPopulistSeatModifiers();
	loadCentristSeatStatistics();
	loadCentristSeatModifiers();
	loadOthSeatStatistics();

	loadIndividualSeatParameters();
	loadPastSeatResults();
	validatePastSeatResults();
	determineEffectiveSeatTppModifiers();
	determinePreviousSwingDeviations();
	accumulateRegionStaticInfo();
	loadSeatBettingOdds();
	loadSeatMinorViability();
	loadSeatPolls();
	loadSeatTppPolls();
	loadNationalsParameters();
	loadNationalsSeatExpectations();

	// Prepare regional inputs and result containers after seat-region
	// membership has been validated and counted.
	determinePreviousVoteEnrolmentRatios();
	resizeRegionSeatCountOutputs();
	countInitialRegionSeatLeads();
	loadRegionBaseBehaviours();
	loadRegionPollBehaviours();
	loadRegionMixBehaviours();
	loadOverallRegionMixParameters();
	loadRegionSwingDeviations();
	calculateTotalPopulation();
	calculateIndEmergenceModifier();
	prepareProminentMinors();
	prepareRunningParties();

	// Automatic live preparation owns the current live-results pipeline.
	// Legacy manual-live code is retained for future adaptation; SimulationRun
	// currently prevents that incomplete path from reaching preparation.
	if (run.isLive()) initializeGeneralLiveData();
	if (run.isLiveManual()) loadLiveManualResults();
	if (run.isLiveAutomatic()) {
		auto livePreparation = LivePreparation(project, sim, run);
		try {
			livePreparation.prepareLiveAutomatic();
		}
		catch (LivePreparation::Exception const& e) {
			throw Exception(e.what());
		}
	}

	calculateLiveAggregates();

	resetResultCounts();
}

void SimulationPreparation::resetLatestReport()
{
	sim.latestReport = Simulation::Report();
}

void SimulationPreparation::resetRegionSpecificOutput()
{
	run.regionLocalModifierAverage.assign(project.regions().count(), 0.0f);
	regionSeatCount.assign(project.regions().count(), 0);
	run.regionPartyWins.assign(project.regions().count(), {});
	run.regionPartyFpDistribution.assign(project.regions().count(), {});
	run.regionTppDistribution.assign(project.regions().count(), {});
	for (int i = 0; i < project.regions().count(); ++i) {
		for (int j = 0; j < project.parties().count(); ++j) {
			run.regionPartyFpDistribution[i][j] = {};
		}
		run.regionTppDistribution[i] = {};
	}
}

void SimulationPreparation::resetSeatSpecificOutput()
{
	std::size_t const seatCount = project.seats().count();
	run.seatPartyOneMarginSum.assign(seatCount, 0.0);
	run.partyOneWinPercent.assign(seatCount, 0.0);
	run.partyTwoWinPercent.assign(seatCount, 0.0);
	run.othersWinPercent.assign(seatCount, 0.0);
	if (run.natPartyIndex >= 0) {
		run.coalitionWinPercent.assign(seatCount, 0.0);
	}
	else {
		run.coalitionWinPercent.clear();
	}

	run.seatFirstPartyPreferenceFlow.assign(seatCount, 0.0f);
	run.seatPreferenceFlowVariation.assign(seatCount, 0.0f);
	run.seatTcpTally.assign(seatCount, { 0, 0 });
	run.seatIndividualBoothGrowth.assign(seatCount, 0.0f);
	run.seatPartyWins.assign(seatCount, {});
	run.cumulativeSeatPartyFpShare.assign(seatCount, {});
	run.seatPartyFpDistribution.assign(seatCount, {});
	run.seatPartyFpZeros.assign(seatCount, {});
	run.seatTcpDistribution.assign(seatCount, {});
	run.seatTppDistribution.assign(seatCount, {});
	if (run.natPartyIndex >= 0) {
		run.seatCoalitionWins.assign(seatCount, {});
	}
	else {
		run.seatCoalitionWins.clear();
	}

	run.seatRegionSwingSums.assign(seatCount, 0.0);
	run.seatElasticitySwingSums.assign(seatCount, 0.0);
	run.seatLocalEffectsSums.assign(seatCount, 0.0);
	run.seatPreviousSwingEffectSums.assign(seatCount, 0.0);
	run.seatFederalSwingEffectSums.assign(seatCount, 0.0);
	run.seatByElectionEffectSums.assign(seatCount, 0.0);
	run.seatThirdPartyExhaustEffectSums.assign(seatCount, 0.0);
	run.seatPollEffectSums.assign(seatCount, 0.0);
	run.seatMrpPollEffectSums.assign(seatCount, 0.0);
	run.seatLocalEffects.assign(seatCount, {});
}

void SimulationPreparation::resetOtherOutput()
{
	std::fill(run.electionTppDistribution.begin(), run.electionTppDistribution.end(), 0);
}

void SimulationPreparation::storeTermCode()
{
	if (project.projections().idToIndex(sim.settings.baseProjection) ==
		ProjectionCollection::InvalidIndex) {
		throw Exception("The simulation does not have a valid base projection.");
	}

	std::string const termCode = project.projections().view(
		sim.settings.baseProjection).getBaseModel(project.models()).getTermCode();
	bool const validYear = termCode.size() >= 4 &&
		std::all_of(termCode.begin(), std::next(termCode.begin(), 4),
			[](unsigned char character) { return std::isdigit(character); });
	if (!validYear || termCode.size() == 4) {
		throw Exception(
			"Election term code \"" + termCode +
			"\" must contain a four-digit year followed by a region code.");
	}
	run.yearCode = termCode.substr(0, 4);
	run.regionCode = termCode.substr(4);
}

void SimulationPreparation::validateIterationInputs() const
{
	auto requireFiniteRange = [](
		float value, float minimum, float maximum,
		std::string const& description, bool allowEndpoints = true) {
		bool const inRange = allowEndpoints ?
			value >= minimum && value <= maximum :
			value > minimum && value < maximum;
		if (!std::isfinite(value) || !inRange) {
			throw Exception(
				description + " must be " +
				(allowEndpoints ? "between " : "strictly between ") +
				std::to_string(minimum) + " and " +
				std::to_string(maximum) + ".");
		}
	};

	if (project.parties().count() < PartyCollection::NumMajorParties) {
		throw Exception("The simulation requires two major parties.");
	}
	if (project.regions().count() == 0) {
		throw Exception("The simulation requires at least one region.");
	}
	if (project.seats().count() == 0) {
		throw Exception("The simulation requires at least one seat.");
	}
	if (sim.settings.numIterations <= 0) {
		throw Exception("The simulation iteration count must be positive.");
	}

	requireFiniteRange(
		sim.settings.prevElection2pp, 0.0f, 100.0f,
		"Previous-election TPP", false);
	if (sim.settings.forceTpp) {
		requireFiniteRange(
			*sim.settings.forceTpp, 0.0f, 100.0f,
			"Forced TPP", false);
	}

	auto const& projection =
		project.projections().view(sim.settings.baseProjection);
	if (sim.settings.fedElectionDate.IsValid() &&
		!projection.getSettings().endDate.IsValid()) {
		throw Exception(
			"A federal election date was configured, but the base "
			"projection has no valid end date.");
	}

	for (int partyIndex = 0;
		partyIndex < project.parties().count(); ++partyIndex) {
		auto const& party = project.parties().viewByIndex(partyIndex);
		std::string const label = party.name.empty() ?
			"Party index " + std::to_string(partyIndex) :
			"Party " + party.name;

		requireFiniteRange(
			party.p1PreferenceFlow, 0.0f, 100.0f,
			label + " preference flow");
		requireFiniteRange(
			party.exhaustRate, 0.0f, 100.0f,
			label + " exhaust rate");
		if (!std::isfinite(party.seatTarget) ||
			party.seatTarget < 0.0f) {
			throw Exception(label + " has an invalid seat target.");
		}
		if (party.ideology < 0 || party.ideology > 4) {
			throw Exception(
				label + " ideology must be between 0 and 4.");
		}
		if (party.consistency < 0 || party.consistency > 2) {
			throw Exception(
				label + " preference consistency must be between 0 and 2.");
		}
		if (!party.homeRegion.empty() &&
			!project.regions().findbyName(party.homeRegion).second) {
			throw Exception(
				label + " refers to an unknown home region: " +
				party.homeRegion + ".");
		}
	}

	for (int regionIndex = 0;
		regionIndex < project.regions().count(); ++regionIndex) {
		auto const& region = project.regions().viewByIndex(regionIndex);
		std::string const label = region.name.empty() ?
			"Region index " + std::to_string(regionIndex) :
			"Region " + region.name;
		if (region.population <= 0) {
			throw Exception(label + " must have a positive population.");
		}
		requireFiniteRange(
			region.lastElection2pp, 0.0f, 100.0f,
			label + " previous-election TPP", false);
		if (!std::isfinite(region.homeRegionMod) ||
			region.homeRegionMod < 0.0f) {
			throw Exception(label + " has an invalid home-region modifier.");
		}
	}

	for (int seatIndex = 0;
		seatIndex < project.seats().count(); ++seatIndex) {
		auto const& seat = project.seats().viewByIndex(seatIndex);
		std::string const label = seat.name.empty() ?
			"Seat index " + std::to_string(seatIndex) :
			"Seat " + seat.name;
		if (project.regions().idToIndex(seat.region) ==
			RegionCollection::InvalidIndex) {
			throw Exception(label + " refers to a region that does not exist.");
		}
		if (project.parties().idToIndex(seat.incumbent) ==
			PartyCollection::InvalidIndex ||
			project.parties().idToIndex(seat.challenger) ==
			PartyCollection::InvalidIndex) {
			throw Exception(
				label + " must have valid incumbent and challenger parties.");
		}
		if (seat.incumbent == seat.challenger) {
			throw Exception(
				label + " has the same incumbent and challenger party.");
		}
		requireFiniteRange(
			seat.tppMargin, -50.0f, 50.0f,
			label + " TPP margin", false);
		for (auto const& [description, value] :
			std::initializer_list<std::pair<std::string, float>>{
				{ "miscellaneous TPP modifier", seat.miscTppModifier },
				{ "previous TPP swing", seat.previousSwing },
				{ "transposed federal swing", seat.transposedTppSwing },
				{ "by-election swing", seat.byElectionSwing } }) {
			if (!std::isfinite(value)) {
				throw Exception(
					label + " has a non-finite " + description + ".");
			}
		}
	}
}

void SimulationPreparation::validatePastSeatResults() const
{
	for (int seatIndex = 0;
		seatIndex < int(run.pastSeatResults.size()); ++seatIndex) {
		auto const& results = run.pastSeatResults[seatIndex];
		std::string const label =
			"Previous results for " +
			project.seats().viewByIndex(seatIndex).name;
		if (results.turnoutCount < 0) {
			throw Exception(label + " have a negative turnout.");
		}
		for (auto const& [partyIndex, voteCount] : results.fpVoteCount) {
			if (voteCount < 0) {
				throw Exception(label + " contain a negative FP vote count.");
			}
		}
		for (auto const& [partyIndex, voteCount] : results.tcpVoteCount) {
			if (!std::isfinite(voteCount) || voteCount < 0.0f) {
				throw Exception(label + " contain an invalid TCP vote count.");
			}
		}
		for (auto const* voteShares :
			{ &results.fpVotePercent, &results.tcpVotePercent }) {
			for (auto const& [partyIndex, voteShare] : *voteShares) {
				if (!std::isfinite(voteShare) ||
					voteShare < 0.0f || voteShare > 100.0f) {
					throw Exception(
						label + " contain a vote share outside 0-100.");
				}
			}
		}
	}
	if (run.totalPreviousTurnout <= 0) {
		throw Exception(
			"Previous results contain no first-preference turnout.");
	}
}

void SimulationPreparation::determineEffectiveSeatTppModifiers()
{
	run.seatPartyOneTppModifier.assign(
		project.seats().count(), 0.0f);
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		Seat const& seat = project.seats().viewByIndex(seatIndex);
		int const incumbentIndex =
			project.parties().idToIndex(seat.incumbent);
		bool const majorParty =
			incumbentIndex >= Mp::First && incumbentIndex <= Mp::Last;
		if (!majorParty) continue;
		typedef SimulationRun::SeatType ST;
		auto type = run.seatTypes[seatIndex];
		bool isRegional = type == ST::Provincial || type == ST::Rural;
		float const direction =
			incumbentIndex == Mp::One ? 1.0f : -1.0f;
		if (isRegional) {
			if (seat.sophomoreCandidate) {
				float effectSize = run.tppSwingFactors.sophomoreCandidateRegional * direction;
				run.seatPartyOneTppModifier[seatIndex] += effectSize;
				run.seatLocalEffects[seatIndex].push_back({ "Candidate sophomore effect", effectSize });
			}
			if (seat.sophomoreParty) {
				float effectSize = run.tppSwingFactors.sophomorePartyRegional * direction;
				run.seatPartyOneTppModifier[seatIndex] += effectSize;
				run.seatLocalEffects[seatIndex].push_back({ "Party sophomore effect", effectSize });
			}
			if (seat.retirement) {
				float effectSize = run.tppSwingFactors.retirementRegional * direction;
				run.seatPartyOneTppModifier[seatIndex] += effectSize;
				run.seatLocalEffects[seatIndex].push_back({ "Retirement effect", effectSize });
			}
		}
		else {
			if (seat.sophomoreCandidate) {
				float effectSize = run.tppSwingFactors.sophomoreCandidateUrban * direction;
				run.seatPartyOneTppModifier[seatIndex] += effectSize;
				run.seatLocalEffects[seatIndex].push_back({ "Candidate sophomore effect", effectSize });
			}
			if (seat.sophomoreParty) {
				float effectSize = run.tppSwingFactors.sophomorePartyUrban * direction;
				run.seatPartyOneTppModifier[seatIndex] += effectSize;
				run.seatLocalEffects[seatIndex].push_back({ "Party sophomore effect", effectSize });
			}
			if (seat.retirement) {
				float effectSize = run.tppSwingFactors.retirementUrban * direction;
				run.seatPartyOneTppModifier[seatIndex] += effectSize;
				run.seatLocalEffects[seatIndex].push_back({ "Retirement effect", effectSize });
			}
		}
		constexpr float DisendorsementMod = 3.2f;
		constexpr float PreviousDisendorsementMod = DisendorsementMod * -1.0f;
		if (seat.disendorsement) {
			float effectSize = DisendorsementMod * direction;
			run.seatPartyOneTppModifier[seatIndex] += DisendorsementMod * direction;
			run.seatLocalEffects[seatIndex].push_back({ "Disendorsement", effectSize });
		}
		if (seat.previousDisendorsement) {
			float effectSize = PreviousDisendorsementMod * direction;
			run.seatPartyOneTppModifier[seatIndex] += PreviousDisendorsementMod * direction;
			run.seatLocalEffects[seatIndex].push_back({ "Recovery from previous disendorsement", effectSize });
		}
		run.seatPartyOneTppModifier[seatIndex] += seat.miscTppModifier;
		if (seat.miscTppModifier != 0.0f) {
			run.seatLocalEffects[seatIndex].push_back({
				"Extraordinary circumstances (treat with extra caution)",
				seat.miscTppModifier });
		}
	}
}

void SimulationPreparation::determinePreviousSwingDeviations()
{
	// Not storing region ids here or relating them to any other process using indices,
	// so using ids rather than indices is fine
	// Also, this should ideally be population-weighted, but since
	// comparisons are being made within states and within-state population
	// differences are fairly small it's a lot easier for implementation to assume all seats
	// are the same size and it won't have a noticeable effect on the outcome
	std::map<int, std::vector<float>> swingsByRegion;
	for (auto const& [id, seat] : project.seats()) {
		swingsByRegion[seat.region].push_back(seat.previousSwing);
	}
	std::map<int, float> averages;
	for (auto const& [id, swings] : swingsByRegion) {
		float sum = std::accumulate(swings.begin(), swings.end(), 0.0f);
		float average = sum / float(swings.size());
		averages[id] = average;
	}
	run.seatPreviousTppSwing.assign(project.seats().count(), 0.0f);
	for (auto const& [id, seat] : project.seats()) {
		int index = project.seats().idToIndex(id);
		run.seatPreviousTppSwing[index] = seat.previousSwing - averages[seat.region];
	}
}

void SimulationPreparation::accumulateRegionStaticInfo()
{
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		Seat const& seat = project.seats().viewByIndex(seatIndex);
		int const regionIndex = project.regions().idToIndex(seat.region);
		if (regionIndex == RegionCollection::InvalidIndex) {
			throw Exception(
				"Seat " + seat.name +
				" refers to a region that does not exist.");
		}
		run.regionLocalModifierAverage[regionIndex] +=
			run.seatPartyOneTppModifier[seatIndex];
		++regionSeatCount[regionIndex];
	}
	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		if (regionSeatCount[regionIndex] > 0) {
			run.regionLocalModifierAverage[regionIndex] /=
				float(regionSeatCount[regionIndex]);
		}
	}
}

void SimulationPreparation::loadSeatBettingOdds()
{
	run.seatBettingOdds.assign(project.seats().count(), {});
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto const& seat = project.seats().viewByIndex(seatIndex);
		for (auto const& [partyCode, odds] : seat.bettingOdds) {
			if (!std::isfinite(odds) || odds <= 0.0f) {
				throw Exception(
					"Seat " + seat.name +
					" has invalid betting odds for " + partyCode + ".");
			}
			int const partyIndex = seatPartyIndex(
				project, seat, partyCode, "betting odds");
			run.seatBettingOdds[seatIndex][partyIndex] = odds;
		}
	}
}

void SimulationPreparation::loadSeatPolls()
{
	run.seatPolls.assign(project.seats().count(), {});
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto const& seat = project.seats().viewByIndex(seatIndex);
		for (auto const& [partyCode, polls] : seat.polls) {
			int const partyIndex = seatPartyIndex(
				project, seat, partyCode, "seat polls");
			for (auto const& [voteShare, credibility] : polls) {
				if (!std::isfinite(voteShare) ||
					voteShare < 0.0f || voteShare > 100.0f) {
					throw Exception(
						"Seat " + seat.name +
						" has an invalid poll result for " +
						partyCode + ".");
				}
			}
			run.seatPolls[seatIndex][partyIndex] = polls;
		}
	}
}

void SimulationPreparation::loadSeatTppPolls()
{
	run.seatTppPolls.assign(project.seats().count(), 0.0f);
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto const& seat = project.seats().viewByIndex(seatIndex);
		auto const& polls = seat.tppPolls;
		if (!polls.empty()) {
			float sum = 0.0f;
			for (auto const& [date, voteShare] : polls) {
				if (!std::isfinite(voteShare) ||
					voteShare <= 0.0f || voteShare >= 100.0f) {
					throw Exception(
						"Seat " + seat.name +
						" has a TPP poll outside the range 0-100.");
				}
				sum += voteShare;
			}
			run.seatTppPolls[seatIndex] = sum / float(polls.size());
		}
	}
	run.seatTppMrpPolls.assign(project.seats().count(), 0.0f);
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto const& seat = project.seats().viewByIndex(seatIndex);
		auto const& polls = seat.tppMrpPolls;
		if (!polls.empty()) {
			float sum = 0.0f;
			for (auto const& [date, voteShare] : polls) {
				if (!std::isfinite(voteShare) ||
					voteShare <= 0.0f || voteShare >= 100.0f) {
					throw Exception(
						"Seat " + seat.name +
						" has an MRP TPP estimate outside the range 0-100.");
				}
				sum += voteShare;
			}
			run.seatTppMrpPolls[seatIndex] = sum / float(polls.size());
		}
	}
}

void SimulationPreparation::loadSeatMinorViability()
{
	run.seatMinorViability.assign(project.seats().count(), {});
	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto const& seat = project.seats().viewByIndex(seatIndex);
		for (auto const& [partyCode, minorViability] : seat.minorViability) {
			if (!std::isfinite(minorViability)) {
				throw Exception(
					"Seat " + seat.name +
					" has non-finite minor-party viability for " +
					partyCode + ".");
			}
			int const partyIndex = seatPartyIndex(
				project, seat, partyCode, "minor-party viability");
			run.seatMinorViability[seatIndex][partyIndex] = minorViability;
		}
	}
}

void SimulationPreparation::determinePreviousVoteEnrolmentRatios()
{
	// if (!run.isLiveAutomatic()) return;

	// // Calculating ordinary and declaration vote totals as a proportion of total enrolment
	// // Will be used to estimate turnout in seats without a previous result to extrapolate from
	// int ordinaryVoteNumerator = 0;
	// int declarationVoteNumerator = 0;
	// int voteDenominator = 0;
	// //for (auto&[key, seat] : project.seats()) {
	// //	if (seat.previousResults) {
	// //		ordinaryVoteNumerator += seat.previousResults->ordinaryVotes();
	// //		declarationVoteNumerator += seat.previousResults->declarationVotes();
	// //		voteDenominator += seat.previousResults->enrolment;
	// //	}
	// //}
	// if (!voteDenominator) return;
	// run.previousOrdinaryVoteEnrolmentRatio = float(ordinaryVoteNumerator) / float(voteDenominator);
	// run.previousDeclarationVoteEnrolmentRatio = float(declarationVoteNumerator) / float(voteDenominator);
}

void SimulationPreparation::resizeRegionSeatCountOutputs()
{
	sim.latestReport.regionPartyIncumbents.assign(
		project.regions().count(), {});
	if (run.natPartyIndex >= 0) {
		sim.latestReport.regionCoalitionIncumbents.assign(
			project.regions().count(), 0);
	}
	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
		sim.latestReport.regionPartyIncumbents[regionIndex].assign(
			project.parties().count(), 0);
		for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
			run.regionPartyWins[regionIndex][partyIndex] = std::vector<int>(regionSeatCount[regionIndex] + 1);
		}
	}
}

void SimulationPreparation::countInitialRegionSeatLeads()
{
	for (auto&[key, seat] : project.seats()) {
		int const regionIndex = project.regions().idToIndex(seat.region);
		int const leadingPartyIndex =
			project.parties().idToIndex(seat.getLeadingParty());
		if (regionIndex == RegionCollection::InvalidIndex ||
			leadingPartyIndex == PartyCollection::InvalidIndex) {
			throw Exception(
				"Seat " + seat.name +
					" has an invalid region or leading party.");
		}
		if (run.natPartyIndex >= 0 &&
			(leadingPartyIndex == Mp::Two ||
				leadingPartyIndex == run.natPartyIndex)) {
			++sim.latestReport.regionCoalitionIncumbents[regionIndex];
		}
		++sim.latestReport.regionPartyIncumbents[regionIndex][leadingPartyIndex];
	}
}

void SimulationPreparation::calculateTotalPopulation()
{
	// Total population is needed for adjusting regional swings after
	// random variation is applied via simulation
	run.totalPopulation = 0.0;
	for (auto const& [key, region] : project.regions()) {
		run.totalPopulation += float(region.population);
	}
}

void SimulationPreparation::loadLiveManualResults()
{
	for (auto outcome = project.outcomes().rbegin(); outcome != project.outcomes().rend(); ++outcome) {
		run.liveSeatTppSwing[project.seats().idToIndex(outcome->seat)] = outcome->partyOneSwing;
		run.liveSeatTcpCounted[project.seats().idToIndex(outcome->seat)] = outcome->getPercentCountedEstimate();
	}
}

void SimulationPreparation::calculateLiveAggregates()
{
	// run.liveOverallTppSwing = 0.0f;
	// run.liveOverallTcpPercentCounted = 0.0f;
	// run.liveOverallTppBasis = 0.0f;
	// run.classicSeatCount = 0.0f;
	// run.sampleRepresentativeness = 0.0f;
	// run.total2cpVotes = 0;
	// run.totalEnrolment = 0;
	// if (run.isLive()) {
	// 	logger << "preparing live aggregates\n";
	// 	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
	// 		updateLiveAggregateForSeat(seatIndex);
	// 	}
	// 	finaliseLiveAggregates();
	// 	logger << "finished preparing live aggregates\n";
	// }

	// if (run.isLiveAutomatic()) {
	// 	sim.latestReport.total2cpPercentCounted = (float(run.totalEnrolment) ? float(run.total2cpVotes) / float(run.totalEnrolment) : 0.0f) * 100.0f;
	// }
	// else if (run.isLiveManual()) {
	// 	sim.latestReport.total2cpPercentCounted = run.liveOverallTcpPercentCounted;
	// }
	// else {
	// 	sim.latestReport.total2cpPercentCounted = 0.0f;
	// }
}

void SimulationPreparation::updateLiveAggregateForSeat([[maybe_unused]] int seatIndex)
{
	// Seat const& seat = project.seats().viewByIndex(seatIndex);
	// if (!seat.isClassic2pp()) return;
	// ++run.classicSeatCount;
	// int regionIndex = project.regions().idToIndex(seat.region);
	// float tcpPercentCounted = run.liveSeatTcpBasis[seatIndex];
	// float tppBasis = run.liveSeatTppBasis[seatIndex];
	// if (!std::isnan(run.liveSeatTppSwing[seatIndex])) {
	// 	float weight = 100.0f - 100.0f / (1.0f + tppBasis * tppBasis * 0.02f);
	// 	float weightedSwing = run.liveSeatTppSwing[seatIndex] * weight;
	// 	run.liveOverallTppSwing += weightedSwing;
	// 	run.liveOverallTppBasis += weight;
	// 	run.liveRegionSwing[regionIndex] += weightedSwing;
	// 	run.liveRegionTppBasis[regionIndex] += weight;
	// }
	// float fpPercentCounted = run.liveSeatFpCounted[seatIndex];
	// for (auto [party, vote] : run.liveSeatFpPercent[seatIndex]) {
	// 	if (!std::isnan(run.liveSeatFpTransformedSwing[seatIndex][party])) {
	// 		float weightedSwing = run.liveSeatFpTransformedSwing[seatIndex][party] * fpPercentCounted;
	// 		run.liveOverallFpSwing[party] += weightedSwing;
	// 		run.liveOverallFpSwingWeight[party] += fpPercentCounted;
	// 	}
	// 	else if (!run.pastSeatResults[seatIndex].fpVoteCount.contains(party) ||
	// 		run.pastSeatResults[seatIndex].fpVoteCount[party] == 0)
	// 	{
	// 		float weightedPercent = run.liveSeatFpPercent[seatIndex][party] * fpPercentCounted;
	// 		run.liveOverallFpNew[party] += weightedPercent;
	// 		run.liveOverallFpNewWeight[party] += fpPercentCounted;
	// 	}
	// }
	// run.liveOverallTcpPercentCounted += tcpPercentCounted;
	// run.liveOverallFpPercentCounted += fpPercentCounted;
	// run.liveRegionTcpPercentCounted[regionIndex] += tcpPercentCounted;
	// ++run.liveRegionClassicSeatCount[regionIndex];
	// run.sampleRepresentativeness += std::min(2.0f, tcpPercentCounted) * 0.5f;
	// //run.total2cpVotes += seat.latestResults->total2cpVotes();
	// //run.totalEnrolment += seat.latestResults->enrolment;
}

void SimulationPreparation::finaliseLiveAggregates()
{
	// if (run.liveOverallTcpPercentCounted) {
	// 	run.liveOverallTppSwing /= run.liveOverallTppBasis;
	// 	run.liveOverallTppBasis /= project.seats().count();
	// 	run.liveOverallTcpPercentCounted /= project.seats().count();
	// 	run.liveOverallFpPercentCounted /= project.seats().count();
	// 	run.sampleRepresentativeness /= run.classicSeatCount;
	// 	run.sampleRepresentativeness = std::sqrt(run.sampleRepresentativeness);
	// 	for (int regionIndex = 0; regionIndex < project.regions().count(); ++regionIndex) {
	// 		run.liveRegionSwing[regionIndex] /= run.liveRegionTppBasis[regionIndex];
	// 		run.liveRegionTppBasis[regionIndex] /= regionSeatCount[regionIndex];
	// 		run.liveRegionTcpPercentCounted[regionIndex] /= regionSeatCount[regionIndex];
	// 	}
	// 	PA_LOG_VAR(run.liveOverallTppSwing);
	// 	PA_LOG_VAR(run.liveOverallTcpPercentCounted);
	// 	PA_LOG_VAR(run.sampleRepresentativeness);
	// }
	// for (auto& [partyIndex, vote] : run.liveOverallFpSwing) {
	// 	vote /= run.liveOverallFpSwingWeight[partyIndex];
	// }
	// for (auto& [partyIndex, vote] : run.liveOverallFpNew) {
	// 	vote /= run.liveOverallFpNewWeight[partyIndex];
	// }
	// PA_LOG_VAR(run.liveOverallFpSwing);
	// PA_LOG_VAR(run.liveOverallFpNew);
}

void SimulationPreparation::resetResultCounts()
{
	run.partyMajority.clear();
	run.partyMinority.clear();
	run.partyMostSeats.clear();
	run.tiedParliament = 0;
	sim.latestReport.partySeatWinFrequency.clear();
	if (run.natPartyIndex >= 0) {
		sim.latestReport.coalitionSeatWinFrequency.assign(
			project.seats().count() + 1, 0);
	}
	sim.latestReport.othersSeatWinFrequency.assign(
		project.seats().count() + 1, 0);
	sim.latestReport.partyPrimaryFrequency.clear();
	sim.latestReport.tppFrequency.clear();
	if (run.natPartyIndex >= 0) sim.latestReport.coalitionFpFrequency.clear();
	sim.latestReport.partyOneSwing = 0.0;
}

void SimulationPreparation::determineSpecificPartyIndices()
{
	run.indPartyIndex = project.parties().indexByShortCode("IND");
	if (run.indPartyIndex == PartyCollection::InvalidIndex) {
		throw Exception(
			"Could not find a party with IND as a short code. "
			"The simulation requires an Independent party.");
	}
	run.grnPartyIndex = project.parties().indexByShortCode("GRN");
	if (run.grnPartyIndex == PartyCollection::InvalidIndex) {
		run.grnPartyIndex = InvalidPartyIndex;
	}
	run.natPartyIndex = project.parties().indexByShortCode("NAT");
	if (run.natPartyIndex == PartyCollection::InvalidIndex) {
		run.natPartyIndex = InvalidPartyIndex;
	}
}

void SimulationPreparation::loadPreviousPreferenceFlows() {
	run.previousPreferenceFlow.clear();
	run.previousExhaustRate.clear();
	std::string const fileName = "analysis/Data/preference-estimates.csv";
	std::vector<std::vector<std::string>> lines;
	try {
		lines = extractElectionDataFromFile(fileName, run.getTermCode());
	}
	catch (std::exception const& error) {
		throw Exception(error.what());
	}
	for (auto const& line : lines) {
		requireColumns(line, 4, fileName, "preference-estimate row");
		std::string party = splitString(line[2], " ")[0];
		int const partyIndex = party == OthersCode ?
			OthersIndex : project.parties().indexByShortCode(party);
		// Estimates for named parties not configured in this project are not
		// part of its simulation. They must not overwrite generic Others.
		if (partyIndex == PartyCollection::InvalidIndex &&
			party != OthersCode) {
			continue;
		}
		float const thisPreferenceFlow = parseFiniteFloat(
			line[3], fileName, "preference flow");
		if (thisPreferenceFlow <= 0.0f ||
			thisPreferenceFlow >= 100.0f) {
			throw Exception(
				fileName + " must have a preference flow strictly between "
				"0 and 100 for " + party + ".");
		}
		run.previousPreferenceFlow[partyIndex] = thisPreferenceFlow;
		if (line.size() >= 5 && !line[4].empty() &&
			line[4][0] != '#') {
			float const thisExhaustRate = parseFiniteFloat(
				line[4], fileName, "exhaust rate");
			if (thisExhaustRate < 0.0f ||
				thisExhaustRate >= 100.0f) {
				throw Exception(
					fileName + " must have an exhaust rate from 0 up to, "
					"but not including, 100 for " + party + ".");
			}
			run.previousExhaustRate[partyIndex] = thisExhaustRate * 0.01f;
		}
		else {
			run.previousExhaustRate[partyIndex] = 0.0f;
		}
	}

	if (!run.previousPreferenceFlow.contains(OthersIndex)) {
		throw Exception(
			fileName + " has no generic Others estimate for " +
			run.getTermCode() + ".");
	}
	run.previousPreferenceFlow[EmergingPartyIndex] = run.previousPreferenceFlow[OthersIndex];
	run.previousExhaustRate[EmergingPartyIndex] = run.previousExhaustRate[OthersIndex];
	run.previousPreferenceFlow[Mp::One] = 100.0f;
	run.previousPreferenceFlow[Mp::Two] = 0.0f;
	run.previousExhaustRate[Mp::One] = 0.0f;
	run.previousExhaustRate[Mp::Two] = 0.0f;
	run.previousPreferenceFlow[CoalitionPartnerIndex] = 15.0f;
	run.previousExhaustRate[CoalitionPartnerIndex] = 0.25f;

	// Ensure any other named parties without a specified preference flow
	// have the same as Others
	for (int partyIndex = 0; partyIndex < project.parties().count(); ++partyIndex) {
		if (!run.previousPreferenceFlow.contains(partyIndex)) {
			run.previousPreferenceFlow[partyIndex] = run.previousPreferenceFlow[OthersIndex];
			run.previousExhaustRate[partyIndex] = run.previousExhaustRate[OthersIndex];
		}
	}
	// This needs to be done last so the preceding step can fill out the IND
	// preference flows if they weren't already specified.
	run.previousPreferenceFlow[EmergingIndIndex] = run.previousPreferenceFlow[run.indPartyIndex];
	run.previousExhaustRate[EmergingIndIndex] = run.previousExhaustRate[run.indPartyIndex];
}

void SimulationPreparation::loadNcPreferenceFlows()
{
	run.ncPreferenceFlow.clear();
	for (auto const& [partyId, party] : project.parties()) {
		int const partyIndex = project.parties().idToIndex(partyId);
		for (auto const& prefFlow : party.ncPreferenceFlow) {
			int const firstParty =
				project.parties().indexByShortCode(prefFlow.first.first);
			int const secondParty =
				project.parties().indexByShortCode(prefFlow.first.second);
			if (firstParty == PartyCollection::InvalidIndex ||
				secondParty == PartyCollection::InvalidIndex) {
				throw Exception(
					"Party " + party.name +
					" has a non-classic preference flow referencing an "
					"unknown party.");
			}
			if (!std::isfinite(prefFlow.second) ||
				prefFlow.second <= 0.0f ||
				prefFlow.second >= 100.0f) {
				throw Exception(
					"Party " + party.name +
					" must have non-classic preference flows strictly "
					"between 0 and 100.");
			}
			run.ncPreferenceFlow[partyIndex][{firstParty, secondParty}] = prefFlow.second;
			run.ncPreferenceFlow[partyIndex][{secondParty, firstParty}] = 100.0f - prefFlow.second;
			if (prefFlow.first.first == "IND") {
				run.ncPreferenceFlow[partyIndex][{EmergingIndIndex, secondParty}] = prefFlow.second;
				run.ncPreferenceFlow[partyIndex][{secondParty, EmergingIndIndex}] = 100.0f - prefFlow.second;
			}
			else if (prefFlow.first.second == "IND") {
				run.ncPreferenceFlow[partyIndex][{firstParty, EmergingIndIndex}] = prefFlow.second;
				run.ncPreferenceFlow[partyIndex][{EmergingIndIndex, firstParty}] = 100.0f - prefFlow.second;
			}
		}
	}
}

const std::map<std::string, std::string> simplifiedStringToPartyCode = {
	{"Labor", "ALP"},
	{"Liberal", "LIB"},
	{"National", "NAT"},
	{"Greens", "GRN"},
	{"One Nation", "ONP"},
	{"United Australia", "UAP"},
	{"Independent", "IND"},
	{"Katter's Australian", "KAP"},
	{"Centre Alliance", "CA"},
	{"Democrats", "DEM"},
	{"SFF", "SFF"}
};

void SimulationPreparation::loadPastSeatResults()
{
	if (sim.settings.prevTermCodes.empty()) {
		throw Exception("No previous term codes given!");
	}
	run.pastSeatResults.assign(
		project.seats().count(), SimulationRun::PastSeatResult());
	run.totalPreviousTurnout = 0;
	std::string const fileName =
		"analysis/elections/results_" +
		sim.settings.prevTermCodes[0] + ".csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	bool fpMode = false;
	int currentSeat = -1;
	bool indSeen = false;
	std::map<std::string, SimulationRun::PastSeatResult> unmatchedResults;
	std::string currentUnmatched;
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty()) continue;
		auto values = splitString(line, ",");
		if (values[0] == "fp") {
			fpMode = true;
		}
		else if (values[0] == "tcp") {
			fpMode = false;
		}
		else if (values[0] == "Seat") {
			requireColumns(values, 2, fileName, "seat row");
			indSeen = false;
			try {
				currentSeat = project.seats().idToIndex(project.seats().accessByName(values[1], true).first);
			}
			catch (SeatDoesntExistException) {
				// Seat might have been abolished, so no need to give an error, log it in case it's wrong
				if (!run.doingBettingOddsCalibrations) logger << "Could not find a match for seat " + values[1] + ". This is ok if the seat was abolished.\n";
				currentSeat = -1;
				unmatchedResults.insert({ values[1] , SimulationRun::PastSeatResult() });
				currentUnmatched = values[1];
			}
		}
		else if (values.size() >= 4) {
			// Candidate names are not quoted and can contain commas. The four
			// result fields are stable at the end of each row, so parse from
			// there rather than assuming the candidate occupies one column.
			std::size_t const partyColumn = values.size() - 4;
			std::string const partyStr = values[partyColumn];
			int const voteCount = parseInt(
				values[partyColumn + 1], fileName, "vote count");
			float const votePercent = parseFiniteFloat(
				values[partyColumn + 2], fileName, "vote percentage");
			if (voteCount < 0.0f ||
				votePercent < 0.0f || votePercent > 100.0f) {
				throw Exception(
					fileName + " contains a negative count or vote "
					"percentage outside 0-100.");
			}
			std::string shortCodeUsed;
			if (simplifiedStringToPartyCode.count(partyStr)) {
				shortCodeUsed = simplifiedStringToPartyCode.at(partyStr);
			}
			// Historical parties not represented in the current project are
			// deliberately combined into Others.
			int partyIndex =
				project.parties().indexByShortCode(shortCodeUsed);
			if (fpMode) {
				if (shortCodeUsed == "IND") {
					if (indSeen) {
						partyIndex = OthersIndex;
					}
					else {
						indSeen = true;
					}
				}
				if (currentSeat < 0) {
					if (currentUnmatched.empty()) {
						throw Exception(
							fileName + " contains a result before its first seat.");
					}
					unmatchedResults.at(currentUnmatched).fpVoteCount[partyIndex] += voteCount;
					unmatchedResults.at(currentUnmatched).fpVotePercent[partyIndex] += votePercent;
				}
				else {
					run.pastSeatResults[currentSeat].fpVoteCount[partyIndex] += voteCount;
					run.pastSeatResults[currentSeat].fpVotePercent[partyIndex] += votePercent;
				}
			}
			else {
				if (currentSeat < 0) {
					if (currentUnmatched.empty()) {
						throw Exception(
							fileName + " contains a result before its first seat.");
					}
					unmatchedResults.at(currentUnmatched).tcpVoteCount[partyIndex] += voteCount;
					unmatchedResults.at(currentUnmatched).tcpVotePercent[partyIndex] += votePercent;
				}
				else {
					run.pastSeatResults[currentSeat].tcpVoteCount[partyIndex] += voteCount;
					run.pastSeatResults[currentSeat].tcpVotePercent[partyIndex] += votePercent;
				}
			}
		}
	}
	for (auto& [key, seat] : project.seats()) {
		if (seat.useFpResults.size()) {
			int thisSeatIndex = project.seats().idToIndex(key);
			try {
				int otherSeatIndex = project.seats().idToIndex(project.seats().accessByName(seat.useFpResults).first);
				run.pastSeatResults[thisSeatIndex] = run.pastSeatResults[otherSeatIndex];
			}
			catch (SeatDoesntExistException) {
				if (unmatchedResults.contains(seat.useFpResults)) {
					run.pastSeatResults[thisSeatIndex] = unmatchedResults.at(seat.useFpResults);
				}
				else {
					logger << "WARNING: Could not match FP results for seat " +
						project.seats().viewByIndex(thisSeatIndex).name +
						" - no seat found matching name " + seat.useFpResults + "\n";
				}
			}
		}
	}
	auto requireParty = [&](std::string const& code) {
		int const partyIndex = project.parties().indexByShortCode(code);
		if (partyIndex == PartyCollection::InvalidIndex) {
			throw Exception(
				"The " + run.getTermCode() +
				" historical-results override requires party " + code + ".");
		}
		return partyIndex;
	};
	auto eraseHistoricalParty = [](
		SimulationRun::PastSeatResult& results,
		int partyIndex) {
		results.fpVoteCount.erase(partyIndex);
		results.fpVotePercent.erase(partyIndex);
	};

	// These overrides represent known candidate continuity or by-election
	// evidence that cannot be expressed by the generic party mapping above.
	if (run.getTermCode() == "2023nsw") {
		int const sffPartyIndex = requireParty("SFF");
		int const onPartyIndex = requireParty("ON");
		// Due to the circumstances of these candidates leaving SFF and becoming independents,
		// a special adjustment needs to be made (there is no appropriate historical precedent.)
		// The SFF vote is treated as if it were independent.
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			if (project.seats().viewByIndex(seatIndex).name == "Barwon") {
				auto& results = run.pastSeatResults[seatIndex];
				eraseHistoricalParty(results, sffPartyIndex);
				results.fpVoteCount[run.indPartyIndex] = 15218;
				results.fpVotePercent[run.indPartyIndex] = 32.96f;
				results.fpVoteCount[OthersIndex] = 5873;
				results.fpVotePercent[OthersIndex] = 12.72f;
				results.tcpVotePercent[run.indPartyIndex] = 56.6f;
			}
			else if (project.seats().viewByIndex(seatIndex).name == "Murray") {
				auto& results = run.pastSeatResults[seatIndex];
				eraseHistoricalParty(results, sffPartyIndex);
				eraseHistoricalParty(results, onPartyIndex);
				results.fpVoteCount[run.indPartyIndex] = 18305;
				results.fpVotePercent[run.indPartyIndex] = 38.75f;
				results.fpVoteCount[OthersIndex] = 6919;
				results.fpVotePercent[OthersIndex] = 14.65f;
				results.tcpVotePercent[run.indPartyIndex] = 53.54f;
			}
			else if (project.seats().viewByIndex(seatIndex).name == "Orange") {
				auto& results = run.pastSeatResults[seatIndex];
				eraseHistoricalParty(results, sffPartyIndex);
				results.fpVoteCount[run.indPartyIndex] = 24718;
				results.fpVotePercent[run.indPartyIndex] = 49.15f;
				results.fpVoteCount[OthersIndex] = 4849;
				results.fpVotePercent[OthersIndex] = 9.64f;
				results.tcpVotePercent[run.indPartyIndex] = 65.18f;
			}
		}
	} else if (run.getTermCode() == "2024qld") {
		int const kapPartyIndex = requireParty("KAP");
		int const onPartyIndex = requireParty("ONP");
		int const uapPartyIndex = requireParty("UAP");
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			if (project.seats().viewByIndex(seatIndex).name == "Mirani") {
				auto& results = run.pastSeatResults[seatIndex];
				eraseHistoricalParty(results, onPartyIndex);
				eraseHistoricalParty(results, uapPartyIndex);
				results.fpVoteCount[kapPartyIndex] = 9320;
				results.fpVotePercent[kapPartyIndex] = 31.66f;
				results.fpVoteCount[OthersIndex] = 1871;
				results.fpVotePercent[OthersIndex] = 6.36f;
				results.tcpVotePercent[kapPartyIndex] = 58.98f;
			}
		}
	}
	else if (run.getTermCode() == "2028fed") {
		int onPartyIndex = project.parties().indexByShortCode("ONP");
		if (onPartyIndex == PartyCollection::InvalidIndex) {
			onPartyIndex = requireParty("ON");
		}
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			if (project.seats().viewByIndex(seatIndex).name == "Calare") {
				// Assign Kate Hook's FPs to Andrew Gee - not doing this causes his support to be seriously underestimated
				run.pastSeatResults[seatIndex].fpVoteCount[run.indPartyIndex] = 41928;
				run.pastSeatResults[seatIndex].fpVotePercent[run.indPartyIndex] = 39.46f;
				run.pastSeatResults[seatIndex].fpVoteCount[OthersIndex] = 9723;
				run.pastSeatResults[seatIndex].fpVotePercent[OthersIndex] = 9.15f;
			}
			if (project.seats().viewByIndex(seatIndex).name == "Farrer") {
				// Model as direct LNP votes to ON directly to match by-election result
				// exact modelling of major party votes not critical as only the ON vote is used in a substantial way
				run.pastSeatResults[seatIndex].fpVoteCount[onPartyIndex] = 40751;
				run.pastSeatResults[seatIndex].fpVotePercent[onPartyIndex] = 39.53f;
				run.pastSeatResults[seatIndex].fpVoteCount[Mp::Two] = 10795;
				run.pastSeatResults[seatIndex].fpVotePercent[Mp::Two] = 10.47f;
			}
		}
	}
	else if (run.getTermCode() == "2027nsw") {
		for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
			if (project.seats().viewByIndex(seatIndex).name == "Kiama") {
				// Use preliminary by-election projection (temporary)
				auto& results = run.pastSeatResults[seatIndex];
				results.fpVoteCount.clear();
				results.fpVotePercent.clear();
				results.fpVoteCount[Mp::One] = 17423;
				results.fpVotePercent[Mp::One] = 37.5f;
				results.fpVoteCount[Mp::Two] = 12173;
				results.fpVotePercent[Mp::Two] = 26.2f;
				results.fpVoteCount[run.grnPartyIndex] = 3805;
				results.fpVotePercent[run.grnPartyIndex] = 8.2f;
				results.fpVoteCount[OthersIndex] = 13068;
				results.fpVotePercent[OthersIndex] = 28.1f;
			}
		}
	}

	// Overrides above are part of the effective historical result. Derive
	// turnout and the prior "others" strength only after they are applied so
	// all downstream weighting uses that final result.
	run.totalPreviousTurnout = 0;
	for (auto& results : run.pastSeatResults) {
		results.prevOthers = 0.0f;
		results.turnoutCount = 0;
		for (auto const& [party, voteShare] : results.fpVotePercent) {
			bool const isMajor =
				party == Mp::One || party == Mp::Two;
			if (!isMajor && party != run.grnPartyIndex) {
				results.prevOthers += std::min(
					voteShare,
					detransformVoteShare(run.indEmergence.fpThreshold));
			}
		}
		results.prevOthers = std::max(2.0f, results.prevOthers);
		for (auto const& [party, voteCount] : results.fpVoteCount) {
			results.turnoutCount += voteCount;
		}
		run.totalPreviousTurnout += results.turnoutCount;
	}
}

void SimulationPreparation::loadSeatTypes()
{
	run.seatTypes.assign(
		project.seats().count(), SimulationRun::SeatType::InnerMetro);
	std::vector<int> matchPriority(project.seats().count(), 0);
	std::string const fileName = "analysis/Data/seat-types.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty()) continue;
		auto const values = splitString(line, ",");
		requireColumns(values, 3, fileName, "seat-type row");
		if (values[1] != run.regionCode) continue;

		int const seatType = parseInt(
			values[2], fileName, "seat type");
		if (seatType < int(SimulationRun::SeatType::InnerMetro) ||
			seatType > int(SimulationRun::SeatType::Rural)) {
			throw Exception(
				fileName + " has an invalid seat type for " +
				values[0] + ".");
		}
		for (int seatIndex = 0;
			seatIndex < project.seats().count(); ++seatIndex) {
			auto const& seat = project.seats().viewByIndex(seatIndex);
			int priority = 0;
			if (values[0] == seat.name) {
				priority = 2;
			}
			else if (
				(!seat.previousName.empty() &&
					values[0] == seat.previousName) ||
				(!seat.useFpResults.empty() &&
					values[0] == seat.useFpResults)) {
				priority = 1;
			}
			if (priority >= matchPriority[seatIndex] && priority > 0) {
				run.seatTypes[seatIndex] =
					SimulationRun::SeatType(seatType);
				matchPriority[seatIndex] = priority;
			}
		}
	}
	for (int seatIndex = 0;
		seatIndex < project.seats().count(); ++seatIndex) {
		if (!matchPriority[seatIndex]) {
			throw Exception(
				fileName + " has no type for seat " +
				project.seats().viewByIndex(seatIndex).name + ".");
		}
	}
}

void SimulationPreparation::loadGreensSeatStatistics()
{
	loadSeatStatisticsFile(
		"analysis/Seat Statistics/statistics_GRN.csv",
		run.greensSeatStatistics);
}

void SimulationPreparation::loadIndSeatStatistics()
{
	loadSeatStatisticsFile(
		"analysis/Seat Statistics/statistics_IND.csv",
		run.indSeatStatistics);
}

void SimulationPreparation::loadOthSeatStatistics()
{
	loadSeatStatisticsFile(
		"analysis/Seat Statistics/statistics_OTH.csv",
		run.othSeatStatistics);
}

void SimulationPreparation::loadIndEmergence()
{
	std::string const fileName =
		"analysis/Seat Statistics/statistics_emerging_IND.csv";
	auto const values = loadRequiredFloatLines(fileName, 15);
	if (values[0] <= 0.0f || values[0] >= 100.0f) {
		throw Exception(
			fileName + " has an FP threshold outside the open range 0-100.");
	}
	run.indEmergence.fpThreshold = transformVoteShare(values[0]);
	run.indEmergence.baseRate = values[1];
	run.indEmergence.fedRateMod = values[2];
	run.indEmergence.ruralRateMod = values[3];
	run.indEmergence.provincialRateMod = values[4];
	run.indEmergence.outerMetroRateMod = values[5];
	run.indEmergence.prevOthersRateMod = values[6];
	run.indEmergence.voteRmse = values[7];
	run.indEmergence.voteKurtosis = values[8];
	run.indEmergence.fedVoteCoeff = values[9];
	run.indEmergence.ruralVoteCoeff = values[10];
	run.indEmergence.provincialVoteCoeff = values[11];
	run.indEmergence.outerMetroVoteCoeff = values[12];
	run.indEmergence.prevOthersVoteCoeff = values[13];
	run.indEmergence.voteIntercept = values[14];
	if (run.indEmergence.voteRmse < 0.0f) {
		throw Exception(fileName + " has a negative vote RMSE.");
	}
	if (std::abs(
		run.indEmergence.voteIntercept -
		run.indEmergence.fpThreshold) <= 0.000001f) {
		throw Exception(
			fileName + " has equal vote-intercept and FP-threshold "
			"values, which would cause division by zero.");
	}
}

void SimulationPreparation::loadPopulistSeatStatistics()
{
	std::string const fileName =
		"analysis/Seat Statistics/statistics_populist.csv";
	auto const values = loadRequiredFloatLines(fileName, 5);
	run.populistStatistics.lowerRmse = values[0];
	run.populistStatistics.upperRmse = values[1];
	run.populistStatistics.lowerKurtosis = values[2];
	run.populistStatistics.upperKurtosis = values[3];
	run.populistStatistics.homeStateCoefficient = values[4];
	if (run.populistStatistics.lowerRmse < 0.0f ||
		run.populistStatistics.upperRmse < 0.0f) {
		throw Exception(fileName + " has a negative RMSE.");
	}
}

void SimulationPreparation::loadPopulistSeatModifiers()
{
	loadSeatModifierFile(
		project, run.regionCode,
		"analysis/Seat Statistics/modifiers_populist.csv",
		run.seatPopulistModifiers);
}

void SimulationPreparation::loadCentristSeatStatistics()
{
	std::string const fileName =
		"analysis/Seat Statistics/statistics_centrist.csv";
	auto const values = loadRequiredFloatLines(fileName, 5);
	run.centristStatistics.lowerRmse = values[0];
	run.centristStatistics.upperRmse = values[1];
	run.centristStatistics.lowerKurtosis = values[2];
	run.centristStatistics.upperKurtosis = values[3];
	run.centristStatistics.homeStateCoefficient = values[4];
	if (run.centristStatistics.lowerRmse < 0.0f ||
		run.centristStatistics.upperRmse < 0.0f) {
		throw Exception(fileName + " has a negative RMSE.");
	}
}

void SimulationPreparation::loadCentristSeatModifiers()
{
	loadSeatModifierFile(
		project, run.regionCode,
		"analysis/Seat Statistics/modifiers_centrist.csv",
		run.seatCentristModifiers);
}

void SimulationPreparation::loadPreviousElectionBaselineVotes()
{
	run.previousFpVoteShare.clear();
	std::string const fileName = "analysis/Data/prior-results.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty()) continue;
		auto const values = splitString(line, ",");
		requireColumns(values, 4, fileName, "prior-result row");
		if (values[0] == run.yearCode && values[1] == run.regionCode) {
			std::string partyCode = splitString(values[2], " ")[0];
			// exclusive others is what we want to store, overall others isn't used
			if (partyCode == "OTH") continue;
			int partyIndex = project.parties().indexByShortCode(partyCode);
			// ignore parties that were significant last election but not expected to be so for this election
			if (partyIndex == OthersIndex && partyCode != UnnamedOthersCode) continue;
			float const voteShare = parseFiniteFloat(
				values[3], fileName, "prior vote share");
			if (voteShare < 0.0f || voteShare > 100.0f) {
				throw Exception(
					fileName + " has a prior vote share outside 0-100.");
			}
			run.previousFpVoteShare[partyIndex] = voteShare;
		}
	}
}

void SimulationPreparation::loadRegionBaseBehaviours()
{
	run.regionBaseBehaviour.clear();
	std::string const fileName =
		"analysis/Regional/" + run.getTermCode() + "-regions-base.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		if (run.regionCode == "fed") logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
		return;
	}
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty()) continue;
		auto const values = splitString(line, ",");
		requireColumns(values, 5, fileName, "regional base row");
		if (values[0] == "all") continue;
		auto match = project.regions().findbyAnalysisCode(values[0]);
		if (match.first == Region::InvalidId) {
			if (values[0] != "all") logger << "Warning: Could not find region to match analysis code " + values[0] + "\n";
			continue;
		}
		auto regionIndex = project.regions().idToIndex(match.first);
		auto& behaviour = run.regionBaseBehaviour[regionIndex];
		behaviour.overallSwingCoeff = parseFiniteFloat(
			values[1], fileName, "overall-swing coefficient");
		behaviour.baseSwingDeviation = parseFiniteFloat(
			values[2], fileName, "base-swing deviation");
		behaviour.rmse = parseFiniteFloat(
			values[3], fileName, "regional RMSE");
		behaviour.kurtosis = parseFiniteFloat(
			values[4], fileName, "regional kurtosis");
		if (behaviour.rmse < 0.0f) {
			throw Exception(
				fileName + " has a negative RMSE for " + values[0] + ".");
		}
	}
}

void SimulationPreparation::loadRegionPollBehaviours()
{
	run.regionPollBehaviour.clear();
	run.generalPollBehaviour = SimulationRun::RegionPollBehaviour();
	std::string const fileName =
		"analysis/Regional/" + run.getTermCode() + "-regions-polled.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		if (run.regionCode == "fed") logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
		return;
	}
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty()) continue;
		auto const values = splitString(line, ",");
		requireColumns(values, 3, fileName, "regional polling row");
		if (values[0] == "all") {
			// Retained for possible shrinkage of noisy region-specific
			// estimates, but not currently used by the simulation.
			run.generalPollBehaviour.overallSwingCoeff = parseFiniteFloat(
				values[1], fileName, "overall polling coefficient");
			run.generalPollBehaviour.baseSwingDeviation = parseFiniteFloat(
				values[2], fileName, "overall polling deviation");
			continue;
		}
		auto match = project.regions().findbyAnalysisCode(values[0]);
		if (match.first == Region::InvalidId) {
			if (values[0] != "all") logger << "Warning: Could not find region to match analysis code " + values[0] + "\n";
			continue;
		}
		auto regionIndex = project.regions().idToIndex(match.first);
		auto& behaviour = run.regionPollBehaviour[regionIndex];
		behaviour.overallSwingCoeff = parseFiniteFloat(
			values[1], fileName, "regional polling coefficient");
		behaviour.baseSwingDeviation = parseFiniteFloat(
			values[2], fileName, "regional polling deviation");
	}
}

void SimulationPreparation::loadRegionMixBehaviours()
{
	run.regionMixBehaviour.clear();
	std::string const fileName =
		"analysis/Regional/" + run.getTermCode() + "-mix-regions.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		if (run.regionCode == "fed") logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
		return;
	}
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty()) continue;
		auto const values = splitString(line, ",");
		requireColumns(values, 3, fileName, "regional mix row");
		auto match = project.regions().findbyAnalysisCode(values[0]);
		if (match.first == Region::InvalidId) {
			if (values[0] != "all") logger << "Warning: Could not find region to match analysis code " + values[0] + "\n";
			continue;
		}
		auto regionIndex = project.regions().idToIndex(match.first);
		auto& behaviour = run.regionMixBehaviour[regionIndex];
		behaviour.bias = parseFiniteFloat(
			values[1], fileName, "regional mix bias");
		behaviour.rmse = parseFiniteFloat(
			values[2], fileName, "regional mix RMSE modifier");
		if (behaviour.rmse < 0.0f) {
			throw Exception(
				fileName + " has a negative RMSE modifier for " +
				values[0] + ".");
		}
	}
}

void SimulationPreparation::loadOverallRegionMixParameters()
{
	run.regionMixParameters = SimulationRun::RegionMixParameters();
	std::string const fileName =
		"analysis/Regional/" + run.getTermCode() + "-mix-parameters.csv";
	auto file = std::ifstream(fileName);
	if (!file) {
		// Not finding a file is fine, but log a message in case this isn't intended behaviour
		if (run.regionCode == "fed") logger << "Info: Could not find file " + fileName + " - default region behaviours will be used\n";
		return;
	}
	bool foundMixFactor = false;
	bool foundRmse = false;
	bool foundKurtosis = false;
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty()) continue;
		auto const values = splitString(line, ",");
		if (values[0] == "mix_factor") {
			requireColumns(values, 3, fileName, "mix-factor row");
			run.regionMixParameters.mixFactorA = parseFiniteFloat(
				values[1], fileName, "mix-factor coefficient");
			run.regionMixParameters.mixFactorB = parseFiniteFloat(
				values[2], fileName, "mix-factor time coefficient");
			foundMixFactor = true;
		}
		else if (values[0] == "rmse") {
			requireColumns(values, 4, fileName, "RMSE row");
			run.regionMixParameters.rmseA = parseFiniteFloat(
				values[1], fileName, "RMSE coefficient");
			run.regionMixParameters.rmseB = parseFiniteFloat(
				values[2], fileName, "RMSE time coefficient");
			run.regionMixParameters.rmseC = parseFiniteFloat(
				values[3], fileName, "RMSE asymptote");
			foundRmse = true;
		}
		else if (values[0] == "kurtosis") {
			requireColumns(values, 3, fileName, "kurtosis row");
			run.regionMixParameters.kurtosisA = parseFiniteFloat(
				values[1], fileName, "kurtosis coefficient");
			run.regionMixParameters.kurtosisB = parseFiniteFloat(
				values[2], fileName, "kurtosis intercept");
			foundKurtosis = true;
		}
	}
	if (!foundMixFactor || !foundRmse || !foundKurtosis) {
		throw Exception(
			fileName + " must contain mix_factor, rmse, and kurtosis rows.");
	}
}

void SimulationPreparation::loadRegionSwingDeviations()
{
	run.regionSwingDeviations.clear();
	run.regionFpSwingDeviations.clear();

	auto loadValues = [&](std::string const& fileName) {
		std::vector<float> values;
		auto file = std::ifstream(fileName);
		if (!file) return values;

		std::string header;
		std::string data;
		if (!std::getline(file, header) || !std::getline(file, data)) {
			throw Exception(
				fileName + " must contain a header and one data row.");
		}
		auto const valueStrings = splitString(data, ",");
		if (valueStrings.empty()) {
			throw Exception(fileName + " has an empty data row.");
		}
		values.reserve(valueStrings.size());
		for (std::size_t index = 0; index < valueStrings.size(); ++index) {
			values.push_back(parseFiniteFloat(
				valueStrings[index], fileName,
				"regional deviation " + std::to_string(index + 1)));
		}
		return values;
	};

	auto requireRegionCount = [&](int required, std::string const& fileName) {
		if (project.regions().count() < required) {
			throw Exception(
				fileName + " requires at least " +
				std::to_string(required) + " configured regions.");
		}
	};
	auto requireValueCount = [&](
		std::vector<float> const& values,
		std::size_t required,
		std::string const& fileName) {
		if (values.size() < required) {
			throw Exception(
				fileName + " contains " +
				std::to_string(values.size()) +
				" regional deviations; expected at least " +
				std::to_string(required) + ".");
		}
	};

	// Generated files use broad analysis regions. Convert those positions to
	// the finer region ordering configured in each project.
	auto applyConversion = [&](
		std::vector<float> const& values,
		std::map<int, float>& destination,
		std::string const& fileName) {
		int const year = parseInt(run.yearCode, fileName, "election year");
		if (run.regionCode == "fed") {
			requireValueCount(values, 6, fileName);
			requireRegionCount(8, fileName);
			for (int index = 0; index < 6; ++index) {
				destination[index] = values[index];
			}
			// Tasmania, ACT and NT share the final generated value.
			destination[6] = values[5];
			destination[7] = values[5];
			return true;
		}
		if (run.regionCode == "vic" && year >= 2026) {
			requireValueCount(values, 4, fileName);
			requireRegionCount(14, fileName);
			for (int index : { 8, 9, 13 }) destination[index] = values[0];
			for (int index : { 5, 6, 7, 10, 11 }) {
				destination[index] = values[1];
			}
			for (int index : { 1, 4, 12 }) destination[index] = values[2];
			for (int index : { 0, 2, 3 }) destination[index] = values[3];
			return true;
		}
		if (run.regionCode == "nsw" && year >= 2027) {
			requireValueCount(values, 2, fileName);
			requireRegionCount(12, fileName);
			for (int index = 6; index < 12; ++index) {
				destination[index] = values[0];
			}
			for (int index = 0; index < 6; ++index) {
				destination[index] = values[1];
			}
			return true;
		}

		logger << "Warning: No region conversion is configured for " +
			fileName + "; skipping import.\n";
		return false;
	};

	std::string const baseName =
		"analysis/Regional/" + run.getTermCode() + "-swing-deviations";
	std::string const tppFileName = baseName + ".csv";
	auto const tppValues = loadValues(tppFileName);
	if (!tppValues.empty()) {
		applyConversion(
			tppValues, run.regionSwingDeviations, tppFileName);
	}
	else if (run.regionCode == "fed") {
		logger << "Info: Could not find file " + tppFileName +
			" - default region behaviours will be used\n";
	}

	int const onIndex = project.parties().indexByShortCode("ON");
	if (onIndex != PartyCollection::InvalidIndex) {
		std::string onFileName = baseName + "-ON.csv";
		if (!std::ifstream(onFileName)) {
			// Generated state files currently use a lowercase suffix.
			onFileName = baseName + "-on.csv";
		}
		auto const onValues = loadValues(onFileName);
		if (!onValues.empty()) {
			std::map<int, float> converted;
			if (applyConversion(
				onValues, converted, onFileName)) {
				run.regionFpSwingDeviations[onIndex] =
					std::move(converted);
			}
		}
		else if (run.regionCode == "fed") {
			logger << "Info: Could not find file " + onFileName +
				" - default region behaviours will be used\n";
		}
	}
	PA_LOG_VAR(run.regionSwingDeviations);
}

void SimulationPreparation::loadTppSwingFactors()
{
	run.tppSwingFactors = SimulationRun::TppSwingFactors();
	std::string const fileName =
		"analysis/Seat Statistics/tpp-swing-factors.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	std::map<std::string, float*> destinations = {
		{ "mean-swing-deviation",
			&run.tppSwingFactors.meanSwingDeviation },
		{ "swing-kurtosis", &run.tppSwingFactors.swingKurtosis },
		{ "federal-modifier", &run.tppSwingFactors.federalModifier },
		{ "retirement-urban", &run.tppSwingFactors.retirementUrban },
		{ "retirement-regional",
			&run.tppSwingFactors.retirementRegional },
		{ "sophomore-candidate-urban",
			&run.tppSwingFactors.sophomoreCandidateUrban },
		{ "sophomore-candidate-regional",
			&run.tppSwingFactors.sophomoreCandidateRegional },
		{ "sophomore-party-urban",
			&run.tppSwingFactors.sophomorePartyUrban },
		{ "sophomore-party-regional",
			&run.tppSwingFactors.sophomorePartyRegional },
		{ "previous-swing-modifier",
			&run.tppSwingFactors.previousSwingModifier },
		{ "by-election-modifier",
			&run.tppSwingFactors.byElectionSwingModifier }
	};
	std::map<std::string, bool> found;
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty()) continue;
		auto const values = splitString(line, ",");
		requireColumns(values, 2, fileName, "TPP swing-factor row");
		auto const destination = destinations.find(values[0]);
		if (destination == destinations.end()) continue;
		*destination->second = parseFiniteFloat(
			values[1], fileName, values[0]);
		found[values[0]] = true;
	}
	for (auto const& [name, destination] : destinations) {
		if (!found[name]) {
			throw Exception(
				fileName + " has no " + name + " value.");
		}
	}
	if (run.tppSwingFactors.meanSwingDeviation < 0.0f) {
		throw Exception(fileName + " has a negative swing deviation.");
	}
	if (run.tppSwingFactors.meanSwingDeviation +
		run.tppSwingFactors.federalModifier < 0.0f) {
		throw Exception(
			fileName + " gives federal seats a negative swing deviation.");
	}
}

void SimulationPreparation::loadNationalsParameters()
{
	run.nationalsParameters = SimulationRun::NationalsParameters();
	std::string const fileName =
		"analysis/Nationals/" + run.getTermCode() + "_stats.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	std::string header;
	std::string data;
	if (!std::getline(file, header) || !std::getline(file, data)) {
		throw Exception(
			fileName + " must contain a header and one data row.");
	}
	auto const values = splitString(data, ",");
	requireColumns(values, 7, fileName, "Nationals statistics row");
	run.nationalsParameters.rmse = parseFiniteFloat(
		values[3], fileName, "seat RMSE");
	run.nationalsParameters.kurtosis = parseFiniteFloat(
		values[4], fileName, "seat kurtosis");
	run.nationalsParameters.overallRmse = parseFiniteFloat(
		values[5], fileName, "overall RMSE");
	run.nationalsParameters.overallKurtosis = parseFiniteFloat(
		values[6], fileName, "overall kurtosis");
	if (run.nationalsParameters.rmse < 0.0f ||
		run.nationalsParameters.overallRmse < 0.0f) {
		throw Exception(fileName + " has a negative RMSE.");
	}
}

void SimulationPreparation::loadNationalsSeatExpectations()
{
	std::string const fileName =
		"analysis/Nationals/" + run.getTermCode() + "_seats.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	std::string line;
	if (!std::getline(file, line)) {
		throw Exception(fileName + " is empty.");
	}
	std::map<std::string, float> oldExpectation;
	while (std::getline(file, line)) {
		if (line.empty()) continue;
		auto const values = splitString(line, ",");
		requireColumns(values, 2, fileName, "seat expectation row");
		float const expectation = parseFiniteFloat(
			values[1], fileName, "Nationals seat expectation");
		if (expectation < 0.0f || expectation > 1.0f) {
			throw Exception(
				fileName + " has an expectation outside 0-1 for " +
				values[0] + ".");
		}
		oldExpectation[values[0]] = expectation;
	}
	run.seatNationalsExpectation.assign(project.seats().count(), 0.0f);
	for (auto seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto const& seat = project.seats().viewByIndex(seatIndex);
		if (oldExpectation.contains(seat.name)) {
			run.seatNationalsExpectation[seatIndex] = oldExpectation[seat.name];
		}
		else if (oldExpectation.contains(seat.previousName)) {
			run.seatNationalsExpectation[seatIndex] = oldExpectation[seat.previousName];
		}
		else if (oldExpectation.contains(seat.useFpResults)) {
			run.seatNationalsExpectation[seatIndex] = oldExpectation[seat.useFpResults];
		}
		// Overrides for some special cases where seat was uncontested/didn't exist in previous election
		if (run.getTermCode() == "2025fed" && seat.name == "Bullwinkel") {
			run.seatNationalsExpectation[seatIndex] = 0.2f; // based on state election results + prominent candidate
		}
		if (run.getTermCode() == "2025fed" && seat.name == "O'Connor") {
			run.seatNationalsExpectation[seatIndex] = 0.23f; // based on 2019 results
		}
	}
}

void SimulationPreparation::loadIndividualSeatParameters()
{
	run.seatParameters.assign(
		project.seats().count(),
		SimulationRun::IndividualSeatParameters());
	std::vector<int> matchPriority(project.seats().count(), 0);
	std::string const fileName =
		"analysis/Seat Statistics/individual-seat-factors.csv";
	auto file = std::ifstream(fileName);
	if (!file) throw Exception("Could not find file " + fileName + "!");
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty()) continue;
		auto const values = splitString(line, ",");
		requireColumns(values, 6, fileName, "individual-seat row");
		if (values[1] != run.regionCode) continue;

		SimulationRun::IndividualSeatParameters parameters;
		parameters.loaded = true;
		parameters.elasticity = parseFiniteFloat(
			values[3], fileName, "seat elasticity");
		parameters.trend = parseFiniteFloat(
			values[4], fileName, "seat trend");
		parameters.volatility = parseFiniteFloat(
			values[5], fileName, "seat volatility");
		if (parameters.elasticity <= 0.0f) {
			throw Exception(
				fileName + " has non-positive elasticity for " +
				values[0] + ".");
		}
		if (parameters.volatility < 0.0f) {
			throw Exception(
				fileName + " has negative volatility for " +
				values[0] + ".");
		}

		for (int seatIndex = 0;
			seatIndex < project.seats().count(); ++seatIndex) {
			auto const& seat = project.seats().viewByIndex(seatIndex);
			int priority = 0;
			if (values[0] == seat.name) {
				priority = 2;
			}
			else if (
				(!seat.previousName.empty() &&
					values[0] == seat.previousName) ||
				(!seat.useFpResults.empty() &&
					values[0] == seat.useFpResults)) {
				priority = 1;
			}
			if (priority > matchPriority[seatIndex]) {
				run.seatParameters[seatIndex] = parameters;
				matchPriority[seatIndex] = priority;
			}
		}
	}
}

void SimulationPreparation::prepareProminentMinors()
{
	std::size_t seatCount = project.seats().count();
	run.seatProminentMinors.assign(seatCount, {});
	for (int seatIndex = 0; seatIndex < int(seatCount); ++seatIndex) {
		auto const& seat = project.seats().viewByIndex(seatIndex);
		for (auto const& code : seat.prominentMinors) {
			int const partyIndex = project.parties().indexByShortCode(code);
			if (partyIndex == PartyCollection::InvalidIndex) {
				throw Exception(
					"Seat " + seat.name +
					" lists an unknown prominent-minor party code: " +
					code + ".");
			}
			run.seatProminentMinors[seatIndex].push_back(partyIndex);
		}
	}
}

void SimulationPreparation::prepareRunningParties()
{
	std::size_t seatCount = project.seats().count();
	std::string const indAbbrev =
		project.parties().viewByIndex(run.indPartyIndex).abbreviation;
	run.runningParties.assign(seatCount, {});
	run.indCount.assign(seatCount, 0);
	run.othCount.assign(seatCount, 0);
	for (int seatIndex = 0; seatIndex < int(seatCount); ++seatIndex) {
		auto const& seat = project.seats().viewByIndex(seatIndex);
		for (auto const& code : seat.runningParties) {
			auto const asteriskSplit = splitString(code, "*");
			if (asteriskSplit.empty() || asteriskSplit[0].empty()) {
				throw Exception(
					"Seat " + seat.name +
					" has an empty running-party code.");
			}
			auto const& partyCode = asteriskSplit[0];
			if (partyCode != OthersCode &&
				project.parties().idByAbbreviation(partyCode) ==
					Party::InvalidId) {
				throw Exception(
					"Seat " + seat.name +
					" lists an unknown running-party abbreviation: " +
					partyCode + ".");
			}
			run.runningParties[seatIndex].push_back(partyCode);
			if (partyCode == indAbbrev) {
				run.indCount[seatIndex] = asteriskSplit.size();
			}
			else if (partyCode == OthersCode) {
				run.othCount[seatIndex] = asteriskSplit.size();
			}
		}
	}
}

void SimulationPreparation::calculateIndEmergenceModifier()
{
	// More independents should be expected to emerge if more
	// are already confirmed relative to the usual number.
	int numConfirmed = std::count_if(project.seats().begin(), project.seats().end(),
		[](const decltype(project.seats().begin())::value_type& seatPair) {
			return seatPair.second.confirmedProminentIndependent && seatPair.second.minorViability.contains("IND") && seatPair.second.minorViability.at("IND") >= 0; }
		);
	int daysToElection = project.projections().view(
		sim.settings.baseProjection).generateSupportSample(
			project.models(), wxInvalidDateTime, 0).daysToElection;
	float expectedConfirmed = std::max(float(daysToElection) * -0.02f + 3.5f, 0.0f) * project.seats().count() / 100.0f;
	run.indEmergenceModifier = std::min((float(numConfirmed) + 1.0f) / (expectedConfirmed + 1.0f), 2.5f);
}

void SimulationPreparation::calculateRegionalProportion()
{
	int regionalSeats = 0;
	for (auto const& seatType : run.seatTypes) {
		if (seatType == SimulationRun::SeatType::Provincial ||
			seatType == SimulationRun::SeatType::Rural) {
			++regionalSeats;
		}
	}
	run.regionalProportion =
		float(regionalSeats) / float(run.seatTypes.size());
}

void SimulationPreparation::initializeGeneralLiveData()
{
	// run.liveSeatTppSwing.resize(project.seats().count(), 0.0f);
	// run.liveSeatTcpCounted.resize(project.seats().count(), 0.0f);
	// run.liveSeatFpSwing.resize(project.seats().count());
	// run.liveSeatFpTransformedSwing.resize(project.seats().count());
	// run.liveSeatFpPercent.resize(project.seats().count());
	// run.liveSeatFpCounted.resize(project.seats().count(), 0.0f);
	// run.liveSeatTcpParties.resize(project.seats().count(), { -1000, -1000 });
	// run.liveSeatTcpSwing.resize(project.seats().count(), 0.0f);
	// run.liveSeatTcpPercent.resize(project.seats().count(), 0.0f);
	// run.liveSeatTcpBasis.resize(project.seats().count(), 0.0f);
	// run.liveSeatTppBasis.resize(project.seats().count(), 0.0f);
	// run.liveSeatPpvcSensitivity.resize(project.seats().count(), 0.0f);
	// run.liveSeatDecVoteSensitivity.resize(project.seats().count(), 0.0f);
	// run.liveEstDecVoteRemaining.resize(project.seats().count(), 0.0f);
	// run.liveRegionSwing.resize(project.regions().count(), 0.0f);
	// run.liveRegionTcpPercentCounted.resize(project.regions().count(), 0.0f);
	// run.liveRegionTppBasis.resize(project.regions().count(), 0.0f);
	// run.liveRegionClassicSeatCount.resize(project.regions().count(), 0.0f);
}
