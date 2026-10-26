#pragma once

#include "BackgroundTask.h"
#include "LiveSnapshotData.h"
#include "LiveSnapshotGraph.h"
#include "LiveSnapshotTable.h"
#include <atomic>
#include <set>

// History belongs exclusively to this viewer's worker. The GUI receives just
// lightweight run metadata and the requested table/graph, never a second copy
// of the entire parsed history. Selection changes replace queued requests.
class LiveSnapshotViewer {
public:
    enum class Mode { NodeInspector, Parliament, SeatExpectations, SeatThresholds,
        Tpp, VoteShares, SeatWinChance, SeatFp, FpCounted, TppSwingBasis,
        TcpSwingBasis, BoothTypeBias, VoteTypeBias, Internal2pp };
    enum class Internal2ppMetric { Projected, RawDeviation };
    struct Request {
        std::filesystem::path directory;
        std::string simulationName, outputSet;
        bool filterSimulationName = false;
        std::map<int, LiveSnapshot::PartyColour> colours;
        bool rescan = false; // Explicit refresh or an external file revision.
        Mode mode = Mode::NodeInspector;
        bool graph = false, allParties = true, followLatest = true, groupByType = true;
        int party = 0;
        std::string seat, filename;
        LiveSnapshot::CategoryStatistic categoryStatistic = LiveSnapshot::CategoryStatistic::Bias;
        Internal2ppMetric internal2ppMetric = Internal2ppMetric::Projected;
    };
    struct Presentation {
        LiveSnapshot::LoadResult metadata;
        LiveSnapshot::ParliamentView view;
        LiveSnapshot::GraphModel graph;
        LiveSnapshot::TableLayout layout;
        std::vector<LiveSnapshot::ThresholdPartyOption> parties;
        std::vector<std::string> seats;
        std::string filename, seat;
        int party = 0;
        bool allParties = true;
        std::size_t parsedRecords = 0; // Useful for bounded-work regression checks.
    };
    using Completion = BackgroundTask<Request, Presentation>::Completion;
    explicit LiveSnapshotViewer(std::function<void()> wake);
    ~LiveSnapshotViewer();
    bool submit(std::uint64_t generation, Request request) { return task.submit(generation, std::move(request)); }
    std::optional<Completion> take() { return task.take(); }
    bool idle() const { return task.idle(); }
    void notifyExports(std::vector<std::filesystem::path> const& paths);
private:
    Presentation prepare(Request request);
    void refreshHistory(Request const& request);
    std::vector<std::filesystem::path> discoverFiles(Request const& request);
    void refreshRecords(Request const& request, std::vector<std::filesystem::path> const& files, bool replaceInventory);
    Presentation metadataFor(Request const& request) const;
    void buildView(Request& request, Presentation& result) const;
    void preparePartySummary(Request& request, Presentation& result) const;
    void prepareInternal2pp(Request const& request, Presentation& result) const;
    void buildGraph(Request const& request, Presentation& result) const;
    std::string seriesKey;
    LiveSnapshot::LoadResult history;
    struct FileStamp {
        std::filesystem::file_time_type modified;
        std::uintmax_t size = 0;
        bool operator==(FileStamp const&) const = default;
    };
    std::map<std::string, FileStamp> stamps;
    std::map<std::string, std::string> knownWarnings;
    std::size_t parsedRecords = 0;
    std::atomic<bool> closing = false;
    std::mutex notificationMutex;
    std::set<std::filesystem::path> notifiedExports;
    BackgroundTask<Request, Presentation> task;
};
