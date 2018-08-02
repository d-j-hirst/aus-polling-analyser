#include "ParentFrame.h"

// ----------------------------------------------------------------------------
// notebook frame
// ----------------------------------------------------------------------------

ParentFrame::ParentFrame(const wxString& title)
	: wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(1024, 720)),
	notebookPanel(nullptr), notebook(nullptr) {
	// Set the frame icon.
	SetIcon(wxICON(sample));

	// *** Menus *** //

	// Create the file menu.
	wxMenu *fileMenu = new wxMenu;
	fileMenu->Append(PA_ItemID_New, "&New Project\tF2", "Start new project");
	fileMenu->Append(PA_ItemID_Open, "&Open Project\tCtrl+O", "Open an existing project");
	fileMenu->Append(PA_ItemID_SaveAs, "&Save Project As", "Save this project under a new filename");
	fileMenu->Append(PA_ItemID_Quit, "E&xit\tAlt-X", "Quit this program");

	// Create the help menu.
	wxMenu *helpMenu = new wxMenu;
	helpMenu->Append(PA_ItemID_About, "&About\tF1", "Show about dialog");

	// Append the freshly created menus to the menu bar.
	wxMenuBar *menuBar = new wxMenuBar();
	menuBar->Append(fileMenu, "&File");
	menuBar->Append(helpMenu, "&Help");

	// Attach this menu bar to the frame.
	SetMenuBar(menuBar);

	// *** Status Bar *** //

	// create a status bar just for fun (by default with 1 pane only)
	//CreateStatusBar(2);
	//SetStatusText("Ready");

	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[3];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\new.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\open.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\save.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	//toolBar = new wxToolBar(this, wxID_ANY);

	toolBar = CreateToolBar();

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(PA_ToolID_New, "New Project", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Project");
	toolBar->AddTool(PA_ToolID_Open, "Open Project", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Open Project");
	toolBar->AddTool(PA_ToolID_SaveAs, "Save Project", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Save Project");

	// Bind events to the menus just added.
	Bind(wxEVT_COMMAND_MENU_SELECTED, &ParentFrame::OnNew, this, PA_ItemID_New);
	Bind(wxEVT_COMMAND_MENU_SELECTED, &ParentFrame::OnOpen, this, PA_ItemID_Open);
	Bind(wxEVT_COMMAND_MENU_SELECTED, &ParentFrame::OnSaveAs, this, PA_ItemID_SaveAs);
	Bind(wxEVT_COMMAND_MENU_SELECTED, &ParentFrame::OnAbout, this, PA_ItemID_About);
	Bind(wxEVT_COMMAND_MENU_SELECTED, &ParentFrame::OnQuit, this, PA_ItemID_Quit);

	// Bind events to the toolbar.
	Bind(wxEVT_TOOL, &ParentFrame::OnNew, this, PA_ToolID_New);
	Bind(wxEVT_TOOL, &ParentFrame::OnOpen, this, PA_ToolID_Open);
	Bind(wxEVT_TOOL, &ParentFrame::OnSaveAs, this, PA_ToolID_SaveAs);

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();

	updateInterface();
}

void ParentFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
	// The argument is true in order to force the program to close.
	Close(true);
}

void ParentFrame::OnAbout(wxCommandEvent& WXUNUSED(event))
{
	// Displays a message box "about" the program.
	wxMessageBox(wxString::Format
		(
		"Welcome to Polling Analyser!\n"
		"\n"
		"This is Polling Analyser, a polling analysis program\n"
		"based on the minimal wxWidgets sample\n"
		"running under %s.",
		wxVERSION_STRING,
		wxGetOsDescription()
		),
		"About Polling Analyser",
		wxOK | wxICON_INFORMATION,
		this);
}

void ParentFrame::OnNew(wxCommandEvent& WXUNUSED(event))
{

	// Check if the user needs to save their current project.
	if (notebook) {
		if (notebook->checkSave()) return;
	}

	// Create the new project frame (where initial settings for the new project are chosen).
	NewProjectFrame *frame = new NewProjectFrame("New Project", this);

	// Show the frame.
	frame->ShowModal();

	// update the interface (so that the save tool is enabled, for instance).
	updateInterface();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void ParentFrame::OnOpen(wxCommandEvent& WXUNUSED(event))
{

	// Check if the user needs to save their current project.
	if (notebook) {
		if (notebook->checkSave()) return;
	}

	// initialize the open dialog
	wxFileDialog* openFileDialog = new wxFileDialog(
		this,
		"Open Project",
		"",
		"",
		"Polling Analysis files (*.pol)|*.pol",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (openFileDialog->ShowModal() == wxID_CANCEL)
		return;     // the user changed their mind...

	std::string pathName = openFileDialog->GetPath().ToStdString();

	notebookPanel.reset(new wxPanel(this, wxID_ANY, wxDefaultPosition, this->GetClientSize()));

	notebook.reset(new ProjectFrame(this, pathName));

	//Layout();
}

void ParentFrame::OnSaveAs(wxCommandEvent& WXUNUSED(event))
{
	if (notebook) {
		notebook->saveAs();
	}
}

void ParentFrame::OnNewProjectReady(NewProjectData& newProjectData) {

	// Initialize the project using the NewProjectData provided.

	notebookPanel.reset(new wxPanel(this, wxID_ANY, wxDefaultPosition, this->GetClientSize()));

	notebook.reset(new ProjectFrame(this, newProjectData));
}