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
#include "ProjectFrame.h"
#include "NewProjectFrame.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands of the ProjectFrame
enum
{
	PA_ProjectFrame_Items_Base = 0, // To avoid mixing events with other frames.
	// Item ID for starting a new project
	PA_ItemID_New = wxID_NEW,

	// Item ID for opening a project
	PA_ItemID_Open = wxID_OPEN,

	// Item ID for saving the project
	PA_ItemID_SaveAs = wxID_SAVEAS,

	// Item ID for exiting the program
	PA_ItemID_Quit = wxID_EXIT,

	// it is important for the id corresponding to the "About" command to have
	// this standard value as otherwise it won't be handled properly under Mac
	// (where it is special and put into the "Apple" menu)
	PA_ItemID_About = wxID_ABOUT
};

// IDs for the controls and the menu commands of the ProjectFrame
enum
{
	PA_PartiesFrame_Tools_Base = 100, // To avoid mixing events with other frames.
	// Tool ID for starting a new project
	PA_ToolID_New = wxID_NEW,

	// Tool ID for opening the project
	PA_ToolID_Open = wxID_OPEN,

	// Tool ID for saving the project
	PA_ToolID_SaveAs = wxID_SAVEAS,
};

struct NewProjectData;
class ProjectFrame;

class ParentFrame : public wxFrame
{
public:
	ParentFrame(const wxString& title);

	friend class ProjectFrame;

	// Calls on the frame to quit.
	void OnQuit(wxCommandEvent& event);

	// Calls on the frame to display an "About" message dialog.
	void OnAbout(wxCommandEvent& event);

	// Calls on the frame to open the dialog for a new project, if appropriate.
	void OnNew(wxCommandEvent& event);

	// Calls on the frame to open the dialog for a existing project, if appropriate.
	void OnOpen(wxCommandEvent& event);

	// Calls on the frame to open the dialog for saving a project, if appropriate.
	void OnSaveAs(wxCommandEvent& event);

	// Calls on the frame to initialize a new project based on the
	// data in "newProjectData".
	void OnNewProjectReady(NewProjectData& newProjectData);

private:

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	std::unique_ptr<wxPanel> notebookPanel;

	// The notebook that handles the different parts of the application's interface.
	std::unique_ptr<ProjectFrame> notebook;

	// This frame's toolbar.
	wxToolBar* toolBar;

};