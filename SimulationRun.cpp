#include "SimulationRun.h"

#include "General.h"
#include "LivePreparationBridge.h"
#include "Log.h"
#include "PollingProject.h"
#include "Simulation.h"
#include "SimulationCompletion.h"
#include "SimulationIteration.h"
#include "SimulationPreparation.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {
	class ScopedBooleanState {
	public:
		ScopedBooleanState(bool& state, bool temporaryValue)
			: state(state), previousValue(state)
		{
			state = temporaryValue;
		}

		~ScopedBooleanState()
		{
			state = previousValue;
		}

		ScopedBooleanState(ScopedBooleanState const&) = delete;
		ScopedBooleanState& operator=(ScopedBooleanState const&) = delete;

	private:
		bool& state;
		bool previousValue;
	};

	class LatestReportTransaction {
	public:
		explicit LatestReportTransaction(Simulation::Report& report)
			: report(report), previousReport(report)
		{
		}

		~LatestReportTransaction()
		{
			if (!committed) report = std::move(previousReport);
		}

		void commit() noexcept
		{
			committed = true;
		}

		LatestReportTransaction(LatestReportTransaction const&) = delete;
		LatestReportTransaction& operator=(LatestReportTransaction const&) = delete;

	private:
		Simulation::Report& report;
		Simulation::Report previousReport;
		bool committed = false;
	};

	// Convert decimal betting odds (for example, $1.65) to an implied chance.
	// The cap and longshot correction prevent very long odds from exerting
	// disproportionate influence on the seat calibration.
	float calculateImpliedChance(
		float odds,
		float evenOdds = 1.88f,
		float cap = 15.0f)
	{
		float const cappedOdds = std::min(odds, cap);
		float impliedChance = 1.0f / (cappedOdds * (2.0f / evenOdds));
		float const longshotAdjustment = impliedChance < 0.4f ?
			-0.6f * (0.4f - impliedChance) :
			0.0f;
		return basicTransformedSwing(
			impliedChance * 100.0f,
			longshotAdjustment * 100.0f) * 0.01f;
	}

	bool hasNowcastProjectionName(std::string const& name)
	{
		std::string normalisedName;
		normalisedName.reserve(name.size());
		for (unsigned char character : name) {
			if (std::isalnum(character)) {
				normalisedName.push_back(char(std::tolower(character)));
			}
		}
		return normalisedName.find("nowcast") != std::string::npos;
	}
}

