#include "SeatsFrame.h"
#include "General.h"

enum SeatColumnsEnum {
	SeatColumn_Name,
	SeatColumn_Incumbent,
	SeatColumn_Challenger,
	SeatColumn_Region,
	SeatColumn_Margin,
	SeatColumn_LocalModifier,
	SeatColumn_IncumbentOdds,
	SeatColumn_ChallengerOdds,
	SeatColumn_ProjectedMargin,
	SeatColumn_WinPercent,
	SeatColumn_TippingPoint,
};

// IDs for the controls and the menu commands
enum {
	PA_SeatsFrame_Base = 600, // To avoid mixing events with other frames.
	PA_SeatsFrame_FrameID,
	PA_SeatsFrame_DataViewID,
	PA_SeatsFrame_NewSeatID,
	PA_SeatsFrame_EditSeatID,
	PA_SeatsFrame_RemoveSeatID,
	PA_SeatsFrame_SeatResultsID,
};

// frame constructor
SeatsFrame::SeatsFrame(ProjectFrame* const parent, PollingProject* project)
	: GenericChildFrame(parent, PA_SeatsFrame_FrameID, "Seats", wxPoint(0, 0), project),
	parent(parent)
{

	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;
	wxBitmap toolBarBitmaps[4];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[1] = wxBitmap("bitmaps\\edit.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[2] = wxBitmap("bitmaps\\remove.png", wxBITMAP_TYPE_PNG);
	toolBarBitmaps[3] = wxBitmap("bitmaps\\details.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY);

	// Add the tools that will be used on the toolbar.
	toolBar->AddTool(PA_SeatsFrame_NewSeatID, "New Seat", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "New Seat");
	toolBar->AddTool(PA_SeatsFrame_EditSeatID, "Edit Seat", toolBarBitmaps[1], wxNullBitmap, wxITEM_NORMAL, "Edit Seat");
	toolBar->AddTool(PA_SeatsFrame_RemoveSeatID, "Remove Seat", toolBarBitmaps[2], wxNullBitmap, wxITEM_NORMAL, "Remove Seat");
	toolBar->AddTool(PA_SeatsFrame_SeatResultsID, "Seat Results", toolBarBitmaps[3], wxNullBitmap, wxITEM_NORMAL, "Seat Results");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();

	// *** Seat Data Table *** //

	int toolBarHeight = toolBar->GetSize().GetHeight();

	dataPanel = new wxPanel(this, wxID_ANY, wxPoint(0, toolBarHeight), GetClientSize() - wxSize(0, toolBarHeight));

	// Create the seat data control.
	seatData = new wxDataViewListCtrl(dataPanel,
		PA_SeatsFrame_DataViewID,
		wxPoint(0, 0),
		dataPanel->GetClientSize());

	// *** Party Data Table Columns *** //

	refreshData();

	// *** Binding Events *** //

	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &SeatsFrame::OnResize, this, PA_SeatsFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &SeatsFrame::OnNewSeat, this, PA_SeatsFrame_NewSeatID);
	Bind(wxEVT_TOOL, &SeatsFrame::OnEditSeat, this, PA_SeatsFrame_EditSeatID);
	Bind(wxEVT_TOOL, &SeatsFrame::OnRemoveSeat, this, PA_SeatsFrame_RemoveSeatID);
	Bind(wxEVT_TOOL, &SeatsFrame::OnSeatResults, this, PA_SeatsFrame_SeatResultsID);

	// Need to update the interface if the selection changes
	Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &SeatsFrame::OnSelectionChange, this, PA_SeatsFrame_DataViewID);
}

void SeatsFrame::OnResize(wxSizeEvent& WXUNUSED(event)) {
	// Set the data table to the entire client size.
	seatData->SetSize(dataPanel->GetClientSize() + wxSize(0, 1));
}

