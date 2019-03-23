#pragma once

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWidgets headers)
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <memory>
#include "wx/bookctrl.h"
#include "GenericChildFrame.h"
#include "PollingProject.h"
#include "NewProjectFrame.h"
#include "ParentFrame.h"

enum TabsEnum {
	Tab_Parties,
	Tab_Pollsters,
	Tab_Polls,
	Tab_Events,
	Tab_Visualiser,
	Tab_Models,
	Tab_Projections,
	Tab_Regions,
	Tab_Seats,
	Tab_Simulations,
	Tab_Display,
	Tab_Live
};

struct NewProjectData;

class ParentFrame;
class PartiesFrame;
class PollstersFrame;
class PollsFrame;
class EventsFrame;
class VisualiserFrame;
class ModelsFrame;
class ProjectionsFrame;
class RegionsFrame;
class SeatsFrame;
class SimulationsFrame;
class DisplayFrame;
class ResultsFrame;

// The parent frame of the polling analyser application.
class ProjectFrame : public wxNotebook
{
public:
	ProjectFrame(ParentFrame* parent);
	ProjectFrame(ParentFrame* parent, std::string pathName);
	ProjectFrame(ParentFrame* parent, NewProjectData newProjectData);

	// *** Event Handlers *** //

	// Refreshes the Polls frame data.
	void refreshPollData();

	// Refreshes the Visualiser frame.
	void refreshVisualiser();

	// Refreshes the Polls frame data.
	void refreshProjectionData();

	// Refreshes the Seats frame data.
	void refreshSeatData();

	// Refreshes the Display frame.
	void refreshDisplay();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	friend class ParentFrame;

private:

	// base constructor, should not actually be called as it does not create a project.
	ProjectFrame(ParentFrame* parent, int dummyInt);

	// action to be taken when the user switches tabs.
	void OnSwitch(wxBookCtrlEvent& event);

	// sets up pages once the project has been defined.
	void setupPages();

	// Checks to see if the user wants to save this project before it is closed.
	bool checkSave();

	// Saves as a new file, always opening the file dialog.
	void saveAs();

	// all data internal to the project (which is to be saved).
	std::unique_ptr<PollingProject> project;

	// points to the parties frame (if it exists).
	PartiesFrame* partiesFrame = nullptr;

	// points to the pollsters frame (if it exists).
	PollstersFrame* pollstersFrame = nullptr;

	// points to the polls frame (if it exists).
	PollsFrame* pollsFrame = nullptr;

	// points to the events frame (if it exists).
	EventsFrame* eventsFrame = nullptr;

	// points to the visualiser frame (if it exists).
	VisualiserFrame* visualiserFrame = nullptr;

	// points to the models frame (if it exists).
	ModelsFrame* modelsFrame = nullptr;

	// points to the projections frame (if it exists).
	ProjectionsFrame* projectionsFrame = nullptr;

	// points to the regions frame (if it exists).
	RegionsFrame* regionsFrame = nullptr;

	// points to the seats frame (if it exists).
	SeatsFrame* seatsFrame = nullptr;

	// points to the simulations frame (if it exists).
	SimulationsFrame* simulationsFrame = nullptr;

	// points to the display frame (if it exists).
	DisplayFrame* displayFrame = nullptr;

	// points to the live frame (if it exists).
	ResultsFrame* resultsFrame = nullptr;

	ParentFrame* parent;
};