#include "LiveRunExport.h"

std::optional<std::filesystem::path> LiveRunExport::exportCompletedAutomaticLiveRun(
	PollingProject&,
	Simulation const&,
	SimulationRun const&,
	int,
	FeedbackFunc,
	ActionRequiredFunc)
{
	return std::nullopt;
}
