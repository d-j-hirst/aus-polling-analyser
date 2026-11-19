#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace LiveResultsInput {

// A malformed delivery can be rejected while later deliveries continue. A
// failure to read supporting configuration or write working files instead
// requires operator attention: retrying the next election count cannot fix it.
class ProcessingError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::filesystem::path cachedSupportFile(std::string source, std::string const& suffix = ".xml");

struct CurrentFile {
	std::filesystem::path path;
	std::optional<std::string> timestamp;
	// The watcher keeps the commission's observation time alongside the compact
	// code used to name reports. A manually selected archive has only its capture code.
	std::optional<std::string> sourceObservationTime = std::nullopt;
};

struct PreferenceCountCutoff {
	std::string time;
	std::string clockRegion;
};

// Identify the instant up to which a separate, dated preference count was
// available. An empty result leaves the caller to use its fixed-file or live clock.
std::optional<PreferenceCountCutoff> snapshotPreferenceCutoff(
	std::optional<CurrentFile> const& selected, std::string const& electionRegion);

struct PreloadFiles {
	std::filesystem::path results;
	std::filesystem::path pollingPlaces;
};

// WA exports have only per-record modification clocks. Prepared replay names
// supply the observation time in WA; fixed-name live files use their original
// file modification instant converted to WA (UTC+8, with no daylight saving).
std::string waObservationTime(CurrentFile const& file);

// A federal preload normally comes from its remote ZIP/cache. Offline replay
// may instead name a workspace-relative or absolute XML with local:<path>.
// Its companion replaces .xml with @polling_places.xml. Reading these files
// keeps operator-normalized identities distinct from an unmodified remote cache.
std::optional<PreloadFiles> localPreloadFiles(std::string_view source);

struct SaReplaySequence {
	std::filesystem::path targetPath;
	std::filesystem::path statePath;
	std::vector<CurrentFile> snapshots;
	std::size_t currentIndex = 0;

	std::size_t remaining() const {
		return snapshots.size() - currentIndex - 1;
	}
};

// Simulation settings store paths as UTF-8, while filesystem operations use
// each platform's native path representation.
std::filesystem::path pathFromUtf8(std::string_view path);
std::filesystem::path pathFromUtf8(std::u8string_view path);
std::string pathToUtf8(std::filesystem::path const& path);

// Returns the portable default setting, <HOME>/Downloads.
std::string defaultDirectory();

// Expands <HOME> for filesystem access and replaces an absolute current-user
// home prefix before a setting is persisted.
std::filesystem::path resolveDirectory(std::string_view setting);
std::string portableDirectory(std::string_view setting);

bool supportsDirectoryFeed(std::string const& regionCode);

// Replay streams only the jurisdiction's detailed results XML from a ZIP.
// Aggregate-only and upper-house VEC members cannot provide booth observations.
bool isSnapshotResultsXml(std::string_view memberName, std::string_view regionCode);

// Selects the current jurisdiction feed, preferring the greatest timestamp
// where the official filename contains one.
std::optional<CurrentFile> findCurrentFile(
	std::filesystem::path const& directory,
	std::string const& regionCode,
	std::string const& termCode);

// Loads the sequence selected by Replay-Sa2026LiveSnapshot.ps1. The state is
// checked against the installed feed so a stale selection cannot skip files.
SaReplaySequence loadSaReplaySequence(
	std::filesystem::path const& directory,
	std::string const& termCode,
	std::filesystem::path const& statePath);

// Installs the next archived feed and updates the PowerShell-compatible state.
// Throws when the sequence is exhausted or files can no longer be read.
CurrentFile advanceSaReplaySequence(SaReplaySequence& sequence);

}
