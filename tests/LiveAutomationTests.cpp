// Small fictional feeds exercise orchestration, not election estimation. The
// ordinary recovery/history functions remain the oracle for accepted counts.
#include "../LiveAutomation.h"
#include "../LiveInputHistory.h"
#include "../LiveHistoryScope.h"
#include "../PreferenceModel.h"
#include "../TurnoutModelIO.h"
#include "../Date.h"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace {
using namespace LiveInputRecovery;
using json = nlohmann::json;
using Path = std::filesystem::path;

void checkStateReplacement(Path const& root) {
    auto path = root/"publication"/"state.json";
    LiveAutomation::atomicJson(path, {{"revision",1}});
    LiveAutomation::atomicJson(path, {{"revision",2}});
    assert(LiveAutomation::readJson(path).at("revision") == 2);
#ifdef _WIN32
    // Ordinary Windows readers do not always allow replacement while their
    // file handle is open. Exercise the actual OS collision, not a mock of the
    // retry loop. A brief reader must delay publication rather than lose state.
    auto lockForReading = [&]() {
        auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(handle != INVALID_HANDLE_VALUE);
        return handle;
    };
    auto reader = lockForReading();
    std::jthread releaseReader([reader] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CloseHandle(reader);
    });
    LiveAutomation::atomicJson(path, {{"revision",3}});
    releaseReader.join();
    assert(LiveAutomation::readJson(path).at("revision") == 3);

    // A lock that outlasts the bounded retry still reports the OS reason and
    // leaves both the old published state and the uncommitted replacement
    // complete. Releasing the lock permits a later normal save.
    reader = lockForReading();
    std::string failure;
    try { LiveAutomation::atomicJson(path, {{"revision",4}}); }
    catch (std::runtime_error const& error) { failure = error.what(); }
    CloseHandle(reader);
    assert(failure.find("Cannot publish automation state: " + path.string()) != std::string::npos);
    assert(failure.find("Windows error ") != std::string::npos);
    assert(LiveAutomation::readJson(path).at("revision") == 3);
    assert(LiveAutomation::readJson(Path(path.string()+".tmp")).at("revision") == 4);
    LiveAutomation::atomicJson(path, {{"revision",4}});
    assert(LiveAutomation::readJson(path).at("revision") == 4);
    assert(!std::filesystem::exists(Path(path.string()+".tmp")));
#endif
}

