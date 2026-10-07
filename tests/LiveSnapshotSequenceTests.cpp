#include "../LiveSnapshotSequence.h"
#include "../SaveIO.h"

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
void touch(std::filesystem::path const& path) { std::ofstream file(path); file << "fictional test input"; }
template<class Operation> void rejects(Operation operation) {
    bool rejected = false;
    try { operation(); } catch (LiveSnapshotSequence::ConfigurationError const&) { rejected = true; }
    assert(rejected);
}
}

int main() {
    using namespace LiveSnapshotSequence;
    auto root = std::filesystem::temp_directory_path() /
        ("polling-sequence-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    Settings settings{"2026sa", "", {}};
    assert(directory(root, settings) == root / "forecasts/2026sa/live-snapshots/feeds");
    auto folder = root / "feeds";
    std::filesystem::create_directory(folder);
    settings.folder = portableFolder(root, folder);
    assert(directory(root, settings) == folder);
    touch(folder / "el2026260321190039.xml");
    touch(folder / "el2026260321183853.xml");
    touch(folder / "el2026260321185746.xml");
    touch(folder / "el2026_ha_detail.xml");
    touch(folder / "el2022221126180000.xml");
    auto discovered = discover(folder, settings.election);
    assert(discovered.error.empty());
    auto available = discovered.files;
    assert(available.size() == 3 && available.front().timestamp == "260321183853");
    settings.codes = parseCodes("260321183853\n260321190039\n");
    assert(resolve(available, settings.codes).back().path == folder / "el2026260321190039.xml");
    assert(parseCodes(codeText(settings.codes)) == settings.codes);
    // Expansion excludes the starting entry and adds as many later entries as
    // requested or available. Reaching the end never interrupts configuration.
    settings.codes = {"260321183853"};
    appendNext(available, settings.codes, 5);
    assert(settings.codes.size() == 3 && settings.codes.back() == "260321190039");
    auto before = settings.codes;
    appendNext(available, settings.codes, 1);
    assert(settings.codes == before);
    auto exact = std::vector<std::string>{"260321183853"};
    appendNext(available, exact, 1);
    assert(exact.size() == 2 && exact.back() == "260321185746");
    auto unchanged = exact;
    appendNext(available, exact, 0);
    assert(exact == unchanged);
    rejects([&] { auto empty = std::vector<std::string>{}; appendNext(available, empty, 1); });
    rejects([&] { resolve(available, {"260321190039", "260321183853"}); });
    rejects([&] { resolve(available, {"260321183853", "20260321183853"}); });
    rejects([&] { resolve(available, {"260321183854"}); });
    rejects([&] { parseCodes("260230120000"); });
    // Missing paths and a file supplied as a folder are normal user-facing
    // errors. In particular, opening the default folder must not throw.
    auto missing = discover(root / "missing", settings.election);
    assert(missing.files.empty() && missing.error.find("does not exist") != std::string::npos);
    auto notFolder = discover(available.front().path, settings.election);
    assert(notFolder.files.empty() && notFolder.error.find("not a folder") != std::string::npos);
    touch(folder / "el2026260230120000.xml");
    assert(!discover(folder, settings.election).error.empty());
    std::filesystem::remove(folder / "el2026260230120000.xml");

    // Exercise the actual appended .pol2 field encoding, including an older
    // version that must leave its existing bytes untouched.
    auto saved = root / "selection.pol2";
    { SaveFileOutput output(saved.string()); saveSettings(output, settings); }
    { SaveFileInput input(saved.string()); auto loaded = loadSettings(input, 67);
      assert(loaded.election == settings.election && loaded.folder == settings.folder && loaded.codes == settings.codes); }
    { SaveFileInput input(saved.string()); auto loaded = loadSettings(input, 66);
      assert(loaded.election.empty() && loaded.folder.empty() && loaded.codes.empty());
      std::string first; input >> first; assert(first == settings.election); }

    auto federal = root / "federal";
    std::filesystem::create_directory(federal);
    touch(federal / "aec-mediafeed-Detailed-Light-12345-20250503195904.zip");
    touch(federal / "aec-mediafeed-results-detailed-light-12345-20250503180100.xml");
    touch(federal / "aec-mediafeed-Detailed-Light-98765-20220521180000.zip");
    auto federalDiscovery = discover(federal, "2025fed");
    assert(federalDiscovery.error.empty());
    auto fed = federalDiscovery.files;
    assert(fed.size() == 2 && fed.front().timestamp == "20250503180100");
    assert(resolve(fed, {"250503180100"}).front().path == fed.front().path);
    touch(federal / "aec-mediafeed-Detailed-Light-12345-20250503180100.zip");
    assert(!discover(federal, "2025fed").error.empty());

    auto victorian = root / "victorian";
    std::filesystem::create_directory(victorian);
    touch(victorian / "State2022mediafilelitepplh_20221205_140012.zip");
    touch(victorian / "State2022mediafilelitepplh_20221126_180329.xml");
    touch(victorian / "State2022mediafilelitelh_20221126_180329.zip");
    touch(victorian / "State2022mediafileliteppuh_20221126_180329.zip");
    touch(victorian / "State2026mediafilelitepplh_20261128_180000.zip");
    auto vicDiscovery = discover(victorian, "2022vic");
    assert(vicDiscovery.error.empty() && vicDiscovery.files.size() == 2);
    auto vicCodes = std::vector<std::string>{"20221126180329"};
    appendNext(vicDiscovery.files, vicCodes, 20);
    assert(vicCodes.back() == "20221205140012" && vicCodes.size() == 2);
    assert(resolve(vicDiscovery.files, vicCodes).front().path.filename() == "State2022mediafilelitepplh_20221126_180329.xml");
    // Remove only this test's newly-created, uniquely-named temporary tree.
    std::filesystem::remove_all(root);
    std::cout << "Snapshot discovery, sequence selection and saved-project settings checks passed.\n";
}
