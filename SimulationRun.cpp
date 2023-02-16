#include "SimulationRun.h"

#include "CountProgress.h"
#include "PollingProject.h"
#include "Simulation.h"
#include "SimulationCompletion.h"
#include "SimulationIteration.h"
#include "SimulationPreparation.h"

static std::random_device rd;
static std::mt19937 gen;


using Mp = Simulation::MajorParty;

void SimulationRun::run(FeedbackFunc feedback) {
	Projection const& thisProjection = project.projections().view(sim.settings.baseProjection);

	if (int(thisProjection.getProjectionLength()) == 0) {
		feedback("Base projection has not yet been run. Run the simulation's base projection before running the simulation itself.");
		return;
	}

	// *** Here we need to do some preparations from the betting odds calibrations
	// and then run repeatedly run the iterations until they have converged
	// put this in a separate procedure
	// maybe create a separate class (inherited from this) to do this?

	auto newRun = SimulationRun(project, sim);

	newRun.runBettingOddsCalibrations();

	SimulationPreparation preparations(project, sim, *this);
	try {
		preparations.prepareForIterations();
	}
	catch (SimulationPreparation::Exception const& e) {

		feedback("Could not run simulation due to the following issue: \n" + std::string(e.what()));
		return;
	}

	int numThreads = project.config().getSimulationThreads();
	std::vector<int> batchSizes;
	int minBatchSize = sim.settings.numIterations / numThreads;
	for (int i = 0; i < numThreads; ++i) batchSizes.push_back(minBatchSize);
	int extraIterations = sim.settings.numIterations - minBatchSize * numThreads;
	for (int i = 0; i < extraIterations; ++i) ++batchSizes[i];

	std::vector<std::thread> threads;
	threads.resize(numThreads);

	auto runIterations = [&](int numIterations) {
		for (int i = 0; i < numIterations; ++i) {
			SimulationIteration iteration(project, sim, *this);
			iteration.runIteration();
		}
	};

	for (int thread = 0; thread < numThreads; ++thread) {
		threads[thread] = std::thread(runIterations, batchSizes[thread]);
	}

	for (int thread = 0; thread < numThreads; ++thread) {
		if (threads[thread].joinable()) threads[thread].join();
	}

	SimulationCompletion completion(project, sim, *this);
	completion.completeRun();

	sim.lastUpdated = wxDateTime::Now();
}

std::string SimulationRun::getTermCode() const
{
	return yearCode + regionCode;
}

// Converts a betting odds (e.g. $1.65) into an implied chance.
// Takes optional parameters for the rake and cap on odds.
template<typename T,
	std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	inline T calculateImpliedChance(T odds, T evenOdds = T(1.88), T cap = T(15.0)) {
	float cappedOdds = std::min(odds, cap);
	// the last part of this line compensates for the typical bookmaker's margin
	float impliedChance = T(1.0) / (cappedOdds * (T(2.0) / evenOdds));
	// significant adjustment downwards to adjust for longshot bias.
	float longshotAdjustment = impliedChance < T(0.4) ?
		T(-0.6) * (T(0.4) - impliedChance) :
		T(0);
	impliedChance = basicTransformedSwing(impliedChance * T(100), longshotAdjustment * T(100)) * T(0.01);
	return impliedChance;
}

void SimulationRun::runBettingOddsCalibrations(FeedbackFunc feedback)
{
	doingBettingOddsCalibrations = true;

	logger << "*** Doing betting odds calibrations ***\n";

	SimulationPreparation preparations(project, sim, *this);
	try {
		preparations.prepareForIterations();
	}
	catch (SimulationPreparation::Exception const& e) {

		feedback("Could not run betting odds calibrations due to the following issue: \n" + std::string(e.what()));
		return;
	}

	// key is (seatIndex, partyIndex)
	std::map<std::pair<int, int>, float> impliedChances;
	for (auto const& [seatId, seat] : project.seats()) {
		int seatIndex = project.seats().idToIndex(seatId);
		for (auto [partyCode, odds] : seat.bettingOdds) {
			int partyIndex = project.parties().indexByShortCode(partyCode);
			if (partyIndex < 0) {
				logger << "Warning: Betting odds did not match to a party for code " << partyCode << " in seat " << seat.name << ".\n";
				continue;
			}
			float thisImpliedChance = calculateImpliedChance(odds);
			impliedChances[{seatIndex, partyIndex}] = thisImpliedChance;
		}
	}
	PA_LOG_VAR(impliedChances);

	int numThreads = project.config().getSimulationThreads();
	std::vector<int> batchSizes;
	const int CycleIterationsDivisor = 20;
	int cycleIterations = sim.settings.numIterations / CycleIterationsDivisor;
	int minBatchSize = cycleIterations / numThreads;
	for (int i = 0; i < numThreads; ++i) batchSizes.push_back(minBatchSize);
	int extraIterations = cycleIterations - minBatchSize * numThreads;
	for (int i = 0; i < extraIterations; ++i) ++batchSizes[i];

	std::vector<std::thread> threads;
	threads.resize(numThreads);

	auto runIterations = [&](int numIterations) {
		for (int i = 0; i < numIterations; ++i) {
			SimulationIteration iteration(project, sim, *this);
			iteration.runIteration();
		}
	};

	for (int thread = 0; thread < numThreads; ++thread) {
		threads[thread] = std::thread(runIterations, batchSizes[thread]);
	}

	for (int thread = 0; thread < numThreads; ++thread) {
		if (threads[thread].joinable()) threads[thread].join();
	}

	logger << "*** Finished betting odds calibrations ***\n";

	doingBettingOddsCalibrations = false;
}