Snapshot readFeed(LiveResultsInput::CurrentFile const& file) {
    auto value = LiveAutomation::readJson(file.path);
    Snapshot result;
    result.election = value.value("election","2026sa");
    result.sourceTime = value.at("time"); result.sourceHash = fingerprint(file.path);
    result.districts = {{"Light",{1000,false}}};
    Record record;
    record.key = "Light/ordinary/School"; record.district = "Light";
    record.name = "School"; record.role = "ordinary";
    record.identity = "Light/School/1,2,3"; record.candidates = {1,2,3}; record.expected = 400;
    record.fp.votes = Counts{{1,value.value("fp",100)},{2,60},{3,40}};
    int pair = value.value("pair",2);
    if (pair) record.tcp.votes = Counts{{1,130},{pair,70}};
    result.records.push_back(record);
    return result;
}
void put(Path const& path,std::string time,int pair = 2,int fp = 100,std::string election = "2026sa") {
    LiveAutomation::atomicJson(path,{{"time",time},{"pair",pair},{"fp",fp},{"election",election}});
}
json config(Path const& source,std::string run = "2026sa-test") {
    return {{"version",1},{"election","2026sa"},{"election_date","2026-03-21"},
        {"timezone","Australia/Adelaide"},{"run_id",run},{"source_directory",source.string()},
        {"kind","replay"},{"visibility","private"},{"display_name","Fictional engineering check"}};
}
void checkNamedConfiguration(Path const& root) {
    std::string const id = "2026sa-replay";
    assert(LiveAutomation::suggestRunId(root, "2026sa", "replay") == id);
    auto folder = root/"live_runs"/id;
    auto settings = config(std::filesystem::path("live_runs")/id/"inbox", id);
    settings["commentary"] = {{"enabled",true},{"model","fixture"},{"spending_limit_usd",1}};
    LiveAutomation::Session saved(root, settings);
    saved.saveConfiguration(true);
    assert(LiveAutomation::readJson(folder/"run.json") == settings);
    assert(std::filesystem::is_directory(folder/"inbox"));
    assert(saved.status().at("state") == "paused");
    assert(!std::filesystem::exists(folder/"packages"));
    assert(LiveAutomation::suggestRunId(root, "2026sa", "replay") == id+"-2");
    bool rejected = false;
    try { saved.saveConfiguration(true); }
    catch (std::runtime_error const&) { rejected = true; }
    assert(rejected && LiveAutomation::readJson(folder/"run.json") == settings);

    auto revised = settings;
    revised["commentary"] = {{"enabled",false}};
    LiveAutomation::Session editor(root, revised);
    {
        // A preconfigured watcher can start normally, and a second instance
        // cannot edit the manifest until the forecast writer is released.
        LiveAutomation::Session active(root, settings);
        active.arm("inputs", false, false);
        rejected = false;
        try { editor.saveConfiguration(false); }
        catch (std::runtime_error const&) { rejected = true; }
        assert(rejected);
        assert(LiveAutomation::readJson(folder/"run.json").at("commentary") == settings.at("commentary"));
    }
    editor.saveConfiguration(false);
    auto edited = LiveAutomation::readJson(folder/"run.json");
    assert(edited.at("commentary") == revised.at("commentary"));
    assert(edited.at("input_revision") == "inputs");
    assert(edited.at("run_id") == id);
    assert(LiveAutomation::readJson(folder/"state.json").at("state") == "paused");
}
void checkFinnissPreferenceCutoff(Path const& root) {
    auto source = root/"inbox";
    std::filesystem::create_directories(source);
    LiveAutomation::Session session(root,config(source,"2026sa-finniss-clock"));
    session.arm("inputs-one",true,false);

    auto includesSample = [](LiveResultsInput::CurrentFile const& file) {
        auto cutoff = LiveResultsInput::snapshotPreferenceCutoff(file,"sa");
        assert(cutoff);
        std::istringstream sample(R"({"schema_version":1,"observations":[{
            "district":"Finniss","sample":"Victor Harbor","source":"test",
            "available_at":"2026-03-30T12:10:00+11:00","sampled_preference_ballots":1053,
            "flow_percent":{"IND:Lou Nicholson":58,"ALP":22,"LNP":13,"ON":7},"additional_sd_pp":7}]})");
        return PreferenceModel::loadCountObservations(sample,cutoff->time,cutoff->clockRegion).contains("Finniss");
    };
    // The SA archive filenames follow Sydney time. The two actual feed
    // clocks straddle publication at 11:40 am Adelaide time on 30 March.
    auto capture = [&](std::string name, std::string sourceTime, bool expected) {
        put(source/name,sourceTime);
        session.scanArrivals(); session.scanArrivals();
        auto next = session.ingest(readFeed);
        assert(next && next->sourceTime == sourceTime);
        assert(includesSample(next->file) == expected);
        session.reserveForecast(*next);
        session.finishForecast(*next,true,"");
    };
    capture("el2026260330113244.xml","2026-03-30T10:59:10",false);
    capture("el2026260330124040.xml","2026-03-30T11:55:30",true);
    // The same clock digits in a manually selected eastern-time filename
    // are still before publication. WA's archive instead uses local time.
    assert(!includesSample({source/"manual.xml","260330115530"}));
    auto perth = LiveResultsInput::snapshotPreferenceCutoff(
        LiveResultsInput::CurrentFile{source/"manual.xml","20260330115530"},"wa");
    assert(perth && perth->clockRegion == "wa" && perth->time == "2026-03-30T11:55:30");
    assert(!LiveResultsInput::snapshotPreferenceCutoff(std::nullopt,"sa"));
    session.setState("disarmed");
}

