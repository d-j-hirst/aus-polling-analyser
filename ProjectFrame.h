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

#include "GenericChildFrame.h"
#include "PollingProject.h"
#include "NewProjectFrame.h"
#include "ParentFrame.h"

#include "wx/bookctrl.h"

#include <memory>

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
class DownloadFrame;
class MapFrame;

class LoadProjectFailedException : public std::runtime_error {
public:
	LoadProjectFailedException() : std::runtime_error("") {}
};

// Frame that controls input and display for a particular project.
// Designed to be a child to the ParentFrame class.
// Includes a notebook-style interface with many tabs for different
// areas of the project
class ProjectFrame : public wxNotebook
{
public:
	// constructor to load the project from a given file
	ProjectFrame(ParentFrame* parent, std::string pathName);

	// constructor to create a new project
	ProjectFrame(ParentFrame* parent, NewProjectData newProjectData);

	class Refresher {
	public:
		void refreshPollData() const;
		void refreshVisualiser() const;
		void refreshProjectionData() const;
		void refreshSeatData() const;
		void refreshDisplay() const;
		void refreshResults() const;
		void refreshMap() const;
		// Gives a reference to the underlying notebook so that this
		// can be used to construct children of the ProjectFrame
		wxNotebook* notebook() const { return &projectFrame; }
	private:
		// ProjectFrame should be the only class allowed to construct this
		friend class ProjectFrame;
		Refresher(ProjectFrame& projectFrame) 
			: projectFrame(projectFrame) {}

		ProjectFrame& projectFrame;
	};

	// Checks to see if the user wants to save this project before it is closed
	bool checkSave();

	// Saves using the existing file name if possible, or opens a dialog if there is no existing name
	void save();

	// Saves as a new file, always opening the file dialog
	void saveAs();

private:

	// Base constructor, should not actually be called as it does not create a project.
	// Delegated to by the above public constructors to avoid duplication of shared functions.
	ProjectFrame(ParentFrame* parent, int dummyInt);

	// Creates an object that can be called on to refresh certain tabs in this frame.
	// Used to allow tabs to trigger display refreshes in other tabs without giving them 
	Refresher createRefresher();

	// action to be taken when the user switches tabs.
	void OnSwitch(wxBookCtrlEvent& event);

	template <class C>
	void createPage(C*& createThis) {
		createThis = new C(createRefresher(), project.get());
		AddPage(createThis, (createThis)->GetLabel(), true);
	}

	// sets up pages once the project has been defined.
	void setupPages();

	// Saves the project under the given filename. Will display a message to indicate success/failure.
	void saveUnderFilename(std::string const& pathName);

	// Cancel construction from an invalid file, displaying an error message to the user
	void cancelConstructionFromFile();

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

	// points to the results frame (if it exists).
	ResultsFrame* resultsFrame = nullptr;

	// points to the download frame (if it exists).
	DownloadFrame* downloadFrame = nullptr;

	// points to the map frame (if it exists).
	MapFrame* mapFrame = nullptr;

	ParentFrame* parent;
};