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
	logger << "Simulation made to run: " << sim.settings.name << "\n";

	Projection const& thisProjection = project.projections().view(sim.settings.baseProjection);

	if (int(thisProjection.getProjectionLength()) == 0) {
		feedback("Base projection has not yet been run. Run the simulation's base projection before running the simulation itself.");
		return;
	}

	logger << "Preparing for simulations: " << wxDateTime::Now().FormatISOCombined() << "\n";

	SimulationPreparation preparations(project, sim, *this);
	preparations.prepareForIterations();

	int numThreads = 8; // temporary, pull from settings later
	std::vector<int> batchSizes;
	int minBatchSize = sim.settings.numIterations / numThreads;
	for (int i = 0; i < numThreads; ++i) batchSizes.push_back(minBatchSize);
	int extraIterations = sim.settings.numIterations - minBatchSize * numThreads;
	for (int i = 0; i < extraIterations; ++i) ++batchSizes[i];
	logger << batchSizes << "\n";

	std::vector<std::thread> threads;
	threads.resize(numThreads);

	auto runIterations = [&](int numIterations) {
		for (int i = 0; i < numIterations; ++i) {
			SimulationIteration iteration(project, sim, *this);
			iteration.runIteration();
		}
	};

	logger << "Doing iterations: " << wxDateTime::Now().FormatISOCombined() << "\n";

	for (int thread = 0; thread < numThreads; ++thread) {
		threads[thread] = std::thread(runIterations, batchSizes[thread]);
	}

	for (int thread = 0; thread < numThreads; ++thread) {
		if (threads[thread].joinable()) threads[thread].join();
	}

	logger << "Completing iterations: " << wxDateTime::Now().FormatISOCombined() << "\n";

	SimulationCompletion completion(project, sim, *this);
	completion.completeRun();
	logger << "Finished simulations: " << wxDateTime::Now().FormatISOCombined() << "\n";

	sim.lastUpdated = wxDateTime::Now();
}
