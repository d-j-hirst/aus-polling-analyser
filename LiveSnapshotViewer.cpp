#include "LiveSnapshotViewer.h"
#include "LiveResultsInput.h"
#include <algorithm>

LiveSnapshotViewer::LiveSnapshotViewer(std::function<void()> wake)
    : task([this](Request request) { return prepare(std::move(request)); }, std::move(wake), true) {}
LiveSnapshotViewer::~LiveSnapshotViewer() { closing = true; task.shutdown(); }

void LiveSnapshotViewer::notifyExports(std::vector<std::filesystem::path> const& paths) {
    std::lock_guard lock(notificationMutex);
    notifiedExports.insert(paths.begin(), paths.end());
}

LiveSnapshotViewer::Presentation LiveSnapshotViewer::prepare(Request request) {
    refreshHistory(request);
    auto result = metadataFor(request);
    buildView(request, result);
    buildGraph(request, result);
    result.parsedRecords = parsedRecords;
    return result;
}

void LiveSnapshotViewer::refreshHistory(Request const& request) {
    auto key = LiveResultsInput::pathToUtf8(request.directory) + "\n" + request.simulationName +
        (request.filterSimulationName ? "\nfiltered" : "\nunfiltered");
    bool changedSeries = key != seriesKey;
    if (changedSeries) {
        // Release the old series first. Repeated mode changes within a series
        // retain this compact cache rather than parsing every retained file.
        history = {}; stamps.clear(); knownWarnings.clear(); seriesKey = std::move(key); parsedRecords = 0;
    }
    history.simulationName = request.simulationName; history.outputSet = request.outputSet;
    if (request.directory.empty()) { history.status = LiveSnapshot::LoadStatus::NoSimulation; return; }
    std::vector<std::filesystem::path> files;
    {
        std::lock_guard lock(notificationMutex);
        files.assign(notifiedExports.begin(), notifiedExports.end()); notifiedExports.clear();
    }
    // Read an existing directory once on series selection. Thereafter only
    // newly committed files need inspection; changing a graph never rescans it.
    bool rescan = changedSeries || request.rescan || history.status != LiveSnapshot::LoadStatus::Ok;
    if (rescan) files = discoverFiles(request);
    if (history.status == LiveSnapshot::LoadStatus::Ok && !files.empty()) refreshRecords(request, files, rescan);
    else if (rescan && history.status == LiveSnapshot::LoadStatus::Ok) {
        history.records.clear(); stamps.clear(); knownWarnings.clear(); history.warnings.clear();
    }
    LiveSnapshot::overlayPartyColours(history.records, request.colours);
}

std::vector<std::filesystem::path> LiveSnapshotViewer::discoverFiles(Request const& request) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    auto iterator = std::filesystem::directory_iterator(request.directory, error);
    if (error) {
        error.clear();
        history.status = std::filesystem::exists(request.directory, error) ?
            LiveSnapshot::LoadStatus::UnreadableFolder : LiveSnapshot::LoadStatus::MissingFolder;
        history.errorMessage = "Could not read snapshot folder: " + LiveResultsInput::pathToUtf8(request.directory);
        return files;
    }
    history.status = LiveSnapshot::LoadStatus::Ok; history.errorMessage.clear();
    for (auto end = std::filesystem::directory_iterator(); iterator != end && !closing; iterator.increment(error)) {
        if (error) break;
        files.push_back(iterator->path());
    }
    if (error) {
        history.status = LiveSnapshot::LoadStatus::UnreadableFolder;
        history.errorMessage = "Could not finish reading the snapshot folder.";
    }
    return files;
}

