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
#include "Seat.h"
#include "EditSeatFrame.h"

class ProjectFrame;
class GenericChildFame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------


// *** SeatsFrame ***
// Frame that allows the user to create aggregated seats using the poll data.
class SeatsFrame : public GenericChildFrame
{
public:
	// "parent" is a pointer to the top-level frame (or notebook page, etc.).
	// "project" is a pointer to the polling project object.
	SeatsFrame(ProjectFrame* const parent, PollingProject* project);

	// Calls on the frame to create a new seat based on "Seat".
	void OnNewSeatReady(Seat& seat);

	// Calls on the frame to edit the currently selected seat based on "Seat".
	void OnEditSeatReady(Seat& seat);

	// updates the data to take into account any changes.
	void refreshData();

private:

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& event);

	// Opens the dialog that allows the user to define settings for a new seat.
	void OnNewSeat(wxCommandEvent& event);

	// Opens the dialog that allows the user to edit an existing seat.
	void OnEditSeat(wxCommandEvent& event);

	// Opens the dialog that allows the user to remove an existing seat.
	void OnRemoveSeat(wxCommandEvent& event);

	// Opens the dialog that allows the user to remove an existing seat.
	void OnSeatResults(wxCommandEvent& event);

	// updates the interface after a change in item selection.
	void OnSelectionChange(wxDataViewEvent& event);

	// does everything required to add the seat "seat".
	void addSeat(Seat seat);

	// adds "seat" to seat data. Should not be used except within addSeat.
	void addSeatToSeatData(Seat seat);

	// does everything required to replace the currently selected seat with "seat".
	void replaceSeat(Seat seat);

	// does everything required to remove the currently selected seat, if possible.
	void removeSeat();

	// Show message boxes that summarize the results of the seat at the last election
	void showSeatResults();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// A pointer to the parent frame.
	ProjectFrame* const parent;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing seat data.
	wxDataViewListCtrl* seatData = nullptr;
};