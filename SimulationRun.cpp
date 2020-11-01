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

	SimulationPreparation preparations(project, sim, *this);
	preparations.prepareForIterations();

	for (currentIteration = 0; currentIteration < sim.settings.numIterations; ++currentIteration) {
		SimulationIteration iteration(project, sim, *this);
		iteration.runIteration();
	}

	SimulationCompletion completion(project, sim, *this);
	completion.completeRun();

	sim.lastUpdated = wxDateTime::Now();
}
