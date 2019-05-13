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

#include "ProjectFrame.h"
#include "NewProjectData.h"

class ParentFrame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
	PA_NewProject_ButtonID_OK = 1,
	PA_NewProject_TextBoxID_NAME = 2,
};

// *** NewProjectFrame ***
// Frame that gives the basic settings for a new project.
class NewProjectFrame : public wxDialog
{
public:
	NewProjectFrame(const wxString& title, ParentFrame* const parent);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateTextName(wxCommandEvent& event);

private:

	// Data container for the preliminary settings for the new project to be created.
	NewProjectData newProjectData;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* nameStaticText;
	wxTextCtrl* nameTextCtrl;
	wxButton* okButton;
	wxButton* cancelButton;

	// A pointer to the parent frame.
	ParentFrame* const parent;
};