void LiveSnapshotViewer::refreshRecords(Request const& request,
    std::vector<std::filesystem::path> const& files, bool replaceInventory) {
    std::map<std::string, std::size_t> old;
    for (std::size_t i = 0; i < history.records.size(); ++i) old[history.records[i].filename] = i;
    auto records = replaceInventory ? std::vector<LiveSnapshot::SnapshotRecord>{} : std::move(history.records);
    auto current = replaceInventory ? std::map<std::string, FileStamp>{} : stamps;
    std::set<std::size_t> removed;
    for (auto const& path : files) {
        if (closing) break;
        if (path.parent_path().lexically_normal() != request.directory.lexically_normal()) continue;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) continue;
        auto filename = LiveResultsInput::pathToUtf8(path.filename());
        if (!filename.starts_with("snapshot_") || !filename.ends_with(".json") || filename.ends_with(".analysis.json")) continue;
        FileStamp stamp{std::filesystem::last_write_time(path,error), std::filesystem::file_size(path,error)};
        if (error) continue;
        current[filename] = stamp;
        if (auto found = old.find(filename); found != old.end() && stamps.contains(filename) && stamps.at(filename) == stamp) {
            if (replaceInventory) records.push_back(std::move(history.records[found->second]));
            continue;
        }
        if (stamps.contains(filename) && stamps.at(filename) == stamp && !old.contains(filename)) continue;
        std::string warning;
        ++parsedRecords;
        auto record = LiveSnapshot::readRecord(path, warning);
        if (!record) knownWarnings[filename] = warning;
        else knownWarnings.erase(filename);
        bool accepted = record && (!request.filterSimulationName || record->simulationName.empty() || record->simulationName == request.simulationName);
        if (!accepted) {
            if (!replaceInventory && old.contains(filename)) removed.insert(old.at(filename));
        } else if (!replaceInventory && old.contains(filename)) records[old.at(filename)] = std::move(*record);
        else records.push_back(std::move(*record));
    }
    // A rewritten unusable file must not leave its former accepted row behind.
    for (auto it = removed.rbegin(); it != removed.rend(); ++it) records.erase(records.begin() + *it);
    std::sort(records.begin(), records.end(), [](auto const& left, auto const& right) {
        if (left.snapshotCode != right.snapshotCode) return left.snapshotCode < right.snapshotCode;
        int completed = LiveSnapshot::compareCompletedAt(left.completedAt, right.completedAt);
        return completed ? completed < 0 : left.filename < right.filename;
    });
    history.records = std::move(records); stamps = std::move(current);
    std::erase_if(knownWarnings, [&](auto const& entry) { return !stamps.contains(entry.first); });
    history.warnings.clear();
    for (auto const& [filename, warning] : knownWarnings) history.warnings.push_back({filename,warning});
}

LiveSnapshotViewer::Presentation LiveSnapshotViewer::metadataFor(Request const& request) const {
    Presentation result;
    result.metadata.status = history.status;
    result.metadata.simulationName = history.simulationName;
    result.metadata.outputSet = history.outputSet;
    result.metadata.errorMessage = history.errorMessage;
    result.metadata.warnings = history.warnings;
    result.filename = request.filename;
    bool found = std::any_of(history.records.begin(), history.records.end(), [&](auto const& r) { return r.filename == result.filename; });
    if ((!found || request.followLatest) && !history.records.empty()) result.filename = history.records.back().filename;
    // Run selectors and Node Inspector need metadata, not every seat's values.
    // Only the selected run's small issue summary is copied for inspection.
    for (auto const& record : history.records) {
        LiveSnapshot::SnapshotRecord metadata;
        metadata.filename = record.filename; metadata.snapshotCode = record.snapshotCode;
        metadata.completedAt = record.completedAt; metadata.simulationName = record.simulationName;
        metadata.termCode = record.termCode; metadata.outputSet = record.outputSet;
        metadata.boothCount = record.boothCount; metadata.analysisSeatCount = record.analysisSeatCount;
        metadata.firstBoothSeat = record.firstBoothSeat; metadata.firstBoothName = record.firstBoothName;
        if (record.filename == result.filename && record.document.contains("input_issues"))
            metadata.document["input_issues"] = record.document["input_issues"];
        result.metadata.records.push_back(std::move(metadata));
    }
    return result;
}

void LiveSnapshotViewer::buildView(Request& request, Presentation& result) const {
    using namespace LiveSnapshot;
    auto const& records = history.records;
    bool seatMetric = request.mode == Mode::SeatWinChance || request.mode == Mode::SeatFp;
    bool completion = request.mode == Mode::FpCounted || request.mode == Mode::TppSwingBasis || request.mode == Mode::TcpSwingBasis;
    if (request.mode == Mode::SeatThresholds) result.parties = listSeatThresholdParties(records);
    else if (seatMetric) result.parties = request.mode == Mode::SeatFp ? listSeatFpParties(records) : listSeatWinChanceParties(records);
    if (!result.parties.empty() && std::none_of(result.parties.begin(), result.parties.end(), [&](auto const& p) { return p.partyIndex == request.party; })) {
        if (request.graph) request.allParties = true;
        else request.party = result.parties.front().partyIndex;
    }
    if (seatMetric || completion) {
        result.seats = listSeatWinChanceSeats(records);
        if (std::find(result.seats.begin(), result.seats.end(), request.seat) == result.seats.end())
            request.seat = result.seats.empty() ? "" : result.seats.front();
    }
    result.party = request.party; result.seat = request.seat; result.allParties = request.allParties;
    std::optional<int> graphParty = request.allParties ? std::nullopt : std::optional<int>(request.party);
    switch (request.mode) {
    case Mode::NodeInspector: return;
    case Mode::Parliament: result.view = buildParliamentView(records); break;
    case Mode::SeatExpectations: preparePartySummary(request, result); break;
    case Mode::SeatThresholds: result.view = buildSeatThresholdView(records, request.party); break;
    case Mode::Tpp: result.view = buildTppView(records); break;
    case Mode::VoteShares: preparePartySummary(request, result); break;
    case Mode::Internal2pp: prepareInternal2pp(request, result); break;
    case Mode::SeatWinChance:
        result.view = request.graph ? buildSeatWinChanceGraphView(records, request.seat, graphParty) : buildSeatWinChanceView(records, request.party); break;
    case Mode::SeatFp:
        result.view = request.graph ? buildSeatFpGraphView(records, request.seat, graphParty) : buildSeatFpView(records, request.party); break;
    case Mode::FpCounted: case Mode::TppSwingBasis: case Mode::TcpSwingBasis: {
        auto key = request.mode == Mode::FpCounted ? "seat_fp_completion" : request.mode == Mode::TppSwingBasis ? "seat_tpp_completion" : "seat_tcp_completion";
        result.view = request.graph ? buildSeatCompletionGraphView(records, request.seat, key) : buildSeatCompletionView(records, key); break;
    }
    case Mode::BoothTypeBias: case Mode::VoteTypeBias: {
        auto key = request.mode == Mode::BoothTypeBias ? "booth_type" : "vote_type";
        result.view = request.graph ? buildCategoryStatGraphView(records, key, request.categoryStatistic) :
            buildCategoryStatTableView(records, key, request.groupByType ? CategoryStatGrouping::ByCategory : CategoryStatGrouping::ByStatistic); break;
    }
    }
    result.layout = makeParliamentLayout(result.view);
    if (seatMetric || completion || request.mode == Mode::BoothTypeBias || request.mode == Mode::VoteTypeBias
        || request.mode == Mode::Internal2pp) result.layout.frozenColumns = 1;
}

