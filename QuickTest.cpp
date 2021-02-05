#include "QuickTest.h"

#include "General.h"
#include "PollingProject.h"

QuickTest::QuickTest()
{
	PollingProject project = PollingProject("Federal election 2019.pol");
	project.models().run(0);
	project.projections().run(0);
	project.simulations().run(0);
	project.simulations().run(0);
	wxMessageBox(formatFloat(project.simulations().view(0).getLatestReport().getPartyWinPercent(Simulation::MajorParty::One), 2));
}
