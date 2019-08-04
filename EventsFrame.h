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
#include "Event.h"

// *** EventsFrame ***
// Frame that allows the user to create events that can be used to influence or annotate the polling model.
class EventsFrame : public GenericChildFrame
{
public:
	// "refresher" is used to refresh the display of other tabs, etc. as needed
	// "project" is a pointer to the polling project object.
	EventsFrame(ProjectFrame::Refresher refresher, PollingProject* project);

	// Calls on the frame to create a new model based on "Model".
	void OnNewEventReady(Event& event);

	// Calls on the frame to edit the currently selected model based on "Model".
	void OnEditEventReady(Event& event);

	// updates the data to take into account any changes, such as removed pollsters/parties.
	void refreshDataTable();

private:

	// Creates the toolbar and its accompanying tools
	void setupToolbar();

	// Create the data table from scratch
	void setupDataTable();

	void bindEventHandlers();

	// Adjusts controls so that they fill the frame space when it is resized.
	void OnResize(wxSizeEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to define settings for a new event.
	void OnNewEvent(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to edit an existing event.
	void OnEditEvent(wxCommandEvent& WXUNUSED(event));

	// Opens the dialog that allows the user to remove an existing event.
	void OnRemoveEvent(wxCommandEvent& WXUNUSED(event));

	// updates the interface after a change in item selection.
	void OnSelectionChange(wxDataViewEvent& event);

	// does everything required to add the event "event".
	void addEvent(Event event);

	// adds "event" to event data. Should not be used except within addEvent.
	void addEventToEventData(Event event);

	// does everything required to replace the currently selected event with "event".
	void replaceEvent(Event event);

	// does everything required to remove the currently selected event, if possible.
	void removeEvent();

	// updates the interface for any changes, such as enabled/disabled buttons.
	void updateInterface();

	// Allows actions in this frame to trigger refreshes in other frames
	ProjectFrame::Refresher refresher;

	// Panel containing event data.
	wxPanel* dataPanel = nullptr;

	// Control containing event data.
	wxDataViewListCtrl* eventData = nullptr;
};