#include "../LiveResultsInput.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void touch(std::filesystem::path const& path)
{
	std::ofstream file(path, std::ios::binary);
	file << "test";
}

}

int main()
{
	assert(LiveResultsInput::defaultDirectory() == "<HOME>/Downloads");
	auto const defaultDirectory = LiveResultsInput::resolveDirectory(
		LiveResultsInput::defaultDirectory());
	assert(defaultDirectory.is_absolute());
	assert(defaultDirectory.filename() == "Downloads");

	auto const home = LiveResultsInput::resolveDirectory("<HOME>");
	auto const privateDirectory = home / "LiveTests" / "SA2026";
	assert(LiveResultsInput::portableDirectory(
		LiveResultsInput::pathToUtf8(privateDirectory)) ==
		"<HOME>/LiveTests/SA2026");
	assert(LiveResultsInput::resolveDirectory(
		"<HOME>/LiveTests/SA2026") == privateDirectory);

	auto const unicodePath = std::filesystem::temp_directory_path() /
		LiveResultsInput::pathFromUtf8(u8"polling-live-é測試");
	assert(LiveResultsInput::pathFromUtf8(
		LiveResultsInput::pathToUtf8(unicodePath)) == unicodePath);

	std::error_code error;
	std::filesystem::remove_all(unicodePath, error);
	std::filesystem::create_directories(unicodePath);
	touch(unicodePath / "unrelated-é.txt");
	touch(unicodePath / "20240101010101_publicResults.zip");
	touch(unicodePath / "20240202020202_publicResults.zip");
	auto qld = LiveResultsInput::findCurrentFile(
		unicodePath, "qld", "2024qld");
	assert(qld);
	assert(qld->path.filename() == "20240202020202_publicResults.zip");
	assert(qld->timestamp == "20240202020202");

	touch(unicodePath / "mediafilelitepplh_20260314_123000.zip");
	touch(unicodePath / "mediafilelitepplh_20260315_001500.zip");
	auto vic = LiveResultsInput::findCurrentFile(
		unicodePath, "vic", "2026vic");
	assert(vic);
	assert(vic->path.filename() ==
		"mediafilelitepplh_20260315_001500.zip");
	assert(vic->timestamp == "20260315001500");

	touch(unicodePath / "001200-SG20270324.zip");
	touch(unicodePath / "000100-SG20270325.zip");
	auto nsw = LiveResultsInput::findCurrentFile(
		unicodePath, "nsw", "2027nsw");
	assert(nsw);
	assert(nsw->path.filename() == "000100-SG20270325.zip");
	assert(nsw->timestamp == "20270325000100");

	touch(unicodePath / "el2026_ha_detail.xml");
	touch(unicodePath / "el2022_ha_detail.xml");
	auto sa = LiveResultsInput::findCurrentFile(
		unicodePath, "sa", "2026sa");
	assert(sa);
	assert(sa->path.filename() == "el2026_ha_detail.xml");
	assert(!sa->timestamp);

	touch(unicodePath / "2025 WAEC LA VERBOSE RESULTS.xml");
	auto wa = LiveResultsInput::findCurrentFile(
		unicodePath, "wa", "2025wa");
	assert(wa);
	assert(wa->path.filename() == "2025 WAEC LA VERBOSE RESULTS.xml");
	assert(!wa->timestamp);

	assert(!LiveResultsInput::findCurrentFile(
		unicodePath, "fed", "2028fed"));
	std::filesystem::remove_all(unicodePath, error);
	return 0;
}
