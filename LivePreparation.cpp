#include "LivePreparation.h"

#include "Date.h"
#include "LatestResultsDataRetriever.h"
#include "LiveResultsInput.h"
#include "LiveV2.h"
#include "PollingProject.h"
#include "ResultsDownloader.h"
#include "Simulation.h"
#include "SimulationRun.h"
#include "Utf16ToUtf8.h"

// This file owns automatic-live setup and file acquisition only:
// - validateAutomaticSetup checks configuration and durable support files;
// - parsePreviousResults/parsePreload build the two election structures;
// - downloadLatestResults/acquireCurrentResults select the current feed;
// - parseCurrentResults applies that feed before LiveV2 takes over.
// Jurisdiction-specific XML/JSON interpretation belongs to ElectionData.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

namespace {

std::optional<std::string> fixedWidthDigits(
	std::string_view text,
	size_t offset,
	size_t length)
{
	if (offset > text.size() || text.size() - offset < length) {
		return std::nullopt;
	}
	auto const value = text.substr(offset, length);
	if (!std::ranges::all_of(value, [](unsigned char character) {
		return std::isdigit(character);
	})) {
		return std::nullopt;
	}
	return std::string(value);
}
void requireLiveInputFile(std::filesystem::path const& path, std::vector<std::string>& missingFiles)
{
	std::error_code error;
	if (!std::filesystem::is_regular_file(path, error)) {
		missingFiles.push_back(LiveResultsInput::pathToUtf8(path));
		return;
	}

	auto const fileSize = std::filesystem::file_size(path, error);
	if (error || fileSize == 0) {
		missingFiles.push_back(LiveResultsInput::pathToUtf8(path));
	}
}

std::filesystem::path currentResultsDirectory(
	Simulation::Settings const& settings)
{
	std::string const configured = settings.currentResultsDirectory.empty() ?
		LiveResultsInput::defaultDirectory() : settings.currentResultsDirectory;
	auto const path = LiveResultsInput::resolveDirectory(configured);
	if (!path.is_absolute()) {
		throw LivePreparation::Exception(
			"The current results directory must be an absolute path: " +
			configured);
	}
	return path.lexically_normal();
}

std::string compactLocalTimestamp()
{
	std::string timestamp;
	for (unsigned char character : Timestamp::now().formatIsoLocal()) {
		if (std::isdigit(character)) timestamp.push_back(char(character));
	}
	return timestamp;
}

std::string cachedUrlFilename(std::string url, std::string const& suffix = ".xml")
{
	std::replace(url.begin(), url.end(), '/', '$');
	std::replace(url.begin(), url.end(), '.', '$');
	std::replace(url.begin(), url.end(), ':', '$');
	return "downloads/" + url + suffix;
}

std::string latestFilenameFromListing(std::string const& listing)
{
	auto const end = listing.find_last_not_of(" \t\r\n");
	if (end == std::string::npos) {
		throw LivePreparation::Exception("The current-results directory listing was empty.");
	}
	auto const lineStart = listing.find_last_of("\r\n", end);
	auto const tokenStart = listing.find_last_of(" \t", end);
	auto const start = tokenStart != std::string::npos &&
		(lineStart == std::string::npos || tokenStart > lineStart) ?
		tokenStart + 1 : (lineStart == std::string::npos ? 0 : lineStart + 1);
	return listing.substr(start, end - start + 1);
}

std::optional<std::string> timestampBeforeExtension(std::string const& filename)
{
	auto const dotOffset = filename.rfind('.');
	if (dotOffset == std::string::npos || dotOffset < 14) return std::nullopt;
	return fixedWidthDigits(filename, dotOffset - 14, 14);
}

constexpr int LiveArchiveDaysBeforeElection = 14;
constexpr int LiveArchiveDaysAfterElection = 42;

bool isLiveArchiveDate(Date date, Date electionDate)
{
	return date.isValid() && electionDate.isValid() &&
		date >= electionDate - LiveArchiveDaysBeforeElection &&
		date <= electionDate + LiveArchiveDaysAfterElection;
}

std::optional<std::string> ecsaCompactUpdateTimestamp(
	tinyxml2::XMLDocument const& document)
{
	auto const* root = document.FirstChildElement("HouseOfAssemblyDetail");
	auto const* updateElement = root ?
		root->FirstChildElement("last_updated") : nullptr;
	char const* updateText = updateElement ? updateElement->GetText() : nullptr;
	if (!updateText) return std::nullopt;

	std::string digits;
	for (unsigned char character : std::string(updateText)) {
		if (std::isdigit(character)) digits.push_back(char(character));
	}
	if (digits.size() != 14 ||
		!Timestamp::parseCompactLocal(digits).has_value()) {
		return std::nullopt;
	}
	return digits;
}

nlohmann::json loadLiveJson(std::string const& filename)
{
	std::ifstream file(filename);
	if (!file) {
		throw LivePreparation::Exception("Could not open required live input file: " + filename);
	}

	try {
		return nlohmann::json::parse(file);
	}
	catch (nlohmann::json::parse_error const& e) {
		throw LivePreparation::Exception("Could not parse required live input file " +
			filename + ": " + e.what());
	}
}

void loadLiveXml(tinyxml2::XMLDocument& document, std::string const& filename)
{
	if (document.LoadFile(filename.c_str()) != tinyxml2::XML_SUCCESS) {
		throw LivePreparation::Exception("Could not load required live input file " +
			filename + ": " + document.ErrorStr());
	}
}

}

