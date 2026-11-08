#include "../LiveVariability.h"
#include "../Config.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using LiveVariability::Counts;

namespace {
Counts sumBooths(std::vector<Counts> const& booths) {
    Counts result;
    for (auto const& booth : booths)
        LiveVariability::addProjectedVotes(result, booth, "Fictional booth");
    return result;
}

// Reference the former per-sample calculation, independently of the cache.
// Compare exact doubles, including zero-party skips and the original RMS order.
Counts oldDifferences(std::vector<Counts> const& booths, Counts const& varied) {
    auto base = sumBooths(booths);
    double baseTotal = LiveVariability::positiveVoteTotal(base, "base");
    double variedTotal = LiveVariability::positiveVoteTotal(varied, "varied");
    Counts result;
    for (auto const& [party, votes] : base) {
        assert(varied.contains(party));
        if (votes == 0 || varied.at(party) == 0) continue;
        result[party] = transformVoteShare(varied.at(party) / variedTotal * 100.0)
            - transformVoteShare(votes / baseTotal * 100.0);
    }
    return result;
}

void compareCachedSamples() {
    // Fictional fractional projections exercise the summation order; using a
    // different seat aggregate or changing working precision would fail equality.
    std::vector<Counts> booths{{{0,123.456},{1,140.123},{100005,32.34},{8,0}},
        {{0,48321.125},{1,50032.4375},{100005,9555.111},{8,0}},
        {{0,.000123},{1,.000456},{100005,.000345},{8,0}}};
    auto totals = sumBooths(booths);
    Counts pair{{1,1234.56},{100005,1123.45}};
    Counts tpp{{0,6789.125},{1,4567.875}};
    auto fast = LiveVariability::prepareBaseline(totals, pair, {}, "fast");
    auto diagnostic = LiveVariability::prepareBaseline(totals, pair, tpp, "diagnostic");
    assert(!fast.tpp && diagnostic.tpp);
    assert(fast.fpTransformed == diagnostic.fpTransformed && fast.tcp == diagnostic.tcp);
    std::map<int, std::vector<double>> before, after;
    for (int i = 0; i < 288; ++i) {
        Counts varied = totals;
        varied[0] *= 1 + double(i % 11) * .013;
        varied[1] *= 1 - double(i % 7) * .017;
        if (i % 13 == 0) varied[100005] = 0;
        auto expected = oldDifferences(booths, varied);
        auto actual = LiveVariability::fpDifferences(fast, varied, "sample");
        assert(expected == actual);
        assert(actual == LiveVariability::fpDifferences(diagnostic, varied, "diagnostic sample"));
        for (auto const& [party, value] : expected) before[party].push_back(value);
        for (auto const& [party, value] : actual) after[party].push_back(value);
        Counts variedPair{{1,1000 + double(i)},{100005,1400 - double(i)}};
        double expectedPair = transformVoteShare(variedPair.at(1) /
            LiveVariability::positiveVoteTotal(variedPair,"varied pair") * 100.0)
            - transformVoteShare(pair.at(1) /
                LiveVariability::positiveVoteTotal(pair,"base pair") * 100.0);
        assert(expectedPair == LiveVariability::pairDifference(variedPair,
            fast.tcp->first,fast.tcp->second,"pair"));
    }
    for (auto& [party, values] : before) {
        std::reverse(after[party].begin(), after[party].end());
        assert(LiveVariability::deterministicRootMeanSquare(values, "old") ==
            LiveVariability::deterministicRootMeanSquare(after[party], "cached"));
    }
}

template<class Action> void rejects(Action action) {
    bool threw = false;
    try { action(); } catch (std::runtime_error const&) { threw = true; }
    assert(threw);
}

void checkMissingAndInvalidAccounts() {
    auto base = LiveVariability::prepareBaseline({{0,60},{1,40},{2,0}}, {{0,100}}, {}, "base");
    assert(!base.tcp && !base.tpp); // A single surviving identity is not a pair.
    assert(!LiveVariability::fpDifferences(base,{{0,65},{1,35},{2,0}},"zero").contains(2));
    rejects([&] { LiveVariability::fpDifferences(base,{{0,65},{1,35}},"missing zero party"); });
    rejects([&] { LiveVariability::fpDifferences(base,{{0,101},{1,-1},{2,0}},"negative"); });
    rejects([&] { LiveVariability::fpDifferences(base,{{0,0},{1,0},{2,0}},"empty"); });
    rejects([&] { LiveVariability::prepareBaseline({{0,INFINITY},{1,40}}, {}, {}, "nonfinite"); });
    // Keep the old skip before transforming a baseline endpoint if that party
    // has no varied votes. If it still has votes, reject the invalid comparison.
    auto endpoint = LiveVariability::prepareBaseline({{0,100},{1,0}}, {}, {}, "endpoint");
    assert(LiveVariability::fpDifferences(endpoint,{{0,0},{1,100}},"skipped").empty());
    rejects([&] { LiveVariability::fpDifferences(endpoint,{{0,80},{1,20}},"endpoint"); });
}

void checkConfigDefaultsAndOptIns() {
    auto file = std::filesystem::temp_directory_path() / "pa-live-variability-options-test.cfg";
    std::ofstream(file) << "iSimulationThreads=4\n";
    Config ordinary(file.string());
    assert(!ordinary.getLiveDetailedLogging() && !ordinary.getLiveLegacyTppDiagnostic());
    assert(ordinary.getSimulationAuditDirectory().empty());
    std::ofstream(file) << "bLiveDetailedLogging=1\nbLiveLegacyTppDiagnostic=0\n";
    Config dump(file.string());
    assert(dump.getLiveDetailedLogging() && !dump.getLiveLegacyTppDiagnostic());
    std::ofstream(file) << "bLiveDetailedLogging=0\nbLiveLegacyTppDiagnostic=1\nsSimulationAuditDirectory=downloads/simulation-audits\r\n";
    Config diagnostic(file.string());
    assert(!diagnostic.getLiveDetailedLogging() && diagnostic.getLiveLegacyTppDiagnostic());
    assert(diagnostic.getSimulationAuditDirectory() == "downloads/simulation-audits");
    std::filesystem::remove(file);
}
}

int main() {
    compareCachedSamples();
    checkMissingAndInvalidAccounts();
    checkConfigDefaultsAndOptIns();
    std::cout << "Cached variability matches all 288 reference samples and RMS; diagnostic options passed.\n";
}