bool SimulationRun::run(
	FeedbackFunc feedback,
	ActionRequiredFunc actionRequired) {
	if (!actionRequired) actionRequired = feedback;
	// Keep every phase of this run on the same local calendar date, even if a
	// long simulation crosses midnight.
	nowcastDate = Date::todayLocal();
	if (!nowcastDate.isValid()) {
		feedback("Could not determine the current local date for this simulation run.");
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(warningMutex);
		warnings.clear();
	}
	if (sim.settings.numIterations <= 0) {
		feedback(
			"Could not run simulation: the configured iteration count must be positive.");
		return false;
	}

	Projection const& thisProjection = project.projections().view(sim.settings.baseProjection);

	if (int(thisProjection.getProjectionLength()) == 0) {
		feedback("Base projection has not yet been run. Run the simulation's base projection before running the simulation itself.");
		return false;
	}

	int const projectionIndex =
		project.projections().idToIndex(sim.settings.baseProjection);
	if (projectionIndex >= 1 &&
		hasNowcastProjectionName(thisProjection.getSettings().name)) {
		std::string const message =
			"This simulation appears to use a legacy nowcast configuration. "
			"Nowcasts must use a full-election projection as their base. "
			"Select the general election projection as the Base projection and "
			"set Forecast/report mode to Nowcast.";
		logger << "Warning: " << message << "\n";
		feedback(message);
		return false;
	}

	if (sim.isNowcast()) {
		auto projectionEndDate = thisProjection.getSettings().endDate;
		if (!projectionEndDate.isValid()) {
			feedback("Could not run nowcast: the base projection has no valid end date.");
			return false;
		}
		if (nowcastDate > projectionEndDate) {
			feedback(
				"Could not run nowcast: the current date is after the base "
				"projection's election date.");
			return false;
		}
		logger << "Projection sampling mode: nowcast; projection: "
			<< thisProjection.getSettings().name << "; date: "
			<< nowcastDate.formatIso() << "\n";
	}
	else {
		logger << "Projection sampling mode: forecast; projection: "
			<< thisProjection.getSettings().name;
		if (thisProjection.getSettings().possibleDates.empty()) {
			logger << "; date: " << thisProjection.getEndDateString();
		}
		else {
			logger << "; dates: configured possible-date distribution";
		}
		logger << "\n";
	}

	if (sim.isLiveAutomatic()) {
		try {
			LivePreparationBridge::validateAutomaticSetup(project, sim);
		}
		catch (LivePreparationBridge::Exception const& e) {
			feedback("Could not run live simulation because its live data is not set up:\n" +
				std::string(e.what()));
			return false;
		}
	}
	if (sim.isLiveManual()) {
		feedback(
			"Manual live simulation is retained for possible future use, "
			"but it has not yet been adapted to the current live-results "
			"pipeline.");
		return false;
	}

	try {
		project.seats().importInfo();
	}
	catch (SeatImportException const& exception) {
		feedback(
			"Could not run simulation because the seat configuration is invalid:\n" +
			std::string(exception.what()));
		return false;
	}
	// Calibration and live-baseline sub-runs use latestReport as temporary
	// workspace. Preserve the last successful report unless the complete main
	// run reaches the commit point below.
	LatestReportTransaction reportTransaction(sim.latestReport);

	if (!runBettingOddsCalibrations(feedback)) return false;

	if (sim.isLiveAutomatic() && !sim.liveBaselineReport) {
		if (!runLiveBaselineSimulation(feedback)) return false;
	}

	auto const mainRunStart = std::chrono::steady_clock::now();

	SimulationPreparation preparations(project, sim, *this);
	try {
		preparations.prepareForIterations();
	}
	catch (SimulationPreparation::Exception const& e) {

		feedback("Could not run simulation due to the following issue: \n" + std::string(e.what()));
		return false;
	}

	const int cycleIterations = sim.settings.numIterations;
	if (!runIterations(*this, cycleIterations, 0, "simulation", feedback)) return false;

	SimulationCompletion completion(project, sim, *this, cycleIterations);
	completion.completeRun(feedback, actionRequired);

	auto const mainRunElapsed =
		std::chrono::steady_clock::now() - mainRunStart;
	double const mainRunSeconds =
		std::chrono::duration<double>(mainRunElapsed).count();
	logger << "*** Main simulation run completed in " << mainRunSeconds
		<< " seconds (includes preparation, iterations, and completion; "
		<< "excludes betting-odds calibration and live baseline simulation) ***\n";

	reportWarnings(feedback);

	sim.lastUpdated = Timestamp::now();
	reportTransaction.commit();
	return true;
}

void SimulationRun::recordWarning(
	WarningCategory category,
	int iterationIndex,
	std::string description)
{
	std::lock_guard<std::mutex> lock(warningMutex);
	auto [warningIt, inserted] = warnings.try_emplace(
		category,
		Warning{iterationIndex, std::move(description)});
	// Worker completion order is nondeterministic, so retain the lowest
	// iteration index rather than whichever thread reached the mutex first.
	if (!inserted) {
		++warningIt->second.occurrenceCount;
		if (iterationIndex < warningIt->second.iterationIndex) {
			warningIt->second.iterationIndex = iterationIndex;
		}
	}
}

