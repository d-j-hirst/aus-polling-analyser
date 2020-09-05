#pragma once
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
#include "ProjectFrame.h"

#include "wx/dataview.h"

#include <memory>

class ProjectFrame;

// *** DownloadFrame ***
// Frame that allows the user to download results from the electoral commission
class DownloadFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	DownloadFrame(ProjectFrame::Refresher refresher, PollingProject* project);

private:

	enum Preset {
		Federal2007,
		Federal2010,
		Federal2013,
		Federal2016,
		Federal2019,
		Federal2022
	};

	// Creates the tool bar and its icons
	void setupToolbar();

	void bindEventHandlers();

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& event);

	void OnGetHistoricBoothData(wxCommandEvent& event);

	void OnGetPreloadData(wxCommandEvent& event);

	void OnGetCustomBoothData(wxCommandEvent& event);

	void OnGetLatestBoothData(wxCommandEvent& event);

	void OnDownloadCompleteData(wxCommandEvent& event);

	void collectHistoricBoothData(bool skipPrompt = false);
	void collectPreloadData(bool skipPrompt = false);
	void collectCustomBoothData(bool skipPrompt = false);
	void collectLatestBoothData(bool skipPrompt = false);

	void collectCompleteData(bool skipPrompt = false);

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// Text box used to enter the name of a seat to input results for
	wxComboBox* presetComboBox = nullptr;

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;
};