#include "LiveResultsInput.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace LiveResultsInput {
namespace {

constexpr std::string_view HomeTag = "<HOME>";

bool filenameContains(
	std::filesystem::path const& path,
	std::string const& marker)
{
	auto const nativeMarker = std::filesystem::path(marker).native();
	auto const nativeFilename = path.filename().native();
	return nativeFilename.find(nativeMarker) !=
		decltype(nativeFilename)::npos;
}

bool filenameEquals(
	std::filesystem::path const& path,
	std::string const& expected)
{
	return path.filename().native() ==
		std::filesystem::path(expected).native();
}

std::optional<std::string> fixedWidthNativeDigits(
	std::filesystem::path::string_type const& text,
	size_t offset,
	size_t length)
{
	if (offset > text.size() || text.size() - offset < length) {
		return std::nullopt;
	}
	std::string value;
	value.reserve(length);
	for (size_t index = offset; index < offset + length; ++index) {
		auto const character = text[index];
		if (character < std::filesystem::path::value_type('0') ||
			character > std::filesystem::path::value_type('9')) {
			return std::nullopt;
		}
		value.push_back(char(character));
	}
	return value;
}

std::optional<std::string> stateFeedTimestamp(
	std::filesystem::path const& path,
	std::string const& regionCode)
{
	if (regionCode == "qld") {
		if (!filenameContains(path, "_publicResults")) return std::nullopt;
		return fixedWidthNativeDigits(path.filename().native(), 0, 14);
	}
	if (regionCode == "vic") {
		auto const marker = std::filesystem::path("mediafilelitepplh_").native();
		if (!filenameContains(path, "mediafilelitepplh_")) return std::nullopt;
		auto const filename = path.filename().native();
		auto const markerOffset = filename.find(marker);
		auto const date = fixedWidthNativeDigits(
			filename, markerOffset + marker.size(), 8);
		auto const time = fixedWidthNativeDigits(
			filename, markerOffset + marker.size() + 9, 6);
		if (!date || !time ||
			filename[markerOffset + marker.size() + 8] !=
				std::filesystem::path::value_type('_')) {
			return std::nullopt;
		}
		return *date + *time;
	}
	if (regionCode == "nsw") {
		auto const marker = std::filesystem::path("-SG").native();
		if (!filenameContains(path, "-SG")) return std::nullopt;
		auto const filename = path.filename().native();
		auto const markerOffset = filename.find(marker);
		auto const time = fixedWidthNativeDigits(filename, 0, markerOffset);
		auto const date = fixedWidthNativeDigits(
			filename, markerOffset + marker.size(), 8);
		if (!date || !time || time->empty()) return std::nullopt;
		return *date + *time;
	}
	return std::nullopt;
}

bool isCurrentResultsCandidate(
	std::filesystem::path const& path,
	std::string const& regionCode,
	std::string const& termCode)
{
	std::error_code error;
	if (!std::filesystem::is_regular_file(path, error)) return false;
	if (regionCode == "sa") {
		return filenameEquals(path,
			"el" + termCode.substr(0, 4) + "_ha_detail.xml");
	}
	if (regionCode == "wa") {
		return filenameContains(path, "LA VERBOSE RESULTS");
	}
	return stateFeedTimestamp(path, regionCode).has_value();
}

std::filesystem::path absolutePath(std::filesystem::path path)
{
	std::error_code error;
	auto absolute = std::filesystem::absolute(path, error);
	return error ? path.lexically_normal() : absolute.lexically_normal();
}

std::filesystem::path homeDirectory()
{
#ifdef _WIN32
	auto environmentValue = [](wchar_t const* name) {
		wchar_t* value = nullptr;
		size_t length = 0;
		std::wstring result;
		if (_wdupenv_s(&value, &length, name) == 0 && value) {
			result.assign(value);
		}
		std::free(value);
		return result;
	};
	if (auto profile = environmentValue(L"USERPROFILE"); !profile.empty()) {
		return std::filesystem::path(std::move(profile));
	}
	auto drive = environmentValue(L"HOMEDRIVE");
	auto homePath = environmentValue(L"HOMEPATH");
	if (!drive.empty() && !homePath.empty()) {
		return std::filesystem::path(drive + homePath);
	}
#else
	if (auto const* home = std::getenv("HOME")) {
		return std::filesystem::path(home);
	}
#endif
	return std::filesystem::current_path();
}

bool isWithinHome(std::filesystem::path const& relative)
{
	if (relative.empty() || relative.is_absolute()) return false;
	return *relative.begin() != "..";
}

std::string genericPathToUtf8(std::filesystem::path const& path)
{
	auto const utf8Path = path.generic_u8string();
	return std::string(
		reinterpret_cast<char const*>(utf8Path.data()), utf8Path.size());
}

}

