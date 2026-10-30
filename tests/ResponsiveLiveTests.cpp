#include "LiveSimulationJob.h"
#include "LiveSnapshotViewer.h"
#include "ForecastSpecificationProjectAdapter.h"
#include "WorkspacePaths.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <future>

namespace {
using namespace std::chrono_literals;
struct Signal {
    std::mutex mutex;
    std::condition_variable changed;
    int wakes = 0;
    void wake() { std::lock_guard lock(mutex); ++wakes; changed.notify_all(); }
    void wait(int count) { std::unique_lock lock(mutex); assert(changed.wait_for(lock, 5s, [&] { return wakes >= count; })); }
};

template<class Worker>
auto waitForCompletion(Worker& worker, std::uint64_t generation) {
    // Queued GUI notifications may lag an already available result. Wait for
    // the requested generation rather than assuming one wake per request.
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto completion = worker.take(); completion && completion->generation == generation)
            return completion;
        std::this_thread::sleep_for(1ms);
    }
    assert(false && "Timed out waiting for worker generation");
    return worker.take();
}

void testMailboxAndShutdown() {
    std::mutex mutex;
    std::condition_variable barrier;
    bool entered = false, release = false;
    std::vector<int> calls;
    Signal signal;
    BackgroundTask<int,int> coalesced([&](int input) {
        std::unique_lock lock(mutex);
        calls.push_back(input);
        if (input == 1) {
            entered = true; barrier.notify_all();
            barrier.wait(lock,[&] { return release; });
        }
        return input;
    },[&] { signal.wake(); },true);
    assert(coalesced.submit(1,1));
    {
        std::unique_lock lock(mutex);
        assert(barrier.wait_for(lock,5s,[&] { return entered; }));
    }
    assert(coalesced.submit(2,2)); assert(coalesced.submit(3,3));
    { std::lock_guard lock(mutex); release = true; barrier.notify_all(); }
    auto latest = waitForCompletion(coalesced,3);
    assert(*latest->result == 3);
    { std::lock_guard lock(mutex); assert((calls == std::vector<int>{1,3})); }
    coalesced.shutdown(); assert(!coalesced.submit(4,4));

    // Destruction waits for an active calculation. No worker outlives its
    // owner, and closing suppresses a late completion notification.
    entered = false; release = false;
    Signal shutdownSignal;
    auto worker = std::make_unique<BackgroundTask<int,int>>([&](int input) {
        std::unique_lock lock(mutex); entered = true; barrier.notify_all();
        barrier.wait(lock,[&] { return release; }); return input;
    },[&] { shutdownSignal.wake(); });
    assert(worker->submit(1,1));
    {
        std::unique_lock lock(mutex);
        assert(barrier.wait_for(lock,5s,[&] { return entered; }));
    }
    auto closing = std::async(std::launch::async,[&] { worker.reset(); });
    assert(closing.wait_for(50ms) == std::future_status::timeout);
    { std::lock_guard lock(mutex); release = true; barrier.notify_all(); }
    assert(closing.wait_for(5s) == std::future_status::ready); closing.get();
    { std::lock_guard lock(shutdownSignal.mutex); assert(shutdownSignal.wakes == 0); }
}

std::unique_ptr<PollingProject> fictionalProject(std::filesystem::path const& root) {
    std::ofstream(root / "config.cfg") << "iSimulationThreads=8\n";
    std::ofstream(root / "seats.txt") << "#Example\nsIncumbent=ALP\nsChallenger=LIB\nsRegion=Area\nfTppMargin=2\n";
    ForecastSpecification spec;
    spec.id = "fictional"; spec.electionName = "Fictional election"; spec.electionCode = "2030fed";
    spec.dataSources.seats = "seats.txt";
    spec.partySettings = {"labor", "liberal"};
    spec.parties = {{0,"labor","Labor","ALP"}, {1,"liberal","Liberal","LIB"}};
    spec.partyOfficialCodes = {{"labor","ALP"}, {"liberal","LIB"}};
    spec.regions = {{0,"area","Area",10000,"Area",50}};
    spec.models = {{"model","Model","2030fed",{{"ALP",0,0},{"LIB",0,0}}}};
    spec.projections = {{"projection","Projection","model",10,*Date::parseIso("2030-01-01"),{}}};
    spec.simulations = {{"first","First","projection",10,ForecastSpecification::SimulationMode::LiveAutomatic},
                       {"second","Second","projection",10,ForecastSpecification::SimulationMode::LiveAutomatic}};
    auto result = ForecastSpecificationProjectAdapter::construct(spec, WorkspacePaths(root));
    if (!result.valid()) std::cerr << result.errorMessage() << '\n';
    assert(result.valid());
    return std::move(result.project);
}

