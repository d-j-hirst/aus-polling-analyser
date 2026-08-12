#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace LiveResultsInput {

struct CurrentFile {
	std::filesystem::path path;
	std::optional<std::string> timestamp;
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

}
