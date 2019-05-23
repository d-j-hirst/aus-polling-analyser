#pragma once

#include "ProjectFrame.h"
#include "NewProjectFrame.h"

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

struct NewProjectData;
class ProjectFrame;

class ParentFrame : public wxFrame
{
public:
	ParentFrame(const wxString& title);

	friend class ProjectFrame;

	// Calls on the frame to exit. This closes the whole program
	void OnExit(wxCommandEvent& event);

	// Calls on the frame to display an "About" message dialog.
	void OnAbout(wxCommandEvent& event);

	// Calls on the frame to open the dialog for a new project, if appropriate.
	void OnNew(wxCommandEvent& event);

	// Calls on the frame to open the dialog for a existing project, if appropriate.
	void OnOpen(wxCommandEvent& event);

	// Calls on the frame to save the current project under the existing filename
	// (or use the save project dialog, if possible)
	void OnSave(wxCommandEvent& event);

	// Calls on the frame to open the dialog for saving a project, if appropriate.
	void OnSaveAs(wxCommandEvent& event);

	// Calls on the frame to initialize a new project based on the
	// data in "newProjectData".
	void OnNewProjectReady(NewProjectData& newProjectData);

private:

	// Sets the icon for the frame's top left corner
	void setIcon();

	// Creates the menu bar (along the top of the window)
	void setupMenuBar();

	// Creates the tool bar and its icons (new project, open project, save project)
	void setupToolBar();

	// Binds event handler routines to their respective controls
	void bindEventHandlers();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	std::unique_ptr<wxPanel> notebookPanel;

	// The notebook that handles the different parts of the application's interface.
	std::unique_ptr<ProjectFrame> notebook;

	// This frame's toolbar.
	wxToolBar* toolBar;

};