void LiveSnapshotViewer::preparePartySummary(Request& request, Presentation& result) const {
    result.view = request.mode == Mode::SeatExpectations ?
        LiveSnapshot::buildSeatExpectationView(history.records) : LiveSnapshot::buildVoteShareView(history.records);
    auto const partyKind = request.mode == Mode::SeatExpectations ?
        LiveSnapshot::ParliamentColumn::Kind::PartySeats : LiveSnapshot::ParliamentColumn::Kind::PartyVoteShare;
    // Use the actual plotted columns, including minor parties with no
    // threshold series. Keep table columns intact; only graph visibility changes.
    for (auto const& column : result.view.columns) {
        if (column.kind == partyKind)
            result.parties.push_back({column.partyIndex, column.header, column.colour});
    }
    // A series switch can remove the selected party. Return to the default
    // rather than leaving an empty graph, while preserving valid selections
    // across new snapshots and table/graph switches.
    if (std::none_of(result.parties.begin(), result.parties.end(),
        [&](auto const& party) { return party.partyIndex == request.party; }))
        request.allParties = true;
    result.party = request.party;
    result.allParties = request.allParties;
    if (request.graph && !request.allParties) {
        for (auto& column : result.view.columns)
            column.includeInGraph = column.kind == partyKind
                && column.partyIndex == request.party;
    }
}

void LiveSnapshotViewer::prepareInternal2pp(Request const& request, Presentation& result) const {
    result.view = LiveSnapshot::buildInternal2ppView(history.records);
    // The table keeps every diagnostic. The graph displays one metric at a
    // time because an absolute TPP and a deviation have different origins.
    if (!request.graph) return;
    auto const key = request.internal2ppMetric == Internal2ppMetric::Projected ?
        "projected_2pp" : "raw_2pp_deviation";
    for (auto& column : result.view.columns)
        column.includeInGraph = column.category == key;
}

void LiveSnapshotViewer::buildGraph(Request const& request, Presentation& result) const {
    if (!request.graph) return;
    using namespace LiveSnapshot;
    switch (request.mode) {
    case Mode::NodeInspector: break;
    case Mode::Parliament: result.graph = makeParliamentGraph(result.view); break;
    case Mode::SeatExpectations: result.graph = makeSeatExpectationGraph(result.view); break;
    case Mode::SeatThresholds: result.graph = makeSeatThresholdGraph(result.view); break;
    case Mode::Tpp: result.graph = makeTppGraph(result.view); break;
    case Mode::VoteShares: result.graph = makeVoteShareGraph(result.view); break;
    case Mode::Internal2pp: result.graph = makeInternal2ppGraph(result.view); break;
    case Mode::SeatWinChance: result.graph = makeSeatWinChanceGraph(result.view, !request.allParties); break;
    case Mode::SeatFp: result.graph = makeSeatFpGraph(result.view); break;
    case Mode::FpCounted: case Mode::TppSwingBasis: case Mode::TcpSwingBasis: result.graph = makeCompletionGraph(result.view); break;
    case Mode::BoothTypeBias: case Mode::VoteTypeBias: result.graph = makeCategoryStatGraph(result.view, categoryStatisticInteger(request.categoryStatistic)); break;
    }
}