void SimulationRun::reportWarnings(FeedbackFunc feedback) const
{
	std::string message;
	{
		std::lock_guard<std::mutex> lock(warningMutex);
		if (warnings.empty()) return;

		for (auto const& [category, warning] : warnings) {
			if (category ==
					WarningCategory::FrequentTerminalFpReconciliation &&
				int64_t(warning.occurrenceCount) * 100 <=
					sim.settings.numIterations) {
				continue;
			}
			if (message.empty()) {
				message =
					"Simulation completed with the following warnings:";
			}

			std::string categoryCode;
			switch (category) {
			case WarningCategory::FpReconciliation:
				categoryCode = "FP_RECONCILIATION";
				break;
			case WarningCategory::FrequentTerminalFpReconciliation:
				categoryCode = "FREQUENT_TERMINAL_FP_RECONCILIATION";
				break;
			case WarningCategory::DiagnosticTest:
				categoryCode = "DIAGNOSTIC_TEST";
				break;
			default:
				categoryCode = "UNKNOWN";
				break;
			}
			message +=
				"\n- [" + categoryCode + "] " +
				std::to_string(warning.occurrenceCount) +
				(warning.occurrenceCount == 1 ?
					" occurrence; first at iteration " :
					" occurrences; first at iteration ") +
				std::to_string(warning.iterationIndex) +
				": " + warning.description;
		}
		if (message.empty()) return;
		message += "\n\nSee PALog.log for diagnostic details.";
	}

	logger << message << "\n";
	feedback(message);
}

bool SimulationRun::runIterations(
	SimulationRun& iterationRun,
	int iterationCount,
	int iterationStartIndex,
	std::string const& phase,
	FeedbackFunc feedback)
{
	if (iterationCount <= 0) {
		feedback("Could not run " + phase + ": the configured iteration count must be positive.");
		return false;
	}

	int const numThreads = std::clamp(
		project.config().getSimulationThreads(), 1, iterationCount);
	std::vector<int> batchSizes(numThreads, iterationCount / numThreads);
	for (int i = 0; i < iterationCount % numThreads; ++i) {
		++batchSizes[i];
	}

	std::atomic<int> totalRetries = 0;
	std::atomic<bool> abortRequested = false;
	std::exception_ptr failure;
	std::mutex failureMutex;

	auto captureFailure = [&](std::exception_ptr error) {
		bool expected = false;
		if (!abortRequested.compare_exchange_strong(expected, true)) return;
		std::lock_guard<std::mutex> lock(failureMutex);
		failure = error;
	};

	auto runBatch = [&](int batchSize, int batchStartIndex) {
		try {
			for (int i = 0; i < batchSize && !abortRequested.load(); ++i) {
				SimulationIteration iteration(
					project, sim, iterationRun, batchStartIndex + i);
				int const iterationRetries = iteration.runIteration();
				int const updatedRetries =
					totalRetries.fetch_add(iterationRetries) + iterationRetries;

				// Use integer arithmetic so "exceeds 1%" has an exact boundary.
				// A few in-flight workers may finish before observing the abort.
				if (int64_t(updatedRetries) * 100 > iterationCount) {
					captureFailure(std::make_exception_ptr(std::runtime_error(
						"The " + phase + " produced " +
						std::to_string(updatedRetries) + " retries across " +
						std::to_string(iterationCount) +
						" expected iterations, exceeding the 1% safety limit.")));
				}
			}
		}
		catch (...) {
			captureFailure(std::current_exception());
		}
	};

	std::vector<std::thread> threads;
	threads.reserve(numThreads);
	int iterationsStarted = iterationStartIndex;
	try {
		for (int thread = 0; thread < numThreads; ++thread) {
			threads.emplace_back(runBatch, batchSizes[thread], iterationsStarted);
			iterationsStarted += batchSizes[thread];
		}
	}
	catch (...) {
		captureFailure(std::current_exception());
	}
	for (std::thread& thread : threads) {
		if (thread.joinable()) thread.join();
	}

	if (!failure) {
		int const retryCount = totalRetries.load();
		if (retryCount > 0) {
			logger << "Completed " << phase << " with " << retryCount <<
				" retried simulation iterations.\n";
		}
		return true;
	}

	std::string detail = "Unknown worker-thread error.";
	try {
		std::rethrow_exception(failure);
	}
	catch (std::exception const& e) {
		detail = e.what();
	}
	catch (...) {
	}

	logger << "Aborted " << phase << ": " << detail << "\n";
	feedback(
		"Could not complete the " + phase + ":\n" + detail +
		"\n\nThe simulation report was not completed. Check PALog.log for the first invalid values.");
	return false;
}

