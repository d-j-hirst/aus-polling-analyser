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

#include <sstream>
#include <wx/valnum.h>

#include "PollingProject.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
	PA_PartySettings_ButtonID_OK,
	PA_PartySettings_TextBoxID_OthersPreferenceFlow,
	PA_PartySettings_TextBoxID_OthersExhaustRate,
};

// *** PartySettingsData ***
// Holds the data that is worked on by this frame.
// If OK is pressed, this data is then passed back to the project and used to actually update the settings.
struct PartySettingsData {
	PartySettingsData(float othersPreferenceFlow, float othersExhaustRate) : 
		othersPreferenceFlow(othersPreferenceFlow), othersExhaustRate(othersExhaustRate) {}
	float othersPreferenceFlow;
	float othersExhaustRate;
};

// *** PartySettingsFrame ***
// Frame that allows the user to adjust general party-related settings such as the "others" preference flow.
class PartySettingsFrame : public wxDialog
{
public:
	// partySettingsData: Party Settings data to be used.
	PartySettingsFrame(PartySettingsData partySettingsData, std::function<void(PartySettingsData)> callback);

private:

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	// Calls upon the window to update the preliminary "others" preference flow data based on
	// the result of the GetFloat() method of "event".
	void updateTextOthersPreferenceFlow(wxCommandEvent& event);

	// Calls upon the window to update the preliminary "others" exhaust rate data based on
	// the result of the GetFloat() method of "event".
	void updateTextOthersExhaustRate(wxCommandEvent& event);

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* othersPreferenceFlowStaticText;
	wxTextCtrl* othersPreferenceFlowTextCtrl;
	wxStaticText* othersExhaustRateStaticText;
	wxTextCtrl* othersExhaustRateTextCtrl;
	wxButton* okButton;
	wxButton* cancelButton;

	// The party settings data being worked on by this frame.
	PartySettingsData partySettingsData;

	// Keeps the "others" preference flow saved in case a text entry results in an invalid value.
	std::string lastOthersPreferenceFlow;

	// Keeps the "others" exhaust rate saved in case a text entry results in an invalid value.
	std::string lastOthersExhaustRate;

	// function to call back to once the user clicks OK.
	std::function<void(PartySettingsData)> callback;
};