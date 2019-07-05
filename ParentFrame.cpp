#include "ParentFrame.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

const std::string AboutScreenText = std::string("Welcome to Polling Analyser!\n\n"
	"This is Polling Analyser, a polling and election analysis program\n"
	"based on the minimal wxWidgets sample\n"
	"using ") + wxVERSION_STRING + std::string("\nunder ") + wxGetOsDescription() + ".";

const std::string AboutScreenTitle = "About Polling Analyser";

// IDs for the controls and the menu commands of the ProjectFrame
enum class Item
{
	Base = 0, // To avoid mixing events with other frames, each frame's IDs have a unique value.
	New = wxID_NEW,
	Open = wxID_OPEN,
	Save = wxID_SAVE,
	SaveAs = wxID_SAVEAS,

	// Item ID for exiting the program
	Exit = wxID_EXIT,

	// it is important for the id corresponding to the "About" command to have
	// this standard value as otherwise it won't be handled properly under Mac
	// (where it is special and put into the "Apple" menu)
	About = wxID_ABOUT
};

// IDs for the controls and the menu commands of the ProjectFrame
enum class Tool
{
	Base = 100, // To avoid mixing events with other frames.
	New = wxID_NEW,
	Open = wxID_OPEN,
	Save = wxID_SAVE,
};

// ----------------------------------------------------------------------------
// notebook frame
// ----------------------------------------------------------------------------

ParentFrame::ParentFrame(const wxString& title)
	: wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(1024, 720)),
	notebookPanel(nullptr), notebook(nullptr)
{
	setIcon();
	setupMenuBar();
	setupToolBar();
	bindEventHandlers();
	updateInterface();
}

wxWindow* ParentFrame::accessNotebookPanel()
{
	return notebookPanel.get();
}

void ParentFrame::OnExit(wxCommandEvent& WXUNUSED(event))
{
	// The argument is true in order to force the program to close.
	Close(true);
}

void ParentFrame::OnAbout(wxCommandEvent& WXUNUSED(event))
{
	// Displays a message box "about" the program.
	wxMessageBox(AboutScreenText, AboutScreenTitle, wxOK | wxICON_INFORMATION, this);
}

void ParentFrame::OnNew(wxCommandEvent& WXUNUSED(event))
{

	// Check if the user needs to save their current project.
	if (notebook) {
		if (notebook->checkSave()) return;
	}

	NewProjectData newProjectData = receiveNewProjectInfoFromUser();
	if (!newProjectData.valid) return;

	createNotebook(newProjectData);

	// update the interface (so that the save tool is enabled, for instance).
	updateInterface();
}

void ParentFrame::OnOpen(wxCommandEvent& WXUNUSED(event))
{

	// Check if the user needs to save their current project.
	if (notebook) {
		if (notebook->checkSave()) return;
	}

	std::string pathName = receiveOpenProjectPathnameFromUser();
	if (pathName.empty()) return;

	// we need to destroy the notebook if loading failed
	try {
		createNotebook(pathName);
	}
	catch (LoadProjectFailedException) {
		notebook.reset();
	}

	// update the interface (so that the save tool is enabled, for instance).
	updateInterface();
}

void ParentFrame::OnSave(wxCommandEvent & WXUNUSED(event))
{
	if (notebook) {
		notebook->save();
	}
}

void ParentFrame::OnSaveAs(wxCommandEvent& WXUNUSED(event))
{
	if (notebook) {
		notebook->saveAs();
	}
}

void ParentFrame::createNotebook(NewProjectData newProjectData) {
	createNotebookPanel();
	notebook.reset(new ProjectFrame(this, newProjectData));
}

void ParentFrame::createNotebook(std::string pathName) {
	createNotebookPanel();
	notebook.reset(new ProjectFrame(this, pathName));
}

void ParentFrame::createNotebookPanel()
{
	notebookPanel.reset(new wxPanel(this, wxID_ANY, wxDefaultPosition, this->GetClientSize()));
}

