// Serial worker for manual snapshot sequences and watched feeds. The GUI owns
// editable settings; this worker owns a detached calculation context. Watching
// adds a durable queue and isolated history around the same calculation path,
// then hands numerical reports to the separate reporting/upload process.
#include "LiveSimulationJob.h"
#include "LiveSnapshotData.h"
#include "Log.h"
#include "LivePreparationBridge.h"
#include "LiveHistoryScope.h"
#include "LiveBoothGeographyInput.h"
#include "LiveNarracanPrimaries.h"
#include "LiveFederalDeclarations.h"
#include "SimulationInputs.h"
#include "ReportUploader.h"
#include "TurnoutModelIO.h"
#include <fstream>

LiveSimulationJob::LiveSimulationJob(PollingProject const& source,
    std::vector<Simulation::Id> ids, std::function<void()> wake, std::function<void()> setupWorker, Runner runner)
    : context(source.createLiveRunContext(ids)), ids(std::move(ids)),
      setupWorker(std::move(setupWorker)),
      runner(runner ? std::move(runner) : Runner([this](PollingProject& project, Simulation& simulation,
          auto feedback, auto actionRequired, Input input) {
          return simulation.run(project, std::move(feedback), std::move(actionRequired), std::move(input),
              [this](std::string message) { recordProgress(std::move(message)); });
      })),
      task([this](TaskInput input) { return calculate(std::move(input)); }, std::move(wake)) {}

bool LiveSimulationJob::submit(std::uint64_t generation, Input input) {
    return !finishing && task.submit(generation, TaskInput(std::in_place, std::move(input)));
}
std::optional<LiveSimulationJob::Completion> LiveSimulationJob::take() {
    return task.take();
}
bool LiveSimulationJob::finish(std::uint64_t generation, std::string state) {
    finishState = std::move(state);
    if (finishing || !task.submit(generation, std::nullopt)) return false;
    finishing = true; return true;
}
void LiveSimulationJob::retire(std::vector<std::shared_ptr<SimulationRun>> runs) {
    std::lock_guard lock(retirementMutex);
    for (auto& run : runs) if (run) retiredRuns.push_back(std::move(run));
}
void LiveSimulationJob::releaseRetiredRuns() {
    std::vector<std::shared_ptr<SimulationRun>> release;
    { std::lock_guard lock(retirementMutex); release.swap(retiredRuns); }
    // Deleting a retained analyser can itself take time in a debug build.
    // Retire it on the worker, after the GUI has adopted detached output.
}
std::string LiveSimulationJob::progress() const {
    std::lock_guard lock(progressMutex);
    return progressMessage;
}
void LiveSimulationJob::recordProgress(std::string message) {
    logger << message << "\n";
    std::lock_guard lock(progressMutex);
    progressMessage = std::move(message);
}

LiveSimulationUpdate LiveSimulationJob::calculate(TaskInput input) {
    if (setupWorker) setupWorker();
    releaseRetiredRuns();
    LiveSimulationUpdate output;
    if (!input) {
        if (automation && automation->status().value("state", "paused") == "armed")
            automation->setState(finishState);
        context.reset(); output.finished = true; return output;
    }
    if (!automationSettings.is_null()) return calculateAutomatic();
    for (auto id : ids) {
        calculateSimulation(id, *input, output);
        if (!output.succeeded) break;
    }
    return output;
}

void LiveSimulationJob::calculateSimulation(Simulation::Id id, Input const& input,
    LiveSimulationUpdate& output) {
    // Each participating simulation follows the ordinary engine path. Capture
    // its detached numerical output only after success; every attempt supplies
    // a diagnostic, and a failed later simulation retains earlier successes.
    auto attempt = runSimulation(id, input);
    auto update = captureSimulationDiagnostic(id, attempt);
    if (attempt.succeeded) captureCompletedSimulation(id, attempt, update, output);
    recordSimulationResult(id, attempt, std::move(update), output);
}