std::filesystem::path pathFromUtf8(std::string_view path)
{
	std::u8string utf8Path;
	utf8Path.reserve(path.size());
	for (unsigned char character : path) {
		utf8Path.push_back(static_cast<char8_t>(character));
	}
	return pathFromUtf8(utf8Path);
}

std::filesystem::path pathFromUtf8(std::u8string_view path)
{
	return std::filesystem::path(path);
}

std::string pathToUtf8(std::filesystem::path const& path)
{
	auto const utf8Path = path.u8string();
	return std::string(
		reinterpret_cast<char const*>(utf8Path.data()), utf8Path.size());
}

std::string defaultDirectory()
{
	return std::string(HomeTag) + "/Downloads";
}

std::filesystem::path resolveDirectory(std::string_view setting)
{
	std::string fallback;
	if (setting.empty()) {
		fallback = defaultDirectory();
		setting = fallback;
	}
	if (setting == HomeTag) return absolutePath(homeDirectory());
	if (setting.starts_with(HomeTag) && setting.size() > HomeTag.size() &&
		(setting[HomeTag.size()] == '/' || setting[HomeTag.size()] == '\\')) {
		return absolutePath(homeDirectory() /
			pathFromUtf8(setting.substr(HomeTag.size() + 1)));
	}
	return pathFromUtf8(setting).lexically_normal();
}

std::string portableDirectory(std::string_view setting)
{
	if (setting.empty()) return defaultDirectory();
	if (setting == HomeTag ||
		(setting.starts_with(HomeTag) && setting.size() > HomeTag.size() &&
			(setting[HomeTag.size()] == '/' ||
				setting[HomeTag.size()] == '\\'))) {
		return std::string(setting);
	}

	auto const path = pathFromUtf8(setting);
	if (!path.is_absolute()) return std::string(setting);
	auto const relative = path.lexically_normal().lexically_relative(
		absolutePath(homeDirectory()));
	if (!isWithinHome(relative)) return std::string(setting);
	if (relative == ".") return std::string(HomeTag);
	return std::string(HomeTag) + "/" + genericPathToUtf8(relative);
}

bool supportsDirectoryFeed(std::string const& regionCode)
{
	return regionCode == "vic" || regionCode == "nsw" ||
		regionCode == "qld" || regionCode == "wa" || regionCode == "sa";
}

std::optional<CurrentFile> findCurrentFile(
	std::filesystem::path const& directory,
	std::string const& regionCode,
	std::string const& termCode)
{
	if (!supportsDirectoryFeed(regionCode)) return std::nullopt;

	std::error_code error;
	std::filesystem::directory_iterator entry(directory, error);
	std::filesystem::directory_iterator const end;
	std::optional<CurrentFile> selected;
	while (!error && entry != end) {
		if (isCurrentResultsCandidate(entry->path(), regionCode, termCode)) {
			auto timestamp = stateFeedTimestamp(entry->path(), regionCode);
			if (!selected ||
				(timestamp && (!selected->timestamp ||
					*timestamp > *selected->timestamp))) {
				selected = CurrentFile{ entry->path(), std::move(timestamp) };
			}
		}
		entry.increment(error);
	}
	return selected;
}

}
