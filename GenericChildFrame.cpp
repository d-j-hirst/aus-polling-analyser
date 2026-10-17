#include "GenericChildFrame.h"
#include "ProjectFrame.h"

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

bool GenericChildFrame::allowsLiveAction(int id) const {
	auto* owner = dynamic_cast<ProjectFrame*>(GetParent());
	return !owner || !owner->isLiveRunning() || liveReadOnlyActions.contains(id);
}

void GenericChildFrame::allowDuringLiveRun(std::initializer_list<int> ids) {
	liveReadOnlyActions.insert(ids.begin(), ids.end());
}

void GenericChildFrame::applyActivityPolicy() {
	if (!toolBar) return;
	auto* owner = dynamic_cast<ProjectFrame*>(GetParent());
	if (!owner || !owner->isLiveRunning()) return;
	// Apply after ordinary selection-based enablement and toolbar recreation.
	// The event filter independently rejects writing commands and shortcuts.
	for (std::size_t i = 0; i < toolBar->GetToolsCount(); ++i) {
		auto* tool = toolBar->GetToolByPos(int(i));
		if (tool && !tool->IsSeparator() && !allowsLiveAction(tool->GetId()))
			toolBar->EnableTool(tool->GetId(), false);
	}
	for (auto* child : toolBar->GetChildren())
		if (!allowsLiveAction(child->GetId())) child->Disable();
}

void GenericChildFrame::refreshActivityPolicy() {
	if (toolBar) {
		for (std::size_t i = 0; i < toolBar->GetToolsCount(); ++i) {
			auto* tool = toolBar->GetToolByPos(int(i));
			if (tool && !tool->IsSeparator()) toolBar->EnableTool(tool->GetId(), true);
		}
		for (auto* child : toolBar->GetChildren()) child->Enable();
	}
	updateInterface();
	applyActivityPolicy();
}
