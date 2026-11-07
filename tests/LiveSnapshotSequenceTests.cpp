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
    // Clipboard line endings must preserve both the visible code boundaries
    // and the parsed selection. CRLF is one break, not an extra blank line;
    // actual blank lines and unfinished/invalid user text remain untouched.
    auto lineCodes = std::vector<std::string>{"20241025201257", "20241026181622"};
    for (std::string separator : {"\n", "\r\n", "\r", "\xC2\x85", "\xE2\x80\xA8", "\xE2\x80\xA9"}) {
        auto pasted = lineCodes[0] + separator + lineCodes[1] + separator;
        auto normalized = normalizeCodeLineEndings(pasted);
        assert(normalized == codeText(lineCodes) + "\n");
        assert(parseCodes(pasted) == lineCodes);
        assert(parseCodes(normalized) == lineCodes);
        assert(normalizeCodeLineEndings(normalized) == normalized);
    }
    assert(normalizeCodeLineEndings("\r\n 20241025201257\r\n\r20241026181622\t\n") ==
        "\n 20241025201257\n\n20241026181622\t\n");
    assert(parseCodes("\r\n 20241025201257\r\n\r20241026181622\t\n") == lineCodes);
    assert(normalizeCodeLineEndings("").empty());
    assert(normalizeCodeLineEndings("unfinished code") == "unfinished code");
    rejects([&] { parseCodes(lineCodes[0] + lineCodes[1]); });
    rejects([&] { parseCodes(lineCodes[0] + "\\n" + lineCodes[1]); });
    // Selecting a missing intermediate capture inserts it rather than making
    // the replay invalid. Re-selecting the same timestamp is harmless, even
    // when written using the alternative short/full-year convention.
    auto inserted = settings.codes;
    insertCode(available, inserted, "260321185746");
    assert((inserted == std::vector<std::string>{"260321183853", "260321185746", "260321190039"}));
    insertCode(available, inserted, "20260321185746");
    assert(inserted.size() == 3);
    auto fromEmpty = std::vector<std::string>{};
    insertCode(available, fromEmpty, "260321185746");
    insertCode(available, fromEmpty, "260321190039");
    insertCode(available, fromEmpty, "260321183853");
    assert(fromEmpty == inserted);
    rejects([&] { insertCode(available, inserted, "260321183854"); });
    assert(fromEmpty == inserted); // A failed addition preserves the selection.
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

    // AEC sometimes publishes an election's final capture the following year.
    // Election-year captures establish the event ID; other years of that same
    // event remain selectable, while unrelated events stay out of the list.
    auto federal2016 = root / "federal2016";
    std::filesystem::create_directory(federal2016);
    touch(federal2016 / "aec-mediafeed-Detailed-Light-20499-20170511174118.zip");
    touch(federal2016 / "aec-mediafeed-Detailed-Light-24310-20190729153147.zip");
    touch(federal2016 / "aec-mediafeed-Detailed-Light-20499-20160702170213.zip");
    touch(federal2016 / "aec-mediafeed-Detailed-Light-24310-20170511174119.zip");
    auto fed2016 = discover(federal2016, "2016fed");
    assert(fed2016.error.empty() && fed2016.files.size() == 2);
    assert(fed2016.files.front().timestamp == "20160702170213");
    assert(fed2016.files.back().timestamp == "20170511174118");
    auto lateSelection = resolve(fed2016.files, {"20160702170213", "20170511174118"});
    assert(lateSelection.back().path.filename() == "aec-mediafeed-Detailed-Light-20499-20170511174118.zip");
    auto lateCodes = std::vector<std::string>{"20160702170213"};
    appendNext(fed2016.files, lateCodes, 5);
    assert(lateCodes.back() == "20170511174118");
    // Duplicate protection also applies to next-year ZIP/XML alternatives.
    touch(federal2016 / "aec-mediafeed-results-detailed-light-20499-20170511174118.xml");
    assert(!discover(federal2016, "2016fed").error.empty());

    auto federal2013 = root / "federal2013";
    std::filesystem::create_directory(federal2013);
    touch(federal2013 / "aec-mediafeed-Detailed-Light-17496-20130907180000.zip");
    touch(federal2013 / "aec-mediafeed-Detailed-Light-17496-20140516155658.zip");
    auto fed2013 = discover(federal2013, "2013fed");
    assert(fed2013.error.empty() && fed2013.files.size() == 2);
    assert(fed2013.files.back().timestamp == "20140516155658");

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

    // Queensland captures timestamp a fixed-name publicResults document.
    // Only top-level captures belong in the editor; earlier election years,
    // archived duplicates and the un-timestamped working file stay excluded.
    auto queensland = root / "queensland";
    std::filesystem::create_directory(queensland);
    touch(queensland / "20241026190000_publicResults.zip");
    touch(queensland / "20241026180000_publicResults.xml");
    touch(queensland / "20241026183000_PUBLICRESULTS.ZIP");
    touch(queensland / "20201031180000_publicResults.zip");
    touch(queensland / "publicResults.xml");
    touch(queensland / "20241026181500_otherResults.zip");
    std::filesystem::create_directory(queensland / "no-data-change");
    touch(queensland / "no-data-change/20241026180000_publicResults.xml");
    auto qldDiscovery = discover(queensland, "2024qld");
    assert(qldDiscovery.error.empty() && qldDiscovery.files.size() == 3);
    auto qldCodes = std::vector<std::string>{"20241026180000", "20241026190000"};
    insertCode(qldDiscovery.files, qldCodes, "20241026183000");
    assert(qldCodes.size() == 3 && qldCodes[1] == "20241026183000");
    assert(resolve(qldDiscovery.files, qldCodes).back().path.filename() == "20241026190000_publicResults.zip");
    touch(queensland / "20241026180000_publicResults.zip");
    assert(!discover(queensland, "2024qld").error.empty());
    std::filesystem::remove(queensland / "20241026180000_publicResults.zip");
    touch(queensland / "20241032180000_publicResults.zip");
    assert(!discover(queensland, "2024qld").error.empty());
    auto western = root / "western";
    std::filesystem::create_directory(western);
    touch(western / "waec-la-verbose-20250308180640.xml");
    touch(western / "waec-la-verbose-20250406125852.xml");
    touch(western / "8 March 2025 State General Election - 250308210640.xml");
    touch(western / "LA VERBOSE RESULTS.xml");
    auto waDiscovery = discover(western, "2025wa");
    assert(waDiscovery.error.empty() && waDiscovery.files.size() == 2);
    assert(waDiscovery.files.front().timestamp == "20250308180640");

    // Late returns can cross a calendar-year boundary for state elections too.
    // SA/VIC retain an election-year prefix; QLD/WA rely on an election-specific
    // folder and source validation when loaded. Exercise discovery, ordering,
    // selection and duplicate rejection with both short and full timestamps.
    struct LateStateCapture {
        std::string election, firstFile, lateFile, unrelatedFile, duplicateFile;
        std::string firstCode, lateCode;
    };
    for (auto const& capture : std::vector<LateStateCapture>{
        {"2026vic", "State2026mediafilelitepplh_20261128_180000.zip",
            "State2026mediafilelitepplh_20270115_120000.xml",
            "State2022mediafilelitepplh_20270115_120001.xml",
            "State2026mediafilelitepplh_20270115_120000.zip",
            "20261128180000", "20270115120000"},
        {"2026sa", "el2026260321180000.xml", "el2026270115120000.xml",
            "el2022270115120001.xml", "EL2026270115120000.XML",
            "260321180000", "270115120000"},
        {"2024qld", "20241026180000_publicResults.zip", "20250115120000_publicResults.xml",
            "20201031180000_publicResults.zip", "20250115120000_publicResults.zip",
            "20241026180000", "20250115120000"},
        {"2025wa", "waec-la-verbose-20250308180000.xml", "waec-la-verbose-20260115120000.xml",
            "waec-la-verbose-20210313180000.xml", "WAEC-LA-VERBOSE-20260115120000.XML",
            "20250308180000", "20260115120000"}
    }) {
        auto stateFolder = root / capture.election;
        std::filesystem::create_directory(stateFolder);
        touch(stateFolder / capture.lateFile);
        touch(stateFolder / capture.unrelatedFile);
        auto lateOnly = discover(stateFolder, capture.election);
        assert(lateOnly.error.empty() && lateOnly.files.size() == 1);
        touch(stateFolder / capture.firstFile);
        auto stateDiscovery = discover(stateFolder, capture.election);
        assert(stateDiscovery.error.empty() && stateDiscovery.files.size() == 2);
        assert(stateDiscovery.files.front().timestamp == capture.firstCode);
        assert(stateDiscovery.files.back().timestamp == capture.lateCode);
        auto codes = std::vector<std::string>{capture.firstCode};
        appendNext(stateDiscovery.files, codes, 5);
        assert(codes.size() == 2 && codes.back() == capture.lateCode);
        assert(resolve(stateDiscovery.files, codes).back().path.filename() == capture.lateFile);
        // Case-only alternatives are the same file on Windows. ZIP/XML variants
        // still test duplicate rejection there, and all four formats do on Linux.
        if (!std::filesystem::exists(stateFolder / capture.duplicateFile)) {
            touch(stateFolder / capture.duplicateFile);
            assert(!discover(stateFolder, capture.election).error.empty());
        }
    }
    // Remove only this test's newly-created, uniquely-named temporary tree.
    std::filesystem::remove_all(root);
    std::cout << "Snapshot discovery, sequence selection and saved-project settings checks passed.\n";
}