void testContext(PollingProject& source) {
    auto id = source.simulations().indexToId(0);
    auto initial = source.simulations().view(id).completedLiveState();
    initial.report.dateCode = "before";
    initial.baseline = Simulation::Report{}; initial.baseline->dateCode = "baseline";
    initial.oddsMeans[std::make_pair(0,1)] = 7;
    initial.oddsInputs[std::make_pair(0,1)] = .5f; initial.oddsIterations = 10;
    initial.updated = Timestamp::now();
    initial.booths.push_back({"Example","Fictional booth"});
    source.simulations().access(id).publishLiveState(initial);
    auto context = source.createLiveRunContext({id});
    assert(context->simulations().count() == 1);
    assert(context->simulations().indexToId(0) == id);
    auto copied = context->simulations().view(id).completedLiveState();
    assert(copied.baseline->dateCode == "baseline" && copied.oddsMeans.at({0,1}) == 7);
    auto modelId = context->models().indexToId(0);
    context->models().replace(modelId, StanModel("Worker model", "2030fed", "ALP,LIB"));
    assert(source.models().view(modelId).getName() == "Model");
    assert(source.simulations().view(id).isValid()); // Invalidation stayed with the new owner.
    assert(!context->simulations().view(id).isValid());
    auto projectionId = context->projections().indexToId(0);
    auto settings = context->projections().view(projectionId).getSettings();
    settings.name = "Private projection cache owner";
    context->projections().access(projectionId).replaceSettings(settings);
    assert(source.projections().view(projectionId).getSettings().name == "Projection");
    auto seatId = context->seats().indexToId(0);
    context->seats().access(seatId).tppMargin = 9;
    assert(source.seats().view(seatId).tppMargin == 2);
    assert(source.simulations().view(id).getLiveBoothSnapshots().size() == 1);
    assert(!source.simulations().access(id).detachRetainedLiveRun());
    assert(source.simulations().view(id).getLiveBoothSnapshots().size() == 1);
    Config config((source.paths().root() / "config.cfg").string());
    config.limitBackgroundSimulationThreads(4); assert(config.getSimulationThreads() == 3);
    config.limitBackgroundSimulationThreads(1); assert(config.getSimulationThreads() == 1);
    config.limitBackgroundSimulationThreads(0); assert(config.getSimulationThreads() == 1);
}

