#include "../TurnoutModelIO.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using json = nlohmann::json;
double maximumError = 0;
void compare(json const& expected, json const& actual, std::string const& context) {
    if (expected.is_array()) {
        if (!actual.is_array() || expected.size() != actual.size()) throw std::runtime_error("Shape mismatch: "+context);
        for (std::size_t i = 0; i < expected.size(); ++i) compare(expected[i],actual[i],context+"/"+std::to_string(i));
    } else {
        double error = std::abs(expected.get<double>()-actual.get<double>());
        maximumError = std::max(maximumError,error);
        if (!std::isfinite(actual.get<double>()) || error > 1e-5) throw std::runtime_error("Turnout parity mismatch "+context+": "+std::to_string(error));
    }
}
template<class F> void rejects(F action) {
    bool threw = false; try { action(); } catch (std::exception const&) { threw = true; }
    assert(threw);
}
void check(std::filesystem::path const& path) {
    std::ifstream stream(path); json value; stream >> value;
    auto artifact = TurnoutModelIO::read(value);
    // Evaluate in reverse source order as well: an earlier replay must not
    // inherit counts, finalisation or evidence from a later evaluation.
    auto const& cases = value.at("cases");
    for (auto it = cases.rbegin(); it != cases.rend(); ++it) {
        auto const& c = *it; auto units = TurnoutModelIO::units(c.at("units"));
        auto history = TurnoutModelIO::history(c.at("history"));
        std::vector<bool> finalised(artifact.prior.seats.size());
        for (auto const& name : c.at("finalised")) for (std::size_t s = 0; s < finalised.size(); ++s) finalised[s] = finalised[s] || name == artifact.prior.seats[s];
        auto options = artifact.options; options.ppvcFactor = c.at("ppvc_factor");
        auto actual = TurnoutModel::prepare(artifact.prior,units,finalised,history,options);
        auto const& expected = c.at("expected"); std::string label = c.at("name");
        compare(expected.at("broad_totals"),actual.broad.totals,label+"/broad totals");
        compare(expected.at("broad_counts"),actual.broad.counts,label+"/broad categories");
        compare(expected.at("remaining"),actual.broad.remaining,label+"/unit remaining");
        compare(expected.at("own_remaining"),actual.broad.ownRemaining,label+"/own unit remaining");
        compare(expected.at("balanced_remaining_means"),actual.balancedRemainingMeans,label+"/imposed allowance");
        compare(expected.at("allocation_evidence"),actual.allocationStrength,label+"/whole-group evidence");
        compare(expected.at("allocation_weight"),actual.allocationWeight,label+"/allocation progress weight");
        compare(expected.at("totals"),actual.totals,label+"/late totals");
        compare(expected.at("counts"),actual.counts,label+"/late categories");
        compare(expected.at("unit_means"),actual.unitMeans,label+"/unit means");
        compare(expected.at("no_addition_probability"),actual.noAdditionProbability,label+"/no addition probability");
        compare(expected.at("mean_totals"),TurnoutModel::meanTotals(actual),label+"/mixture means");
        std::vector<double> evidence; for (auto const& e : actual.evidence) evidence.push_back(e.strength);
        compare(expected.at("evidence"),evidence,label+"/progress evidence");
        assert(expected.at("components").size() == actual.components.size());
        for (std::size_t k = 0; k < actual.components.size(); ++k) {
            auto const& e = expected.at("components")[k]; auto const& a = actual.components[k];
            assert(e.at("unit") == a.unit);
            compare(e.at("weight"),a.weight,label+"/mixture weight");
            compare(e.at("probabilities").at("previous"),a.previousProbability,label+"/previous probability");
            compare(e.at("probabilities").at("small"),a.smallProbability,label+"/small probability");
            compare(e.at("probabilities").at("batch"),a.batchProbability,label+"/batch probability");
            compare(e.at("small_mean"),a.smallMean,label+"/small branch");
            compare(e.at("batch_mean"),a.batchMean,label+"/batch branch");
            compare(e.at("capacity"),a.capacity,label+"/branch capacity");
            compare(e.at("reference"),a.reference,label+"/branch reference");
            compare(e.at("reference_odds"),a.referenceOdds,label+"/branch reference odds");
            compare(e.at("small_odds"),a.smallOdds,label+"/branch small odds");
            compare(e.at("batch_odds"),a.batchOdds,label+"/branch batch odds");
        }
        auto diagnostic = TurnoutModelIO::diagnostic(artifact,units,actual,c.at("source_time"));
        assert(diagnostic.at("maximum_accounting_error").get<double>() < 1e-6);
        assert(diagnostic.at("minimum_category_addition").get<double>() >= -1e-7);
        // An explicit FP finalisation flag makes the shared zero event certain.
        std::fill(finalised.begin(),finalised.end(),true);
        auto final = TurnoutModel::prepare(artifact.prior,units,finalised,history,options);
        assert(final.components.empty());
        for (double probability : final.noAdditionProbability) assert(probability == 1);
        for (auto const& row : final.totals) for (std::size_t s = 0; s < row.size(); ++s) assert(row[s] == final.broad.counted[s]);
    }
    auto invalid = value; invalid["schema_version"] = 99;
    rejects([&] { TurnoutModelIO::read(invalid); });
    invalid = value; invalid.erase("allocation_model");
    rejects([&] { TurnoutModelIO::read(invalid); });
    invalid = value; invalid["prior"]["totals"][0][0] = -1;
    rejects([&] { TurnoutModelIO::read(invalid); });
    invalid = value; invalid["units"][0]["counted"] = nullptr;
    rejects([&] { TurnoutModelIO::read(invalid); });
    std::cout << path.filename().string() << ": " << cases.size() << " source-backed cases passed.\n";
}
}
int main(int argc, char** argv) {
    try {
        auto start = std::chrono::steady_clock::now();
        auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(".");
        check(root/"tests/fixtures/turnout/2026sa-turnout-v1.json");
        check(root/"tests/fixtures/turnout/2025fed-turnout-v1.json");
        rejects([] { TurnoutModel::sourceHour("2025-02-30T12:00:00"); });
        rejects([] { TurnoutModel::sourceHour("2025-05-03T18:00:00+10:00"); });
        compare(.1,TurnoutModel::reportingFactor(78),"PPVC delay");
        // Optional local full-election checkpoint: normal CI needs only the
        // committed small fixtures and never depends on the archive directory.
        if (argc == 4) {
            auto loadStarted = std::chrono::steady_clock::now();
            auto a = TurnoutModelIO::load(argv[2]);
            std::ifstream input(argv[3]); json checkpoint; input >> checkpoint;
            auto units = TurnoutModelIO::units(checkpoint.at("units"));
            std::vector<bool> finalised(a.prior.seats.size());
            for (auto const& name : checkpoint.at("finalised")) for (std::size_t s = 0; s < finalised.size(); ++s) finalised[s] = finalised[s] || name == a.prior.seats[s];
            auto options = a.options; options.ppvcFactor = checkpoint.at("ppvc_factor");
            auto preparationStarted = std::chrono::steady_clock::now();
            auto result = TurnoutModel::prepare(a.prior,units,finalised,a.history,options);
            double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-preparationStarted).count();
            auto means = TurnoutModel::meanTotals(result);
            compare(checkpoint.at("expected_mean_totals"),means,"full-election mean totals");
            compare(checkpoint.at("expected_unit_means"),result.unitMeans,"full-election unit means");
            std::cout << a.prior.election << ": " << a.prior.seats.size() << " districts, " << a.prior.totals.size()
                << " preparations x " << options.countDraws << " count draws; compute " << seconds
                << " seconds; load and compute " << std::chrono::duration<double>(std::chrono::steady_clock::now()-loadStarted).count() << " seconds.\n";
        }
        std::cout << "Maximum Python/C++ difference: " << maximumError << "; seconds: "
            << std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count() << "\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << "\n"; return 1; }
}
