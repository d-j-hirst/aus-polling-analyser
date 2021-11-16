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
#include "Region.h"

class FloatInput;
class IntInput;
class TextInput;

// *** EditRegionFrame ***
// Frame that allows the user to edit an already-existing region
// or create a new one.
class EditRegionFrame : public wxDialog
{
public:
	enum class Function {
		New,
		Edit
	};

	typedef std::function<void(Region)> OkCallback;

	// function: whether this is for a new party or editing an existing party
	// callback: function to be called when this 
	EditRegionFrame(Function function, OkCallback callback,
		Region region = Region("Enter region name here", 0 , 50.0f, 50.0f));

private:

	void createControls(int& y);

	// Each of these takes a value for the current y-position
	void createNameInput(int& y);
	void createPopulationInput(int& y);
	void createLastElection2ppInput(int& y);
	void createSample2ppInput(int& y);
	void createAnalysisCodeInput(int& y);
	void createHomeRegionModInput(int& y);

	void createOkCancelButtons(int& y);

	void setFinalWindowHeight(int y);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	// Data container for the preliminary settings for the region to be created.
	Region region;

	std::unique_ptr<TextInput> nameInput;
	std::unique_ptr<IntInput> populationInput;
	std::unique_ptr<FloatInput> lastElection2ppInput;
	std::unique_ptr<FloatInput> sample2ppInput;
	std::unique_ptr<TextInput> analysisCodeInput;
	std::unique_ptr<FloatInput> homeRegionModInput;

	wxButton* okButton;
	wxButton* cancelButton;

	// function to call back to once the user clicks OK.
	OkCallback callback;
};