LiveSimulationJob::CalculationAttempt LiveSimulationJob::runSimulation(
    Simulation::Id id, Input const& input) {
    auto& simulation = context->simulations().access(id);
    recordProgress("Running " + simulation.getSettings().name);
    CalculationAttempt attempt;
    attempt.outcomesBefore = context->outcomes().count();
    auto record = [&](std::string message) {
        recordProgress(message);
        if (!attempt.feedback.empty()) attempt.feedback += "\n\n";
        attempt.feedback += message;
    };
    try {
        attempt.succeeded = runner(*context, simulation, record,
            [&](std::string message) { attempt.actionRequired = true; record(std::move(message)); }, input);
        if (attempt.succeeded) attempt.completedAt = Timestamp::now();
    } catch (std::exception const& error) { record(error.what()); }
    catch (...) { record("Unknown failure while calculating the live simulation."); }
    return attempt;
}

LiveSimulationUpdate::SimulationUpdate LiveSimulationJob::captureSimulationDiagnostic(
    Simulation::Id id, CalculationAttempt const& attempt) const {
    auto const& simulation = context->simulations().view(id);
    LiveSimulationUpdate::SimulationUpdate update{id, {},
        simulation.getLiveInputDiagnostic(), simulation.getLiveInputDetails()};
    if (!attempt.succeeded && (!update.diagnostic.is_object() || update.diagnostic.value("completed", true))) {
        // Configuration checks can fail before the input reader creates its
        // diagnostic. Never publish a previous successful attempt's notice as
        // though it described this failed update.
        update.diagnostic = {{"completed", false}, {"issues", nlohmann::json::array()},
                             {"failure", attempt.feedback.empty() ? "The live update failed; previous results are retained." : attempt.feedback}};
        update.details = nlohmann::json::array();
    }
    if (!attempt.succeeded && !attempt.feedback.empty()) {
        // Early setup failures may only have the engine's generic failed-run
        // notice. Keep its issue rows, but expose the concrete reason in Results.
        update.diagnostic["failure"] = attempt.feedback;
    }
    return update;
}

void LiveSimulationJob::captureCompletedSimulation(Simulation::Id id, CalculationAttempt const& attempt,
    LiveSimulationUpdate::SimulationUpdate& update, LiveSimulationUpdate& output) {
    auto& simulation = context->simulations().access(id);
    update.completed = simulation.completedLiveState();
    output.completedAt = attempt.completedAt;
    // Outcomes are prepended. Preserve their current ordering when the GUI
    // prepends this detached delta after the whole snapshot finishes.
    int added = context->outcomes().count() - attempt.outcomesBefore;
    auto it = context->outcomes().begin();
    std::vector<Outcome> changes;
    for (int i = 0; i < added; ++i, ++it) changes.push_back(*it);
    output.outcomes.insert(output.outcomes.begin(), changes.begin(), changes.end());
    if (auto path = simulation.completedSnapshotPath()) output.exports.push_back(*path);
    // Only seat values are needed at publication. Avoid copying trained
    // models or historical outcomes into every completion payload.
    output.seats = context->liveSeatValues();
    // The next snapshot reuses the detached report, baseline and calibration
    // caches. Its completed analyser is no longer needed after export and
    // booth extraction; releasing it here also bounds multiple-simulation jobs.
    auto analysedRun = simulation.releaseRetainedLiveRun();
}

void LiveSimulationJob::recordSimulationResult(Simulation::Id id, CalculationAttempt const& attempt,
    LiveSimulationUpdate::SimulationUpdate update, LiveSimulationUpdate& output) const {
    // Export failures are action-required notices following a successful
    // calculation. Publish that calculation but stop the sequence, just as
    // a numerical failure stops after any preceding successful simulations.
    output.simulations.push_back(std::move(update));
    if (!attempt.succeeded || attempt.actionRequired) {
        output.succeeded = false;
        output.failure = context->simulations().view(id).getSettings().name + ":\n" + attempt.feedback;
    }
}

void LiveSimulationJob::publish(PollingProject& project, LiveSimulationUpdate& update) {
    if (update.seats) project.publishLiveSeats(std::move(*update.seats));
    for (auto& simulation : update.simulations) {
        auto& target = project.simulations().access(simulation.id);
        if (simulation.completed) {
            update.retiredRuns.push_back(target.releaseRetainedLiveRun());
            target.publishLiveState(std::move(*simulation.completed));
        }
        else target.publishLiveDiagnostics(std::move(simulation.diagnostic), std::move(simulation.details));
    }
    for (auto it = update.outcomes.rbegin(); it != update.outcomes.rend(); ++it)
        project.outcomes().add(*it);
}


