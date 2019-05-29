#include "DownloadFrame.h"

#include "LatestResultsDataRetriever.h"
#include "Log.h"
#include "PreloadDataRetriever.h"
#include "PreviousElectionDataRetriever.h"
#include "ResultsDownloader.h"

// IDs for the controls and the menu commands
enum {
	PA_DownloadFrame_Base = 800, // To avoid mixing events with other frames.
	PA_DownloadFrame_FrameID,
	PA_DownloadFrame_PresetID,
	PA_DownloadFrame_GetHistoricBoothDataID,
	PA_DownloadFrame_GetPreloadDataID,
	PA_DownloadFrame_GetCustomBoothDataID,
	PA_DownloadFrame_GetLatestBoothDataID,
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
	Bind(wxEVT_TOOL, &DownloadFrame::OnGetHistoricBoothData, this, PA_DownloadFrame_GetHistoricBoothDataID);
	Bind(wxEVT_TOOL, &DownloadFrame::OnGetPreloadData, this, PA_DownloadFrame_GetPreloadDataID);
	Bind(wxEVT_TOOL, &DownloadFrame::OnGetCustomBoothData, this, PA_DownloadFrame_GetCustomBoothDataID);
	Bind(wxEVT_TOOL, &DownloadFrame::OnGetLatestBoothData, this, PA_DownloadFrame_GetLatestBoothDataID);

	// Need to update the interface if the selection changes
}

void DownloadFrame::OnResize(wxSizeEvent & WXUNUSED(event))
{
}

void DownloadFrame::OnGetHistoricBoothData(wxCommandEvent& WXUNUSED(event))
{
	std::string defaultUrl;
	switch (presetComboBox->GetSelection()) {
	case Preset::Federal2010: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/13745/Detailed/Verbose/aec-mediafeed-Detailed-Verbose-13745-20080903130113.zip"; break;
	case Preset::Federal2013: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/15508/Detailed/Verbose/aec-mediafeed-Detailed-Verbose-15508-20101022115746.zip"; break;
	case Preset::Federal2016: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/17496/Detailed/Verbose/aec-mediafeed-Detailed-Verbose-17496-20140516155658.zip"; break;
	case Preset::Federal2019: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/20499/Detailed/Verbose/aec-mediafeed-Detailed-Verbose-20499-20170511174118.zip"; break;
	}

	std::string userUrl = wxGetTextFromUser("Enter a URL to download results from:", "Download Results", defaultUrl);
	if (userUrl.empty()) return;

	// Comment this out if we don't want to waste everyone's data
	// downloading and re-downloading the same data
	ResultsDownloader resultsDownloader;
	resultsDownloader.loadZippedFile(userUrl, PreviousElectionDataRetriever::UnzippedFileName);

	wxMessageBox("Downloaded historic data from: " + userUrl);
	PreviousElectionDataRetriever previousElevationDataRetriever;
	previousElevationDataRetriever.collectData();
	project->incorporatePreviousElectionResults(previousElevationDataRetriever);
}

void DownloadFrame::OnGetPreloadData(wxCommandEvent& WXUNUSED(event))
{
	std::string defaultUrl;
	switch (presetComboBox->GetSelection()) {
	case Preset::Federal2010: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/15508/Detailed/Preload/aec-mediafeed-Detailed-Preload-15508-20100817220132.zip"; break;
	case Preset::Federal2013: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/17496/Detailed/Preload/aec-mediafeed-Detailed-Preload-17496-20130903105057.zip"; break;
	case Preset::Federal2016: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/20499/Detailed/Preload/aec-mediafeed-Detailed-Preload-20499-20160629114751.zip"; break;
	case Preset::Federal2019: defaultUrl = "ftp://mediafeed.aec.gov.au/24310/Detailed/Preload/aec-mediafeed-Detailed-Preload-24310-20190517164959.zip"; break;
	}

	std::string userUrl = wxGetTextFromUser("Enter a URL to download results from:", "Download Results", defaultUrl);
	if (userUrl.empty()) return;

	// Commenting out this for now since we don't want to waste everyone's data
	// downloading and re-downloading the same data
	ResultsDownloader resultsDownloader;
	resultsDownloader.loadZippedFile(userUrl, PreloadDataRetriever::UnzippedCandidatesFileName, PreloadDataRetriever::CandidateMatch);
	wxMessageBox("Downloaded preload candidate data from: " + userUrl);
	resultsDownloader.loadZippedFile(userUrl, PreloadDataRetriever::UnzippedBoothsFileName, PreloadDataRetriever::BoothsMatch);
	wxMessageBox("Downloaded preload booth data from: " + userUrl);

	PreloadDataRetriever preloadDataRetriever;
	preloadDataRetriever.collectData();
	project->incorporatePreloadData(preloadDataRetriever);
}