std::string SimulationRun::getTermCode() const
{
	return yearCode + regionCode;
}

bool SimulationRun::isLiveAutomatic() const
{
	return sim.isLiveAutomatic() && !doingBettingOddsCalibrations && !doingLiveBaselineSimulation;
}

bool SimulationRun::isLiveManual() const
{
	return sim.isLiveManual() && !doingBettingOddsCalibrations && !doingLiveBaselineSimulation;
}

bool SimulationRun::runBettingOddsCalibrations(FeedbackFunc feedback)
{
	ScopedBooleanState calibrationState(doingBettingOddsCalibrations, true);

	// key is (seatIndex, partyIndex)
	std::map<std::pair<int, int>, float> impliedChances;
	for (auto const& [seatId, seat] : project.seats()) {
		int const seatIndex = project.seats().idToIndex(seatId);
		for (auto const& [partyCode, odds] : seat.bettingOdds) {
			if (!std::isfinite(odds) || odds <= 0.0f) {
				feedback(
					"Could not run betting odds calibrations: seat " +
					seat.name + " has invalid betting odds for " +
					partyCode + ".");
				return false;
			}
			int const partyIndex =
				project.parties().indexByShortCode(partyCode);
			if (partyIndex < 0) {
				std::string const message =
					"Could not run betting odds calibrations: party code " +
					partyCode + " in seat " + seat.name +
					" does not match a configured party.";
				logger << message << "\n";
				feedback(message);
				return false;
			}
			impliedChances[{seatIndex, partyIndex}] =
				calculateImpliedChance(odds);
		}
	}

	oddsCalibrationMeans.clear();
	oddsFinalMeans.clear();
	bool const calibrationInputsChanged =
		sim.cachedOddsIterations != sim.settings.numIterations ||
		sim.cachedOddsInputs != impliedChances;
	if (calibrationInputsChanged) {
		// The live baseline incorporates the calibrated odds and uses the same
		// configured sample size, so it must be regenerated as well.
		sim.liveBaselineReport.reset();
	}

	if (impliedChances.empty()) {
		sim.cachedOddsFinalMeans.clear();
		sim.cachedOddsInputs.clear();
		sim.cachedOddsIterations = sim.settings.numIterations;
		logger << "*** Skipping betting odds calibrations as there are no odds currently entered ***\n";
		return true;
	}

	bool const cachedOddsMatch =
		sim.isLive() &&
		!sim.cachedOddsFinalMeans.empty() &&
		!calibrationInputsChanged;
	if (cachedOddsMatch) {
		logger << "*** Skipping betting odds calibrations for a live forecast as there are already cached results ***\n";
		oddsFinalMeans = sim.cachedOddsFinalMeans;
		return true;
	}
	if (sim.isLive() && !sim.cachedOddsFinalMeans.empty()) {
		logger <<
			"*** Recalculating cached live betting odds because the odds or "
			"iteration count changed ***\n";
	}

	logger << "*** Doing betting odds calibrations ***\n";
	PA_LOG_VAR(impliedChances);

	std::map<std::pair<int, int>, bool> previouslyHigh;
	std::map<std::pair<int, int>, float> currentIncrement;
	float const initialValue = transformVoteShare(20.0f);
	for (auto const& impliedChance : impliedChances) {
		auto const& identifier = impliedChance.first;
		oddsCalibrationMeans[identifier] = initialValue;
		previouslyHigh[identifier] = false;
		currentIncrement[identifier] = 20.0f;
	}

	constexpr int NumRevisionRounds = 20;
	constexpr int CycleIterationsDivisor = 20;
	int const cycleIterations =
		std::max(10, sim.settings.numIterations / CycleIterationsDivisor);
	for (int revision = 0; revision < NumRevisionRounds; ++revision) {
		SimulationRun newRun(project, sim, true);
		newRun.nowcastDate = nowcastDate;
		SimulationPreparation preparations(project, sim, newRun);
		try {
			preparations.prepareForIterations();
		}
		catch (SimulationPreparation::Exception const& e) {

			feedback("Could not run betting odds calibrations due to the following issue: \n" + std::string(e.what()));
			return false;
		}
		newRun.oddsCalibrationMeans = oddsCalibrationMeans;

		// Keep calibration indices outside the main run. Reusing the same range
		// in every revision compares successive adjustments against identical
		// deterministic scenarios, reducing calibration noise.
		if (!runIterations(
			newRun, cycleIterations, sim.settings.numIterations,
			"betting-odds calibration", feedback)) {
			return false;
		}

		SimulationCompletion completion(project, sim, newRun, cycleIterations);
		completion.completeRun(feedback);

		// This directional search reduces its step when it crosses the target,
		// but does not retain the tested mean with the smallest probability
		// error. In particular, the final update below produces an untested next
		// estimate. A future refinement should validate that estimate and choose
		// the best result observed across all rounds.
		for (auto const& [identifier, impliedChance] : impliedChances) {
			auto const& seatWinPercent =
				sim.latestReport.seatPartyWinPercent[identifier.first];
			auto const winPercentIt = seatWinPercent.find(identifier.second);
			float const winPercent = winPercentIt == seatWinPercent.end() ?
				0.0f : winPercentIt->second;
			if (revision == NumRevisionRounds - 1) {
				PA_LOG_VAR(project.seats().viewByIndex(identifier.first).name);
				PA_LOG_VAR(identifier);
				PA_LOG_VAR(oddsCalibrationMeans[identifier]);
				PA_LOG_VAR(impliedChance);
				PA_LOG_VAR(winPercent);
			}
			if (winPercent * 0.01f < impliedChance) {
				if (revision && previouslyHigh[identifier]) {
					currentIncrement[identifier] *= 0.4f;
				}
				oddsCalibrationMeans[identifier] += currentIncrement[identifier];
				previouslyHigh[identifier] = false;
			}
			else {
				if (revision && !previouslyHigh[identifier]) {
					currentIncrement[identifier] *= 0.4f;
				}
				oddsCalibrationMeans[identifier] -= currentIncrement[identifier];
				previouslyHigh[identifier] = true;
			}
		}
	}

	oddsFinalMeans = oddsCalibrationMeans;
	oddsCalibrationMeans.clear();
	sim.cachedOddsFinalMeans = oddsFinalMeans;
	sim.cachedOddsInputs = impliedChances;
	sim.cachedOddsIterations = sim.settings.numIterations;

	logger << "*** Finished betting odds calibrations ***\n";
	return true;
}