void LiveSimulationJob::configureAutomation(nlohmann::json settings, bool acknowledgeChange) {
    // Called before the first worker submission. Settings are detached from the
    // editable GUI project, just like every other live calculation input.
    context->setAutomationSettings(settings);
    automationSettings = std::move(settings);
    acknowledgeInputChange = acknowledgeChange;
}

void LiveSimulationJob::writeAutomaticReport(Simulation const& simulation,
    std::string const& forecastId, LiveAutomation::Capture const* capture,
    std::optional<std::filesystem::path> diagnostic, std::optional<Timestamp> completedAt) {
    using nlohmann::json;
    auto completed = completedAt.value_or(Timestamp::now());
    Simulation::SavedReport saved{simulation.getLatestReport(),completed,forecastId};
    auto report = ReportUploader(saved,simulation,*context).serializeReport();
    if (!capture) report.erase("statewidePreferenceFlows");
    auto const& config = automation->configuration();
    json numerical{{"version",1},{"run_id",config.at("run_id")},{"forecast_id",forecastId},
        {"source_time",capture ? json(capture->sourceTime) : json(nullptr)},
        {"delivered_at",capture ? capture->deliveredAt : completed.formatIsoUtc()},
        {"completed_at",completed.formatIsoUtc()},{"input_revision",automation->revision()},
        {"input_fingerprint",automation->revision()},
        {"source_fingerprint",capture ? capture->hash : "pre-count"},
        {"simulation",simulation.getSettings().name},
        {"model_revision",std::string("build ")+__DATE__+" "+__TIME__},
        {"iterations",simulation.getSettings().numIterations},{"origin",capture == nullptr},{"report",std::move(report)}};
    // Diagnostic paths are local worker inputs, removed from the public package.
    if (diagnostic)
        numerical["diagnostic_file"] = LiveResultsInput::pathToUtf8(std::filesystem::absolute(*diagnostic));
    LiveAutomation::publishNumerical(automation->directory(), numerical);
}

