#include "../LivePreparedStorage.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <map>
#include <thread>
#include <type_traits>

namespace {
// Candidate maps make accidental deep copying observable, rather than merely
// checking a reference count. These are fictional counts, not private feeds.
struct Booth {
    std::map<int, float> fp{{0, 300.f}, {1, 250.f}, {2, 150.f}};
    std::map<int, float> tcp{{0, 360.f}, {1, 340.f}};
    static inline std::atomic<int> copies = 0;
    Booth() = default;
    Booth(Booth const& other) : fp(other.fp), tcp(other.tcp) { ++copies; }
};

using Storage = LivePreparedStorage<Booth>;
static_assert(std::is_same_v<decltype(std::declval<Storage&>().read()),
    std::vector<Booth> const&>);

template<typename Action>
void expectLifecycleError(Action action) {
    bool rejected = false;
    try { action(); }
    catch (std::logic_error const&) { rejected = true; }
    assert(rejected);
}

void preparationCopiesStayIndependent() {
    Storage original;
    original.edit().resize(3);
    expectLifecycleError([&] { original.requireFrozen(); });
    Booth::copies = 0;
    auto sample = original;
    assert(Booth::copies == 3);
    sample.edit()[0].fp[0] = 450;
    sample.edit()[1].tcp.clear();
    assert(original.read()[0].fp.at(0) == 300);
    assert(original.read()[1].tcp.size() == 2);
    // Freezing a sample must not freeze its source or a sibling sample.
    sample.freeze();
    original.edit()[2].fp[1] = 400;
    assert(sample.read()[2].fp.at(1) == 250);
}

Storage retainedFrozenStorage() {
    Storage original;
    original.edit().resize(512);
    auto const* address = original.read().data();
    Booth::copies = 0;
    original.freeze();
    assert(Booth::copies == 0);
    assert(original.read().data() == address);
    original.requireFrozen();
    expectLifecycleError([&] { original.edit(); });
    expectLifecycleError([&] { original.freeze(); });
    expectLifecycleError([&] { original.requirePreparing(); });
    auto retained = original;
    assert(retained.read().data() == address);
    return retained;
}

void frozenCopiesShareLifetimeAndAllowConcurrentReads() {
    auto prepared = retainedFrozenStorage();
    // The original has died. Each worker owns its view and private projected
    // values; concurrent reads must not depend on that original object's life.
    auto const* address = prepared.read().data();
    Booth::copies = 0;
    std::vector<std::jthread> workers;
    std::vector<double> totals(8);
    for (int worker = 0; worker < 8; ++worker) {
        workers.emplace_back([&, worker, owned = prepared] {
            for (int iteration = 0; iteration < 125; ++iteration) {
                auto scenario = owned;
                assert(scenario.read().data() == address);
                double total = 0;
                for (auto const& booth : scenario.read()) {
                    auto projected = booth.fp;
                    projected[0] += float(worker);
                    total += projected.at(0);
                }
                totals[worker] = total;
            }
        });
    }
    workers.clear(); // Join before inspecting worker outputs.
    assert(Booth::copies == 0);
    for (int worker = 0; worker < 8; ++worker)
        assert(totals[worker] == 512.0 * (300 + worker));
    assert(prepared.read()[0].fp.at(0) == 300);
}

void emptyStorageAlsoHasAnExplicitPhase() {
    Storage empty;
    empty.freeze();
    auto scenario = empty;
    scenario.requireFrozen();
    assert(scenario.read().empty());
    expectLifecycleError([&] { scenario.edit(); });
}
}

int main() {
    preparationCopiesStayIndependent();
    frozenCopiesShareLifetimeAndAllowConcurrentReads();
    emptyStorageAlsoHasAnExplicitPhase();
    std::cout << "Prepared storage: independent scratch data, owned frozen views, "
        "and 1,000 concurrent scenario copies without booth copies passed.\n";
}
