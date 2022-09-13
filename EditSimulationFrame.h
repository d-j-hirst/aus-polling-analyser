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

#include "Simulation.h"

class ChoiceInput;
class FloatInput;
class IntInput;
class ProjectionCollection;
class TextInput;
class DateInput;

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// *** EditSimulationFrame ***
// Frame that allows the user to edit an already-existing simulation
// or create a new one if isNewSimulation is set to true.
class EditSimulationFrame : public wxDialog
{
public:
	enum class Function {
		New,
		Edit
	};

	typedef std::function<void(Simulation::Settings)> OkCallback;

	// function: whether this is for a new projection or editing an existing projection
	// callback: function to be called when the OK button is pressed
	EditSimulationFrame(Function function, OkCallback okCallback, ProjectionCollection const& projections, Simulation::Settings settings = Simulation::Settings());

private:

	void createControls(int& y);

	// Each of these takes a value for the current y-position
	void createNameInput(int& y);
	void createProjectionInput(int& y);
	void createNumIterationsInput(int& y);
	void createPrevElection2ppInput(int& y);
	void createPrevTermCodesInput(int& y);
	void createLiveInput(int& y);
	void createReportModeInput(int& y);
	void createPreviousResultsUrlInput(int& y);
	void createPreloadUrlInput(int& y);
	void createCurrentTestUrlInput(int& y);
	void createCurrentRealUrlInput(int& y);
	void createFedElectionDateInput(int& y);

	void createOkCancelButtons(int& y);

	void setFinalWindowHeight(int y);

	// Calls upon the window to send its data to the parent frame and close.
	void OnOK(wxCommandEvent& WXUNUSED(event));

	// Callbacks for the controls to update the party data.
	void updatePrevTermCodes(std::string prevTermCodes);

	// Data container for the preliminary settings for the simulation to be created.
	Simulation::Settings simulationSettings;

	ProjectionCollection const& projections;

	// Control pointers that are really only here to shut up the
	// compiler about unused variables in the constructor - no harm done.
	std::unique_ptr<TextInput> nameInput;
	std::unique_ptr<ChoiceInput> projectionInput;
	std::unique_ptr<IntInput> numIterationsInput;
	std::unique_ptr<FloatInput> prevElection2ppInput;
	std::unique_ptr<TextInput> prevTermCodesInput;
	std::unique_ptr<ChoiceInput> liveInput;
	std::unique_ptr<ChoiceInput> reportModeInput;
	std::unique_ptr<TextInput> previousResultsUrlInput;
	std::unique_ptr<TextInput> preloadUrlInput;
	std::unique_ptr<TextInput> currentTestUrlInput;
	std::unique_ptr<TextInput> currentRealUrlInput;
	std::unique_ptr<DateInput> fedElectionDateInput;

	wxButton* okButton;
	wxButton* cancelButton;

	// function to call back to once the user clicks OK.
	OkCallback okCallback;
};