std::string LiveSimulationJob::automaticInputRevision(Simulation const& simulation) {
    auto seats = context->seats().importInfo();
    if (!seats) throw std::runtime_error("Could not load the configured seat inputs. Correct the seat-file error before watching.");
    auto inputs = SimulationInputs::capture(*context,simulation,std::move(seats),Date{});
    std::string term = automationSettings.at("election");
    nlohmann::json support = nlohmann::json::object();
    auto fingerprintFile = [&](std::filesystem::path const& path) {
        auto key = LiveResultsInput::pathToUtf8(path);
        if (!std::filesystem::exists(path)) {
            support[key] = nullptr; supportFingerprints.erase(path); return;
        }
        auto stamp = std::to_string(std::filesystem::file_size(path))+"/"+
            std::to_string(std::filesystem::last_write_time(path).time_since_epoch().count());
        auto& cached = supportFingerprints[path];
        if (cached.first != stamp) cached = {stamp,LiveInputRecovery::fingerprint(path)};
        support[key] = cached.second;
    };
    fingerprintFile(TurnoutModelIO::priorPath(context->paths().root(),term));
    if (term == "2026vic") fingerprintFile(TurnoutModelIO::priorPath(context->paths().root(),term).parent_path()/"evc-centres.json");
    if (term == "2026vic") fingerprintFile(LiveNarracanPrimaries::historyPath(context->paths().root()));
    if (auto path = LiveFederalDeclarations::inputPath(context->paths().root(), term))
        fingerprintFile(*path);
    // Victoria uses both inputs for rehearsal and election night. SA shares
    // a declaration prior across replays and selects ordinary geography per run.
    if (auto path = LiveBoothGeography::inputPath(context->paths().root(),term,automationSettings)) {
        if (!std::filesystem::is_regular_file(*path))
            throw std::runtime_error("Missing prepared federal booth geography: "+path->string());
        fingerprintFile(*path);
    }
    // These optional manual live inputs are deliberately outside non-live
    // calibration identity, but must still be fixed within an unattended run.
    auto liveInputs = context->paths().root()/"forecasts"/term/"live-inputs";
    for (auto const* name : {"preference-counts.json","historical-baseline.json"}) fingerprintFile(liveInputs/name);
    for (auto const* name : {"historical-notional-tpp.csv","historical-party-tpp-flows.csv"})
        fingerprintFile(context->paths().root()/"analysis"/"Data"/name);
    fingerprintFile(context->paths().root()/"analysis"/"Live Overrides"/(term+".csv"));
    auto const& settings = simulation.getSettings();
    if (!settings.prevTermCodes.empty())
        fingerprintFile(context->paths().root()/"analysis"/"Booth Results"/(settings.prevTermCodes.front()+".json"));
    if (term.ends_with("fed")) {
        if (auto files = LiveResultsInput::localPreloadFiles(settings.preloadUrl)) {
            fingerprintFile(context->paths().root()/files->results);
            fingerprintFile(context->paths().root()/files->pollingPlaces);
        } else {
            fingerprintFile(context->paths().root()/LiveResultsInput::cachedSupportFile(settings.preloadUrl));
            fingerprintFile(context->paths().root()/LiveResultsInput::cachedSupportFile(settings.preloadUrl,"@polling_places.xml"));
        }
        auto previous = settings.previousResultsUrl.starts_with("local:")
            ? LiveResultsInput::pathFromUtf8(settings.previousResultsUrl.substr(6))
            : LiveResultsInput::cachedSupportFile(settings.previousResultsUrl);
        fingerprintFile(context->paths().root()/previous);
    }
    for (auto const* suffix : {"candidates.xml","booths.xml","zeros.xml","candidates_prev.xml",
        "booths_prev.xml","results_prev.xml","candidates_current.xml","booths_current.xml"})
        fingerprintFile(context->paths().root()/"downloads"/(term+"_"+suffix));
    support["preload_location"] = settings.preloadUrl;
    support["previous_results_location"] = settings.previousResultsUrl;
    support["projection_updated"] = context->projections().view(settings.baseProjection).getLastUpdatedDate().formatIsoUtc();
    return LiveAutomation::digest(inputs->contentIdentity()+support.dump());
}

