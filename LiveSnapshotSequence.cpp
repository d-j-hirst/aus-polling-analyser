#include "LiveSnapshotSequence.h"

#include "Date.h"
#include "SaveIO.h"

#include <algorithm>
#include <map>
#include <regex>
#include <sstream>

namespace LiveSnapshotSequence {
namespace {
// Codes retain the archive's spelling in the editor. A full-year key prevents
// different filename conventions from changing chronological comparisons.
std::string timeKey(std::string const& code) {
    std::string key = code.size() == 12 ? "20" + code : code;
    if (key.size() != 14 || !Timestamp::parseCompactLocal(key))
        throw ConfigurationError("Invalid snapshot timestamp: " + code);
    return key;
}
}

void saveSettings(SaveFileOutput& output, Settings const& settings) {
    output << settings.election << settings.folder << settings.codes;
}

Settings loadSettings(SaveFileInput& input, int projectVersion) {
    // Projects saved before the sequence feature have no trailing selection.
    // Do not consume any bytes from their existing layout.
    Settings settings;
    if (projectVersion >= 67) input >> settings.election >> settings.folder >> settings.codes;
    return settings;
}

std::filesystem::path directory(std::filesystem::path const& root, Settings const& settings) {
    if (settings.folder.empty())
        return root / "forecasts" / settings.election / "live-snapshots" / "feeds";
    auto path = LiveResultsInput::resolveDirectory(settings.folder);
    return path.is_absolute() ? path : root / path;
}

std::string portableFolder(std::filesystem::path const& root, std::filesystem::path const& folder) {
    auto relative = folder.lexically_normal().lexically_relative(root.lexically_normal());
    if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
        return LiveResultsInput::pathToUtf8(relative);
    return LiveResultsInput::portableDirectory(LiveResultsInput::pathToUtf8(folder));
}

Discovery discover(std::filesystem::path const& folder,
    std::string const& election) {
    // Naming rules belong to the feed format, not a particular replay year.
    // Final exports and fixed-name working files are not timestamped captures.
    std::regex pattern;
    if (election.size() == 6 && election.substr(4) == "sa")
        pattern = std::regex("^el" + election.substr(0, 4) + "([0-9]{12})\\.xml$", std::regex::icase);
    else if (election.size() == 7 && election.substr(4) == "fed")
        pattern = std::regex("^aec-mediafeed-(?:results-)?detailed(?:-light)?-[0-9]+-([0-9]{14})\\.(?:zip|xml)$", std::regex::icase);
    else if (election.size() == 7 && election.substr(4) == "vic")
        pattern = std::regex("^state" + election.substr(0, 4) + "mediafilelitepplh_([0-9]{8})_([0-9]{6})\\.(?:zip|xml)$", std::regex::icase);
    else return {{}, "Snapshot sequences currently support SA, Federal and Victorian feeds."};

    std::error_code error;
    auto folderName = LiveResultsInput::pathToUtf8(folder);
    auto folderStatus = std::filesystem::status(folder, error);
    if (folderStatus.type() == std::filesystem::file_type::not_found)
        return {{}, "The snapshot folder does not exist:\n" + folderName +
            "\n\nCreate it and place the timestamped feeds there, or choose an existing folder with Browse."};
    if (error) return {{}, "Could not inspect the snapshot folder:\n" + folderName + "\n" + error.message()};
    if (!std::filesystem::is_directory(folderStatus))
        return {{}, "The snapshot path is not a folder:\n" + folderName};
    std::filesystem::directory_iterator entry(folder, error), end;
    if (error) return {{}, "Could not read snapshot folder:\n" + folderName + "\n" + error.message()};
    std::map<std::string, LiveResultsInput::CurrentFile> sorted;
    // Check each operation before using the iterator again: a removed entry or
    // an interrupted directory scan should become feedback, never a dereference
    // of an exhausted iterator or a throwing filesystem status query.
    for (; entry != end && !error; entry.increment(error)) {
        bool const regular = entry->is_regular_file(error);
        if (error) break;
        if (!regular) continue;
        auto name = LiveResultsInput::pathToUtf8(entry->path().filename());
        std::smatch match;
        if (!std::regex_match(name, match, pattern)) continue;
        std::string code = match[1].str();
        // VEC filenames separate the date and time with an underscore. The
        // editor uses the same continuous 14-digit code as federal captures.
        if (election.substr(4) == "vic") code += match[2].str();
        auto key = code.size() == 12 ? "20" + code : code;
        if (!Timestamp::parseCompactLocal(key))
            return {{}, "A feed filename has an invalid snapshot timestamp: " + name};
        if (key.substr(0, 4) != election.substr(0, 4)) continue;
        if (!sorted.emplace(key, LiveResultsInput::CurrentFile{entry->path(), code}).second)
            return {{}, "More than one feed has snapshot code " + code +
                ". Keep one source for each timestamp in this folder."};
    }
    if (error) return {{}, "Could not finish reading the snapshot folder:\n" + folderName + "\n" + error.message()};
    Discovery result;
    for (auto const& [key, file] : sorted) result.files.push_back(file);
    return result;
}

std::vector<std::string> parseCodes(std::string_view text) {
    std::istringstream input{std::string(text)};
    std::vector<std::string> codes;
    std::string code;
    while (input >> code) { timeKey(code); codes.push_back(code); }
    return codes;
}

std::string codeText(std::vector<std::string> const& codes) {
    std::string text;
    for (auto const& code : codes) { if (!text.empty()) text += '\n'; text += code; }
    return text;
}

std::vector<LiveResultsInput::CurrentFile> resolve(
    std::vector<LiveResultsInput::CurrentFile> const& available,
    std::vector<std::string> const& codes) {
    std::vector<LiveResultsInput::CurrentFile> result;
    std::string previous;
    for (auto const& code : codes) {
        auto key = timeKey(code);
        if (!previous.empty() && key <= previous)
            throw ConfigurationError("Snapshot codes must be in increasing order without duplicates: " + code);
        auto found = std::find_if(available.begin(), available.end(), [&](auto const& file) {
            return file.timestamp && timeKey(*file.timestamp) == key;
        });
        if (found == available.end()) throw ConfigurationError("Snapshot is not available: " + code);
        result.push_back(*found);
        previous = std::move(key);
    }
    return result;
}

void appendNext(std::vector<LiveResultsInput::CurrentFile> const& available,
    std::vector<std::string>& codes, std::size_t count) {
    if (codes.empty()) throw ConfigurationError("Enter a starting code, or add one from the available list first.");
    auto selected = resolve(available, codes);
    auto last = std::find_if(available.begin(), available.end(), [&](auto const& file) {
        return file.path == selected.back().path;
    });
    auto remaining = std::size_t(std::distance(last + 1, available.end()));
    // The shortcut means "up to this many": reaching the archive's end is a
    // normal selection operation, including when no later captures remain.
    count = std::min(count, remaining);
    for (std::size_t index = 1; index <= count; ++index) codes.push_back(*last[index].timestamp);
}
}
