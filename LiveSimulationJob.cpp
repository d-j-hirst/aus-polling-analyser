#include "LiveSimulationJob.h"
#include "Log.h"

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
bool LiveSimulationJob::finish(std::uint64_t generation) {
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
    if (!input) { context.reset(); output.finished = true; return output; }
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