LiveSimulationUpdate LiveSimulationJob::calculateAutomatic() {
    LiveSimulationUpdate output; output.watching = true;
    try {
        if (ids.size() != 1) throw std::runtime_error("Folder watching requires exactly one automatic live simulation.");
        auto& simulation = context->simulations().access(ids.front());
        LivePreparationBridge::validateAutomaticSetup(*context,simulation,{},true);
        // A saved project supplies the election configuration needed to read
        // counts, even when the generated model or projection is not in memory.
        // Only a new forecast (or this run's first origin) needs both.
        auto const& projection = context->projections().view(simulation.getSettings().baseProjection);
        bool const ready = projection.getBaseModel(context->models()).isReadyForProjection() &&
            projection.getProjectionLength() > 0;
        auto requirePreparedForecast = [&] {
            if (!ready) throw std::runtime_error(
                "Run the base model and projection, then choose Watch / Resume to calculate the pending forecast.");
        };
        if (!automation) {
            automation = std::make_unique<LiveAutomation::Session>(context->paths().root(),automationSettings);
            // Validate required support files outside per-capture error handling:
            // missing configuration must pause, rather than reject every feed.
            auto revision = ready ? std::make_optional(automaticInputRevision(simulation)) : std::nullopt;
            automation->arm(revision,acknowledgeInputChange);
            auto settings = simulation.getSettings();
            settings.liveOutputFolder = automationSettings.at("run_id");
            simulation.replaceSettings(settings);
        }
        if (ready && automaticInputRevision(simulation) != automation->revision())
            throw std::runtime_error("Simulation inputs changed while watching. Pause, review the change, then explicitly Resume.");
        LiveHistoryScope::Scope history(automation->directory()/"history");
        if (automation->needsOrigin()) {
            requirePreparedForecast();
            // This is a separate ordinary pre-count forecast with the SAME
            // iteration count and projection. It is not the smaller internal
            // calibration baseline, and does not bypass the live forecast path.
            recordProgress("Calculating the start-of-counting reference...");
            auto initial = simulation.copyForLiveRun();
            auto settings = initial.getSettings();
            settings.live = Simulation::Settings::Mode::Projection;
            settings.liveOutputFolder.clear();
            initial.replaceSettings(settings);
            // The engine can return false after explaining a missing model,
            // projection or other prerequisite through its feedback callback.
            // Keep that explanation in the pause notice shown on Resume.
            // Routine progress messages use a separate callback.
            std::string feedback;
            auto recordFeedback = [&](std::string message) {
                recordProgress(message);
                if (!feedback.empty()) feedback += "\n";
                feedback += message;
            };
            if (!runner(*context, initial, recordFeedback,
                    [](auto message) { throw std::runtime_error(message); }, {})) {
                std::string failure = "Could not calculate the start-of-counting reference.";
                if (!feedback.empty()) failure += "\n" + feedback;
                throw std::runtime_error(failure);
            }
            writeAutomaticReport(initial,"origin",nullptr);
        }
        recordProgress("Checking for feed updates...");
        auto prior = TurnoutModelIO::loadForElection(context->paths().root(),automationSettings.at("election"));
        auto capture = automation->ingest([&](auto const& file) {
            return LivePreparationBridge::readSnapshot(*context,simulation,file);
        },[&](LiveInputRecovery::Snapshot const& accepted) {
            // The lightweight ingestion path must also retain fresh turnout
            // observations. Otherwise skipped forecasts lose batch timing even
            // though the original candidate votes were successfully retained.
            auto units = prior.prior.units;
            LiveInputRecovery::RecordStatuses statuses;
            std::map<std::string,LiveInputRecovery::Record const*> records;
            for (auto const& record : accepted.records) {
                records[record.key] = &record;
                statuses[record.key] = {record.fp.metadata(),record.tcp.metadata()};
            }
            for (auto& unit : units) {
                auto key = prior.prior.seats.at(unit.seat)+"/"+unit.kind+"/"+unit.name;
                auto found = records.find(key);
                unit.counted = 0;
                if (found != records.end() && found->second->fp.votes)
                    for (auto const& [candidate,votes] : *found->second->fp.votes) unit.counted += votes;
            }
            auto observation = LiveInputRecovery::freshObservation(prior.prior,units,statuses,accepted.sourceTime);
            TurnoutModelIO::recordObservation(automation->directory()/"history"/"turnout",accepted.election,
                accepted.sourceTime,observation,TurnoutModelIO::observationMapping(prior.prior));
        });
        if (capture) {
            requirePreparedForecast();
            LivePreparationBridge::validateAutomaticSetup(*context,simulation,capture->file);
            if (automaticInputRevision(simulation) != automation->revision())
                throw std::runtime_error("Simulation inputs changed while watching. Pause, review the change, then explicitly Resume.");
            auto id = automation->reserveForecast(*capture);
            recordProgress("Forecasting newest accepted feed: "+LiveSnapshot::formatCommissionTime(
                capture->sourceTime, automationSettings.value("timezone",std::string())));
            calculateSimulation(ids.front(),capture->file,output);
            if (automaticInputRevision(simulation) != automation->revision()) {
                output.simulations.clear(); output.outcomes.clear(); output.seats.reset();
                throw std::runtime_error("Simulation inputs changed during the calculation. This result was not published; review and explicitly Resume.");
            }
            if (!output.succeeded && !output.simulations.empty() && output.simulations.back().completed)
                throw std::runtime_error(output.failure); // Calculated but export/storage failed.
            if (output.succeeded) writeAutomaticReport(simulation,id,&*capture,
                output.exports.empty() ? std::nullopt : std::make_optional(output.exports.back()),output.completedAt);
            automation->finishForecast(*capture,output.succeeded,output.failure);
            // A numerical failure is local to this input; the next distinct
            // feed gets another attempt. Export/history failures are caught
            // below and pause rather than silently losing completed outputs.
            output.succeeded = true;
        }
        output.automationStatus = automation->status();
    } catch (std::exception const& error) {
        output.succeeded = false; output.failure = error.what();
        if (automation) {
            try { automation->setState("paused",error.what()); output.automationStatus = automation->status(); }
            catch (...) {} // Keep the original storage error visible to the GUI.
        }
    }
    return output;
}
