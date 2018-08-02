#include "NewProjectFrame.h"

// frame constructor
NewProjectFrame::NewProjectFrame(const wxString& title, ParentFrame* const parent)
	: wxDialog(NULL, 0, title), parent(parent)
{

	// setup the internal data, we'll use it to define the buttons to avoid bugs.
	newProjectData.projectName = "Enter project name here";

	// Create the controls for the project name.
	nameStaticText = new wxStaticText(this, 0, "Project Name:", wxPoint(2, 2), wxSize(100, 23));
	nameTextCtrl = new wxTextCtrl(this, PA_NewProject_TextBoxID_NAME, newProjectData.projectName, wxPoint(100, 0), wxSize(300, 23));

	// Create the OK and cancel buttons.
	okButton = new wxButton(this, PA_NewProject_ButtonID_OK, "OK", wxPoint(67, 24), wxSize(100, 24));
	cancelButton = new wxButton(this, wxID_CANCEL, "Cancel", wxPoint(233, 24), wxSize(100, 24));

	// Bind events to the functions that should be carried out by them.
	Bind(wxEVT_BUTTON, &NewProjectFrame::OnOK, this, PA_NewProject_ButtonID_OK);
	Bind(wxEVT_TEXT, &NewProjectFrame::updateTextName, this, PA_NewProject_TextBoxID_NAME);
}

void NewProjectFrame::OnOK(wxCommandEvent& WXUNUSED(event)) {

	// Get the parent frame to initialize the project using the data.
	parent->OnNewProjectReady(newProjectData);

	// Then close this dialog.
	Close();
}

void NewProjectFrame::updateTextName(wxCommandEvent& event) {

	// updates the preliminary project data with the string from the event.
	newProjectData.projectName = event.GetString();
}