bool SimulationRun::runLiveBaselineSimulation(FeedbackFunc feedback) {
	ScopedBooleanState baselineState(doingLiveBaselineSimulation, true);

	logger << "*** Doing live baseline simulation ***\n";

	SimulationRun newRun(project, sim, false, true);
	newRun.nowcastDate = nowcastDate;
	SimulationPreparation preparations(project, sim, newRun);
	newRun.oddsFinalMeans = oddsFinalMeans;
	newRun.oddsCalibrationMeans = oddsCalibrationMeans;
	try {
		preparations.prepareForIterations();
	}
	catch (SimulationPreparation::Exception const& e) {

		feedback("Could not run live baseline simulation due to the following issue: \n" + std::string(e.what()));
		return false;
	}

	int const cycleIterations = std::max(10, sim.settings.numIterations);

	// Keep baseline indices outside the main run. Overlap with the temporary
	// calibration range is harmless because those results are discarded.
	if (!runIterations(
		newRun, cycleIterations, sim.settings.numIterations,
		"live baseline simulation", feedback)) {
		return false;
	}

	SimulationCompletion completion(project, sim, newRun, cycleIterations);
	completion.completeRun(feedback);
	sim.liveBaselineReport = sim.latestReport;

	logger << "*** Finished live baseline simulation ***\n";
	return true;
}

