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

void SimulationRun::run() {

	Projection const& thisProjection = project.projections().view(sim.settings.baseProjection);

	if (int(thisProjection.getProjectionLength()) == 0) return;

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