void testPublication(PollingProject& source) {
    Signal signal;
    auto first = source.simulations().indexToId(0), second = source.simulations().indexToId(1);
    std::mutex barrierMutex;
    std::condition_variable barrier;
    bool entered = false, released = false;
    int calls = 0;
    auto runner = [&](PollingProject& context, Simulation& sim, auto, auto, LiveSimulationJob::Input) {
        ++calls;
        if (calls == 1) {
            std::unique_lock lock(barrierMutex); entered = true; barrier.notify_all();
            barrier.wait(lock, [&] { return released; });
        }
        if (sim.getSettings().name == "Second") {
            sim.publishLiveDiagnostics({{"completed",false},{"failure","Fictional bad input"}},{});
            return false;
        }
        auto state = sim.completedLiveState();
        assert(state.baseline && state.oddsMeans.at({0,1}) == 7);
        state.report.dateCode = "completed"; state.updated = Timestamp::now();
        state.report.countProgress = LiveData::CountProgress{1200,1050,2000};
        state.diagnostic = {{"completed",true}};
        sim.publishLiveState(std::move(state));
        context.seats().access(context.seats().indexToId(0)).tppMargin = 3;
        context.outcomes().add(Outcome(0,1,5,5,0,0,-2));
        return true;
    };
    {
        LiveSimulationJob job(source,{first,second},[&] { signal.wake(); },{},runner);
        assert(job.submit(1,{}));
        {
            std::unique_lock lock(barrierMutex); assert(barrier.wait_for(lock,5s,[&] { return entered; }));
            assert(!job.submit(2,{}));
            assert(source.simulations().view(first).getLatestReport().dateCode == "before");
            assert(source.outcomes().count() == 0);
            released = true; barrier.notify_all();
        }
        signal.wait(1);
        // One completion must be taken before another snapshot can start.
        assert(!job.submit(2,{}));
        auto result = job.take(); assert(result && !result->error && result->generation == 1);
        assert(!result->result->succeeded && result->result->simulations.size() == 2);
        LiveSimulationJob::publish(source,*result->result);
        job.retire(std::move(result->result->retiredRuns));
        assert(source.simulations().view(first).getLatestReport().dateCode == "completed");
        auto const& published = source.simulations().view(first).getLatestReport();
        assert(published.countProgress && published.countProgress->fpPercent() == 60);
        assert(published.getTcpPercentCounted() == 52.5f);
        assert(source.simulations().view(second).getLiveInputDiagnostic().at("completed") == false);
        assert(source.outcomes().count() == 1 && source.seats().viewByIndex(0).tppMargin == 3);
        assert(source.outcomes().begin()->partyOneSwing == 1);
        assert(source.outcomes().begin()->rawPartyOneSwing == -2);
        assert(job.finish(1)); signal.wait(2);
        auto finished = job.take(); assert(finished && finished->result->finished);
        assert(!job.submit(2,{}));
        // Not dispatching another snapshot is the stop boundary. Destruction
        // joins the waiting worker without another calculation or publication.
    }
    assert(calls == 2);
    Signal attentionSignal;
    LiveSimulationJob attention(source,{first},[&] { attentionSignal.wake(); },{},
        [](PollingProject&,Simulation& sim,auto,auto action,LiveSimulationJob::Input) {
            auto state = sim.completedLiveState(); state.report.dateCode = "export-failed";
            sim.publishLiveState(std::move(state)); action("Fictional export failure"); return true;
        });
    assert(attention.submit(3,{})); attentionSignal.wait(1);
    auto result = attention.take(); assert(result && !result->result->succeeded);
    LiveSimulationJob::publish(source,*result->result);
    assert(source.simulations().view(first).getLatestReport().dateCode == "export-failed");
    Signal failedSignal;
    LiveSimulationJob failed(source,{first},[&] { failedSignal.wake(); },{},
        [](PollingProject& context, Simulation& sim,auto,auto,LiveSimulationJob::Input) -> bool {
            auto state = sim.completedLiveState(); state.report.dateCode = "uncommitted";
            sim.publishLiveState(std::move(state)); context.outcomes().add(Outcome(0,99,99,99,0,0));
            throw std::runtime_error("Fictional structural failure");
        });
    assert(failed.submit(4,{})); failedSignal.wait(1);
    auto rejected = failed.take(); assert(rejected && !rejected->result->succeeded);
    LiveSimulationJob::publish(source,*rejected->result);
    assert(source.simulations().view(first).getLatestReport().dateCode == "export-failed");
    assert(source.outcomes().count() == 1);
    assert(source.simulations().view(first).getLiveInputDiagnostic().at("completed") == false);
}