LivePreparation::LivePreparation(PollingProject& project, Simulation& sim, SimulationRun& run)
	: project(project), sim(sim), run(run),
	previousElection(run.getTermCode()), currentElection(run.getTermCode())
{
}

void LivePreparation::validateAutomaticSetup(PollingProject const& project, Simulation const& sim)
{
	auto const& settings = sim.getSettings();
	auto const& projection = project.projections().view(settings.baseProjection);
	std::string const termCode = projection.getBaseModel(project.models()).getTermCode();
	if (termCode.size() < 5) {
		throw Exception("The base model has an invalid election code: " + termCode);
	}
	if (settings.prevTermCodes.empty()) {
		throw Exception("No previous election code has been configured.");
	}

	std::string const regionCode = termCode.substr(4);
	std::string const previousTermCode = settings.prevTermCodes.front();
	std::vector<std::string> missingFiles;
	if (regionCode == "vic") {
		requireLiveInputFile("downloads/" + termCode + "_candidates.xml", missingFiles);
		requireLiveInputFile("downloads/" + termCode + "_booths.xml", missingFiles);
		requireLiveInputFile("analysis/Booth Results/" + previousTermCode + ".json", missingFiles);
	}
	else if (regionCode == "nsw" || regionCode == "qld") {
		requireLiveInputFile("downloads/" + termCode + "_zeros.xml", missingFiles);
		requireLiveInputFile("analysis/Booth Results/" + previousTermCode + ".json", missingFiles);
	}
	else if (regionCode == "wa") {
		requireLiveInputFile("downloads/" + termCode + "_candidates_prev.xml", missingFiles);
		requireLiveInputFile("downloads/" + termCode + "_booths_prev.xml", missingFiles);
		requireLiveInputFile("downloads/" + termCode + "_results_prev.xml", missingFiles);
		requireLiveInputFile("downloads/" + termCode + "_candidates_current.xml", missingFiles);
		requireLiveInputFile("downloads/" + termCode + "_booths_current.xml", missingFiles);
	}
	else if (regionCode == "sa") {
		requireLiveInputFile("downloads/" + termCode + "_zeros.xml", missingFiles);
		requireLiveInputFile("analysis/Booth Results/" + previousTermCode + ".json", missingFiles);
	}
	else if (regionCode == "fed") {
		if (settings.previousResultsUrl.empty()) {
			throw Exception("No previous-results URL has been configured.");
		}
		if (settings.preloadUrl.empty()) {
			throw Exception("No preload URL has been configured.");
		}
		if (settings.currentRealUrl.empty() && settings.currentTestUrl.empty()) {
			throw Exception("No current-results URL has been configured.");
		}
	}
	else {
		throw Exception("Automatic live preparation is not supported for region code " + regionCode + ".");
	}

	auto const* activeCurrentSource = &settings.currentRealUrl;
	if (regionCode == "fed" && settings.currentRealUrl.empty()) {
		activeCurrentSource = &settings.currentTestUrl;
	}
	if (activeCurrentSource->starts_with("local:")) {
		requireLiveInputFile(
			"downloads/" + activeCurrentSource->substr(6), missingFiles);
	}

	if (!missingFiles.empty()) {
		std::string message = "The live simulation is not set up for " + termCode +
			". Missing required file";
		message += missingFiles.size() == 1 ? ":\n" : "s:\n";
		for (auto const& filename : missingFiles) {
			message += filename + "\n";
		}
		throw Exception(message);
	}

	if (regionCode != "fed" && settings.currentRealUrl.empty()) {
		auto const inputDirectory = currentResultsDirectory(settings);
		std::error_code directoryError;
		if (!std::filesystem::is_directory(inputDirectory, directoryError)) {
			throw Exception(
				"The configured current results directory could not be opened: " +
				LiveResultsInput::pathToUtf8(inputDirectory));
		}
		if (!LiveResultsInput::findCurrentFile(
			inputDirectory, regionCode, termCode)) {
			throw Exception("No current-results file for " + regionCode +
				" was found in the configured directory: " +
				LiveResultsInput::pathToUtf8(inputDirectory));
		}
	}
}

