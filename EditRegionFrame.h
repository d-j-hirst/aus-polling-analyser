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
#include <wx/clrpicker.h>

#include "RegionsFrame.h"
#include "Debug.h"
#include "Region.h"

class RegionsFrame;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// IDs for the controls and the menu commands
enum
{
	PA_EditRegion_ButtonID_OK,
	PA_EditRegion_TextBoxID_Name,
	PA_EditRegion_TextBoxID_Population,
	PA_EditRegion_TextBoxID_LastElection2pp,
	PA_EditRegion_TextBoxID_Sample2pp,
	PA_EditRegion_TextBoxID_AdditionalUncertainty
};

// *** EditRegionFrame ***
// Frame that allows the user to edit an already-existing region
// or create a new one if isNewRegion is set to true.
class EditRegionFrame : public wxDialog
{
public:
	// isNewRegion: true if this dialog is for creating a new region, false if it's for editing.
	// parent: Parent frame for this (must be a RegionsFrame).
	// region: Region data to be used if editing (has default values for creating a new region).
	EditRegionFrame(bool isNewRegion, RegionsFrame* const parent,
		Region region = Region("Enter region name here", 0 , 50.0f, 50.0f));

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

private:

	// Calls upon the window to update the preliminary name data based on
	// the result of the GetString() method of "event".
	void updateTextName(wxCommandEvent& event);

	// Calls upon the window to update the preliminary population data based on
	// the result of the GetFloat() method of "event".
	void updateTextPopulation(wxCommandEvent& event);

	// Calls upon the window to update the preliminary last election 2pp data based on
	// the result of the GetFloat() method of "event".
	void updateTextLastElection2pp(wxCommandEvent& event);

	// Calls upon the window to update the preliminary sample 2pp data based on
	// the result of the GetFloat() method of "event".
	void updateTextSample2pp(wxCommandEvent& event);

	// Calls upon the window to update the additional uncertainty data based on
	// the result of the GetFloat() method of "event".
	void updateTextAdditionalUncertainty(wxCommandEvent& event);

	// Data container for the preliminary settings for the region to be created.
	Region region;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	wxStaticText* nameStaticText;
	wxTextCtrl* nameTextCtrl;
	wxStaticText* populationStaticText;
	wxTextCtrl* populationTextCtrl;
	wxStaticText* lastElection2ppStaticText;
	wxTextCtrl* lastElection2ppTextCtrl;
	wxStaticText* sample2ppStaticText;
	wxTextCtrl* sample2ppTextCtrl;
	wxStaticText* additionalUncertaintyStaticText;
	wxTextCtrl* additionalUncertaintyTextCtrl;
	wxButton* okButton;
	wxButton* cancelButton;

	// A pointer to the parent frame.
	RegionsFrame* const parent;

	// Stores whether this dialog is for creating a new region (true) or editing an existing one (false).
	bool isNewRegion;

	std::string lastPopulation;
	std::string lastLastElection2pp;
	std::string lastSample2pp;
	std::string lastAdditionalUncertainty;
};