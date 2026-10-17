#pragma once

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

// A bounded mailbox for one worker. Calculations require the GUI to consume a
// completion before accepting another input; viewer requests may replace an
// older queued selection. Neither mode retains a growing queue of reports.
template<class Request, class Result>
class BackgroundTask {
public:
    struct Completion {
        std::uint64_t generation = 0;
        std::optional<Result> result;
        std::exception_ptr error;
    };
    using Work = std::function<Result(Request)>;
    BackgroundTask(Work work, std::function<void()> wake, bool coalesce = false)
        : work(std::move(work)), wake(std::move(wake)), coalesce(coalesce),
          worker([this] { execute(); }) {}
    ~BackgroundTask() { shutdown(); }
    BackgroundTask(BackgroundTask const&) = delete;
    BackgroundTask& operator=(BackgroundTask const&) = delete;

    bool submit(std::uint64_t generation, Request request) {
        std::lock_guard lock(mutex);
        if (closing || (!coalesce && (processing || pending || completed))) return false;
        pending = Input{generation, std::move(request)};
        ready.notify_one();
        return true;
    }
    std::optional<Completion> take() {
        std::lock_guard lock(mutex);
        auto result = std::move(completed);
        completed.reset();
        return result;
    }
    bool idle() const {
        std::lock_guard lock(mutex);
        return !processing && !pending;
    }
    void shutdown() {
        {
            std::lock_guard lock(mutex);
            closing = true;
            pending.reset();
        }
        ready.notify_one();
        if (worker.joinable()) worker.join();
    }
private:
    struct Input { std::uint64_t generation; Request request; };
    void execute() {
        for (;;) {
            std::optional<Input> input;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [this] { return closing || pending.has_value(); });
                if (closing) return;
                input = std::move(pending); pending.reset(); processing = true;
            }
            Completion output;
            output.generation = input->generation;
            try { output.result = work(std::move(input->request)); }
            catch (...) { output.error = std::current_exception(); }
            bool notify = false;
            {
                std::lock_guard lock(mutex);
                processing = false;
                if (closing) return;
                notify = !completed.has_value();
                completed = std::move(output);
            }
            // The owner stays alive until shutdown joins this thread. Wake
            // merely queues an event; GUI publication happens after take().
            if (notify) wake();
        }
    }
    Work work;
    std::function<void()> wake;
    bool coalesce;
    mutable std::mutex mutex;
    std::condition_variable ready;
    bool closing = false, processing = false;
    std::optional<Input> pending;
    std::optional<Completion> completed;
    std::thread worker;
};
