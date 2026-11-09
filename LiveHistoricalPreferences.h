#pragma once

#include "LivePartialCount.h"

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>

// Additional PREVIOUS-election Labor/Coalition counts for places whose actual
// TCP involved another candidate. During live preparation these counts can fill
// a missing preference comparison, without treating an independent/Greens TCP
// as TPP. They never supply current votes or establish current count completion.
//
// The maintained CSV keeps the original candidate FP vector beside each TPP.
// Both must describe the same ballots: matching only a place name or total could
// attach preferences to a different batch or a synthetic redistributed account.
// Election construction reads this file once; scenarios keep only the matched
// two-candidate counts and their prepared preference offset, not this index.
namespace LiveHistoricalPreferences {
using Counts = LivePartialCount::Counts;
using Flows = LivePartialCount::Flows;

struct Account {
    Counts fp; // Commission candidate IDs and actual first-preference votes.
    int labor = 0, coalition = 0; // Notional TPP votes on that same population.
};

// The stored scrutiny identifies the Coalition as a whole. A source district
// with one Coalition candidate must use that candidate's mapped Liberal or
// National identity; inventing Liberal primaries in a National-only district
// would incorrectly put the National vote into the minor-party preference pool.
inline Counts mappedTpp(Account const& account, Counts const& mappedFp, int nationals) {
    int coalition = nationals >= 0 && LivePartialCount::value(mappedFp,nationals) > LivePartialCount::value(mappedFp,1)
        ? nationals : 1;
    return {{0,account.labor},{coalition,account.coalition}};
}

using Key = std::tuple<std::string,std::string,int>; // Source seat, booth/declaration, commission ID or VoteType.

struct History {
    std::map<Key,Account> accounts;

    // Ignore explicit zero candidates when comparing populations: some feeds
    // omit their rows. Every positive candidate count must match exactly.
    Account const* find(std::string const& seat, std::string const& kind, int id,
        Counts const& primaries) const {
        auto found = accounts.find({seat,kind,id});
        if (found == accounts.end()) return nullptr;
        Counts positive;
        for (auto const& [candidate,votes] : primaries) {
            if (votes < 0) return nullptr;
            if (votes > 0) positive.emplace(candidate,votes);
        }
        return positive == found->second.fp ? &found->second : nullptr;
    }
};

inline int integer(std::string const& text) {
    std::size_t used = 0;
    long long number = std::stoll(text,&used);
    if (used != text.size() || number < 0 || number > std::numeric_limits<int>::max())
        throw std::runtime_error("Invalid historical preference count or identity.");
    return int(number);
}

inline Account readAccount(std::vector<std::string> const& cells) {
    Account account;
    account.labor = integer(cells[5]);
    account.coalition = integer(cells[6]);
    std::istringstream counts(cells[4]);
    for (std::string count; std::getline(counts,count,';');) {
        auto colon = count.find(':');
        if (colon == std::string::npos)
            throw std::runtime_error("Invalid historical candidate count.");
        int candidate = integer(count.substr(0,colon)), votes = integer(count.substr(colon+1));
        if (candidate <= 0 || votes <= 0 || !account.fp.emplace(candidate,votes).second)
            throw std::runtime_error("Invalid or repeated historical candidate.");
    }
    double total = LivePartialCount::total(account.fp);
    if (total <= 0 || total != double(account.labor)+account.coalition)
        throw std::runtime_error("Historical FP and notional TPP populations differ.");
    return account;
}

inline History readHistory(std::istream& input, std::string const& previousTerm,
    std::string const& currentTerm) {
    History history;
    // A replay may share a directory with later elections. Only its configured
    // comparison term in the same jurisdiction is eligible; never use the
    // latest available table or the current election's eventual results.
    if (previousTerm.size() < 5 || currentTerm.size() < 5 ||
        previousTerm.substr(4) != currentTerm.substr(4) || previousTerm.substr(0,4) >= currentTerm.substr(0,4))
        return history;
    std::string line;
    std::getline(input,line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "term,seat,kind,id,fp,labor,coalition")
        throw std::runtime_error("Invalid historical notional TPP header.");
    while (std::getline(input,line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::istringstream row(line);
        std::vector<std::string> cells;
        for (std::string cell; std::getline(row,cell,',');) cells.push_back(cell);
        if (cells.size() != 7)
            throw std::runtime_error("Invalid historical notional TPP row.");
        if (cells[0] != previousTerm) continue;
        if (cells[1].empty() || (cells[2] != "booth" && cells[2] != "declaration"))
            throw std::runtime_error("Invalid historical preference location.");
        int id = integer(cells[3]);
        if (id == 0 || !history.accounts.emplace(Key{cells[1],cells[2],id},readAccount(cells)).second)
            throw std::runtime_error("Repeated historical preference location.");
    }
    return history;
}

// Fit the old mixture only when the usual historical TCP supplies no usable
// preference correction. Callers retain the established pooled calculation for
// those existing comparisons; replacing its arithmetic is a different change.
//
// Counts use mapped modelling-party IDs. Starting rates already include the
// previous Greens/returning-independent information. We fit only the discrepancy
// left AFTER those priors, rather than adding that same candidate history twice.
// Both major primaries stay fixed. One common shift of the other candidates'
// log odds is chosen to reproduce the old transfers, evaluating each candidate's
// own starting rate with the ACTUAL old vote mix. The returned offset is natural
// log odds, not percent or the analyser's log-odds-times-25 scale.
// This different scale lets a common adjustment vary flows while keeping each
// result between zero and 100%; adding percentage points directly would not.
//
// This assumes the old residual is informative for the next election. It does
// not identify the party responsible or make preference flows certain. Current
// pooled preference evidence and existing conversion uncertainty still apply.
inline std::optional<double> fitOffset(Counts const& fp, Counts const& tpp,
    Flows const& rates, int first, int second) {
    using namespace LivePartialCount;
    if (total(fp) <= 0 || total(fp) != total(tpp)) return {};
    auto transfer = measureTransfer(fp,tpp,first,second);
    if (!transfer) return {};
    double adjustableVotes = 0;
    for (auto const& [candidate,votes] : fp) {
        auto found = rates.find(candidate);
        if (votes < 0 || found == rates.end() || !std::isfinite(found->second)
            || found->second < 0 || found->second > 1) return {};
        if (candidate != first && candidate != second && found->second > 0 && found->second < 1)
            adjustableVotes += votes;
    }
    if (adjustableVotes <= 0) return {}; // No identifiable adjustment, rather than an arbitrary endpoint fit.
    // Use the existing half-vote endpoint allowance: it is negligible for a
    // substantial pool but keeps an all-one-way tiny transfer finite.
    double target = value(fp,first)+transfer->votes*(transfer->received+.5)/(transfer->votes+1);
    double lower = -40, upper = 40;
    for (int step = 0; step < 80; ++step) {
        double middle = (lower+upper)/2;
        if (expectedFirst(fp,rates,middle) < target) lower = middle;
        else upper = middle;
    }
    double offset = (lower+upper)/2;
    if (std::abs(expectedFirst(fp,rates,offset)-target) > 1e-6) return {};
    return offset;
}
}