void check(Path const& root) {
    auto source = root/"inbox"; std::filesystem::create_directories(source);
    auto settings = config(source);
    std::string const first = "2026-03-21T18:00:00", second = "2026-03-21T18:01:30", last = "2026-03-21T18:03:00";
    {
        std::cout << "Checking stable copies and skipped feeds..." << std::endl;
        LiveAutomation::Session session(root,settings);
        session.arm("inputs-one",true,false);
        {
            auto before = fingerprint(session.directory()/"state.json");
            LiveAutomation::Session competing(root,settings);
            bool locked = false;
            try { competing.arm("different",true,false); } catch (std::exception const&) { locked = true; }
            assert(locked && fingerprint(session.directory()/"state.json") == before);
        }
        put(source/"first.xml",first);
        session.scanArrivals();
        assert(!session.ingest(readFeed)); // A manual copy is not yet stable.
        session.scanArrivals();
        auto a = session.ingest(readFeed);
        assert(a && a->sourceTime == first);
        auto firstId = session.reserveForecast(*a);
        assert(firstId == "f00000001");
        session.finishForecast(*a,true,"");
        assert(!session.ingest(readFeed));
        // All arrivals are accepted before selecting the newest forecast. The
        // withdrawn and replacement pairs remain available at their own clocks.
        put(source/"withdrawal.xml",second,0);
        put(source/"replacement.xml",last,3);
        session.scanArrivals(); session.scanArrivals();
        auto b = session.ingest(readFeed);
        std::cout << "Checking retained pairs and replay cutoff..." << std::endl;
        assert(b && b->sourceTime == last);
        auto now = readFeed(b->file);
        auto history = session.directory()/"history"/"accepted-counts";
        auto retained = loadPairPreferenceHistory(history,now);
        assert(retained.at(now.records[0].key).contains({1,2}));
        auto earlier = readFeed({source/"withdrawal.xml",{}});
        auto cutoff = loadPairPreferenceHistory(history,earlier);
        assert(!cutoff.at(earlier.records[0].key).contains({1,3}));
        auto secondId = session.reserveForecast(*b);
        assert(secondId == "f00000002");
        session.finishForecast(*b,false,"controlled calculation failure");
        assert(!session.ingest(readFeed)); // Do not repeatedly retry a broken feed.
        put(source/"later.xml","2026-03-21T18:04:30",3,110);
        put(source/"late-arrival.xml","2026-03-21T18:02:00",2);
        put(source/"conflict.xml",first,2,120);
        put(source/"wrong.xml","2026-03-21T18:09:00",2,100,"2024qld");
        { std::ofstream stream(source/"broken.xml"); stream << "<unfinished"; }
        session.scanArrivals(); session.scanArrivals();
        auto c = session.ingest(readFeed);
        assert(c && c->sourceTime == "2026-03-21T18:04:30");
        int rejected = 0, accepted = 0;
        std::cout << "Checking invalid and late captures..." << std::endl;
        auto status = session.status();
        for (auto const& record : status["captures"].items()) {
            rejected += record.value().value("state", "") == "rejected";
            accepted += record.value().value("state", "") == "accepted";
        }
        assert(rejected == 3 && accepted == 5);
        session.reserveForecast(*c); // Simulate interruption before completion.
        assert(!std::filesystem::exists(root/"forecasts"/"2026sa"/"live-snapshots"));
        for (auto const& entry : std::filesystem::directory_iterator(history))
            assert(entry.path().filename().string().find("2026-03-") == std::string::npos);
        LiveHistoryScope::Scope scope(session.directory()/"history");
        assert(directory(root,"2026sa") == history);
        assert(TurnoutModelIO::historyDirectory(root,"2026sa") == session.directory()/"history"/"turnout");
    }
    {
        std::cout << "Checking restart and isolation..." << std::endl;
        LiveAutomation::Session resumed(root,settings);
        bool rejected = false;
        try { resumed.arm("unexpected-edit",false,false); } catch (std::exception const&) { rejected = true; }
        assert(rejected);
        resumed.arm("inputs-one",false,false);
        auto next = resumed.ingest(readFeed);
        assert(next && next->sourceTime == "2026-03-21T18:04:30");
        assert(resumed.reserveForecast(*next) == "f00000003");
        resumed.finishForecast(*next,true,"");
        resumed.setState("paused");
        assert(!resumed.ingest(readFeed));
    }
    LiveAutomation::Session paused(root,settings);
    assert(paused.status().at("state") == "paused");
    LiveAutomation::Session isolated(root,config(source,"2026sa-other"));
    assert(isolated.status()["captures"].empty());
}

