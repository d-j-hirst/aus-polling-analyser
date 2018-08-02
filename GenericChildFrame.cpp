#include "GenericChildFrame.h"

class ParentFrame;

// Constructor for the ProjectFrame

GenericChildFrame::GenericChildFrame(wxNotebook* parent, int id, wxString title, wxPoint pos, PollingProject* project)
	: wxPanel(parent, id, pos, wxSize(0, 0), 2621440L, title),
	project(project),
	handlingResizeEvent(false),
	isClosed(false),
	toolBar(nullptr)
{
	wxSize parentSize = parent->GetClientSize();
	parentSize.DecBy(8, 25);
	SetSize(parentSize);
}

bool GenericChildFrame::getIsClosed() {
	return isClosed;
}

void GenericChildFrame::OnClose(wxCloseEvent& WXUNUSED(event)) {
	isClosed = true;
	Destroy();
}