void LivePreparation::loadEcsaXmlDocument(tinyxml2::XMLDocument& document, std::string const& filename) const
{
	try {
		XmlEncoding::loadUtf16AwareXmlDocument(document, filename);
	}
	catch (std::runtime_error const& e) {
		throw Exception("Failed to load ECSA XML file: " + filename + " (" + e.what() + ")");
	}
}

void LivePreparation::prepareLiveAutomatic()
{
	downloadPreviousResults();
	parsePreviousResults();
	downloadPreload();
	parsePreload();
	downloadPollingPlaces();
	parsePollingPlaces();
	if (sim.settings.currentRealUrl.size()) {
		downloadLatestResults();
	}
	else {
		acquireCurrentResults();
	}
	parseCurrentResults();

	run.liveElection = std::make_unique<LiveV2::Election>(previousElection, currentElection, project, sim, run);

	for (int seatIndex = 0; seatIndex < project.seats().count(); ++seatIndex) {
		auto seat = project.seats().viewByIndex(seatIndex);
		if (run.liveElection->getSeatFpCompletion(seat.name) > 0) {
			project.outcomes().add(Outcome(
				seatIndex,
				run.liveElection->getSeatRawTppSwing(seat.name),
				run.liveElection->getSeatFpCompletion(seat.name) * 100.0f,
				run.liveElection->getSeatTcpCompletion(seat.name) * 100.0f,
				0,
				40
			));
		}
	}
}

void LivePreparation::downloadPreviousResults()
{
	if (run.regionCode != "fed") return;
	ResultsDownloader resultsDownloader;
	std::string mangledName = sim.settings.previousResultsUrl;
	if (mangledName.starts_with("local:")) {
		xmlFilename = mangledName.substr(6);
		return;
	}
	mangledName = cachedUrlFilename(mangledName);
	std::filesystem::path mangledPath(mangledName);
	if (std::filesystem::exists(mangledPath)) {
		logger << "Already found previous results file at: " << mangledName << "\n";
		xmlFilename = mangledName;
	}
	else {
		xmlFilename = resultsDownloader.loadZippedFile(sim.settings.previousResultsUrl, mangledName);
		logger << "Downloaded file: " << sim.settings.previousResultsUrl << "\n";
		logger << "and saved it as: " << mangledName << "\n";
	}
}

