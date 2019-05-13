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

#include "wx/dataview.h"

#include <memory>
#include "GenericChildFrame.h"
#include "PollingProject.h"
#include "ProjectFrame.h"
#include "EditSimulationFrame.h"

class ProjectFrame;
class GenericChildFame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// *** SimulationsFrame ***
// Frame that allows the user to create aggregated simulations using the poll data.
class DownloadFrame : public GenericChildFrame
{
public:
	// "parent" is a pointer to the top-level frame (or notebook page, etc.).
	// "project" is a pointer to the polling project object.
	DownloadFrame(ProjectFrame* parent, PollingProject* project);

private:

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& event);

	void OnDownloadHistoricBoothData(wxCommandEvent& event);

	void OnDownloadPreloadData(wxCommandEvent& event);

	void OnDownloadLatestBoothData(wxCommandEvent& event);

	void refreshToolbar();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// A pointer to the parent frame.
	ProjectFrame* parent;
};