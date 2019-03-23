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
#include "Debug.h"
#include "Seat.h"
#include "EditSeatFrame.h"

class ProjectFrame;
class GenericChildFame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum {
	PA_SeatsFrame_Base = 600, // To avoid mixing events with other frames.
	PA_SeatsFrame_FrameID,
	PA_SeatsFrame_DataViewID,
	PA_SeatsFrame_NewSeatID,
	PA_SeatsFrame_EditSeatID,
	PA_SeatsFrame_RemoveSeatID,
};

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
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to define settings for a new seat.
	void OnNewSeat(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to edit an existing seat.
	void OnEditSeat(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to remove an existing seat.
	void OnRemoveSeat(wxCommandEvent& WXUNUSED(event));

	// updates the interface after a change in item selection.
	void OnSelectionChange(wxDataViewEvent& event);

	// does everything required to add the seat "seat".
	void addSeat(Seat seat);

	// adds "seat" to seat data. Should not be used except within addSeat.
	void addSeatToSeatData(Seat seat);

	// does everything required to replace the currently selected seat with "seat".
	void replaceSeat(Seat seat);

	// replaces the currently selected seat with "seat" in seat data.
	// Should not be used except within replaceSeat.
	void replaceSeatInSeatData(Seat seat);

	// does everything required to remove the currently selected seat, if possible.
	void removeSeat();

	// removes the currently selected seat from seat data.
	// Should not be used except within removeSeat.
	void removeSeatFromSeatData();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// A pointer to the parent frame.
	ProjectFrame* const parent;

	// Panel containing poll data.
	wxPanel* dataPanel = nullptr;

	// Control containing seat data.
	wxDataViewListCtrl* seatData = nullptr;
};