void LivePreparation::parsePreviousResults()
{
	std::string prevTermCode = sim.settings.prevTermCodes.at(0);
	if (run.regionCode == "fed") {
		loadLiveXml(xml, xmlFilename);
		previousElection = Results2::Election::createAec(xml, prevTermCode);
	}
	else if (run.regionCode == "vic") {
		tinyxml2::XMLDocument candidatesXml;
		loadLiveXml(candidatesXml, "downloads/" + getTermCode() + "_candidates.xml");
		tinyxml2::XMLDocument boothsXml;
		loadLiveXml(boothsXml, "downloads/" + getTermCode() + "_booths.xml");
		nlohmann::json resultsJson = loadLiveJson("analysis/Booth Results/" + prevTermCode + ".json");
		previousElection = Results2::Election::createVec(resultsJson, candidatesXml, boothsXml, prevTermCode);
	}
	else if (run.regionCode == "nsw") {
		tinyxml2::XMLDocument zerosXml;
		loadLiveXml(zerosXml, "downloads/" + getTermCode() + "_zeros.xml");
		nlohmann::json resultsJson = loadLiveJson("analysis/Booth Results/" + prevTermCode + ".json");
		previousElection = Results2::Election::createNswec(resultsJson, zerosXml, prevTermCode);
	}
	else if (run.regionCode == "qld") {
		tinyxml2::XMLDocument zerosXml;
		loadLiveXml(zerosXml, "downloads/" + getTermCode() + "_zeros.xml");
		nlohmann::json resultsJson = loadLiveJson("analysis/Booth Results/" + prevTermCode + ".json");
		previousElection = Results2::Election::createQec(resultsJson, zerosXml, prevTermCode);
	}
	else if (run.regionCode == "wa") {
		tinyxml2::XMLDocument candidatesXml;
		loadLiveXml(candidatesXml, "downloads/" + getTermCode() + "_candidates_prev.xml");
		tinyxml2::XMLDocument boothsXml;
		loadLiveXml(boothsXml, "downloads/" + getTermCode() + "_booths_prev.xml");
		previousElection = Results2::Election::createWaec(candidatesXml, boothsXml, prevTermCode);
		tinyxml2::XMLDocument resultsXml;
		loadLiveXml(resultsXml, "downloads/" + getTermCode() + "_results_prev.xml");
		previousElection.update(resultsXml, Results2::Election::Format::WAEC);
	}
	else if (run.regionCode == "sa") {
		tinyxml2::XMLDocument zerosXml;
		loadEcsaXmlDocument(zerosXml, "downloads/" + getTermCode() + "_zeros.xml");
		nlohmann::json resultsJson = loadLiveJson("analysis/Booth Results/" + prevTermCode + ".json");
		previousElection = Results2::Election::createEcsa(resultsJson, zerosXml, prevTermCode);
	}
}

void LivePreparation::downloadPreload()
{
	if (run.regionCode != "fed") return;
	ResultsDownloader resultsDownloader;
	std::string mangledName = cachedUrlFilename(sim.settings.preloadUrl);
	std::filesystem::path mangledPath(mangledName);
	xmlFilename = mangledName;
	if (std::filesystem::exists(mangledPath)) {
		logger << "Already found preload file at: " << mangledName << "\n";
	}
	else {
		xmlFilename = resultsDownloader.loadZippedFile(sim.settings.preloadUrl, mangledName);
		logger << "Downloaded file from: " << sim.settings.preloadUrl << "\n";
		logger << "and saved preload data as: " << mangledName << "\n";
	}
}

void LivePreparation::parsePreload()
{
	if (run.regionCode == "fed") {
		loadLiveXml(xml, xmlFilename);
		currentElection = Results2::Election::createAec(xml, run.getTermCode());
	}
	else if (run.regionCode == "vic") {
		tinyxml2::XMLDocument candidatesXml;
		loadLiveXml(candidatesXml, "downloads/" + getTermCode() + "_candidates.xml");
		tinyxml2::XMLDocument boothsXml;
		loadLiveXml(boothsXml, "downloads/" + getTermCode() + "_booths.xml");
		currentElection = Results2::Election::createVec(candidatesXml, boothsXml, run.getTermCode());
	}
	else if (run.regionCode == "nsw") {
		tinyxml2::XMLDocument zerosXml;
		loadLiveXml(zerosXml, "downloads/" + getTermCode() + "_zeros.xml");
		currentElection = Results2::Election::createNswec(nlohmann::json(), zerosXml, run.getTermCode());
	}
	else if (run.regionCode == "qld") {
		tinyxml2::XMLDocument zerosXml;
		loadLiveXml(zerosXml, "downloads/" + getTermCode() + "_zeros.xml");
		currentElection = Results2::Election::createQec(nlohmann::json(), zerosXml, run.getTermCode());
	}
	else if (run.regionCode == "wa") {
		tinyxml2::XMLDocument candidatesXml;
		loadLiveXml(candidatesXml, "downloads/" + getTermCode() + "_candidates_current.xml");
		tinyxml2::XMLDocument boothsXml;
		loadLiveXml(boothsXml, "downloads/" + getTermCode() + "_booths_current.xml");
		currentElection = Results2::Election::createWaec(candidatesXml, boothsXml, run.getTermCode());
	}
	else if (run.regionCode == "sa") {
		tinyxml2::XMLDocument zerosXml;
		loadEcsaXmlDocument(zerosXml, "downloads/" + getTermCode() + "_zeros.xml");
		currentElection = Results2::Election::createEcsa(nlohmann::json(), zerosXml, run.getTermCode());
	}
}