void testRepeatedPublication(PollingProject& source) {
    auto id = source.simulations().indexToId(0);
    int const previousOutcomes = source.outcomes().count();
    int calls = 0;
    LiveSimulationJob job(source,{id},[] {},{},
        [&](PollingProject& context,Simulation& simulation,auto,auto,LiveSimulationJob::Input input) {
            auto state = simulation.completedLiveState();
            ++calls;
            state.report.dateCode = *input->timestamp;
            state.booths = {{"Example","Counted booth"}};
            state.previousSeats.resize(1); state.previousSeats[0][0] = float(calls);
            state.oddsMeans[{0,1}] = float(calls);
            state.updated = Timestamp::now();
            simulation.publishLiveState(std::move(state));
            context.outcomes().add(Outcome(0,0,10,10,0,0));
            return true;
        });
    // Reuse one context through a long controlled series. Completion packets
    // are consumed once; simulation caches retain their current values only.
    for (std::uint64_t step = 1; step <= 128; ++step) {
        std::string code = "fictional-step-" + std::to_string(step);
        assert(job.submit(step,LiveResultsInput::CurrentFile{"fictional.xml",code}));
        auto result = waitForCompletion(job,step); assert(!result->error);
        assert(result->result->simulations.size() == 1 && result->result->outcomes.size() == 1);
        LiveSimulationJob::publish(source,*result->result);
        job.retire(std::move(result->result->retiredRuns));
        assert(!job.take());
        auto published = source.simulations().view(id).completedLiveState();
        assert(published.report.dateCode == code && published.booths.size() == 1);
        assert(published.previousSeats.size() == 1 && published.oddsMeans.size() == 1);
        assert(published.oddsMeans.at({0,1}) == float(step));
    }
    assert(calls == 128 && source.outcomes().count() == previousOutcomes + 128);
    assert(job.finish(200)); auto finished = waitForCompletion(job,200);
    assert(finished->result->finished && !job.submit(201,{}));
}

nlohmann::json snapshot(std::string code, std::string simulation = "First") {
    return {{"format_version",2}, {"run",{{"snapshot_code",code},{"completed_at","2030-01-01T20:00:00+11:00"},
        {"simulation_name",simulation},{"term_code","2030fed"},{"output_set","test"}}},
        {"live_analysis_summary",{{"projected_2pp",52.0},{"raw_2pp_deviation",-1.4}}},
        {"simulation_report",{{"seat_name",{"Example"}},
            {"seat_party_win_percent", {{{{"party_index",0},{"value",50}}}}},
            {"seat_party_mean_fp_share", {{{{"party_index",0},{"value",40}}}}}}}};
}

