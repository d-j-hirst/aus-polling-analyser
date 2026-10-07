#pragma once

#include "LiveResultsInput.h"

#include <filesystem>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class SaveFileOutput;
class SaveFileInput;

// Selection is project data; discovery and expansion are independent of the
// GUI and never load votes into the forecast or change the retained feeds.
namespace LiveSnapshotSequence {
// Invalid editor input and incomplete replay settings are expected feedback,
// rather than calculation failures. A separate exception type lets a debugger
// ignore these checks while still breaking on unexpected runtime errors.
class ConfigurationError : public std::exception {
public:
    explicit ConfigurationError(std::string message) : message(std::move(message)) {}
    char const* what() const noexcept override { return message.c_str(); }
private:
    std::string message;
};

struct Settings {
    std::string election;
    std::string folder; // Empty uses forecasts/<election>/live-snapshots/feeds.
    std::vector<std::string> codes;
};

void saveSettings(SaveFileOutput& output, Settings const& settings);
Settings loadSettings(SaveFileInput& input, int projectVersion);

std::filesystem::path directory(std::filesystem::path const& root, Settings const& settings);
std::string portableFolder(std::filesystem::path const& root, std::filesystem::path const& folder);
// A missing/unreadable collection is ordinary configuration feedback, not an
// exception. Keep the reason with the result so the GUI can explain recovery.
struct Discovery {
    std::vector<LiveResultsInput::CurrentFile> files;
    std::string error;
};
Discovery discover(std::filesystem::path const& folder, std::string const& election);
std::vector<std::string> parseCodes(std::string_view text);
std::string codeText(std::vector<std::string> const& codes);
std::vector<LiveResultsInput::CurrentFile> resolve(
    std::vector<LiveResultsInput::CurrentFile> const& available,
    std::vector<std::string> const& codes);
// Adds up to count entries after the final selected entry. The starting entry
// is excluded; fewer remaining entries are all added, and exhaustion is a no-op.
void appendNext(std::vector<LiveResultsInput::CurrentFile> const& available,
    std::vector<std::string>& codes, std::size_t count);
}
