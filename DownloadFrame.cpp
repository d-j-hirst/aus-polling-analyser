#include "DownloadFrame.h"

#include "latestResultsDataRetriever.h"
#include "PreloadDataRetriever.h"
#include "PreviousElectionDataRetriever.h"
#include "ResultsDownloader.h"

// IDs for the controls and the menu commands
enum {
	PA_DownloadFrame_Base = 800, // To avoid mixing events with other frames.
	PA_DownloadFrame_FrameID,
	PA_DownloadFrame_DownloadHistoricBoothDataID,
	PA_DownloadFrame_DownloadPreloadDataID,
	PA_DownloadFrame_DownloadLatestBoothDataID,
};

DownloadFrame::DownloadFrame(ProjectFrame* parent, PollingProject* project)
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
	Bind(wxEVT_TOOL, &DownloadFrame::OnDownloadPreloadData, this, PA_DownloadFrame_DownloadPreloadDataID);
	Bind(wxEVT_TOOL, &DownloadFrame::OnDownloadLatestBoothData, this, PA_DownloadFrame_DownloadLatestBoothDataID);

	// Need to update the interface if the selection changes
}

void DownloadFrame::OnResize(wxSizeEvent & WXUNUSED(event))
{
}

void DownloadFrame::OnDownloadHistoricBoothData(wxCommandEvent& WXUNUSED(event))
{

	// 2013 federal election results - use when simulating 2016 federal election
	// std::string defaultUrl = "ftp://mediafeedarchive.aec.gov.au/17496/Detailed/Verbose/aec-mediafeed-Detailed-Verbose-17496-20140516155658.zip";

	// 2010 federal election results - use when simulating 2013 federal election
	//std::string defaultUrl = "ftp://mediafeedarchive.aec.gov.au/15508/Detailed/Verbose/aec-mediafeed-Detailed-Verbose-15508-20101022115746.zip";

	// 2007 federal election results - use when simulating 2010 federal election
	std::string defaultUrl = "ftp://mediafeedarchive.aec.gov.au/13745/Detailed/Verbose/aec-mediafeed-Detailed-Verbose-13745-20080903130113.zip";

	std::string userUrl = wxGetTextFromUser("Enter a URL to download results from:", "Download Results", defaultUrl);

	// Comment this out if we don't want to waste everyone's data
	// downloading and re-downloading the same data
	ResultsDownloader resultsDownloader;
	//resultsDownloader.loadZippedFile(userUrl, PreviousElectionDataRetriever::UnzippedFileName);

	wxMessageBox("Downloaded historic data from: " + userUrl);
	PreviousElectionDataRetriever previousElevationDataRetriever;
	previousElevationDataRetriever.collectData();
	project->incorporatePreviousElectionResults(previousElevationDataRetriever);
}

void DownloadFrame::OnDownloadPreloadData(wxCommandEvent& WXUNUSED(event))
{
	// 2016 federal election preload
	// std::string defaultUrl = "ftp://mediafeedarchive.aec.gov.au/20499/Detailed/Preload/aec-mediafeed-Detailed-Preload-20499-20160629114751.zip";

	// 2013 federal election preload
	//std::string defaultUrl = "ftp://mediafeedarchive.aec.gov.au/17496/Detailed/Preload/aec-mediafeed-Detailed-Preload-17496-20130903105057.zip";

	// 2010 federal election preload
	std::string defaultUrl = "ftp://mediafeedarchive.aec.gov.au/15508/Detailed/Preload/aec-mediafeed-Detailed-Preload-15508-20100817220132.zip";

	std::string userUrl = wxGetTextFromUser("Enter a URL to download results from:", "Download Results", defaultUrl);

	// Commenting out this for now since we don't want to waste everyone's data
	// downloading and re-downloading the same data
	ResultsDownloader resultsDownloader;
	//resultsDownloader.loadZippedFile(userUrl, PreloadDataRetriever::UnzippedCandidatesFileName, PreloadDataRetriever::CandidateMatch);
	wxMessageBox("Downloaded preload candidate data from: " + userUrl);
	//resultsDownloader.loadZippedFile(userUrl, PreloadDataRetriever::UnzippedBoothsFileName, PreloadDataRetriever::BoothsMatch);
	wxMessageBox("Downloaded preload booth data from: " + userUrl);

	PreloadDataRetriever preloadDataRetriever;
	preloadDataRetriever.collectData();
	project->incorporatePreloadData(preloadDataRetriever);
}

void DownloadFrame::OnDownloadLatestBoothData(wxCommandEvent& WXUNUSED(event))
{
	// Commenting out this for now since we don't want to waste everyone's data
	// downloading and re-downloading the same data
	std::string defaultUrl = "ftp://mediafeedarchive.aec.gov.au/15508/Detailed/Light/aec-mediafeed-Detailed-Light-15508-20100821190032.zip";
	std::string userUrl = wxGetTextFromUser("Enter a URL to download results from:", "Download Results", defaultUrl);
	if (userUrl.empty()) return;
	ResultsDownloader resultsDownloader;
	resultsDownloader.loadZippedFile(userUrl, LatestResultsDataRetriever::UnzippedFileName);
	wxMessageBox("Downloaded latest data from: " + userUrl);
	LatestResultsDataRetriever latestResultsDataRetriever;
	latestResultsDataRetriever.collectData();
	project->incorporateLatestResults(latestResultsDataRetriever);
	parent->refreshResults();
}

void DownloadFrame::refreshToolbar()
{
	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_TEXT | wxTB_NOICONS);

	toolBar->AddTool(PA_DownloadFrame_DownloadHistoricBoothDataID, "Download Historic Booth Data", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Download Historic Booth Data");
	toolBar->AddTool(PA_DownloadFrame_DownloadPreloadDataID, "Download Preload Data", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Download Preload Data");
	toolBar->AddTool(PA_DownloadFrame_DownloadLatestBoothDataID, "Download Latest Booth Data", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Download Latest Booth Data");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void DownloadFrame::updateInterface()
{
	// Do nothing for now
}