void checkCrashAndProcessingErrors(Path const& root) {
    auto source = root/"inbox"; std::filesystem::create_directories(source);
    auto settings = config(source,"2026sa-crash");
    auto directory = root/"live_runs"/"2026sa-crash";
    {
        LiveAutomation::Session session(root,settings);
        session.arm("inputs-one",true,false);
        put(source/"first.xml","2026-03-21T18:00:00");
        session.scanArrivals(); session.scanArrivals();
        bool stopped = false;
        try { session.ingest([](auto const&) -> Snapshot {
            throw LiveResultsInput::ProcessingError("Controlled storage failure");
        }); } catch (LiveResultsInput::ProcessingError const&) { stopped = true; }
        assert(stopped);
        auto pending = session.status();
        assert(pending["captures"].begin().value().at("state") == "discovered");
        auto capture = session.ingest(readFeed);
        auto id = session.reserveForecast(*capture);
        LiveAutomation::atomicJson(directory/"packages"/id/"numerical.json",{
            {"run_id",settings["run_id"]},{"forecast_id",id}});
        LiveAutomation::atomicJson(directory/"packages"/id/"ready-numerical.json",{{"forecast_id",id}});
        // Crash here: the numerical marker is durable, the ledger has not yet
        // acknowledged success. Restart must keep this immutable identity.
    }
    LiveAutomation::Session restarted(root,settings);
    restarted.arm("inputs-one",false,false);
    assert(!restarted.ingest(readFeed));
    assert(restarted.status().at("latest_forecast") == "2026-03-21T18:00:00");
    restarted.setState("paused");
    restarted.arm("inputs-two",true,false);
    auto recalculation = restarted.ingest(readFeed);
    assert(recalculation && restarted.reserveForecast(*recalculation) == "f00000002");
    restarted.finishForecast(*recalculation,true,"");
    // Redelivery under another name and an unchanged observation with a NEW
    // source clock are different cases: only the latter advances counting time.
    std::filesystem::copy_file(source/"first.xml",source/"identical.xml");
    put(source/"quiet-later.xml","2026-03-21T18:01:30");
    restarted.scanArrivals(); restarted.scanArrivals();
    auto later = restarted.ingest(readFeed);
    assert(later && later->sourceTime == "2026-03-21T18:01:30");
    assert(restarted.status()["captures"].size() == 2);
}

