#pragma once

// Optional, unrounded evidence for comparing two independently executed main
// forecasts. Each record describes one successful simulated election, identified
// by its iteration and retry numbers, before the report combines many draws.
// This avoids hiding a rare seat change behind rounded probabilities or averages.
// It is diagnostic output only: neither draws nor the live baseline read it.

#include "json.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>

namespace SimulationAudit {
class Writer {
public:
    Writer(std::filesystem::path const& directory, nlohmann::json metadata) {
        std::filesystem::create_directories(directory);
        auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        // Claim a new subdirectory: repeated runs must never overwrite evidence
        // from the comparison run, even when their simulation names are equal.
        for (int suffix = 0; ; ++suffix) {
            auto folder = directory / (std::to_string(stamp) + "-" + std::to_string(suffix));
            if (std::filesystem::create_directory(folder)) {
                path = folder / "iterations.jsonl";
                break;
            }
        }
        output.open(path, std::ios::binary);
        metadata["schema"] = 1;
        metadata["kind"] = "metadata";
        write(metadata);
    }
    void record(nlohmann::json const& iteration) {
        std::lock_guard<std::mutex> guard(mutex);
        write(iteration);
    }
    void finish(int iterations) {
        write({{"kind", "complete"}, {"iterations", iterations}});
        output.flush();
        output.close();
        if (!output) throw std::runtime_error("Could not finish simulation precision audit.");
    }
    std::filesystem::path const& filename() const { return path; }
private:
    void write(nlohmann::json const& record) {
        // json.h writes doubles with enough digits to recover their exact
        // binary values. Do not use the rounded live-analysis archive here.
        output << record.dump() << '\n';
        if (!output) throw std::runtime_error("Could not write simulation precision audit.");
    }
    std::filesystem::path path;
    std::ofstream output;
    std::mutex mutex;
};
}
