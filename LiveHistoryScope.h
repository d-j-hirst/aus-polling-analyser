#pragma once

#include <filesystem>
#include <utility>

// An automated run has its own accepted-count and turnout history. The scope
// belongs to the serial preparation worker, never to simulation iteration
// threads. Ordinary manual runs retain their existing election-wide locations.
// Using one scope for both readers prevents a replay from borrowing future
// turnout observations while correctly isolating its candidate-count history.
namespace LiveHistoryScope {
inline thread_local std::filesystem::path active;
class Scope {
public:
    explicit Scope(std::filesystem::path directory) : previous(std::exchange(active, std::move(directory))) {}
    ~Scope() { active = std::move(previous); }
    Scope(Scope const&) = delete;
    Scope& operator=(Scope const&) = delete;
private:
    std::filesystem::path previous;
};
}
