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
#include "wx/bookctrl.h"

#include "PollingProject.h"

class GenericChildFrame : public wxPanel {

public:

	// "parent" is a pointer to the top-level frame (or notebook page, etc.).
	// "project" is a pointer to the polling project object.
	GenericChildFrame(wxNotebook* parent, int id, wxString title, wxPoint pos, PollingProject* project);

	// Checks whether the window is closed.
	bool getIsClosed();

protected:

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnClose(wxCloseEvent& WXUNUSED(event));

	// Pointer to the polling project.
	PollingProject* project;

	// Used to avoid recursion when the frame resize event is triggered.
	bool handlingResizeEvent;

	// stores whether the frame has already been closed (and therefore shouldn't be closed again).
	bool isClosed;

	// This frame's toolbar.
	wxToolBar* toolBar;
};