void LivePreparation::downloadPollingPlaces()
{
	// Polling places data contains location data that is used to estimate
	// votes for booths that don't have a previous-election match.
	if (run.regionCode != "fed") return;
	ResultsDownloader resultsDownloader;
	std::string mangledName = cachedUrlFilename(
		sim.settings.preloadUrl, "@polling_places.xml");
	std::filesystem::path mangledPath(mangledName);
	xmlFilename = mangledName;
	if (std::filesystem::exists(mangledPath)) {
		logger << "Already found preload file at: " << mangledName << "\n";
	}
	else {
		xmlFilename = resultsDownloader.loadZippedFile(sim.settings.preloadUrl, mangledName, true);
		logger << "Downloaded file from: " << sim.settings.preloadUrl << "\n";
		logger << "and saved polling places data as: " << mangledName << "\n";
	}
}

void LivePreparation::parsePollingPlaces()
{
	if (run.regionCode == "fed") {
		loadLiveXml(xml, xmlFilename);
		currentElection.updateAecPollingPlaces(xml);
	}
}

void LivePreparation::downloadLatestResults()
{
	if (sim.settings.currentRealUrl.starts_with("local:")) {
		xmlFilename = "downloads/" + sim.settings.currentRealUrl.substr(6);
		if (auto timestamp = timestampBeforeExtension(xmlFilename)) {
			sim.latestReport.dateCode = *timestamp;
		}
		return;
	}
	ResultsDownloader resultsDownloader;
	std::string directoryListing;
	resultsDownloader.loadUrlToString(sim.settings.currentRealUrl, directoryListing);
	std::string const latestFileName = latestFilenameFromListing(directoryListing);
	std::string latestUrl = sim.settings.currentRealUrl + latestFileName;
	xmlFilename = resultsDownloader.loadZippedFile(latestUrl, LatestResultsDataRetriever::UnzippedFileName);
	logger << "Downloaded file: " << latestUrl << "\n";
	logger << "and saved it as: " << xmlFilename << "\n";

	auto const timestamp = timestampBeforeExtension(latestUrl);
	if (!timestamp) {
		throw Exception("The latest current-results filename does not contain the expected 14-digit timestamp: " + latestFileName);
	}
	sim.latestReport.dateCode = *timestamp;
}

