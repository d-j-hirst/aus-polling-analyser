#include "DownloadFrame.h"

#include "PreviousElectionDataRetriever.h"
#include "ResultsDownloader.h"

// IDs for the controls and the menu commands
enum {
	PA_DownloadFrame_Base = 800, // To avoid mixing events with other frames.
	PA_DownloadFrame_FrameID,
	PA_DownloadFrame_DownloadHistoricBoothDataID,
};

DownloadFrame::DownloadFrame(ProjectFrame* const parent, PollingProject* project)
	: GenericChildFrame(parent, PA_DownloadFrame_FrameID, "Downloads", wxPoint(0, 0), project),
		parent(parent)
{
	// *** Toolbar *** //

	// Load the relevant bitmaps for the toolbar icons.
	wxLogNull something;

	refreshToolbar();

	// *** Simulation Data Table *** //

	// int toolBarHeight = toolBar->GetSize().GetHeight();

	// *** Binding Events *** //

	// Need to resize controls if this frame is resized.
	Bind(wxEVT_SIZE, &DownloadFrame::OnResize, this, PA_DownloadFrame_FrameID);

	// Need to record it if this frame is closed.
	//Bind(wxEVT_CLOSE_WINDOW, &SimulationsFrame::OnClose, this, PA_PartiesFrame_FrameID);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &DownloadFrame::OnDownloadHistoricBoothData, this, PA_DownloadFrame_DownloadHistoricBoothDataID);

	// Need to update the interface if the selection changes
}

void DownloadFrame::OnResize(wxSizeEvent & WXUNUSED(event))
{
}

void DownloadFrame::OnDownloadHistoricBoothData(wxCommandEvent& WXUNUSED(event))
{
	// Commenting out this for now since we don't want to waste everyone's data
	// downloading and re-downloading the same data
	std::string url = "ftp://mediafeedarchive.aec.gov.au/17496/Detailed/Verbose/aec-mediafeed-Detailed-Verbose-17496-20140516155658.zip";
	//ResultsDownloader resultsDownloader;
	//resultsDownloader.loadZippedFile(url);
	wxMessageBox("Downloaded data from: " + url);
	PreviousElectionDataRetriever previousElevationDataRetriever;
	previousElevationDataRetriever.collectData();
}

void DownloadFrame::refreshToolbar()
{
	wxBitmap toolBarBitmaps[1];
	toolBarBitmaps[0] = wxBitmap("bitmaps\\add.png", wxBITMAP_TYPE_PNG);

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_TEXT | wxTB_NOICONS);

	toolBar->AddTool(PA_DownloadFrame_DownloadHistoricBoothDataID, "Download Historic Booth Data", toolBarBitmaps[0], wxNullBitmap, wxITEM_NORMAL, "Download Historic Booth Data");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void DownloadFrame::updateInterface()
{
	// Do nothing for now
}
