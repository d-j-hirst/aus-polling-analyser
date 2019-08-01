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
class GenericChildFame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// *** ModelsFrame ***
// Frame that allows the user to create aggregated models using the poll data.
class ModelsFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	ModelsFrame(ProjectFrame::Refresher refresher, PollingProject* project);

	// Calls on the frame to create a new model based on "Model".
	void OnNewModelReady(Model& model);

	// Calls on the frame to edit the currently selected model based on "Model".
	void OnEditModelReady(Model& model);

	// refresh the (already existing) data table
	void refreshDataTable();

private:

	// Creates the toolbar and its accompanying tools
	void setupToolbar();

	// Create the data table from scratch
	void setupDataTable();

	void bindEventHandlers();

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to define settings for a new model.
	void OnNewModel(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to edit an existing model.
	void OnEditModel(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to remove an existing model.
	void OnRemoveModel(wxCommandEvent& WXUNUSED(event));

	// Runs the selected model.
	void OnRunModel(wxCommandEvent& WXUNUSED(event));

	// Runs the selected model.
	void OnExtendModel(wxCommandEvent& WXUNUSED(event));

	// updates the interface after a change in item selection.
	void OnSelectionChange(wxDataViewEvent& event);

	// does everything required to add the model "model".
	void addModel(Model model);

	// adds "model" to model data. Should not be used except within addModel.
	void addModelToModelData(Model model);

	// does everything required to replace the currently selected model with "model".
	void replaceModel(Model model);

	// does everything required to remove the currently selected model, if possible.
	void removeModel();

	// extends the time length of the currently selected model to the latest poll.
	void extendModel();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// does everything required to run the currently selected model, if possible.
	void runModel();

	void prepareModelForRun(Model& model);

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing model data.
	wxDataViewListCtrl* modelData = nullptr;
};