void checkCollectionDuringForecast(Path const& root) {
    auto source = root/"inbox"; std::filesystem::create_directories(source);
    LiveAutomation::Session session(root,config(source,"2026sa-background"));
    session.arm("inputs-one",true);
    put(source/"first.xml","2026-03-21T18:00:00");
    session.scanArrivals(); session.scanArrivals();
    auto frozen = session.ingest(readFeed);
    assert(frozen);
    session.reserveForecast(*frozen);
    auto frozenHash = fingerprint(session.directory()/"state.json");
    put(source/"withdrawal.xml","2026-03-21T18:01:30",0);
    put(source/"replacement.xml","2026-03-21T18:03:00",3);
    // The calculation can take longer than the federal arrival interval. The
    // collector remains active, but it only writes immutable delivery records.
    auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(5);
    std::size_t arrivals = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        arrivals = 0;
        for (auto const& entry : std::filesystem::directory_iterator(session.directory()/"arrivals"))
            if (entry.path().extension() == ".json") ++arrivals;
        if (arrivals == 3) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(arrivals == 3 && session.status()["captures"].size() == 1);
    assert(fingerprint(session.directory()/"state.json") == frozenHash);
    session.finishForecast(*frozen,true,"");
    auto newest = session.ingest(readFeed);
    assert(newest && newest->sourceTime == "2026-03-21T18:03:00");
    auto history = session.directory()/"history"/"accepted-counts";
    auto raw = readFeed(newest->file);
    assert(loadPairPreferenceHistory(history,raw).at(raw.records[0].key).contains({1,2}));
    assert(session.status()["captures"].size() == 3);
    session.setState("disarmed");
}

void checkDowntimeBacklog(Path const& root) {
    // Three hours of federal-rate arrivals can accumulate while the computer
    // is unavailable. Accept all observations in time order, but dispatch only
    // the final one. No full election calculations or real-time waits are used.
    auto source = root/"inbox"; std::filesystem::create_directories(source);
    auto settings = config(source,"2026sa-backlog");
    auto beginning = Timestamp::parseCompactLocal("20260321180000").value();
    std::string finalTime;
    for (int i = 0; i < 120; ++i) {
        auto time = Timestamp::fromUnixMilliseconds(beginning.unixMilliseconds()+90000LL*i).formatIsoLocal();
        finalTime = time.substr(0,19);
        put(source/("capture-"+std::to_string(i)+".xml"),finalTime,i < 60 ? 2 : 3,100+i);
    }
    LiveAutomation::Session session(root,settings);
    session.arm("inputs-one",true,false);
    session.scanArrivals(); session.scanArrivals();
    std::vector<std::string> accepted;
    auto newest = session.ingest(readFeed,[&](auto const& snapshot) { accepted.push_back(snapshot.sourceTime); });
    assert(newest && newest->sourceTime == finalTime && accepted.size() == 120);
    assert(std::is_sorted(accepted.begin(),accepted.end()));
    assert(session.reserveForecast(*newest) == "f00000001");
    session.finishForecast(*newest,true,"");
    auto records = session.status().at("captures");
    int skipped = 0;
    for (auto const& [identity,record] : records.items())
        skipped += record.value("forecast", "") == "skipped_newer_available";
    assert(skipped == 119 && !session.ingest(readFeed));
    auto raw = readFeed(newest->file);
    auto pairs = loadPairPreferenceHistory(session.directory()/"history"/"accepted-counts",raw);
    assert(pairs.at(raw.records[0].key).contains({1,2}));
    session.setState("disarmed");
}
}
int main() {
    // A short workspace path also exercises installations without Windows long
    // path support; hosted temporary directories can themselves be very deep.
    auto root = std::filesystem::current_path()/"cli-build"/"automation-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    try {
        checkStateReplacement(root);
        checkNamedConfiguration(root);
        checkFinnissPreferenceCutoff(root/"finniss");
        check(root);
        checkCrashAndProcessingErrors(root/"crash");
        checkCollectionDuringForecast(root/"background");
        checkDowntimeBacklog(root/"backlog");
    }
    catch (std::exception const& error) { std::cerr << error.what() << std::endl; return 1; }
    LiveInputHistory::clearMemoryCache();
    std::filesystem::remove_all(root);
    std::cout << "Arrival stability, recovery, retained pairs, cutoff, failures, restart and isolated history passed.\n";
}
