#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LiveResultsInput {

struct CurrentFile {
	std::filesystem::path path;
	std::optional<std::string> timestamp;
};

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