void LivePreparation::acquireCurrentResults()
{
	ResultsDownloader resultsDownloader;
	if (run.regionCode == "fed") {
		if (sim.settings.currentTestUrl.starts_with("local:")) {
			xmlFilename = "downloads/" + sim.settings.currentTestUrl.substr(6);
			if (auto timestamp = timestampBeforeExtension(xmlFilename)) {
				sim.latestReport.dateCode = *timestamp;
			}
			return;
		}

		std::string mangledName = cachedUrlFilename(sim.settings.currentTestUrl);
		std::filesystem::path mangledPath(mangledName);
		if (std::filesystem::exists(mangledPath)) {
			logger << "Already found current test file at: " << mangledName << "\n";
		}
		else {
			resultsDownloader.loadZippedFile(sim.settings.currentTestUrl, mangledName);
			logger << "Downloaded file: " << sim.settings.currentTestUrl << "\n";
			logger << "and saved it as: " << mangledName << "\n";
		}
		xmlFilename = mangledName;

		auto const timestamp = timestampBeforeExtension(sim.settings.currentTestUrl);
		if (!timestamp) {
			throw Exception("The current test-results filename does not contain the expected 14-digit timestamp.");
		}
		sim.latestReport.dateCode = *timestamp;
	}
	else {
		auto const inputDirectory = currentResultsDirectory(sim.settings);
		std::error_code directoryError;
		if (!std::filesystem::is_directory(inputDirectory, directoryError)) {
			throw Exception(
				"The configured current results directory could not be opened: " +
				LiveResultsInput::pathToUtf8(inputDirectory));
		}
		auto const selected = LiveResultsInput::findCurrentFile(
			inputDirectory, run.regionCode, run.getTermCode());
		if (!selected) {
			throw Exception("No current-results file for " + run.regionCode +
				" was found in the configured directory: " +
				LiveResultsInput::pathToUtf8(inputDirectory));
		}
		auto const& selectedPath = selected->path;

		xmlFilename = "downloads/" + run.getTermCode() + "_latest.xml";
		if (run.regionCode == "sa") {
			std::filesystem::copy_file(selectedPath, xmlFilename,
				std::filesystem::copy_options::overwrite_existing);
			// Replay snapshots are always usable, but only genuine live-period
			// feeds are archived as at-the-time captures.
			auto const electionDate = project.projections().view(
				sim.settings.baseProjection).getSettings().endDate;
			tinyxml2::XMLDocument archiveXml;
			loadEcsaXmlDocument(archiveXml, xmlFilename);
			auto const updateTimestamp = ecsaCompactUpdateTimestamp(archiveXml);
			bool const archiveCurrent = isLiveArchiveDate(
				Date::todayLocal(), electionDate) && updateTimestamp &&
				isLiveArchiveDate(
					Timestamp::parseCompactLocal(*updateTimestamp)->localDate(),
					electionDate);
			if (archiveCurrent) {
				auto const archivedPath = inputDirectory /
					("el" + run.getTermCode().substr(0, 4) +
						updateTimestamp->substr(2) + ".xml");
				if (!std::filesystem::exists(archivedPath)) {
					std::filesystem::copy_file(selectedPath, archivedPath);
					logger << "Archived SA results file as: " <<
						LiveResultsInput::pathToUtf8(archivedPath) << "\n";
				}
			}
		}
		else if (run.regionCode == "wa") {
			auto const electionDate = project.projections().view(
				sim.settings.baseProjection).getSettings().endDate;
			if (isLiveArchiveDate(Date::todayLocal(), electionDate)) {
				auto archiveName = selectedPath.filename().native();
				auto const marker =
					std::filesystem::path("LA VERBOSE RESULTS").native();
				auto const markerOffset = archiveName.find(marker);
				auto const timestamp = compactLocalTimestamp();
				if (markerOffset != std::string::npos && timestamp.size() == 14) {
					archiveName.replace(markerOffset, marker.size(),
						std::filesystem::path(timestamp.substr(2)).native());
					auto const archivedPath = selectedPath.parent_path() /
						std::filesystem::path(archiveName);
					if (std::filesystem::copy_file(selectedPath, archivedPath,
						std::filesystem::copy_options::skip_existing)) {
						logger << "Archived WA results file as: " <<
							LiveResultsInput::pathToUtf8(archivedPath) << "\n";
					}
				}
			}
			std::filesystem::copy_file(selectedPath, xmlFilename,
				std::filesystem::copy_options::overwrite_existing);
		}
		else {
			// The existing extractor invokes PowerShell. Stage the selected ZIP
			// under an ASCII repository path so custom paths with spaces or
			// Unicode characters remain safe.
			std::filesystem::path const stagedArchive =
				"downloads/TempCurrentResults.zip";
			std::filesystem::copy_file(selectedPath, stagedArchive,
				std::filesystem::copy_options::overwrite_existing);
			try {
				resultsDownloader.unzipFile(
					stagedArchive.string(), xmlFilename);
			}
			catch (...) {
				std::error_code removeError;
				std::filesystem::remove(stagedArchive, removeError);
				throw;
			}
			std::error_code removeError;
			std::filesystem::remove(stagedArchive, removeError);
		}
		logger << "Prepared current results from: " <<
			LiveResultsInput::pathToUtf8(selectedPath) <<
			"\nSaved current results as: " << xmlFilename << "\n";
	}
}

void LivePreparation::parseCurrentResults()
{
	if (run.regionCode == "sa") {
		// ECSA's XML files are UTF-16 encoded, so we need to use a special function to load them
		loadEcsaXmlDocument(xml, xmlFilename);
	}
	else {
		loadLiveXml(xml, xmlFilename);
	}
	Results2::Election::Format format;
	if (run.regionCode == "fed") format = Results2::Election::Format::AEC;
	else if (run.regionCode == "vic") format = Results2::Election::Format::VEC;
	else if (run.regionCode == "nsw") format = Results2::Election::Format::NSWEC;
	else if (run.regionCode == "qld") format = Results2::Election::Format::QEC;
	else if (run.regionCode == "wa") format = Results2::Election::Format::WAEC;
	else if (run.regionCode == "sa") format = Results2::Election::Format::ECSA;
	else format = Results2::Election::Format::AEC;
	currentElection.update(xml, format);
}

std::string LivePreparation::getTermCode() const
{
	return run.yearCode + run.regionCode;
}