void ParentFrame::setIcon()
{
	// This is just a default wxWidgets application icon
	SetIcon(wxICON(sample));
}

void ParentFrame::setupMenuBar()
{
	// Create the file menu.
	wxMenu *fileMenu = new wxMenu;
	fileMenu->Append(int(Item::New), "&New Project\tF2", "Start new project");
	fileMenu->Append(int(Item::Open), "&Open Project\tCtrl+O", "Open an existing project");
	fileMenu->Append(int(Item::Save), "&Save\tCtrl+S", "Save this project under its previous filename");
	fileMenu->Append(int(Item::SaveAs), "Save Project &As", "Save this project under a new filename");
	fileMenu->Append(int(Item::Exit), "E&xit\tAlt-X", "Exit this program");

	// Create the help menu.
	wxMenu *helpMenu = new wxMenu;
	helpMenu->Append(int(Item::About), "&About\tF1", "Show about dialog");

	// Append the freshly created menus to the menu bar.
	wxMenuBar *menuBar = new wxMenuBar();
	menuBar->Append(fileMenu, "&File");
	menuBar->Append(helpMenu, "&Help");

	// Attach this menu bar to the frame.
	SetMenuBar(menuBar);
}

void ParentFrame::setupToolBar()
{
	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[3];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\new.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\open.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\save.png", wxBITMAP_TYPE_PNG);

	toolBar = CreateToolBar();

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(int(Tool::New), "New Project", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Project");
	toolBar->AddTool(int(Tool::Open), "Open Project", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Open Project");
	toolBar->AddTool(int(Tool::Save), "Save Project", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Save Project");

	// Realize the toolbar, so that the tools actually display.
	toolBar->Realize();
}

void ParentFrame::bindEventHandlers()
{
	// Bind events to the menus just added.
	Bind(wxEVT_COMMAND_MENU_SELECTED, &ParentFrame::OnNew, this, int(Item::New));
	Bind(wxEVT_COMMAND_MENU_SELECTED, &ParentFrame::OnOpen, this, int(Item::Open));
	Bind(wxEVT_COMMAND_MENU_SELECTED, &ParentFrame::OnSave, this, int(Item::Save));
	Bind(wxEVT_COMMAND_MENU_SELECTED, &ParentFrame::OnSaveAs, this, int(Item::SaveAs));
	Bind(wxEVT_COMMAND_MENU_SELECTED, &ParentFrame::OnAbout, this, int(Item::About));
	Bind(wxEVT_COMMAND_MENU_SELECTED, &ParentFrame::OnExit, this, int(Item::Exit));

	// Bind events to the toolbar.
	Bind(wxEVT_TOOL, &ParentFrame::OnNew, this, int(Tool::New));
	Bind(wxEVT_TOOL, &ParentFrame::OnOpen, this, int(Tool::Open));
	Bind(wxEVT_TOOL, &ParentFrame::OnSave, this, int(Tool::Save));
}

void ParentFrame::updateInterface() {
	bool projectExists = notebook != nullptr;
	toolBar->EnableTool(int(Tool::Save), projectExists);
}

NewProjectData ParentFrame::receiveNewProjectInfoFromUser()
{
	// The NewProjectFrame will add the relevant information into this structure
	NewProjectData newProjectData;

	// When the frame is shown it will recieve the settings for the new project and store them in newProjectData
	NewProjectFrame *frame = new NewProjectFrame(this, newProjectData);
	frame->ShowModal();

	delete frame;

	return newProjectData;
}

std::string ParentFrame::receiveOpenProjectPathnameFromUser()
{
	// initialize the open dialog
	wxFileDialog* openFileDialog = new wxFileDialog(
		this,
		"Open Project",
		"",
		"",
		"Polling Analysis files (*.pol)|*.pol",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (openFileDialog->ShowModal() == wxID_CANCEL)
		return "";     // the user changed their mind...

	return openFileDialog->GetPath().ToStdString();
}