void SeatsFrame::OnNewSeat(wxCommandEvent& WXUNUSED(event)) {

	if (project->getRegionCount() == 0) {
		wxMessageDialog* message = new wxMessageDialog(this,
			"Seats are defined as belonging to a particular region. Please define one region before creating a seat.");

		message->ShowModal();
		return;
	}

	// Create the new project frame (where initial settings for the new project are chosen).
	EditSeatFrame *frame = new EditSeatFrame(true, this, project, Seat());

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void SeatsFrame::OnEditSeat(wxCommandEvent& WXUNUSED(event)) {

	int seatIndex = seatData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (seatIndex == -1) return;

	// Create the new project frame (where initial settings for the new project are chosen).
	EditSeatFrame *frame = new EditSeatFrame(false, this, project, project->getSeat(seatIndex));

	// Show the frame.
	frame->ShowModal();

	// This is needed to avoid a memory leak.
	delete frame;
	return;
}

void SeatsFrame::OnRemoveSeat(wxCommandEvent& WXUNUSED(event)) {

	int seatIndex = seatData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (seatIndex == -1) return;

	removeSeat();

	return;
}

// updates the interface after a change in item selection.
void SeatsFrame::OnSeatResults(wxCommandEvent& WXUNUSED(event)) {

	int seatIndex = seatData->GetSelectedRow();

	// If the button is somehow clicked when there is no poll selected, just stop.
	if (seatIndex == -1) return;
	
	showSeatResults();
}

// updates the interface after a change in item selection.
void SeatsFrame::OnSelectionChange(wxDataViewEvent& WXUNUSED(event)) {
	updateInterface();
}

void SeatsFrame::OnNewSeatReady(Seat& seat) {
	addSeat(seat);
}

void SeatsFrame::OnEditSeatReady(Seat& seat) {
	replaceSeat(seat);
}

void SeatsFrame::refreshData() {

	seatData->DeleteAllItems();
	seatData->ClearColumns();

	// *** Seat Data Table Columns *** //

	// Add the data columns that show the properties of the seats.
	seatData->AppendTextColumn("Seat Name", wxDATAVIEW_CELL_INERT, 120, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Incumbent", wxDATAVIEW_CELL_INERT, 75, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Challenger", wxDATAVIEW_CELL_INERT, 75, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Region", wxDATAVIEW_CELL_INERT, 60, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Margin", wxDATAVIEW_CELL_INERT, 55, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Local Inc. Modifier", wxDATAVIEW_CELL_INERT, 115, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Inc. Odds", wxDATAVIEW_CELL_INERT, 65, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Ch. Odds", wxDATAVIEW_CELL_INERT, 65, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Inc. Win %", wxDATAVIEW_CELL_INERT, 80, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Tipping Point %", wxDATAVIEW_CELL_INERT, 100, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);
	seatData->AppendTextColumn("Sim. Margin", wxDATAVIEW_CELL_INERT, 80, wxALIGN_LEFT,
		wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_COL_SORTABLE);

	// Add the seat data
	for (int i = 0; i < project->getSeatCount(); ++i) {
		addSeatToSeatData(project->getSeat(i));
	}

	updateInterface();
}

void SeatsFrame::addSeat(Seat seat) {
	// Simultaneously add to the party data control and to the polling project.
	project->addSeat(seat);

	refreshData();
}

void SeatsFrame::addSeatToSeatData(Seat seat) {
	// Create a vector with all the party data.
	wxVector<wxVariant> data;
	data.push_back(wxVariant(seat.name));
	data.push_back(wxVariant(seat.incumbent->name));
	data.push_back(wxVariant(seat.challenger->name));
	data.push_back(wxVariant(seat.region->name));
	data.push_back(wxVariant(formatFloat(seat.margin, 2)));
	data.push_back(wxVariant(formatFloat(seat.localModifier, 2)));
	data.push_back(wxVariant(formatFloat(seat.incumbentOdds, 3)));
	data.push_back(wxVariant(formatFloat(seat.challengerOdds, 3)));
	data.push_back(wxVariant(formatFloat(seat.incumbentWinPercent, 2)));
	data.push_back(wxVariant(formatFloat(seat.tippingPointPercent, 2)));
	data.push_back(wxVariant(formatFloat(seat.simulatedMarginAverage, 2)));

	seatData->AppendItem(data);
}

void SeatsFrame::replaceSeat(Seat seat) {
	int seatIndex = seatData->GetSelectedRow();
	// Simultaneously replace data in the seat data control and the polling project.
	project->replaceSeat(seatIndex, seat);

	refreshData();
}

void SeatsFrame::removeSeat() {
	// Simultaneously add to the seat data control and to the polling project.
	project->removeSeat(seatData->GetSelectedRow());

	refreshData();
}

void SeatsFrame::showSeatResults()
{
	auto results = project->getSeat(seatData->GetSelectedRow()).latestResults;
	if (!results) {
		wxMessageBox("No previous election results for this seat!\n");
		return;
	}
	std::string summaryString = results->name + "\n";
	summaryString += "Official ID: " + std::to_string(results->officialId) + "\n";
	summaryString += "Enrolment: " + std::to_string(results->enrolment) + "\n";
	summaryString += "Ordinary Votes: " + std::to_string(results->ordinaryVotes()) +
		" (" + formatFloat(results->ordinaryVotePercent(), 2) + "%)\n";
	summaryString += "Absent Votes: " + std::to_string(results->absentVotes()) +
		" (" + formatFloat(results->absentVotePercent(), 2) + "%)\n";
	summaryString += "Provisional Votes: " + std::to_string(results->provisionalVotes()) +
		" (" + formatFloat(results->provisionalVotePercent(), 2) + "%)\n";
	summaryString += "Prepoll Votes: " + std::to_string(results->prepollVotes()) +
		" (" + formatFloat(results->prepollVotePercent(), 2) + "%)\n";
	summaryString += "Postal Votes: " + std::to_string(results->postalVotes()) +
		" (" + formatFloat(results->postalVotePercent(), 2) + "%)\n";
	wxMessageBox(summaryString);
	std::string tcpString = "";
	logger << results->leadingCandidate().affiliationId << "\n";
	logger << results->trailingCandidate().affiliationId << "\n";
	logger << results->leadingCandidate().candidateId << "\n";
	logger << results->trailingCandidate().candidateId << "\n";
	tcpString += project->getCandidateById(results->leadingCandidate().candidateId)->name + " vs. " + 
		project->getCandidateById(results->trailingCandidate().candidateId)->name + "\n";
	tcpString += "Total votes: " + std::to_string(results->leadingCandidate().totalVotes()) + " (" +
		formatFloat(float(results->leadingCandidate().totalVotes()) / float(results->total2cpVotes()) * 100.0f, 2) + "%), " +
		std::to_string(results->trailingCandidate().totalVotes()) + " (" +
		formatFloat(float(results->trailingCandidate().totalVotes()) / float(results->total2cpVotes()) * 100.0f, 2) + "%)\n---\n";
	tcpString += "Ordinary votes: " + std::to_string(results->leadingCandidate().ordinaryVotes) + " (" +
		formatFloat(float(results->leadingCandidate().ordinaryVotes) / float(results->ordinaryVotes()) * 100.0f, 2) + "%), " +
		std::to_string(results->trailingCandidate().ordinaryVotes) + " (" +
		formatFloat(float(results->trailingCandidate().ordinaryVotes) / float(results->ordinaryVotes()) * 100.0f, 2) + "%)\n";
	tcpString += "Declaration votes: " + std::to_string(results->leadingCandidate().declarationVotes()) + " (" +
		formatFloat(float(results->leadingCandidate().declarationVotes()) / float(results->declarationVotes()) * 100.0f, 2) + "%), " +
		std::to_string(results->trailingCandidate().declarationVotes()) + " (" +
		formatFloat(float(results->trailingCandidate().declarationVotes()) / float(results->declarationVotes()) * 100.0f, 2) +
		"%)\n---\nBreakdown of declaration votes:\n";
	tcpString += "Absent votes: " + std::to_string(results->leadingCandidate().absentVotes) + " (" +
		formatFloat(float(results->leadingCandidate().absentVotes) / float(results->absentVotes()) * 100.0f, 2) + "%), " +
		std::to_string(results->trailingCandidate().absentVotes) + " (" +
		formatFloat(float(results->trailingCandidate().absentVotes) / float(results->absentVotes()) * 100.0f, 2) + "%)\n";
	tcpString += "Provisional votes: " + std::to_string(results->leadingCandidate().provisionalVotes) + " (" +
		formatFloat(float(results->leadingCandidate().provisionalVotes) / float(results->provisionalVotes()) * 100.0f, 2) + "%), " +
		std::to_string(results->trailingCandidate().provisionalVotes) + " (" +
		formatFloat(float(results->trailingCandidate().provisionalVotes) / float(results->provisionalVotes()) * 100.0f, 2) + "%)\n";
	tcpString += "Prepoll votes: " + std::to_string(results->leadingCandidate().prepollVotes) + " (" +
		formatFloat(float(results->leadingCandidate().prepollVotes) / float(results->prepollVotes()) * 100.0f, 2) + "%), " +
		std::to_string(results->trailingCandidate().prepollVotes) + " (" +
		formatFloat(float(results->trailingCandidate().prepollVotes) / float(results->prepollVotes()) * 100.0f, 2) + "%)\n";
	tcpString += "Postal votes: " + std::to_string(results->leadingCandidate().postalVotes) + " (" +
		formatFloat(float(results->leadingCandidate().postalVotes) / float(results->postalVotes()) * 100.0f, 2) + "%), " +
		std::to_string(results->trailingCandidate().postalVotes) + " (" +
		formatFloat(float(results->trailingCandidate().postalVotes) / float(results->postalVotes()) * 100.0f, 2) + "%)\n";
	wxMessageBox(tcpString);
	std::string boothString = "Booth results: ";
	boothString += project->getCandidateById(results->leadingCandidate().candidateId)->name + " vs. " + 
		project->getCandidateById(results->trailingCandidate().candidateId)->name + "\n";
	int numBooths = 0;
	for (int boothId : results->booths) {
		auto boothResults = project->getBooth(boothId);
		boothString += boothResults.name + ": " + std::to_string(boothResults.tcpVote[0]) +
			" (" + formatFloat(boothResults.percentVote(0), 2) + "%) vs. " +
			std::to_string(boothResults.tcpVote[1]) +
			" (" + formatFloat(boothResults.percentVote(1), 2) + "%)\n";
		++numBooths;
		if (numBooths >= 20) {
			wxMessageBox(boothString);
			numBooths = 0;
			boothString = "Booth results: ";
			boothString += project->getCandidateById(results->leadingCandidate().candidateId)->name + " vs. " +
				project->getCandidateById(results->trailingCandidate().candidateId)->name + "\n";
		}
	}
	if (numBooths) {
		wxMessageBox(boothString);
		numBooths = 0;
		boothString = "Booth results: ";
		boothString += project->getCandidateById(results->leadingCandidate().candidateId)->name + " vs. " +
			project->getCandidateById(results->trailingCandidate().candidateId)->name + "\n";
	}
}

void SeatsFrame::updateInterface() {
	bool somethingSelected = (seatData->GetSelectedRow() != -1);
	toolBar->EnableTool(PA_SeatsFrame_EditSeatID, somethingSelected);
	toolBar->EnableTool(PA_SeatsFrame_RemoveSeatID, somethingSelected);
}