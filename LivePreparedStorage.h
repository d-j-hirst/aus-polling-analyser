#pragma once

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

// Preparation changes booth projections and needs independent writable copies.
// Once preparation finishes, simulation draws only read those booths. Moving
// them into const shared ownership avoids copying thousands of candidate maps
// for every draw, and keeps diagnostics valid even after the original owner dies.
template<typename T>
class LivePreparedStorage {
public:
    std::vector<T> const& read() const {
        return frozen ? *frozen : preparing;
    }

    std::vector<T>& edit() {
        requirePreparing();
        return preparing;
    }

    void freeze() {
        requirePreparing();
        frozen = std::make_shared<std::vector<T> const>(std::move(preparing));
        // Do not rely on the unspecified contents of a moved-from vector:
        // frozen copies must have no writable elements left to duplicate.
        preparing.clear();
    }

    void requirePreparing() const {
        if (frozen) throw std::logic_error("Live booth preparation has already finished.");
    }

    void requireFrozen() const {
        if (!frozen) throw std::logic_error("Live scenarios require finished booth preparation.");
    }

    // Value copying before freeze deliberately copies scratch data. After
    // freeze it copies only the owner, never the booth elements. Election's
    // named copy paths check which phase their caller requires.
private:
    std::vector<T> preparing;
    std::shared_ptr<std::vector<T> const> frozen;
};