void DownloadFrame::OnGetCustomBoothData(wxCommandEvent& WXUNUSED(event))
{
	// Commenting out this for now since we don't want to waste everyone's data
	// downloading and re-downloading the same data
	std::string defaultUrl = "ftp://mediafeedarchive.aec.gov.au/24310/Detailed/Light/aec-mediafeed-Detailed-Light-24310-20190515142406.zip";
	std::string userUrl = wxGetTextFromUser("Enter a URL to download results from:", "Download Results", defaultUrl);
	if (userUrl.empty()) return;
	ResultsDownloader resultsDownloader;
	resultsDownloader.loadZippedFile(userUrl, LatestResultsDataRetriever::UnzippedFileName);
	wxMessageBox("Downloaded latest data from: " + userUrl);
	LatestResultsDataRetriever latestResultsDataRetriever;
	latestResultsDataRetriever.collectData();
	project->incorporateLatestResults(latestResultsDataRetriever);
	parent->refreshResults();
	parent->refreshMap();
}

void DownloadFrame::OnGetLatestBoothData(wxCommandEvent& WXUNUSED(event))
{
	std::string directory;
	switch (presetComboBox->GetSelection()) {
	case Preset::Federal2010: directory = "ftp://mediafeedarchive.aec.gov.au/15508/Detailed/Light/"; break;
	case Preset::Federal2013: directory = "ftp://mediafeedarchive.aec.gov.au/17496/Detailed/Light/"; break;
	case Preset::Federal2016: directory = "ftp://mediafeedarchive.aec.gov.au/20499/Detailed/Light/"; break;
	case Preset::Federal2019: directory = "ftp://mediafeed.aec.gov.au/24310/Detailed/Light/"; break;
	}
	std::string userUrl = wxGetTextFromUser("Enter a URL to download results from:", "Download Results", directory);
	if (userUrl.empty()) return;
	ResultsDownloader resultsDownloader;
	std::string directoryListing;
	resultsDownloader.loadUrlToString(directory, directoryListing);
	std::string latestFileName = directoryListing.substr(directoryListing.rfind(" ") + 1);
	latestFileName = latestFileName.substr(0, latestFileName.length() - 1);
	std::string latestUrl = directory + latestFileName;
	resultsDownloader.loadZippedFile(latestUrl, LatestResultsDataRetriever::UnzippedFileName);
	wxMessageBox("Downloaded latest data from: " + latestUrl.substr(0, 50) + "\n" + latestUrl.substr(50));
	logger << "Downloaded latest data from: " << latestUrl << "\n";
	LatestResultsDataRetriever latestResultsDataRetriever;
	latestResultsDataRetriever.collectData();
	project->incorporateLatestResults(latestResultsDataRetriever);
	parent->refreshResults();
	parent->refreshMap();
}

void DownloadFrame::refreshToolbar()
{

	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_TEXT | wxTB_NOICONS);

	wxArrayString presetNames;
	presetNames.Add("2010 federal election");
	presetNames.Add("2013 federal election");
	presetNames.Add("2016 federal election");
	presetNames.Add("2019 federal election");

	auto presetText = new wxStaticText(toolBar, wxID_ANY, "Set URLs for:");
	presetComboBox = new wxComboBox(toolBar, PA_DownloadFrame_PresetID, presetNames[0], wxPoint(0, 0), wxSize(150, 22), presetNames);
	presetComboBox->SetSelection(0);

	toolBar->AddControl(presetText);
	toolBar->AddControl(presetComboBox);
	toolBar->AddSeparator();
	toolBar->AddTool(PA_DownloadFrame_GetHistoricBoothDataID, "Get Historic Booth Data", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Get Historic Booth Data");
	toolBar->AddTool(PA_DownloadFrame_GetPreloadDataID, "Get Preload Data", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Get Preload Data");
	toolBar->AddTool(PA_DownloadFrame_GetCustomBoothDataID, "Get Custom Booth Data", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Get Custom Booth Data");
	toolBar->AddTool(PA_DownloadFrame_GetLatestBoothDataID, "Get Latest Booth Data", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Get Latest Booth Data");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void DownloadFrame::updateInterface()
{
	// Do nothing for now
}
