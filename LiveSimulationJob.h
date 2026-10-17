#pragma once

#include "BackgroundTask.h"
#include "PollingProject.h"

// Worker output contains detached values only. A failed attempt still carries
// diagnostics; preceding successful simulations in the same snapshot remain
// publishable, matching the existing serial live-run behaviour.
struct LiveSimulationUpdate {
    struct SimulationUpdate {
        Simulation::Id id;
        std::optional<Simulation::PublishedLiveState> completed;
        nlohmann::json diagnostic, details;
    };
    std::vector<SimulationUpdate> simulations;
    std::vector<Outcome> outcomes;
    std::vector<std::filesystem::path> exports;
    std::optional<SeatCollection::SeatContainer> seats;
    bool succeeded = true;
    std::string failure;
    bool finished = false;
    std::vector<std::shared_ptr<SimulationRun>> retiredRuns;
};

class LiveSimulationJob {
public:
    using Input = std::optional<LiveResultsInput::CurrentFile>;
    using TaskInput = std::optional<Input>; // Empty outer optional is worker teardown.
    using Completion = BackgroundTask<TaskInput, LiveSimulationUpdate>::Completion;
    // Portable tests supply a controlled calculation; GUI callers use the
    // ordinary simulation engine through exactly the same publication flow.
    using Runner = std::function<bool(PollingProject&, Simulation&, SimulationRun::FeedbackFunc,
        SimulationRun::ActionRequiredFunc, Input)>;
    LiveSimulationJob(PollingProject const& source, std::vector<Simulation::Id> ids,
                      std::function<void()> wake, std::function<void()> setupWorker = {}, Runner runner = {});
    bool submit(std::uint64_t generation, Input input);
    std::optional<Completion> take();
    bool finish(std::uint64_t generation);
    void retire(std::vector<std::shared_ptr<SimulationRun>> runs);
    bool idle() const { return task.idle(); }
    std::string progress() const;
    static void publish(PollingProject& project, LiveSimulationUpdate& update);
private:
    struct CalculationAttempt {
        int outcomesBefore = 0;
        bool succeeded = false, actionRequired = false;
        std::string feedback;
    };
    LiveSimulationUpdate calculate(TaskInput input);
    void releaseRetiredRuns();
    void calculateSimulation(Simulation::Id id, Input const& input, LiveSimulationUpdate& output);
    CalculationAttempt runSimulation(Simulation::Id id, Input const& input);
    LiveSimulationUpdate::SimulationUpdate captureSimulationDiagnostic(
        Simulation::Id id, CalculationAttempt const& attempt) const;
    void captureCompletedSimulation(Simulation::Id id, CalculationAttempt const& attempt,
        LiveSimulationUpdate::SimulationUpdate& update, LiveSimulationUpdate& output);
    void recordSimulationResult(Simulation::Id id, CalculationAttempt const& attempt,
        LiveSimulationUpdate::SimulationUpdate update, LiveSimulationUpdate& output) const;
    void recordProgress(std::string message);
    std::unique_ptr<PollingProject> context;
    std::vector<Simulation::Id> ids;
    bool finishing = false; // Submission and publication belong to the GUI owner.
    mutable std::mutex progressMutex;
    std::string progressMessage;
    std::mutex retirementMutex;
    std::vector<std::shared_ptr<SimulationRun>> retiredRuns;
    std::function<void()> setupWorker;
    Runner runner;
    BackgroundTask<TaskInput, LiveSimulationUpdate> task;
};
