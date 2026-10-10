#include "../General.h"
#include "../SimulationRun.h"

#include <atomic>
#include <cassert>
#include <map>
#include <thread>
#include <vector>

int main()
{
	// Regional files are optional. Every worker must get the same default when
	// an entry is absent, without adding entries to shared prepared inputs. Use
	// both an entirely missing file and partially populated regional inputs.
	std::map<int, float> const noPollingDeviations;
	std::map<int, float> const pollingDeviations{{2, -1.25f}};
	std::map<int, SimulationRun::RegionBaseBehaviour> const baseBehaviours{
		{2, {0.8f, 0.5f, 1.5f, 3.0f}}};
	std::map<int, SimulationRun::RegionMixBehaviour> const mixBehaviours;
	std::atomic<bool> start{false};
	std::atomic<bool> correct{true};
	std::vector<std::thread> workers;
	for (int worker = 0; worker < 28; ++worker) {
		workers.emplace_back([&] {
			while (!start.load()) std::this_thread::yield();
			for (int attempt = 0; attempt < 2000; ++attempt) {
				for (int region = 0; region < 8; ++region) {
					auto const base = getAt(baseBehaviours, region,
						SimulationRun::RegionBaseBehaviour{});
					auto const mix = getAt(mixBehaviours, region,
						SimulationRun::RegionMixBehaviour{});
					if (getAt(noPollingDeviations, region, 0.0f) != 0.0f ||
						getAt(pollingDeviations, region, 0.0f) !=
							(region == 2 ? -1.25f : 0.0f) ||
						base.overallSwingCoeff != (region == 2 ? 0.8f : 1.0f) ||
						base.rmse != (region == 2 ? 1.5f : 2.0f) ||
						mix.bias != 1.0f || mix.rmse != 2.0f) {
						correct.store(false);
					}
				}
			}
		});
	}
	start.store(true);
	for (auto& worker : workers) worker.join();
	assert(correct.load());
	assert(noPollingDeviations.empty());
	assert(pollingDeviations.size() == 1);
	assert(baseBehaviours.size() == 1);
	assert(mixBehaviours.empty());
}