void testViewer(std::filesystem::path const& root) {
    auto folder = root / "snapshots"; std::filesystem::create_directory(folder);
    std::ofstream(folder / "snapshot_20300101190000__a.json") << snapshot("20300101190000");
    std::ofstream(folder / "snapshot_20300101193000__b.json") << snapshot("20300101193000");
    std::ofstream(folder / "snapshot_20300101193000__other.json") << snapshot("20300101193000","Second");
    Signal signal;
    LiveSnapshotViewer viewer([&] { signal.wake(); });
    LiveSnapshotViewer::Request request; request.directory = folder; request.simulationName = "First";
    request.outputSet = "test"; request.filterSimulationName = true;
    assert(viewer.submit(1,request)); signal.wait(1);
    auto first = viewer.take(); assert(first && !first->error && first->result->metadata.records.size() == 2);
    assert(first->result->filename == "snapshot_20300101193000__b.json");
    auto parses = first->result->parsedRecords;
    request.followLatest = false; request.filename = "snapshot_20300101190000__a.json";
    std::ofstream(folder / "snapshot_20300101200000__c.json") << snapshot("20300101200000");
    viewer.notifyExports({folder / "snapshot_20300101200000__c.json"});
    assert(viewer.submit(2,request)); signal.wait(2);
    auto held = viewer.take(); assert(held && held->result->metadata.records.size() == 3);
    assert(held->result->filename == request.filename && held->result->parsedRecords == parses + 1);
    request.followLatest = true; request.mode = LiveSnapshotViewer::Mode::SeatFp;
    assert(viewer.submit(3,request)); signal.wait(3);
    auto followed = viewer.take(); assert(followed && followed->result->filename == "snapshot_20300101200000__c.json");
    assert(followed->result->parsedRecords == parses + 1);
    for (auto const& metadata : followed->result->metadata.records)
        assert(metadata.seatFpShares.seats.empty() && metadata.seatWinChances.seats.empty());
    assert(!followed->result->view.rows.empty());
    // Coalescing keeps only the newest queued request/result; consuming any
    // older in-flight completion never authorises it as the current selection.
    for (std::uint64_t i = 4; i <= 30; ++i) assert(viewer.submit(i,request));
    std::optional<LiveSnapshotViewer::Completion> latest;
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto value = viewer.take()) { latest = std::move(value); if (latest->generation == 30) break; }
        std::this_thread::yield();
    }
    assert(latest && latest->generation == 30 && latest->result->parsedRecords == parses + 1);
    // Every output uses the same asynchronous preparation path. Small fixtures
    // with absent optional statistics must still produce a valid presentation.
    for (int mode = 0; mode < 14; ++mode) for (bool graph : {false,true}) {
        request.mode = static_cast<LiveSnapshotViewer::Mode>(mode); request.graph = graph;
        auto generation = 31 + std::uint64_t(mode)*2 + (graph ? 1 : 0);
        assert(viewer.submit(generation,request));
        auto presentation = waitForCompletion(viewer,generation);
        assert(presentation && !presentation->error && presentation->result->parsedRecords == parses + 1);
    }
    // Metric changes reuse the compact history and leave the full diagnostic
    // table available. Each graph must contain only the requested series and
    // fit its own level, including an unchanged absolute series around 52%.
    request.mode = LiveSnapshotViewer::Mode::Internal2pp; request.graph = true;
    assert(viewer.submit(90,request)); auto projected = waitForCompletion(viewer,90);
    assert(!projected->error && projected->result->graph.series.size() == 1);
    assert(projected->result->graph.series.front().label == "Internal projected 2PP");
    assert(projected->result->graph.yMin > 51 && projected->result->graph.yMax < 53);
    request.internal2ppMetric = LiveSnapshotViewer::Internal2ppMetric::RawDeviation;
    assert(viewer.submit(91,request)); auto deviation = waitForCompletion(viewer,91);
    assert(!deviation->error && deviation->result->graph.series.size() == 1);
    assert(deviation->result->graph.series.front().label == "Raw 2PP deviation");
    assert(deviation->result->graph.yMax < 0);
    request.graph = false;
    assert(viewer.submit(92,request)); auto fullTable = waitForCompletion(viewer,92);
    assert(!fullTable->error && fullTable->result->view.columns.size() == 20);
    assert(fullTable->result->view.columns[1].includeInGraph && fullTable->result->view.columns[2].includeInGraph);
    assert(fullTable->result->parsedRecords == parses + 1);

    // A duplicate capture is appended without rereading the old series, and
    // the existing ordering/grouping code still chooses one presentation row.
    auto duplicatePath = folder / "snapshot_20300101200000__duplicate.json";
    std::ofstream(duplicatePath) << snapshot("20300101200000");
    viewer.notifyExports({duplicatePath, duplicatePath});
    request.mode = LiveSnapshotViewer::Mode::SeatFp; request.graph = false;
    assert(viewer.submit(100,request)); auto duplicate = waitForCompletion(viewer,100);
    assert(!duplicate->error && duplicate->result->metadata.records.size() == 4);
    assert(duplicate->result->view.rows.size() == 3 && duplicate->result->view.rows.back().duplicateCount == 2);
    assert(duplicate->result->parsedRecords == parses + 2);
    // Rewriting a notified file updates its cached record. A series switch
    // releases the previous compact state, including its party/seat arrays.
    std::ofstream(duplicatePath) << snapshot("20300101210000");
    viewer.notifyExports({duplicatePath});
    assert(viewer.submit(101,request)); auto revised = waitForCompletion(viewer,101);
    assert(!revised->error && revised->result->view.rows.size() == 4);
    request.simulationName = "Second";
    assert(viewer.submit(102,request)); auto switched = waitForCompletion(viewer,102);
    assert(!switched->error && switched->result->metadata.records.size() == 1);
    assert(switched->result->parsedRecords == 5);
}
}

int main() {
    auto root = std::filesystem::temp_directory_path() /
        ("polling-responsive-live-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    auto project = fictionalProject(root);
    testMailboxAndShutdown(); testContext(*project); testPublication(*project);
    testRepeatedPublication(*project); testViewer(root);
    // This test deletes only the unique temporary directory it created.
    auto parent = std::filesystem::temp_directory_path();
    if (parent.filename().empty()) parent = parent.parent_path();
    assert(std::filesystem::absolute(root).parent_path().lexically_normal() == std::filesystem::absolute(parent).lexically_normal());
    std::filesystem::remove_all(root);
    std::cout << "Responsive live tests passed\n";
}
