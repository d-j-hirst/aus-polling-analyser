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

class TextInput;

struct GeneralSettingsData {
	GeneralSettingsData(std::string electionName) :
		electionName(electionName) {}
	std::string electionName;
};

// *** EditPollFrame ***
// Frame that allows the user to edit an already-existing poll
// or create a new one
class GeneralSettingsFrame : public wxDialog
{
public:
	typedef std::function<void(GeneralSettingsData)> OkCallback;

	GeneralSettingsFrame(OkCallback okCallback, GeneralSettingsData data);

private:

	void createControls(int& y);

	void createElectionNameInput(int& y);

	void createOkCancelButtons(int& y);

	void setFinalWindowHeight(int y);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	GeneralSettingsData data;

	std::unique_ptr<TextInput> electionNameInput;

	wxButton* okButton;
	wxButton* cancelButton;

	OkCallback okCallback;
};