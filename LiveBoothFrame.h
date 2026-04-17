#pragma once

#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <vector>

#include "GenericChildFrame.h"
#include "PollingProject.h"
#include "ProjectFrame.h"

class GenericChildFrame;

class LiveBoothFrame : public GenericChildFrame
{
public:
	LiveBoothFrame(ProjectFrame::Refresher refresher, PollingProject* project);

	void paint();

	void refreshData();

private:
	void OnPaint(wxPaintEvent& event);

	void OnMouseMove(wxMouseEvent& event);

	void bindEventHandlers();

	void refreshToolbar();

	void render(wxDC& dc);

	std::vector<LiveV2::Election::BoothSnapshot> getSnapshots() const;

	ProjectFrame::Refresher refresher;

	wxComboBox* primaryViewComboBox = nullptr;
	wxComboBox* secondaryViewComboBox = nullptr;
	wxTextCtrl* filterTextCtrl = nullptr;

	wxPanel* dcPanel = nullptr;
};
