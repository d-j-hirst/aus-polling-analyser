#pragma once
#include <utility>

// A run may use its current report and counted outcomes as workspace while it
// prepares an update. Until the final commit, any return or exception restores
// the previous successful state. This helper deliberately performs no disk IO.
template<class State> class RetainedLiveState {
public:
    explicit RetainedLiveState(State& state) : state(state), previous(state) {}
    ~RetainedLiveState() { if (!committed) state = std::move(previous); }
    void commit() noexcept { committed = true; }
    RetainedLiveState(RetainedLiveState const&) = delete;
    RetainedLiveState& operator=(RetainedLiveState const&) = delete;
private:
    State& state;
    State previous;
    bool committed = false;
};
