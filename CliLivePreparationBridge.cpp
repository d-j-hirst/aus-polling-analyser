#include "LivePreparationBridge.h"

LiveInputRecovery::Snapshot LivePreparationBridge::readSnapshot(PollingProject&, Simulation&,
    LiveResultsInput::CurrentFile const&)
{
    throw LivePreparationBridge::Exception("Feed parsing requires the GUI live preparation bridge.");
}

namespace {
	[[noreturn]] void rejectLivePreparation()
	{
		throw LivePreparationBridge::Exception(
			"Live simulations are not supported by the command-line forecast "
			"runner.");
	}
}

void LivePreparationBridge::validateAutomaticSetup(
	PollingProject const&,
	Simulation const&,
	std::optional<LiveResultsInput::CurrentFile> const&, bool)
{
	rejectLivePreparation();
}

void LivePreparationBridge::prepareAutomatic(
	PollingProject&,
	Simulation&,
	SimulationRun&)
{
	rejectLivePreparation();
}
