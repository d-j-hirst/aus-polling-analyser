#include "LiveBoothFrame.h"

#include "General.h"

#include "wx/dcbuffer.h"

enum ControlId {
	Base = 820,
	Frame,
	DcPanel,
	PrimaryView,
	SecondaryView,
	FilterText
};

LiveBoothFrame::LiveBoothFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Live Booths", wxPoint(333, 0), project),
	refresher(refresher)
{
	refreshToolbar();

	int toolbarHeight = toolBar->GetSize().GetHeight();
	dcPanel = new wxPanel(this, ControlId::DcPanel, wxPoint(0, toolbarHeight), GetClientSize() - wxSize(0, toolbarHeight));

	Layout();
	paint();
	bindEventHandlers();
}

void LiveBoothFrame::paint() {
	wxClientDC dc(dcPanel);
	wxBufferedDC bdc(&dc, dcPanel->GetClientSize());
	render(bdc);
}

void LiveBoothFrame::refreshData() {
	refreshToolbar();
	paint();
}

void LiveBoothFrame::OnPaint(wxPaintEvent& WXUNUSED(event))
{
	wxBufferedPaintDC dc(dcPanel);
	render(dc);
}

void LiveBoothFrame::OnMouseMove(wxMouseEvent&)
{
	paint();
}

void LiveBoothFrame::bindEventHandlers()
{
	dcPanel->Bind(wxEVT_MOTION, &LiveBoothFrame::OnMouseMove, this, ControlId::DcPanel);
	dcPanel->Bind(wxEVT_PAINT, &LiveBoothFrame::OnPaint, this, ControlId::DcPanel);
}

void LiveBoothFrame::refreshToolbar()
{
	if (toolBar) toolBar->Destroy();

	toolBar = new wxToolBar(this, wxID_ANY);

	wxArrayString primaryChoices;
	primaryChoices.push_back("Booths");
	primaryChoices.push_back("Seats");
	primaryChoices.push_back("Compare");

	wxArrayString secondaryChoices;
	secondaryChoices.push_back("Summary");
	secondaryChoices.push_back("Projected");
	secondaryChoices.push_back("Raw");

	primaryViewComboBox = new wxComboBox(toolBar, ControlId::PrimaryView, "Booths", wxPoint(0, 0), wxSize(120, 30), primaryChoices);
	secondaryViewComboBox = new wxComboBox(toolBar, ControlId::SecondaryView, "Summary", wxPoint(0, 0), wxSize(120, 30), secondaryChoices);
	filterTextCtrl = new wxTextCtrl(toolBar, ControlId::FilterText, "", wxPoint(0, 0), wxSize(180, 24));

	toolBar->AddControl(new wxStaticText(toolBar, wxID_ANY, "Primary:"));
	toolBar->AddControl(primaryViewComboBox);
	toolBar->AddSeparator();
	toolBar->AddControl(new wxStaticText(toolBar, wxID_ANY, "Secondary:"));
	toolBar->AddControl(secondaryViewComboBox);
	toolBar->AddSeparator();
	toolBar->AddControl(new wxStaticText(toolBar, wxID_ANY, "Filter:"));
	toolBar->AddControl(filterTextCtrl);

	toolBar->Realize();
}

void LiveBoothFrame::render(wxDC& dc)
{
	dc.SetBackground(wxBrush(wxColour(255, 255, 255)));
	dc.Clear();

	dc.SetTextForeground(*wxBLACK);

	int y = 20;
	dc.DrawText("Live booth inspector proof of concept", wxPoint(20, y));
	y += 40;

	auto snapshots = getSnapshots();
	if (snapshots.empty()) {
		dc.DrawText("No live booth snapshots available.", wxPoint(20, y));
		return;
	}

	auto const& booth = snapshots.front();
	dc.DrawText("First booth snapshot:", wxPoint(20, y));
	y += 30;
	dc.DrawText(wxString::FromUTF8("Seat: " + booth.seatName), wxPoint(20, y));
	y += 25;
	dc.DrawText(wxString::FromUTF8("Booth: " + booth.boothName), wxPoint(20, y));
}

std::vector<LiveV2::Election::BoothSnapshot> LiveBoothFrame::getSnapshots() const
{
	for (auto const& [key, simulation] : project->simulations()) {
		if (!simulation.isLive()) continue;
		auto snapshots = simulation.getLiveBoothSnapshots();
		if (!snapshots.empty()) return snapshots;
	}
	return {};
}
