#include "DownloadFrame.h"

#include "LatestResultsDataRetriever.h"
#include "Log.h"
#include "PreloadDataRetriever.h"
#include "PreviousElectionDataRetriever.h"
#include "ResultsDownloader.h"

// Make this true if we want to skip downloading data and use the local copies
// Important to use if doing repeated testing, so we aren't hogging bandwidth
constexpr bool SkipDownloads = false;

// IDs for the controls and the menu commands
enum ControlId {
	Base = 800, // To avoid mixing events with other frames.
	Frame,
	Preset,
	GetHistoricBoothData,
	GetPreloadData,
	GetCustomBoothData,
	GetLatestBoothData,
	DownloadCompleteData,
};

DownloadFrame::DownloadFrame(ProjectFrame::Refresher refresher, PollingProject* project)
	: GenericChildFrame(refresher.notebook(), ControlId::Frame, "Downloads", wxPoint(0, 0), project),
		refresher(refresher)
{
	setupToolbar();
	bindEventHandlers();
}

void DownloadFrame::setupToolbar()
{
	// Initialize the toolbar.
	toolBar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTB_TEXT | wxTB_NOICONS);

	wxArrayString presetNames;
	presetNames.Add("2010 federal election");
	presetNames.Add("2013 federal election");
	presetNames.Add("2016 federal election");
	presetNames.Add("2019 federal election");

	int presetSelection = Preset::Federal2019;
	auto presetText = new wxStaticText(toolBar, wxID_ANY, "Set URLs for:");
	presetComboBox = new wxComboBox(toolBar, ControlId::Preset, presetNames[presetSelection], wxPoint(0, 0), wxSize(150, 22), presetNames);
	presetComboBox->SetSelection(presetSelection);

	toolBar->AddControl(presetText);
	toolBar->AddControl(presetComboBox);
	toolBar->AddSeparator();
	toolBar->AddTool(ControlId::GetHistoricBoothData, "Get Historic Booth Data", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Get Historic Booth Data");
	toolBar->AddTool(ControlId::GetPreloadData, "Get Preload Data", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Get Preload Data");
	toolBar->AddTool(ControlId::GetCustomBoothData, "Get Custom Booth Data", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Get Custom Booth Data");
	toolBar->AddTool(ControlId::GetLatestBoothData, "Get Latest Booth Data", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Get Latest Booth Data");
	toolBar->AddTool(ControlId::DownloadCompleteData, "Download Complete Data", wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, "Download Complete Data");

	// Realize the toolbar, so that the tools display.
	toolBar->Realize();
}

void DownloadFrame::bindEventHandlers()
{
	Bind(wxEVT_SIZE, &DownloadFrame::OnResize, this, ControlId::Frame);

	// Binding events for the toolbar items.
	Bind(wxEVT_TOOL, &DownloadFrame::OnGetHistoricBoothData, this, ControlId::GetHistoricBoothData);
	Bind(wxEVT_TOOL, &DownloadFrame::OnGetPreloadData, this, ControlId::GetPreloadData);
	Bind(wxEVT_TOOL, &DownloadFrame::OnGetCustomBoothData, this, ControlId::GetCustomBoothData);
	Bind(wxEVT_TOOL, &DownloadFrame::OnGetLatestBoothData, this, ControlId::GetLatestBoothData);
	Bind(wxEVT_TOOL, &DownloadFrame::OnDownloadCompleteData, this, ControlId::DownloadCompleteData);
}

void DownloadFrame::OnResize(wxSizeEvent & WXUNUSED(event))
{
}

void DownloadFrame::OnGetHistoricBoothData(wxCommandEvent& WXUNUSED(event))
{
	collectHistoricBoothData(false);
}

void DownloadFrame::OnGetPreloadData(wxCommandEvent& WXUNUSED(event))
{
	collectPreloadData(false);
}

void DownloadFrame::OnGetCustomBoothData(wxCommandEvent& WXUNUSED(event))
{
	collectCustomBoothData(false);
}

void DownloadFrame::OnGetLatestBoothData(wxCommandEvent& WXUNUSED(event))
{
	collectLatestBoothData(false);
}

void DownloadFrame::OnDownloadCompleteData(wxCommandEvent & WXUNUSED(event))
{
	collectCompleteData(true);
}

void DownloadFrame::collectHistoricBoothData(bool skipPrompt)
{
	if (!SkipDownloads) {
		std::string defaultUrl;
		switch (presetComboBox->GetSelection()) {
		case Preset::Federal2010: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/13745/Detailed/Verbose/aec-mediafeed-Detailed-Verbose-13745-20080903130113.zip"; break;
		case Preset::Federal2013: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/15508/Detailed/Verbose/aec-mediafeed-Detailed-Verbose-15508-20101022115746.zip"; break;
		case Preset::Federal2016: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/17496/Detailed/Verbose/aec-mediafeed-Detailed-Verbose-17496-20140516155658.zip"; break;
		case Preset::Federal2019: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/20499/Detailed/Verbose/aec-mediafeed-Detailed-Verbose-20499-20170511174118.zip"; break;
		}

		std::string userUrl = (skipPrompt ? defaultUrl : wxGetTextFromUser("Enter a URL to download results from:", "Download Results", defaultUrl));
		if (userUrl.empty()) return;

		ResultsDownloader resultsDownloader;
		resultsDownloader.loadZippedFile(userUrl, PreviousElectionDataRetriever::UnzippedFileName);
		if (!skipPrompt) wxMessageBox("Downloaded historic data from: " + userUrl);
	}

	auto const& election = project->elections().add(PreviousElectionDataRetriever().collectData());
	logger << "Added election: " << election.name << "\n";
}

void DownloadFrame::collectPreloadData(bool skipPrompt)
{
	if (!SkipDownloads) {
		std::string defaultUrl;
		switch (presetComboBox->GetSelection()) {
		case Preset::Federal2010: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/15508/Detailed/Preload/aec-mediafeed-Detailed-Preload-15508-20100817220132.zip"; break;
		case Preset::Federal2013: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/17496/Detailed/Preload/aec-mediafeed-Detailed-Preload-17496-20130903105057.zip"; break;
		case Preset::Federal2016: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/20499/Detailed/Preload/aec-mediafeed-Detailed-Preload-20499-20160629114751.zip"; break;
		case Preset::Federal2019: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/24310/Detailed/Preload/aec-mediafeed-Detailed-Preload-24310-20190517164959.zip"; break;
		}

		std::string userUrl = (skipPrompt ? defaultUrl : wxGetTextFromUser("Enter a URL to download results from:", "Download Results", defaultUrl));
		if (userUrl.empty()) return;

		ResultsDownloader resultsDownloader;
		resultsDownloader.loadZippedFile(userUrl, PreloadDataRetriever::UnzippedCandidatesFileName, PreloadDataRetriever::CandidateMatch);
		if (!skipPrompt) wxMessageBox("Downloaded preload candidate data from: " + userUrl);
		resultsDownloader.loadZippedFile(userUrl, PreloadDataRetriever::UnzippedBoothsFileName, PreloadDataRetriever::BoothsMatch);
		if (!skipPrompt) wxMessageBox("Downloaded preload booth data from: " + userUrl);
	}

	PreloadDataRetriever preloadDataRetriever;
	preloadDataRetriever.collectData();
	project->results().incorporatePreloadData(preloadDataRetriever);
}

void DownloadFrame::collectCustomBoothData(bool skipPrompt)
{
	if (!SkipDownloads) {
		std::string defaultUrl = "ftp://mediafeedarchive.aec.gov.au/24310/Detailed/Light/aec-mediafeed-Detailed-Light-24310-20190624103135.zip";
		std::string userUrl = (skipPrompt ? defaultUrl : wxGetTextFromUser("Enter a URL to download results from:", "Download Results", defaultUrl));
		if (userUrl.empty()) return;
		ResultsDownloader resultsDownloader;
		resultsDownloader.loadZippedFile(userUrl, LatestResultsDataRetriever::UnzippedFileName);
		if (!skipPrompt) wxMessageBox("Downloaded latest data from: " + userUrl);
	}

	LatestResultsDataRetriever latestResultsDataRetriever;
	latestResultsDataRetriever.collectData();
	project->results().incorporateLatestResults(latestResultsDataRetriever);
	refresher.refreshResults();
	refresher.refreshMap();
}

void DownloadFrame::collectLatestBoothData(bool skipPrompt)
{
	if (!SkipDownloads) {
		std::string defaultUrl;
		switch (presetComboBox->GetSelection()) {
		case Preset::Federal2010: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/15508/Detailed/Light/"; break;
		case Preset::Federal2013: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/17496/Detailed/Light/"; break;
		case Preset::Federal2016: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/20499/Detailed/Light/"; break;
		case Preset::Federal2019: defaultUrl = "ftp://mediafeedarchive.aec.gov.au/24310/Detailed/Light/"; break;
		}
		std::string userUrl = (skipPrompt ? defaultUrl : wxGetTextFromUser("Enter a URL to download results from:", "Download Results", defaultUrl));
		if (userUrl.empty()) return;
		ResultsDownloader resultsDownloader;
		std::string directoryListing;
		resultsDownloader.loadUrlToString(defaultUrl, directoryListing);
		std::string latestFileName = directoryListing.substr(directoryListing.rfind(" ") + 1);
		latestFileName = latestFileName.substr(0, latestFileName.length() - 1);
		std::string latestUrl = defaultUrl + latestFileName;
		resultsDownloader.loadZippedFile(latestUrl, LatestResultsDataRetriever::UnzippedFileName);
		if (!skipPrompt) wxMessageBox("Downloaded latest data from: " + latestUrl.substr(0, 50) + "\n" + latestUrl.substr(50));
		logger << "Downloaded latest data from: " << latestUrl << "\n";
	}

	LatestResultsDataRetriever latestResultsDataRetriever;
	latestResultsDataRetriever.collectData();
	project->results().incorporateLatestResults(latestResultsDataRetriever);
	refresher.refreshResults();
	refresher.refreshMap();
}

void DownloadFrame::collectCompleteData(bool skipPrompt)
{
	collectHistoricBoothData(skipPrompt);
	collectPreloadData(skipPrompt);
	collectCustomBoothData(skipPrompt);
	wxMessageBox("Downloaded all default data for the selected election.");
}

void DownloadFrame::updateInterface()
{
	// Do nothing for now
}
