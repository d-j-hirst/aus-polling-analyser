// GUI sources may include Windows RPC headers before the turnout headers.
// Reproduce their `small` macro without making this portable test depend on the SDK.
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>
#define small char
#include "../WaEarlyCounting.h"
#undef small

#include "../TurnoutModelIO.h"
#include "../DeclarationAllocation.h"
#include "../LiveTurnoutMath.h"
#include "../LiveTurnout.h"
#include "../LivePreferenceCorrections.h"
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
void checkRevisionRounding() {
    // A late district can receive corrections from several reporting units.
    // Updating its float projection by each fraction separately used to drift
    // below the double counted account, even with no outstanding voters.
    for (double amount : {.15, -.15, .301732, -.301732}) {
        for (double coverage : {0., .5, 1.}) {
            LiveCountRevisions::Counts counted{{0,46000}};
            std::map<int,float> projected{{0,47000}};
            double expectedCount = 46000, expectedTotal = 47000;
            for (int i = 0; i < 100; ++i) {
                LiveCountRevisions::apply(counted,projected,{{0,1}},coverage,amount);
                expectedCount += amount*coverage; expectedTotal += amount;
                assert(std::abs(counted.at(0)-expectedCount) < 1e-8);
                assert(std::abs(projected.at(0)-expectedTotal) < .2);
                LiveTurnoutMath::remaining(projected.at(0),counted.at(0));
            }
        }
        LiveCountRevisions::Counts counted{{0,46000},{1,20000}};
        std::map<int,float> projected{{0,46000},{1,20000}};
        for (int i = 0; i < 100; ++i) {
            LivePreferenceCorrections::transfer(counted,projected,0,1,amount);
            LiveCountRevisions::apply(counted,projected,{{0,1}},1,amount);
            double total = double(projected.at(0))+projected.at(1);
            auto progress = LiveTurnoutMath::completion(LiveCountRevisions::total(counted),total);
            assert(progress <= 1);
            for (auto const& [party,votes] : counted)
                LiveTurnoutMath::remaining(projected.at(party),votes);
            auto shares = LiveTurnoutMath::reconcileForecast({counted,projected},{{0,10},{1,90}});
            assert(std::abs(shares.at(0)-100*counted.at(0)/LiveCountRevisions::total(counted)) < .001);
        }
    }
    // Several candidate accounts can become one anonymous Others group. Sum
    // their float inputs in double precision before storing the group once.
    LiveCountRevisions::Counts counted;
    std::map<int,float> projected;
    for (int party = 0; party < 20; ++party) {
        counted[party] = 2300.15; projected[party] = float(counted.at(party));
    }
    auto grouped = LiveTurnoutMath::partitionAccount({&counted,&projected},[](int) { return -1; });
    LiveTurnoutMath::remaining(grouped.projected.at(-1),grouped.counted.at(-1));
    rejects([] {
        LiveCountRevisions::Counts observed{{0,46000}};
        std::map<int,float> invalid{{0,45999}};
        LiveCountRevisions::apply(observed,invalid,{{0,1}},1,.15);
    });
}

void checkWaEarlyBatches() {
    using namespace WaEarlyCounting;
    auto close = TurnoutModel::sourceHour("2025-03-08T18:00:00");
    auto night = close+4, noon = close+18, next = close+30;
    auto small = prepare(454,{454},night,close,false);
    auto large = prepare(7761,{7761},night,close,false);
    assert(small.smallOnly > .85 && large.largeOnly > .84 && large.both > .14);
    assert(prepare(7761,{7761},noon,close,false).both > large.both);
    assert(prepare(7761,{7761},next,close,false).both == 1);
    assert(substantialBatches({454,7548,7569}) == 2);
    assert(substantialBatches({11923,13932,13939}) == 2);
    assert(substantialBatches({8594,7969,7962}) == 1);
    assert(substantialBatches({232,0,4479,0,4479}) == 2);
    assert(substantialBatches({0,0}) == 0);
    assert(prepare(454,{454},night,{},false).smallOnly == small.smallOnly);

    TurnoutModel::Prior prior;
    prior.election = "2025wa"; prior.seats = {"Example"}; prior.subdivisions = {"X"};
    prior.groups = {"ordinary","early"}; prior.enrolment = {20000};
    for (int i = 0; i < 128; ++i) {
        double early = 9000+20*i;
        prior.totals.push_back({early+5000}); prior.counts.push_back({5000,early});
    }
    TurnoutModel::Unit day, early;
    day.name = day.category = "Ordinary"; day.kind = "ordinary"; day.weight = 1; day.counted = 4000;
    early.name = "Early centre"; early.category = "Early"; early.kind = "ppvc";
    early.group = 1; early.weight = 1; early.incremental = true; early.counted = 7548;
    early.receivedFpTotals = {454,7548};
    prior.units = {day,early};
    TurnoutModel::Options options; options.pollClose = close;
    std::vector<TurnoutModel::Observation> history{{night,{}}};
    auto result = TurnoutModel::prepare(prior,prior.units,{false},history,options);
    assert(result.waEarlyPlaces.size() == 1 && result.waEarlyPlaces[0].state.batches == 2);
    compare(7548,result.unitMeans[1],"WA completed routine FP after two batches");
    auto plan = TurnoutModel::makeDrawPlan(result,prior.units,prior.enrolment);
    for (int i = 0; i < 100; ++i) {
        auto counts = TurnoutModel::drawUnitCounts(result,plan,i);
        compare(7548,counts[1],"WA two-batch draw"); compare(4000,counts[0],"unchanged ordinary count");
    }
    // A genuinely later revision becomes the new accepted count, not a third
    // expected routine batch. Other jurisdictions retain the old treatment.
    prior.units[1].counted = 7962;
    compare(7962,TurnoutModel::prepare(prior,prior.units,{false},history,options).unitMeans[1],"accept later actual revision");
    prior.election = "2024qld";
    auto other = TurnoutModel::prepare(prior,prior.units,{false},history,options);
    assert(other.waEarlyPlaces.empty() && other.unitMeans[1] > 7962);
    prior.election = "2025wa"; prior.units[1].counted = 0; prior.units[1].receivedFpTotals.clear();
    history[0].hour = next;
    auto unreported = TurnoutModel::prepare(prior,prior.units,{false},history,options);
    assert(unreported.waEarlyPlaces.empty() && unreported.unitMeans[1] > 0);

    // Main draw means and diagnostic means use the same one-batch mixture.
    prior.units[1].counted = 7000; prior.units[1].receivedFpTotals = {7000}; history[0].hour = night;
    auto mixed = TurnoutModel::prepare(prior,prior.units,{false},history,options);
    auto mixedPlan = TurnoutModel::makeDrawPlan(mixed,prior.units,prior.enrolment);
    double mean = 0;
    for (int i = 0; i < 50000; ++i) mean += TurnoutModel::drawUnitCounts(mixed,mixedPlan,i)[1]/50000;
    assert(std::abs(mean-mixed.unitMeans[1]) < 15);
    compare(mixed.unitMeans[0]+mixed.unitMeans[1],TurnoutModel::meanTotals(mixed)[0],"WA total accounting");
}

void checkSignedCountRevisions() {
    TurnoutModelIO::Artifact artifact;
    auto& prior = artifact.prior;
    prior.election = "fictional"; prior.seats = {"Example"}; prior.subdivisions = {"X"};
    prior.groups = {"ordinary"}; prior.enrolment = {20000};
    prior.totals = {{9000},{10000}}; prior.counts = {{9000},{10000}};
    TurnoutModel::Unit unit; unit.name = unit.category = "Booth"; unit.kind = "ordinary";
    unit.weight = 1; unit.counted = 8000; prior.units = {unit};
    auto counts = LiveTurnout::Prepared::prepare(artifact,prior.units,{0},1,{false},{},"2025-03-09T10:00:00");
    for (double coverage : {0.,.5,1.}) {
        LiveTurnout::UnitComposition u; u.fp = {{0,4000},{1,3000},{2,1000}};
        u.tpp = {{0,4800*coverage},{1,3200*coverage}}; u.tcp = {{0,4800*coverage},{1,3200*coverage}};
        if (coverage == 0) { u.tpp.clear(); u.tcp.clear(); }
        u.projectedFp = {{0,4000},{1,3000},{2,1000}}; u.projectedTpp = {{0,4800},{1,3200}}; u.projectedTcp = u.projectedTpp;
        LiveTurnout::SeatComposition s; s.name = "Example"; s.fp = u.fp; s.tpp = u.tpp; s.tcp = u.tcp;
        s.projectedFp = u.projectedFp; s.projectedTpp = u.projectedTpp; s.projectedTcp = u.projectedTcp;
        auto ready = counts->withComposition({u},{s});
        int unchanged = 0, negative = 0, positive = 0; double meanChange = 0;
        auto preferenceRecheck = [&](auto& projected, auto& observed) {
            if (coverage == 0) return;
            // Existing preference-only rechecks precede ballot revisions.
            // Retain their transfer in both accounts, rather than replacing
            // their corrected floor with the original reported pair.
            LivePreferenceCorrections::transfer(observed.tpp[0],projected[0].tpp,0,1,-100);
            LivePreferenceCorrections::transfer(observed.tcp[0],projected[0].tcp,0,1,-100);
        };
        for (int i = 0; i < 50000; ++i) {
            auto projected = ready->draw(i); auto observed = ready->scenarioCounts();
            preferenceRecheck(projected,observed);
            ready->revise(i,projected,observed);
            auto repeat = ready->draw(i); auto repeatObserved = ready->scenarioCounts();
            preferenceRecheck(repeat,repeatObserved);
            ready->revise(i,repeat,repeatObserved);
            assert(projected[0].fp == repeat[0].fp && observed.fp == repeatObserved.fp);
            double fp = 0, tpp = 0, tcp = 0;
            for (auto const& [p,v] : projected[0].fp) { assert(v >= -1e-6); fp += v; }
            for (auto const& [p,v] : projected[0].tpp) {
                assert(v >= -1e-6); tpp += v;
                assert(v+1e-3 >= LiveCountRevisions::get(observed.tpp[0],p));
            }
            for (auto const& [p,v] : projected[0].tcp) tcp += v;
            assert(std::abs(fp-tpp) < .003 && std::abs(fp-tcp) < .003);
            assert(std::abs(fp-LiveCountRevisions::total(observed.fp[0])) < .003);
            double change = fp-8000;
            unchanged += change == 0; negative += change < 0; positive += change > 0; meanChange += change/50000;
            if (change == 0 && coverage > 0) assert(projected[0].tpp.at(0) == 4700);
            if (coverage == 0) assert(observed.tpp[0].empty());
            if (coverage == 1) {
                auto finalShares = LiveTurnoutMath::reconcileForecast({observed.tpp[0],projected[0].tpp},{{0,80},{1,20}});
                assert(std::abs(finalShares.at(0)-100*projected[0].tpp.at(0)/tpp) < 1e-4);
            }
        }
        assert(unchanged > 44000 && unchanged < 46000 && negative > 2200 && positive > 2200);
        assert(std::abs(meanChange) < 3);
        assert(ready->counted(LiveData::CountKind::Fp,0) == u.fp);
    }
    // A candidate-specific error can be concentrated in one primary rather
    // than proportionally scaling the booth. Coalition aliases retain NAT's
    // votes in the classic pair; a genuine zero account has no revision.
    auto alias = LiveCountRevisions::prepare({{0,4000},{9,3000},{2,1000}},{{0,4800},{1,3200}},{},{{0,4800},{1,3200}},{},{{9,1}});
    auto nat = std::find_if(alias.directions.begin(),alias.directions.end(),[](auto const& d) { return d.candidate == 9; });
    assert(nat != alias.directions.end() && nat->tpp == LiveCountRevisions::Weights({{1,1}}));
    assert(!LiveCountRevisions::draw(LiveCountRevisions::prepare({},{},{},{},{}),1).direction);
    // Actual pre-correction FP vectors from the two inspected WA centres.
    // Conditional size bounds for tiny candidates must not eliminate the
    // observed +510 and -632 total revisions from the overall central 99%.
    for (auto const& fp : std::vector<LiveCountRevisions::Counts>{
        {{0,3502},{1,2902},{2,68},{3,141},{4,43},{5,352},{6,124},{7,217},{8,101}},
        {{0,4603},{1,2071},{2,688},{3,483},{4,136},{5,613}}}) {
        auto distribution = LiveCountRevisions::prepare(fp,{},{},{},{},{},LiveCountRevisions::MixedEarlyRelativeSd);
        std::vector<double> revisions;
        for (int i = 0; i < 300000; ++i) revisions.push_back(LiveCountRevisions::draw(distribution,i).amount);
        std::sort(revisions.begin(),revisions.end());
        double lower = revisions[1500], upper = revisions[298499];
        double total = LiveCountRevisions::total(fp);
        assert(total == 7450 ? upper > 510 : lower < -632);
        std::cout << "Revision total " << total << ": central 99% changes " << lower << " to " << upper << " votes.\n";
    }
}

void checkOperationalInputs(std::filesystem::path const& root) {
    // Switching projects selects only that project's required prior. There is
    // no cached selection, optional mode or legacy sizing fallback.
    auto workspace = std::filesystem::temp_directory_path()/
        ("turnout-inputs-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::ifstream fixture(root/"tests/fixtures/turnout/synthetic-turnout-v1.json");
    json value; fixture >> value;
    // A postponed contest has no November count account. Its forecast seat
    // remains available, but even a small reported count requires reviewing
    // the explicit configuration rather than silently discarding that count.
    value["inactive_contests"] = {{{"seat","Example Postponed"}, {"status","postponed"}, {"reason","Polling was postponed."}}};
    auto artifact = TurnoutModelIO::read(value);
    auto legacy = value; legacy["count_model"] = "live-counts-6";
    compare(artifact.prior.counts,TurnoutModelIO::read(legacy).prior.counts,"existing frozen count inputs");
    legacy["count_model"] = "live-counts-7";
    compare(artifact.prior.counts,TurnoutModelIO::read(legacy).prior.counts,"version 7 frozen count inputs");
    for (auto version : {"live-counts-8","live-counts-9","live-counts-10","live-counts-11","live-counts-12","live-counts-13","live-counts-14"}) {
        legacy["count_model"] = version;
        compare(artifact.prior.counts,TurnoutModelIO::read(legacy).prior.counts,"unchanged frozen prior format");
    }
    legacy["count_model"] = "live-counts-unknown";
    rejects([&] { TurnoutModelIO::read(legacy); });
    std::map<std::string,double> population;
    for (auto const& name : artifact.prior.seats) population[name] = 0;
    TurnoutModelIO::validateDistrictPopulation(artifact,population);
    population["Example Postponed"] = 0;
    TurnoutModelIO::validateDistrictPopulation(artifact,population);
    population["Example Postponed"] = 1;
    rejects([&] { TurnoutModelIO::validateDistrictPopulation(artifact,population); });
    population.erase("Example Postponed");
    population.erase(artifact.prior.seats.front());
    rejects([&] { TurnoutModelIO::validateDistrictPopulation(artifact,population); });
    auto overlapping = value;
    overlapping["inactive_contests"][0]["seat"] = artifact.prior.seats.front();
    rejects([&] { TurnoutModelIO::read(overlapping); });
    value.erase("inactive_contests");
    for (std::string code : {"2026sa","2025fed"}) {
        value["election"] = code;
        auto path = TurnoutModelIO::priorPath(workspace,code);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path); output << value;
    }
    for (std::string code : {"2025fed","2026sa","2025fed"})
        assert(TurnoutModelIO::loadForElection(workspace,code).prior.election == code);
    rejects([&] { TurnoutModelIO::loadForElection(workspace,"2026vic"); });
    value["election"] = "2025fed";
    { std::ofstream output(TurnoutModelIO::priorPath(workspace,"2026sa")); output << value; }
    rejects([&] { TurnoutModelIO::loadForElection(workspace,"2026sa"); });

    // This cache starts empty during a real election, retains source-time
    // revisions and hides later sources when replaying an earlier snapshot.
    auto directory = TurnoutModelIO::historyDirectory(workspace,"2025fed");
    assert(TurnoutModelIO::loadHistory(directory,"2025fed","2025-05-04T10:00:00","example-mapping").empty());
    auto record = [&](std::string stamp, double count) {
        TurnoutModel::Observation observation;
        observation.hour = TurnoutModel::sourceHour(stamp);
        observation.counts[{"Example East","Postal"}] = count;
        TurnoutModelIO::recordObservation(directory,"2025fed",stamp,observation,"example-mapping");
    };
    record("2025-05-05T10:00:00",500);
    record("2025-05-03T20:00:00",200);
    record("2025-05-04T10:00:00",300);
    record("2025-05-03T20:00:00",190);
    auto earlier = TurnoutModelIO::loadHistory(directory,"2025fed","2025-05-04T10:00:00","example-mapping");
    assert(earlier.size() == 1 && earlier.front().counts.at({"Example East","Postal"}) == 190);
    auto all = TurnoutModelIO::loadHistory(directory,"2025fed","2025-05-06T10:00:00","example-mapping");
    assert(all.size() == 3 && all[0].hour < all[1].hour && all[1].hour < all[2].hour);
    rejects([&] { TurnoutModelIO::loadHistory(directory,"2026sa","2025-05-06T10:00:00","example-mapping"); });
    rejects([&] { TurnoutModelIO::loadHistory(directory,"2025fed","2025-05-06T10:00:00","changed-mapping"); });
    auto prior = TurnoutModelIO::read(value).prior;
    auto mapping = TurnoutModelIO::observationMapping(prior);
    std::reverse(prior.units.begin(),prior.units.end());
    assert(TurnoutModelIO::observationMapping(prior) == mapping);
    prior.units.front().group = (prior.units.front().group+1)%prior.groups.size();
    assert(TurnoutModelIO::observationMapping(prior) != mapping);
    // Remove only files created by this test, inside its unique workspace.
    for (auto const& entry : std::filesystem::directory_iterator(directory)) std::filesystem::remove(entry.path());
    for (auto path : {directory,directory.parent_path()}) std::filesystem::remove(path);
    for (std::string code : {"2026sa","2025fed"}) {
        auto path = TurnoutModelIO::priorPath(workspace,code);
        std::filesystem::remove(path); std::filesystem::remove(path.parent_path());
        std::filesystem::remove(path.parent_path().parent_path());
    }
    std::filesystem::remove(workspace/"forecasts"); std::filesystem::remove(workspace);
    std::cout << "Required election input and received-history checks passed.\n";
}

void checkPreparedComponent(std::filesystem::path const& root) {
    // Exercise the production count/composition boundary through every synthetic
    // progression case. Live seats deliberately use a different order from the
    // prior; missing preferences and genuine future voters remain separate.
    std::ifstream stream(root/"tests/fixtures/turnout/synthetic-turnout-v1.json");
    json fixture; stream >> fixture;
    auto artifact = TurnoutModelIO::read(fixture);
    auto n = artifact.prior.seats.size();
    for (auto const& test : fixture.at("cases")) {
        auto units = TurnoutModelIO::units(test.at("units"));
        auto history = TurnoutModelIO::history(test.at("history"));
        std::vector<bool> finalised(n);
        for (auto const& name : test.at("finalised"))
            for (std::size_t s = 0; s < n; ++s) finalised[s] = finalised[s] || name == artifact.prior.seats[s];
        auto options = artifact.options; options.ppvcFactor = test.at("ppvc_factor");
        auto input = artifact; input.options = options; input.decayPpvc = false;
        auto expected = TurnoutModel::prepare(input.prior,units,finalised,history,options);
        auto plan = TurnoutModel::makeDrawPlan(expected,units,input.prior.enrolment);
        std::vector<int> indexes;
        for (std::size_t j = 0; j < units.size(); ++j) indexes.push_back(int(j));
        auto prepared = LiveTurnout::Prepared::prepare(input,units,indexes,units.size(),finalised,history,test.at("source_time"));
        std::vector<LiveTurnout::UnitComposition> compositions;
        std::vector<LiveTurnout::SeatComposition> seats(n);
        for (std::size_t s = 0; s < n; ++s) seats[n-1-s].name = input.prior.seats[s];
        for (std::size_t j = 0; j < units.size(); ++j) {
            compare(expected.unitMeans[j],prepared->fpTarget(j),"component count mean");
            auto s = n-1-units[j].seat;
            double counted = units[j].counted, addition = expected.unitMeans[j]-counted;
            LiveTurnout::UnitComposition u; u.booth = j; u.seat = s;
            u.fp = {{0,.6*counted},{1,.4*counted}};
            u.tpp = {{0,.54*counted},{1,.36*counted}}; // 10% preferences not yet reported.
            u.tcp = {{10,.54*counted},{11,.36*counted}};
            u.projectedFp = {{0,float(.6*counted+.55*addition)},{1,float(.4*counted+.45*addition)}};
            u.projectedTpp = {{0,float(.6*counted+.55*addition)},{1,float(.4*counted+.45*addition)}};
            u.projectedTcp = {{10,float(.6*counted+.55*addition)},{11,float(.4*counted+.45*addition)}};
            auto add = [](auto const& from, auto& to) { for (auto const& [p,v] : from) to[p] += v; };
            add(u.fp,seats[s].fp); add(u.tpp,seats[s].tpp); add(u.tcp,seats[s].tcp);
            add(u.projectedFp,seats[s].projectedFp); add(u.projectedTpp,seats[s].projectedTpp); add(u.projectedTcp,seats[s].projectedTcp);
            compositions.push_back(u);
        }
        auto ready = prepared->withComposition(compositions,seats);
        for (std::size_t s = 0; s < n; ++s) assert(ready->seatIndex(seats[s].name) == s);
        for (unsigned long long seed = 0; seed < 64; ++seed) {
            auto sampled = ready->draw(seed);
            auto counts = TurnoutModel::drawUnitCounts(expected,plan,seed);
            std::vector<double> totals(n);
            for (std::size_t j = 0; j < units.size(); ++j) totals[n-1-units[j].seat] += counts[j];
            for (std::size_t s = 0; s < n; ++s) for (auto kind : {LiveData::CountKind::Fp,LiveData::CountKind::Tpp,LiveData::CountKind::Tcp}) {
                auto const& votes = kind == LiveData::CountKind::Fp ? sampled[s].fp : kind == LiveData::CountKind::Tpp ? sampled[s].tpp : sampled[s].tcp;
                double sum = 0;
                for (auto const& [p,v] : votes) { sum += v; assert(v >= ready->counted(kind,s).at(p)-.002); }
                assert(std::abs(sum-totals[s]) < .01);
                auto varied = ready->vary(kind,s,votes,{{votes.begin()->first,1.}});
                double variedSum = 0;
                for (auto const& [p,v] : varied) { variedSum += v; assert(v >= ready->counted(kind,s).at(p)-.002); }
                assert(std::abs(variedSum-sum) < .01);
            }
        }
        // Finalisation removes additional voters, not pending preferences.
        std::fill(finalised.begin(),finalised.end(),true);
        auto final = LiveTurnout::Prepared::prepare(input,units,indexes,units.size(),finalised,history,test.at("source_time"));
        for (auto& seat : seats) { seat.projectedFp.clear(); seat.projectedTpp.clear(); seat.projectedTcp.clear(); }
        for (auto& u : compositions) {
            double counted = units[u.booth].counted;
            u.projectedFp = {{0,float(.6*counted)},{1,float(.4*counted)}};
            u.projectedTpp = u.projectedFp; u.projectedTcp = {{10,float(.6*counted)},{11,float(.4*counted)}};
            for (auto const& [p,v] : u.projectedFp) seats[u.seat].projectedFp[p] += v;
            for (auto const& [p,v] : u.projectedTpp) seats[u.seat].projectedTpp[p] += v;
            for (auto const& [p,v] : u.projectedTcp) seats[u.seat].projectedTcp[p] += v;
        }
        auto finished = final->withComposition(compositions,seats)->draw(7);
        for (std::size_t s = 0; s < n; ++s) {
            double expectedCount = 0, pairCount = 0;
            for (auto const& [p,v] : seats[s].fp) expectedCount += v;
            for (auto const& [p,v] : finished[s].tpp) pairCount += v;
            assert(std::abs(pairCount-expectedCount) < .01);
        }
    }
    std::cout << "Production count/composition component checks passed.\n";
}
void checkLiveAccounts() {
    // A fractional FP remainder is shared by all projections. Preferences on
    // already-counted FP remain outstanding even if part of the pair count has
    // reported or a draw expects no additional FP votes. A published TCP excess
    // is preserved; none of these changes invents additional FP turnout.
    compare(5.25,LiveTurnoutMath::remaining(105.25,100),"fractional remainder");
    compare(105.25,LiveTurnoutMath::pairTarget(105.25,100,97),"partially reported pair");
    compare(100,LiveTurnoutMath::pairTarget(100,100,97),"complete FP with missing preferences");
    compare(108.25,LiveTurnoutMath::pairTarget(105.25,100,103),"published TCP excess");
    compare(105.25,LiveTurnoutMath::pairTarget(105.25,100,0),"unreported pair");
    compare(100/105.25,LiveTurnoutMath::completion(100,105.25),"fractional completion");
    compare(0,LiveTurnoutMath::completion(0,0),"unavailable service");
    rejects([] { LiveTurnoutMath::remaining(99,100); });

    // The main sampler may move preferences among outstanding votes but cannot
    // rewrite published counts. Exercise both ordinary variation and large
    // opposing changes, including a candidate whose expected addition is zero.
    std::map<int,float> projected{{0,610},{1,345},{7,45}};
    std::map<int,double> counted{{0,600},{1,340},{7,45}};
    assert(LiveTurnoutMath::varyRemaining(projected,counted,{}) == projected);
    assert(LiveTurnoutMath::varyRemaining(projected,counted,{{0,0},{1,0}}) == projected);
    for (double change : {-100.,-1.,.01,1.,100.}) {
        auto varied = LiveTurnoutMath::varyRemaining(projected,counted,{{0,change},{1,-change}});
        double total = 0;
        for (auto const& [party,votes] : varied) { assert(votes >= counted.at(party)); total += votes; }
        assert(std::abs(total-1000) < .0002); // projected party maps use float.
    }
    std::map<int,float> finished{{0,600},{1,340},{7,45}};
    assert(LiveTurnoutMath::varyRemaining(finished,counted,{{0,100},{1,-100}}) == finished);
    // With no fixed votes, a small binary change agrees with the existing
    // transformed-scale response; smoothing contributes less than 0.001 share.
    auto binary = LiveTurnoutMath::varyRemaining({{0,600},{1,400}}, {}, {{0,.01}});
    double expected = 1000/(1+std::exp(-(std::log(1.5)+.04*.01)));
    assert(std::abs(binary.at(0)-expected) < .1);
    // FP normalises independently perturbed parties. Opposing binary changes
    // have the same first-order response as one pair-log-odds change, rather
    // than twice that change merely because both parties were perturbed.
    auto opposing = LiveTurnoutMath::varyRemaining({{0,600},{1,400}}, {}, {{0,.01},{1,-.01}}, true);
    assert(std::abs(opposing.at(0)-expected) < .1);
    // When FP has reported but pair preferences have not, the simulator's
    // proposed pair share should pass through without magnifying its movement.
    // Exercise both classic TPP and arbitrary TCP identities, including both
    // movement directions and a starting pair that is already unequal.
    for (auto pair : {std::pair{0,1}, std::pair{17,42}}) {
        for (float start : {50.f,60.f}) for (float target : {40.f,50.f,60.f,75.f}) {
            auto shares = LiveTurnoutMath::reconcileForecast(
                {{},{{pair.first,10*start},{pair.second,10*(100-start)}}},
                {{pair.first,target},{pair.second,100-target}});
            assert(std::abs(shares.at(pair.first)-target) < .03); // Quarter-vote smoothing only.
            assert(std::abs(shares.at(pair.first)+shares.at(pair.second)-100) < .00002);
        }
    }
    // Partial counts still constrain both candidates. Compare with the single
    // pair-change contract and check that relabelling the pair is symmetric.
    LiveTurnoutMath::PartyAccount partial{{{0,300},{1,200}},{{0,500},{1,500}}};
    double pairChange = 25*std::log(600.25/400.25);
    auto expectedPair = LiveTurnoutMath::varyRemaining(partial.projected,partial.counted,{{0,pairChange}});
    auto partialShares = LiveTurnoutMath::reconcileForecast(partial,{{0,60},{1,40}});
    auto relabelled = LiveTurnoutMath::reconcileForecast(
        {{{0,200},{1,300}},{{0,500},{1,500}}},{{0,40},{1,60}});
    assert(std::abs(partialShares.at(0)-expectedPair.at(0)/10) < .00002);
    assert(std::abs(partialShares.at(0)-relabelled.at(1)) < .00002);
    assert(partialShares.at(0)*10 >= 300 && partialShares.at(1)*10 >= 200);
    auto completePair = LiveTurnoutMath::reconcileForecast(
        {{{0,600},{1,400}},{{0,600},{1,400}}},{{0,20},{1,80}});
    assert(completePair.at(0) == 60 && completePair.at(1) == 40);
    rejects([&] { LiveTurnoutMath::varyRemaining(projected,{{0,611}},{{0,1}}); });
    // An invented handoff illustrates a prior below one candidate's counted
    // support. A represented independent must also occupy only one bucket,
    // however small its share happens to be. No feed counts are used here.
    LiveTurnoutMath::PartyAccount example{
        {{0,12000},{1,7000},{7,1400},{9,1700}},
        {{0,12200},{1,7100},{7,1450},{9,1750}}};
    LiveData::VoteCountAccount source{&example.counted,&example.projected};
    auto grouped = LiveTurnoutMath::partitionAccount(source, [](int p) { return p == 9 ? -1 : p; });
    assert(grouped.counted.at(7) == 1400 && grouped.counted.at(-1) == 1700);
    auto reconciled = LiveTurnoutMath::reconcileForecast(grouped,
        {{0,48.f},{1,30.f},{7,6.f},{-1,16.f}},true);
    double n = 0, shares = 0;
    for (auto const& [p,v] : grouped.projected) n += v;
    for (auto const& [p,v] : reconciled) {
        shares += v; assert(n * v / 100 >= grouped.counted.at(p) - .002);
    }
    assert(std::abs(shares - 100) < .00002);
    auto closed = LiveTurnoutMath::reconcileForecast({counted,finished},{{0,1},{1,98},{7,1}},true);
    assert(std::abs(closed.at(0)-100*600./985) < .00001);
    rejects([] { LiveTurnoutMath::reconcileForecast({{{0,600},{1,400}},{{0,400},{1,600}}},{{0,40},{1,60}}); });
    auto coalition = LiveTurnoutMath::resizeAccount({{{1,600},{2,340}},{{1,610},{2,345}}},945);
    auto split = LiveTurnoutMath::reconcileForecast(coalition,{{1,10},{2,90}});
    assert(945*split.at(1)/100 >= 600-.001 && 945*split.at(2)/100 >= 340-.001);
    rejects([] { LiveTurnoutMath::resizeAccount({{{1,600}},{{1,610}}},590); });
    // A partial pair's missing preferences belong in the cached base, so its
    // mean reconstructs after adding only the genuinely new FP votes. Those
    // missing preferences remain estimates, including when FP additions are zero.
    auto measured = LiveTurnoutMath::prepareUnitComposition({{0,620},{1,380}},{{0,600},{1,350}},970);
    assert(measured.base.at(0) == 608 && measured.base.at(1) == 362);
    assert(std::abs(measured.additionShares[0].second-.4) < 1e-10);
    compare(620,measured.base.at(0)+30*measured.additionShares[0].second,"partial-pair cached mean");
    auto noNewFp = LiveTurnoutMath::prepareUnitComposition({{0,608},{1,362}},{{0,600},{1,350}},970);
    compare(970,noNewFp.base.at(0)+noNewFp.base.at(1),"missing preferences survive no-addition draw");
    auto variedPair = LiveTurnoutMath::varySampledRemaining({{0,608},{1,362}},{{0,608},{1,362}},{{0,600},{1,350}},{{0,1}});
    assert(variedPair.at(0) >= 600 && variedPair.at(1) >= 350 && variedPair.at(0) != 608);
    auto inferred = LiveTurnoutMath::prepareUnitComposition({{0,620},{1,380}},{},970);
    assert(std::abs(inferred.base.at(0)-601.4) < 1e-10);
    auto transported = LiveTurnoutMath::varySampledRemaining({{0,800},{1,650},{7,50}},
        projected,counted,{{0,1},{1,-1}},true);
    double transportedTotal = 0;
    for (auto const& [p,v] : transported) { assert(v >= counted.at(p)); transportedTotal += v; }
    assert(std::abs(transportedTotal-1500) < .0003);
    assert(LiveTurnoutMath::varySampledRemaining(finished,projected,counted,{{0,100}}) == finished);
    std::cout << "Live count/party account checks passed.\n";
}
void checkClosedProgress() {
    TurnoutModel::Prior prior;
    prior.seats = {"Active", "Unavailable"};
    prior.groups = {"ordinary", "absent"};
    prior.enrolment = {4000, 4000};
    prior.totals = {{2500, 2500}, {2900, 2900}};
    prior.counts = {{1400, 1100, 1400, 1100}, {1600, 1300, 1600, 1300}};
    std::vector<TurnoutModel::Unit> units(2);
    for (std::size_t s = 0; s < units.size(); ++s) {
        auto& unit = units[s];
        unit.seat = s; unit.group = 1; unit.kind = "declaration";
        unit.name = unit.category = "Absent"; unit.weight = 1;
    }
    units[0].counted = 1000;
    units[1].closed = true; units[1].closureReason = "Service unavailable.";
    std::vector<TurnoutModel::Observation> history;
    for (int day = 0; day < 25; ++day) {
        TurnoutModel::Observation observation; observation.hour = day*24.;
        observation.counts[{"Active", "Absent"}] = 1000;
        observation.counts[{"Unavailable", "Absent"}] = 0;
        history.push_back(observation);
    }
    auto withoutClosed = history;
    for (auto& observation : withoutClosed) observation.counts.erase({"Unavailable", "Absent"});
    // Archived zeros must not delay another district, either nationally or
    // within a subdivision. An open service awaiting results still must.
    for (std::vector<std::string> const& states : std::vector<std::vector<std::string>>{
            {"", ""}, {"X", "X"}, {"X", "Y"}}) {
        prior.subdivisions = states;
        auto expected = TurnoutModel::progress(prior,units,withoutClosed);
        auto actual = TurnoutModel::progress(prior,units,history);
        assert(expected[0].strength > .7);
        assert(actual[0].strength == expected[0].strength);
        assert(actual[0].pooledStarted == expected[0].pooledStarted);
        assert(actual[0].stateWeight == expected[0].stateWeight);
        assert(actual[1].coverage == 0 && actual[1].strength == 0);
        auto open = units; open[1].closed = false;
        assert(TurnoutModel::progress(prior,open,history)[0].strength < expected[0].strength);
    }
    auto sibling = units[0]; sibling.counted = 0; sibling.closed = true;
    auto mixed = TurnoutModel::progress(prior,{units[0],sibling},history);
    auto single = TurnoutModel::progress(prior,{units[0]},history);
    assert(mixed[0].strength == single[0].strength && mixed[1].strength == 0);
    assert(TurnoutModel::progress(prior,{units[1]},history)[0].strength == 0);

    // Exercise the production whole-group allowance path too. Its synthetic
    // group units must carry availability from their real reporting services.
    // Leaving the closed group's zero in history cannot alter the active seat.
    for (std::size_t s = 0; s < prior.seats.size(); ++s) {
        TurnoutModel::Unit ordinary; ordinary.seat = s;
        ordinary.kind = "ordinary"; ordinary.name = ordinary.category = "Ordinary";
        ordinary.counted = 1200; ordinary.weight = 1;
        units.push_back(ordinary);
    }
    prior.units = units;
    for (auto& observation : history) {
        observation.counts[{"Active", "allocation:absent"}] = 1000;
        observation.counts[{"Unavailable", "allocation:absent"}] = 0;
    }
    withoutClosed = history;
    for (auto& observation : withoutClosed) {
        observation.counts.erase({"Unavailable", "Absent"});
        observation.counts.erase({"Unavailable", "allocation:absent"});
    }
    for (bool scheduled : {false, true}) {
        TurnoutModel::Options options; options.countDraws = 2;
        if (scheduled) options.receiptDeadline = 24.;
        // B's ordinary booth is complete and its only declaration service is
        // unavailable. Unit completion suffices; no district flag is needed.
        auto expected = TurnoutModel::prepare(prior,units,{false,false},withoutClosed,options);
        auto actual = TurnoutModel::prepare(prior,units,{false,false},history,options);
        assert(actual.allocationStrength[0] > 0);
        assert(actual.allocationStrength == expected.allocationStrength);
        assert(actual.allocationWeight == expected.allocationWeight);
        assert(actual.totals == expected.totals);
        assert(actual.unitMeans == expected.unitMeans);
        assert(actual.evidence[1].coverage == 0 && actual.evidence[1].strength == 0);
        assert(actual.unitMeans[1] == units[1].counted);
    }
}

void checkCompletedCountAccounts() {
    TurnoutModel::Prior prior;
    prior.seats = {"Complete", "Unreported"};
    prior.groups = {"ordinary", "absent"}; prior.subdivisions = {"X", "X"};
    prior.enrolment = {4000, 4000};
    prior.totals = {{2500, 2500}, {2900, 2900}};
    prior.counts = {{1400, 1100, 1400, 1100}, {1600, 1300, 1600, 1300}};
    // No district has an authoritative final flag. The first district's
    // services nevertheless resolve its count completely; the second still
    // has unreported services and must retain the original prior uncertainty.
    for (int configuration : {0, 1, 2}) {
        std::vector<TurnoutModel::Unit> units;
        for (std::size_t s = 0; s < 2; ++s) {
            TurnoutModel::Unit booth; booth.seat = s; booth.kind = "ordinary";
            booth.name = booth.category = "Ordinary"; booth.weight = 1;
            if (s == 0 && configuration != 2) booth.counted = 1800;
            if (s == 0 && configuration == 2) { booth.closed = true; booth.closureReason = "Service unavailable."; }
            units.push_back(booth);
            if (s == 0 && configuration == 1) continue; // Ordinary-only account.
            TurnoutModel::Unit declaration; declaration.seat = s; declaration.group = 1;
            declaration.kind = "declaration"; declaration.name = declaration.category = "Absent";
            declaration.weight = 1;
            if (s == 0) { declaration.closed = true; declaration.closureReason = "Service unavailable."; }
            units.push_back(declaration);
        }
        prior.units = units;
        auto result = TurnoutModel::prepare(prior,units,{false,false},{});
        double counted = configuration == 2 ? 0 : 1800;
        for (std::size_t r = 0; r < prior.totals.size(); ++r) {
            assert(result.broad.totals[r][0] == counted);
            assert(result.broad.counts[r][0]+result.broad.counts[r][1] == counted);
            compare(prior.totals[r][1],result.broad.totals[r][1],"unreported district prior");
            for (std::size_t j = 0; j < units.size(); ++j) if (units[j].seat == 0) {
                assert(result.broad.complete[j]);
                assert(result.broad.unitCounts[r][j] == units[j].counted);
                assert(result.broad.remaining[r][j] == 0 && result.broad.ownRemaining[r][j] == 0);
            }
        }
        for (auto const& component : result.components) assert(units[component.unit].seat == 1);
        for (auto const& row : result.totals) assert(row[0] == counted);
        assert(result.noAdditionProbability[0] == 1 && result.noAdditionProbability[1] == 0);
        auto plan = TurnoutModel::makeDrawPlan(result,units,prior.enrolment);
        for (unsigned long long seed : {1ULL, 2ULL, 3ULL}) {
            auto draw = TurnoutModel::drawUnitCounts(result,plan,seed);
            for (std::size_t j = 0; j < units.size(); ++j) if (units[j].seat == 0)
                assert(draw[j] == units[j].counted);
        }
        // Omission of every service is not completion evidence. Keep the
        // unsupported-configuration error instead of silently making a zero.
        auto missing = units;
        missing.erase(std::remove_if(missing.begin(),missing.end(),[](auto const& u) { return u.seat == 0; }),missing.end());
        rejects([&] { TurnoutModel::prepare(prior,missing,{false,false},{}); });
    }
}

void checkIndependentServiceSizes() {
    // Identical service accounts must not inherit a booth shortfall, whether
    // historical inputs combine those services (QLD/SA) or separate them.
    for (bool mixed : {false,true}) {
        TurnoutModel::Prior p;
        p.seats = {"Example"}; p.subdivisions = {"Example state"}; p.enrolment = {5000};
        p.groups = mixed ? std::vector<std::string>{"ordinary_other","postal"}
            : std::vector<std::string>{"ordinary","provisional","telephone","postal"};
        p.totals = {{2320},{2980}};
        p.counts = mixed ? TurnoutModel::Matrix{{1920,400},{2380,600}}
            : TurnoutModel::Matrix{{1800,80,40,400},{2200,120,60,600}};
        std::vector<TurnoutModel::Unit> units(4);
        std::vector<std::string> names = {"Booth","Provisional","Telephone","Postal"};
        std::vector<double> weights = {.94,.04,.02,1};
        for (std::size_t j = 0; j < units.size(); ++j) {
            auto& u = units[j]; u.name = u.category = names[j];
            u.kind = j == 0 ? "ordinary" : "declaration";
            u.group = mixed ? (j < 3 ? 0 : 1) : j;
            u.weight = mixed ? weights[j] : 1; u.matched = true;
        }
        p.units = units;
        auto origin = TurnoutModel::update(p,units,{false});
        for (double counted : {900.,800.}) {
            units[0].counted = counted;
            auto result = TurnoutModel::update(p,units,{false});
            for (std::size_t r = 0; r < p.totals.size(); ++r) {
                for (std::size_t j = 1; j < units.size(); ++j)
                    compare(origin.unitCounts[r][j],result.unitCounts[r][j],"retain service uncertainty");
                compare(origin.totals[r][0]-origin.unitCounts[r][0]+counted,
                    result.totals[r][0],"district follows actual booth size");
            }
        }
        // Partial counts, genuine zero closures, finalisation and enrolment
        // constraints still override size estimates without erasing real votes.
        units[1].counted = 125; units[2].closed = true;
        auto partial = TurnoutModel::update(p,units,{false});
        for (auto const& row : partial.unitCounts) { assert(row[1] >= 125); assert(row[2] == 0); }
        auto final = TurnoutModel::update(p,units,{true});
        for (auto const& row : final.totals) assert(row[0] == 925);
        units[0].counted = 4874;
        auto bounded = TurnoutModel::update(p,units,{false});
        for (std::size_t r = 0; r < p.totals.size(); ++r) {
            assert(bounded.totals[r][0] < 5000 && bounded.totals[r][0] > 4999);
            assert(bounded.unitCounts[r][0] == 4874 && bounded.unitCounts[r][1] >= 125);
        }
    }
}

void checkTurnoutTransfer() {
    // A completed booth is short by 250 votes. Either the unfinished service
    // is correspondingly larger, or turnout is lower. Retain both possibilities
    // in preparation and in actual main-simulation count draws.
    TurnoutModel::Prior p;
    p.election = "example"; p.seats = {"Example"}; p.subdivisions = {"X"};
    p.groups = {"ordinary","service"}; p.enrolment = {10000};
    p.totals.assign(256,{3000}); p.counts.assign(256,{2000,1000});
    TurnoutModel::Unit booth, service;
    booth.name = booth.category = "Booth"; booth.kind = "ordinary"; booth.weight = 1;
    service.name = service.category = "Service"; service.kind = "declaration"; service.weight = 1; service.group = 1;
    p.units = {booth,service};
    booth.counted = 1750;
    auto r = TurnoutModel::prepare(p,{booth,service},{false},{});
    assert(r.broad.transferProbability.front()[0] > .5 && r.broad.transferProbability.front()[0] < .8);
    auto plan = TurnoutModel::makeDrawPlan(r,{booth,service},p.enrolment);
    bool sawTransfer = false, sawTurnout = false;
    for (unsigned long long seed = 0; seed < 512; ++seed) {
        auto counts = TurnoutModel::drawUnitCounts(r,plan,seed);
        assert(counts[0] == 1750);
        assert(std::abs(counts[1]-1000) < 1e-6 || std::abs(counts[1]-1250) < 1e-6);
        sawTransfer = sawTransfer || std::abs(counts[1]-1250) < 1e-6;
        sawTurnout = sawTurnout || std::abs(counts[1]-1000) < 1e-6;
    }
    assert(sawTransfer && sawTurnout);
    // Severe shortfalls, or reductions almost eliminating a category, favour
    // an overall turnout change. An explicit exclusion preserves its own count.
    booth.counted = 800;
    auto extreme = TurnoutModel::update(p,{booth,service},{false});
    assert(extreme.transferProbability.front()[0] < .02);
    booth.counted = 2900;
    auto excess = TurnoutModel::update(p,{booth,service},{false});
    for (auto const& row : excess.totals) assert(std::abs(row[0]-3900) < 1e-6);
    booth.counted = 1750; service.transferExclusionReason = "New service without a comparable category.";
    auto excluded = TurnoutModel::update(p,{booth,service},{false});
    for (auto const& row : excluded.unitCounts) assert(row[1] == 1000);
    for (auto const& row : excluded.transferProbability) assert(row[0] == 0);
}

void checkDistrictEnrolmentConflicts() {
    // A valid completed booth retains every counted vote. A contradictory
    // input must identify its district and figures, including when the feed
    // marks the district final; finalisation does not make excess votes valid.
    TurnoutModel::Prior prior;
    prior.seats = {"Example"}; prior.groups = {"ordinary"}; prior.subdivisions = {"X"};
    prior.enrolment = {4000}; prior.totals = {{2500}, {2900}}; prior.counts = prior.totals;
    TurnoutModel::Unit unit;
    unit.name = unit.category = "Ordinary"; unit.kind = "ordinary"; unit.weight = 1;
    prior.units = {unit};
    unit.counted = 3999;
    auto valid = TurnoutModel::update(prior,{unit},{false});
    for (auto const& row : valid.unitCounts) assert(row[0] == unit.counted);

    for (double counted : {4000., 4100.}) for (bool finalised : {false, true}) {
        unit.counted = counted;
        bool rejected = false;
        try {
            TurnoutModel::update(prior,{unit},{finalised});
        } catch (std::runtime_error const& error) {
            rejected = true;
            auto expected = std::string("Counted formal votes in Example (")
                + (counted == 4000 ? "4000" : "4100")
                + ") reach or exceed the prepared enrolment (4000). "
                + "Check the result feed for duplicate or inconsistent counts, or correct the prepared enrolment if it is wrong.";
            assert(std::string(error.what()) == expected);
        }
        assert(rejected);
    }
}

void checkCountDraws() {
    // A close count can have a modest mean remainder while retaining batches
    // larger than its margin. A normal error around the mean or a mean-only
    // account cannot represent this exact-zero/large-addition combination.
    TurnoutModel::Result r;
    r.broad.unitCounts = {{24500,25000},{24600,25200}};
    r.broad.totals = r.broad.unitCounts;
    r.noAdditionProbability = {.64,0};
    TurnoutModel::Component c; c.unit = 0; c.weight = 1;
    c.previousProbability = 0; c.smallProbability = .34; c.batchProbability = .02;
    c.reference = {500,600};
    c.referenceOdds = {std::log(500./5500),std::log(600./5400)};
    c.smallOdds = {std::log(.25/5500),std::log(.25/5400)};
    c.batchOdds = {std::log(1700./5500),std::log(1800./5400)};
    r.components = {c};
    std::vector<TurnoutModel::Unit> units(2); units[0].counted = 24000;
    units[1].counted = 23000; units[1].seat = 1;
    auto plan = TurnoutModel::makeDrawPlan(r,units,{30000,30000});
    int zeros = 0, batches = 0;
    for (unsigned long long k = 0; k < 10000; ++k) {
        auto draw = TurnoutModel::drawUnitCounts(r,plan,k);
        assert(draw == TurnoutModel::drawUnitCounts(r,plan,k));
        assert(draw[0] >= 24000 && draw[0] < 30000);
        assert(draw[1] == 25000 || draw[1] == 25200);
        zeros += draw[0] == 24000; batches += draw[0] > 24077;
    }
    assert(std::abs(zeros/10000.-.64) < .02 && batches > 50);
    // Before slowing evidence, the same joint row retains between-seat
    // covariance. A seat's explicit completion does not change other draws.
    r.components.clear(); r.noAdditionProbability = {0,0};
    auto early = TurnoutModel::makeDrawPlan(r,units,{30000,30000});
    for (unsigned long long k = 0; k < 100; ++k) {
        auto draw = TurnoutModel::drawUnitCounts(r,early,k);
        assert((draw[0] == 24500 && draw[1] == 25000) || (draw[0] == 24600 && draw[1] == 25200));
        r.noAdditionProbability[0] = 1;
        auto final = TurnoutModel::drawUnitCounts(r,early,k);
        assert(final[0] == 24000 && final[1] == draw[1]);
        r.noAdditionProbability[0] = 0;
    }
    std::cout << "Explicit no-addition, late-batch and joint-prior draw checks passed.\n";
}
void checkLocalCompletion() {
    // A newly arrived count can approach its expected size before quietness
    // supplies any evidence. Above the prior's range, retain the competing
    // explanation of an undersized prior instead of declaring completion.
    TurnoutModel::Prior p;
    p.election = "test"; p.seats = {"Example"}; p.groups = {"ordinary","postal","absent"};
    p.subdivisions = {"X"}; p.enrolment = {10000};
    p.counts = {{3800,800,400},{4200,1200,900}}; p.totals = {{5000},{6300}};
    for (std::size_t g = 0; g < 3; ++g) {
        TurnoutModel::Unit u; u.group = g; u.weight = 1;
        u.name = u.category = g == 0 ? "Ordinary" : g == 1 ? "Postal" : "Absent";
        u.kind = g == 0 ? "ordinary" : "declaration";
        u.counted = g == 0 ? 3800 : g == 1 ? 1000 : 600;
        p.units.push_back(u);
    }
    auto units = p.units;
    auto measured = TurnoutModel::prepare(p,units,{false},{});
    auto const& middle = measured.components.back();
    assert(middle.originalSizeZ && middle.completionWeight > .4 && middle.completionWeight < .7);
    units[2].counted = 50;
    auto early = TurnoutModel::prepare(p,units,{false},{});
    units[2].counted = 4000;
    auto large = TurnoutModel::prepare(p,units,{false},{});
    assert(early.components.back().completionWeight < .01);
    assert(large.components.back().completionWeight < .05);
    assert(large.components.back().originalSizeZ > 3);
    // A missing or fixed size is not affirmative evidence of completion.
    units[2].counted = 0;
    auto unstarted = TurnoutModel::prepare(p,units,{false},{});
    assert(unstarted.components.back().completionWeight == 0);
    assert(!unstarted.components.back().originalSizeZ);

    TurnoutModel::Observation observation; observation.hour = 0;
    for (auto const& u : p.units) observation.counts[{"Example",u.category}] = u.counted;
    TurnoutModel::Options options; options.postalDeadline = 480;
    auto beforeReceipt = TurnoutModel::prepare(p,p.units,{false},{observation},options);
    compare(beforeReceipt.components.front().completionWeight,measured.components.front().completionWeight,"postal main-count support");
    assert(beforeReceipt.components.front().postalRemainderLogOdds);
    compare(middle.completionWeight,beforeReceipt.components.back().completionWeight,"postal safeguard is local");

    // Isolate the intervention from upstream count conditioning: for identical
    // random draws, only the selected category may shrink. Sibling categories,
    // another district, exact-zero outcomes and old exceptional batches remain.
    TurnoutModel::Result base;
    base.broad.counted = {1500,1600}; base.noAdditionProbability = {.1,.2};
    base.broad.unitCounts = {{1000,600,700,1100,500,600},{1000,650,750,1100,550,650}};
    base.broad.totals = {{2300,2200},{2400,2300}};
    std::vector<TurnoutModel::Unit> services(6);
    std::vector<double> counted{1000,200,300,1100,200,300};
    for (std::size_t j = 0; j < services.size(); ++j) {
        services[j].seat = j/3; services[j].counted = counted[j];
        if (j%3 == 0) continue;
        TurnoutModel::Component c; c.unit = j; c.weight = .3;
        c.previousProbability = 1-base.noAdditionProbability[j/3]-.2;
        c.smallProbability = .18; c.batchProbability = .02;
        for (std::size_t r = 0; r < 2; ++r) {
            double capacity = 5000-base.broad.totals[r][j/3];
            double reference = base.broad.unitCounts[r][j]-counted[j];
            c.capacity.push_back(capacity); c.reference.push_back(reference);
            c.referenceOdds.push_back(std::log(reference/capacity));
            c.smallOdds.push_back(std::log(.25/capacity));
            c.batchOdds.push_back(std::log((reference+100)/capacity));
        }
        base.components.push_back(c);
    }
    auto adjusted = base; adjusted.components.front().completionWeight = .7;
    auto plan = TurnoutModel::makeDrawPlan(base,services,{5000,5000});
    std::size_t reductions = 0, retained = 0;
    for (unsigned long long seed = 0; seed < 4096; ++seed) {
        auto old = TurnoutModel::drawUnitCounts(base,plan,seed);
        auto revised = TurnoutModel::drawUnitCounts(adjusted,plan,seed);
        for (std::size_t j = 0; j < old.size(); ++j) {
            assert(revised[j] >= counted[j]);
            if (j != 1) assert(revised[j] == old[j]);
        }
        assert(revised[1] <= old[1]);
        if (revised[1] < old[1]) ++reductions;
        else ++retained;
    }
    assert(reductions > 1000 && retained > 1000);
    // The supplementary positive path starts AFTER the existing size-based
    // completion interval. It must retain exact zero/small/batch draws and
    // every sibling outcome, including when another category is Postal.
    auto positive = adjusted;
    positive.components.front().positiveRemainderWeight = .25;
    positive.components.front().positiveRemainderMedian = 50;
    reductions = 0;
    for (unsigned long long seed = 0; seed < 4096; ++seed) {
        auto old = TurnoutModel::drawUnitCounts(adjusted,plan,seed);
        auto revised = TurnoutModel::drawUnitCounts(positive,plan,seed);
        for (std::size_t j = 0; j < old.size(); ++j) {
            assert(revised[j] >= counted[j]);
            if (j != 1) assert(revised[j] == old[j]);
        }
        assert(revised[1] <= old[1]);
        if (revised[1] < old[1]) ++reductions;
        if (old[1] == counted[1]) assert(revised[1] == old[1]);
    }
    assert(reductions > 100);
    std::cout << "Local completion, postal receipt and sibling-isolation checks passed.\n";
}

void checkPostalRemainder() {
    for (auto [days,share] : std::vector<std::pair<double,double>>{{4,.06},{2,.04},{0,.02}}) {
        double odds = std::exp(TurnoutModel::postalRemainderLogOdds(days)-TurnoutModel::PostalRemainderMedianShift);
        compare(share,odds/(1+odds),"postal median share");
        assert(std::abs(TurnoutModel::postalRemainderLogOdds(days+1e-7)
            -TurnoutModel::postalRemainderLogOdds(days-1e-7)) < 1e-6);
    }
    compare(.5*.02/.98,std::exp(TurnoutModel::postalRemainderLogOdds(-2)-TurnoutModel::PostalRemainderMedianShift),"postal post-deadline decay");
    compare(.06/.94*std::exp(.32/2),std::exp(TurnoutModel::postalRemainderLogOdds(4)+1.25*1.25/2),"postal unchanged expected remainder");
    compare(TurnoutModel::postalRemainderLogOdds(4),TurnoutModel::postalRemainderLogOdds({}),"unknown postal timetable");
    // Two identical seats with plenty of remaining allocation expose the
    // shared and local remainder directly. A routine path always selects the
    // near-finished outcome here; real counts retain the other mixture paths.
    TurnoutModel::Result result;
    result.broad.counted = {5000,5000}; result.noAdditionProbability = {0,0};
    result.broad.totals = {{10000,10000}}; result.broad.unitCounts = {{10000,10000}};
    std::vector<TurnoutModel::Unit> units(2);
    for (std::size_t j = 0; j < 2; ++j) {
        units[j].seat = j; units[j].counted = 5000;
        TurnoutModel::Component c; c.unit = j; c.previousProbability = 1; c.completionWeight = 1;
        c.capacity = {10000}; c.reference = {5000}; c.referenceOdds = {std::log(.5)};
        c.smallOdds = {std::log(.25/10000)}; c.batchOdds = {0};
        c.postalCounted = 5000; c.postalRemainderLogOdds = TurnoutModel::postalRemainderLogOdds(4);
        result.components.push_back(c);
    }
    auto plan = TurnoutModel::makeDrawPlan(result,units,{20000,20000});
    double sumA = 0, sumB = 0, squareA = 0, squareB = 0, product = 0;
    constexpr int samples = 32768;
    for (int seed = 0; seed < samples; ++seed) {
        auto counts = TurnoutModel::drawUnitCounts(result,plan,seed);
        double a = std::log((counts[0]-5000)/5000)-*result.components[0].postalRemainderLogOdds;
        double b = std::log((counts[1]-5000)/5000)-*result.components[1].postalRemainderLogOdds;
        assert(counts[0] > 5000 && counts[0] <= 10000);
        assert(counts[1] > 5000 && counts[1] <= 10000);
        sumA += a; sumB += b; squareA += a*a; squareB += b*b; product += a*b;
    }
    double varianceA = squareA/samples-std::pow(sumA/samples,2);
    double varianceB = squareB/samples-std::pow(sumB/samples,2);
    double correlation = (product/samples-sumA*sumB/(samples*samples*1.))/std::sqrt(varianceA*varianceB);
    assert(std::abs(sumA/samples) < .02 && std::abs(sumB/samples) < .02);
    assert(std::abs(varianceA-1.25*1.25) < .04 && std::abs(correlation-.16/(1.25*1.25)) < .03);
    std::cout << "Postal remainder centres, bounds and shared draw checks passed.\n";

    // Ordinary simulation draws must retain the wider declaration remainder,
    // including its small shared part, independently of diagnostic quantiles.
    // Leave abundant capacity so the allocation cap does not hide its width.
    result.broad.totals = {{1e8,1e8}}; result.broad.unitCounts = {{1e8,1e8}};
    for (auto& c : result.components) {
        c.completionWeight = 0; c.positiveRemainderWeight = 1; c.positiveRemainderMedian = 100;
        c.capacity = {1e8}; c.reference = {1e8-5000}; c.referenceOdds = {std::log((1e8-5000)/1e8)};
        c.postalRemainderLogOdds.reset();
    }
    plan = TurnoutModel::makeDrawPlan(result,units,{2e8,2e8});
    sumA = sumB = squareA = squareB = product = 0;
    for (int seed = 0; seed < samples; ++seed) {
        auto counts = TurnoutModel::drawUnitCounts(result,plan,seed);
        double a = std::log((counts[0]-5000)/100), b = std::log((counts[1]-5000)/100);
        assert(std::isfinite(a) && std::isfinite(b));
        sumA += a; sumB += b; squareA += a*a; squareB += b*b; product += a*b;
    }
    varianceA = squareA/samples-std::pow(sumA/samples,2);
    varianceB = squareB/samples-std::pow(sumB/samples,2);
    correlation = (product/samples-sumA*sumB/(samples*samples*1.))/std::sqrt(varianceA*varianceB);
    assert(std::abs(sumA/samples) < .04 && std::abs(sumB/samples) < .04);
    assert(std::abs(varianceA-1.75*1.75) < .08);
    assert(std::abs(correlation-.25/(1.75*1.75)) < .025);
    std::cout << "Positive declaration remainder width and shared draw checks passed.\n";
}

void checkSavedHandoff(std::filesystem::path const& diagnosticPath,
    std::filesystem::path const& reportPath) {
    // Optional regression against local GUI exports. Feed the formerly
    // impossible exported mean shares into the new handoff, rather than
    // assuming preservation inside LiveV2 guarantees preservation afterwards.
    std::ifstream ds(diagnosticPath), rs(reportPath); json diagnostic, report;
    ds >> diagnostic; rs >> report; auto const& sr = report.at("simulation_report");
    auto readMap = [](json const& rows) {
        std::map<int,float> values;
        for (auto const& row : rows) values[row.at("party_index")] = row.at("value");
        return values;
    };
    int tested = 0;
    for (auto const& seat : diagnostic.at("seats")) {
        auto name = seat.at("name").get<std::string>(); auto const& node = seat.at("node");
        auto projected = readMap(node.at("fp_votes_projected"));
        std::map<int,double> counted;
        int rawInd = seat.at("live_independent_party_index").is_number()
            ? seat.at("live_independent_party_index").get<int>() : -999999;
        for (auto const& row : node.at("fp_votes_current"))
            if (row.at("value").get<double>() > 0)
                counted[row.at("party_index").get<int>() == rawInd ? 6 : row.at("party_index").get<int>()] += row.at("value").get<double>();
        auto const& names = sr.at("seat_name");
        auto found = std::find(names.begin(),names.end(),name);
        assert(found != names.end());
        auto proposed = readMap(sr.at("seat_party_mean_fp_share").at(found-names.begin()));
        auto classify = [&](int party) {
            if (party == 6 && proposed[6] <= 1e-9 && proposed[-2] > 1e-9) return -2;
            return proposed[party] > 1e-9 ? party : -1;
        };
        auto account = LiveTurnoutMath::partitionAccount({&counted,&projected},classify);
        auto shares = LiveTurnoutMath::reconcileForecast(account,proposed,true);
        double total = 0, shareTotal = 0;
        for (auto const& [p,v] : projected) total += v;
        for (auto const& [p,v] : shares) shareTotal += v;
        assert(std::abs(shareTotal-100) < .00003);
        for (auto const& [p,v] : account.counted) assert(total*shares.at(p)/100 >= v-.004);
        if (name == "Croydon") std::cout << "Croydon saved-proposal regression: Labor "
            << proposed.at(0) << "% -> " << shares.at(0) << "%; counted " << counted.at(0) << ".\n";
        ++tested;
    }
    std::cout << tested << " saved district handoffs preserve counted groups and total votes.\n";
}
// A finite diagnostic sample cannot establish the main sampler's mean. Check
// paired ordinary iteration draws too, including a young below-expectation
// count that exposed a bias in a sample-only recentering. The analytical rule
// preserves each allocated draw's mean; observed differences should be ordinary
// sampling noise. Every non-postal unit must remain bit-for-bit identical.
void checkPostalSamplerMeans(std::filesystem::path const& root) {
    std::ifstream stream(root/"tests/fixtures/turnout/synthetic-turnout-v1.json");
    json value; stream >> value; auto artifact = TurnoutModelIO::read(value);
    for (auto const& row : value.at("cases")) {
        if (row.at("name") != "declarations-before-deadline" && row.at("name") != "early-pause-below-expectation") continue;
        auto units = TurnoutModelIO::units(row.at("units"));
        auto history = TurnoutModelIO::history(row.at("history"));
        auto options = artifact.options; options.ppvcFactor = row.at("ppvc_factor");
        options.postalWidening = false;
        auto base = TurnoutModel::prepare(artifact.prior,units,std::vector<bool>(artifact.prior.seats.size()),history,options);
        options.postalWidening = true;
        auto wide = TurnoutModel::prepare(artifact.prior,units,std::vector<bool>(artifact.prior.seats.size()),history,options);
        auto plan = TurnoutModel::makeDrawPlan(base,units,artifact.prior.enrolment);
        constexpr int samples = 200000;
        std::vector<double> change(units.size()),square(units.size()),baseSum(units.size()),baseSquare(units.size()),wideSum(units.size()),wideSquare(units.size());
        for (int seed = 0; seed < samples; ++seed) {
            auto x = TurnoutModel::drawUnitCounts(base,plan,seed);
            auto y = TurnoutModel::drawUnitCounts(wide,plan,seed);
            std::vector<double> totals(plan.enrolment.size());
            for (std::size_t j = 0; j < units.size(); ++j) {
                double difference = y[j]-x[j]; change[j] += difference/samples; square[j] += difference*difference/samples;
                baseSum[j] += x[j]/samples; baseSquare[j] += x[j]*x[j]/samples;
                wideSum[j] += y[j]/samples; wideSquare[j] += y[j]*y[j]/samples;
                if (units[j].category != "Postal") assert(x[j] == y[j]);
                assert(y[j] >= units[j].counted); totals[units[j].seat] += y[j];
            }
            for (std::size_t seat = 0; seat < totals.size(); ++seat) assert(totals[seat] <= plan.enrolment[seat]+1e-7);
        }
        bool increasedVariance = false;
        for (std::size_t j = 0; j < units.size(); ++j) if (units[j].category == "Postal") {
            double se = std::sqrt(std::max(0.,square[j]-change[j]*change[j])/samples);
            assert(std::abs(change[j]) <= 5*se+1e-8);
            double baseVariance = baseSquare[j]-baseSum[j]*baseSum[j];
            double wideVariance = wideSquare[j]-wideSum[j]*wideSum[j];
            increasedVariance = increasedVariance || wideVariance > baseVariance*1.01;
        }
        assert(increasedVariance);
    }
    std::cout << "Postal output means, main-sampler variance and unchanged sibling draws passed.\n";
}

void checkRoutineRemainderSampler() {
    // A single fictional open category isolates the new iteration formula.
    // Its routine allowance is 1,000 extra votes; the processing distribution
    // has mean 100. Twenty percent retain the allowance, and the other eighty
    // percent use the broad, capped positive amount. Compare with the known
    // capped-lognormal mean, not with a short diagnostic preparation sample.
    TurnoutModel::Result base;
    base.broad.totals={{1500}};base.broad.counts={{1500}};base.broad.unitCounts={{1500}};
    base.noAdditionProbability={0};base.broad.complete={false};
    TurnoutModel::Unit unit;unit.seat=0;unit.group=0;unit.name="Absent";unit.category="Absent";
    unit.kind="declaration";unit.counted=500;
    TurnoutModel::Component c;c.unit=0;c.previousProbability=1;c.weight=.1;
    c.reference={1000};c.capacity={1e10-1500};c.referenceOdds={std::log(1000/c.capacity[0])};
    c.smallOdds={std::log(.25/c.capacity[0])};c.batchOdds={std::log(5000/c.capacity[0])};
    base.components={c};
    auto shaped=base;
    shaped.components[0].routineSupport=1;shaped.components[0].routineMean=100;
    shaped.components[0].routineMaturity=1;
    std::vector<TurnoutModel::Unit> units{unit};
    auto plan=TurnoutModel::makeDrawPlan(shaped,units,{1e10});
    constexpr int samples=100000;
    double total=0,square=0;
    for (int seed=0;seed<samples;++seed) {
        auto value=TurnoutModel::drawUnitCounts(shaped,plan,seed)[0]-500;
        total+=value/samples;square+=value*value/samples;
        assert(value>=0 && value<=1000+1e-5);
    }
    double sd=TurnoutModel::RoutineRemainderSd,mu=std::log(100.)-sd*sd/2;
    auto normalCdf=[](double z) {return .5*std::erfc(-z/std::sqrt(2.));};
    double capped=100*normalCdf((std::log(1000.)-mu-sd*sd)/sd)
        +1000*(1-normalCdf((std::log(1000.)-mu)/sd));
    double expected=.2*1000+.8*capped;
    assert(std::abs(total-expected)<5*std::sqrt((square-total*total)/samples)+.01);

    // The main sampler knows a batch was selected; none of those outcomes
    // may be modified by the new routine treatment. A missing-history setup
    // (support zero) likewise leaves every original draw exactly unchanged.
    base.components[0].previousProbability=0;base.components[0].batchProbability=1;
    shaped.components[0]=base.components[0];
    shaped.components[0].routineSupport=1;shaped.components[0].routineMean=100;shaped.components[0].routineMaturity=1;
    for (int seed=0;seed<2048;++seed)
        assert(TurnoutModel::drawUnitCounts(base,plan,seed)==TurnoutModel::drawUnitCounts(shaped,plan,seed));

    // Transfer only the small-addition path to a modest positive amount.
    // The original batch/zero probabilities and the finite turnout account
    // remain available; a tight roll tests the simultaneous physical bound.
    shaped.components[0].batchProbability=0;shaped.components[0].smallProbability=1;
    shaped.components[0].routineBridge=1;
    double modestMean=0;
    for (int seed=0;seed<16384;++seed)
        modestMean+=(TurnoutModel::drawUnitCounts(shaped,plan,seed)[0]-500)/16384;
    assert(modestMean>80 && modestMean<110);
    shaped.broad.totals={{1500}};shaped.components[0].capacity={500};
    shaped.components[0].smallOdds={std::log(.25/500)};
    shaped.components[0].referenceOdds={std::log(1000./500)};
    auto tightPlan=TurnoutModel::makeDrawPlan(shaped,units,{2000});
    for (int seed=0;seed<2048;++seed) {
        auto value=TurnoutModel::drawUnitCounts(shaped,tightPlan,seed)[0];
        assert(value>=500 && value<=2000+1e-7);
    }
    std::cout<<"Three-part routine means, modest additions, retained batches and count bounds passed.\n";
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
        if (c.contains("processing_timetables")) {
            auto configured = value;
            configured["options"]["processing_timetables"] = c.at("processing_timetables");
            options.processingTimetables = TurnoutModelIO::read(configured).options.processingTimetables;
        }
        auto actual = TurnoutModel::prepare(artifact.prior,units,finalised,history,options);
        auto unspreadOptions = options; unspreadOptions.postalWidening = false;
        auto unspread = TurnoutModel::prepare(artifact.prior,units,finalised,history,unspreadOptions);
        compare(unspread.unitMeans,actual.unitMeans,"postal widening preserves final unit means");
        compare(TurnoutModel::meanTotals(unspread),TurnoutModel::meanTotals(actual),"postal widening preserves district means");
        auto const& expected = c.at("expected"); std::string label = c.at("name");
        compare(expected.at("broad_totals"),actual.broad.totals,label+"/broad totals");
        compare(expected.at("broad_counts"),actual.broad.counts,label+"/broad categories");
        compare(expected.at("remaining"),actual.broad.remaining,label+"/unit remaining");
        compare(expected.at("own_remaining"),actual.broad.ownRemaining,label+"/own unit remaining");
        compare(expected.at("balanced_remaining_means"),actual.balancedRemainingMeans,label+"/imposed allowance");
        compare(expected.at("transfer_probability"),actual.broad.transferProbability,label+"/transfer probability");
        compare(expected.at("transfer_selected"),actual.broad.transferSelected,label+"/transfer branch");
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
            compare(e.at("usual_log_shift"),a.usualLogShift,label+"/routine size shift");
            compare(e.at("completion_weight"),a.completionWeight,label+"/local completion weight");
            compare(e.at("early_completion_retention"),a.earlyCompletionRetention,label+"/early completion retention");
            compare(e.at("batch_log_odds_sd"),a.batchSd,label+"/batch spread");
            assert(e.at("postal_widening_enabled") == a.postalWidening);
            compare(e.at("postal_routine_extra_log_odds_sd"),a.postalRoutineExtraSd,label+"/allocated postal routine extra spread");
            compare(e.at("postal_batch_extra_log_odds_sd"),a.postalBatchExtraSd,label+"/allocated postal batch extra spread");
            compare(e.at("positive_remainder_weight"),a.positiveRemainderWeight,label+"/positive remainder weight");
            compare(e.at("positive_remainder_median_votes"),a.positiveRemainderMedian,label+"/positive remainder median");
            compare(e.at("routine_remainder_support"),a.routineSupport,label+"/routine remainder support");
            compare(e.at("routine_remainder_mean_votes"),a.routineMean,label+"/routine remainder mean");
            compare(e.at("routine_remainder_maturity"),a.routineMaturity,label+"/routine remainder maturity");
            compare(e.at("routine_remainder_bridge_weight"),a.routineBridge,label+"/routine remainder bridge");
            compare(e.at("routine_remainder_reference_mean_votes"),a.routineReferenceMean,label+"/routine reference mean");
            compare(e.at("routine_remainder_mean_adjustment_votes"),a.routineMeanAdjustment,label+"/routine mean adjustment");
            if (e.at("postal_remainder_median_fraction").is_null()) assert(!a.postalRemainderLogOdds);
            else {
                assert(a.postalRemainderLogOdds);
                compare(e.at("postal_remainder_median_fraction"),1/(1+std::exp(-*a.postalRemainderLogOdds)),label+"/postal remainder median");
            }
            if (e.at("original_size_z").is_null()) assert(!a.originalSizeZ);
            else { assert(a.originalSizeZ); compare(e.at("original_size_z"),*a.originalSizeZ,label+"/original size position"); }
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
            double zero = actual.noAdditionProbability[units[a.unit].seat];
            assert(a.previousProbability >= 0 && a.smallProbability >= 0 && a.batchProbability >= 0);
            assert(std::abs(zero+a.previousProbability+a.smallProbability+a.batchProbability-1) < 1e-12);
        }
        auto diagnostic = TurnoutModelIO::diagnostic(artifact,units,actual,c.at("source_time"));
        assert(!diagnostic.contains("joint_update"));
        // Displayed means must use the analytical expectation of the neutral
        // spread, rather than picking up noise from its short percentile sample.
        for (std::size_t s = 0; s < artifact.prior.seats.size(); ++s) {
            auto const& seat = diagnostic.at("seats")[s];
            compare(TurnoutModel::meanTotals(unspread)[s],seat.at("mean_total"),label+"/displayed district mean");
            for (std::size_t g = 0; g < artifact.prior.groups.size(); ++g) {
                double mean = 0;
                for (std::size_t j = 0; j < units.size(); ++j)
                    if (units[j].seat == s && units[j].group == g) mean += unspread.unitMeans[j];
                compare(mean,seat.at("groups")[g].at("mean_total"),label+"/displayed category mean");
            }
        }
        for (auto const& component : diagnostic.at("late_components")) {
            auto const& probabilities = component.at("probabilities");
            double sum = 0;
            for (auto const& probability : probabilities) sum += probability.get<double>();
            compare(1.,sum,label+"/effective local mixture probabilities");
        }
        assert(diagnostic.at("maximum_accounting_error").get<double>() < 1e-6);
        assert(diagnostic.at("minimum_category_addition").get<double>() >= -1e-7);
        auto plan = TurnoutModel::makeDrawPlan(actual,units,artifact.prior.enrolment);
        for (unsigned long long k = 0; k < 256; ++k) {
            auto sampled = TurnoutModel::drawUnitCounts(actual,plan,k);
            auto baseline = TurnoutModel::drawUnitCounts(unspread,plan,k);
            for (std::size_t j = 0; j < units.size(); ++j)
                if (units[j].category != "Postal") assert(sampled[j] == baseline[j]);
            std::vector<double> totals(artifact.prior.seats.size());
            for (std::size_t j = 0; j < units.size(); ++j) {
                assert(std::isfinite(sampled[j]) && sampled[j] >= units[j].counted);
                totals[units[j].seat] += sampled[j];
                if (actual.broad.complete[j]) assert(sampled[j] == units[j].counted);
            }
            for (std::size_t s = 0; s < totals.size(); ++s) assert(totals[s] <= artifact.prior.enrolment[s]+1e-6);
        }
        // An explicit FP finalisation flag makes the shared zero event certain.
        std::fill(finalised.begin(),finalised.end(),true);
        auto final = TurnoutModel::prepare(artifact.prior,units,finalised,history,options);
        assert(final.components.empty());
        for (double probability : final.noAdditionProbability) assert(probability == 1);
        auto finalPlan = TurnoutModel::makeDrawPlan(final,units,artifact.prior.enrolment);
        auto finalDraw = TurnoutModel::drawUnitCounts(final,finalPlan,123);
        for (std::size_t j = 0; j < units.size(); ++j) assert(finalDraw[j] == units[j].counted);
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
    std::cout << path.filename().string() << ": " << cases.size() << " comparison cases passed.\n";
}
}
void checkProcessingTimetables(std::filesystem::path const& root) {
    using namespace TurnoutModel;
    Options options; options.pollClose=sourceHour("2025-03-08T18:00:00");
    options.receiptDeadline=sourceHour("2025-03-13T09:00:00");
    auto absent=processingTimetable(options,"Absent");
    auto provisional=processingTimetable(options,"Provisional");
    assert(absent && absent->referenceHour==*options.pollClose+14*24);
    assert(provisional && provisional->referenceHour==*options.pollClose+18*24);
    assert(!processingTimetable(options,"SIR") && !processingTimetable(options,"early"));
    auto changed=options; changed.receiptDeadline=*options.receiptDeadline+7*24;
    assert(processingTimetable(changed,"Absent")->referenceHour==absent->referenceHour);
    assert(processingTimetable(changed,"Postal")->referenceHour!=processingTimetable(options,"Postal")->referenceHour);
    changed.processingTimetables["Absent"]=ProcessingTimetable{16,2};
    assert(processingTimetable(changed,"Absent")->referenceHour==*options.pollClose+16*24);
    changed.processingTimetables["Absent"]={};
    assert(!processingTimetable(changed,"Absent"));
    changed.pollClose.reset();
    assert(!processingTimetable(changed,"Provisional"));
    changed.processingTimetables["Absent"]=ProcessingTimetable{14,0};
    rejects([&]{(void)processingTimetable(changed,"Absent");});

    // A wholly non-postal account must be identical when the postal clock is
    // moved or removed, including shared progress, batch odds and final amounts.
    Prior prior; prior.seats={"A","B"}; prior.subdivisions={"X","X"};
    prior.groups={"ordinary","absent"}; prior.enrolment={4000,4000};
    prior.totals={{2500,2500},{2900,2900}};
    prior.counts={{1400,1100,1400,1100},{1600,1300,1600,1300}};
    std::vector<Unit> units;
    for (std::size_t s=0;s<2;++s) {
        Unit booth;booth.seat=s;booth.kind="ordinary";booth.name=booth.category="Ordinary";
        booth.weight=1;booth.counted=1200;units.push_back(booth);
        Unit declaration;declaration.seat=s;declaration.group=1;declaration.kind="declaration";
        declaration.name=declaration.category="Absent";declaration.weight=1;declaration.counted=100;
        units.push_back(declaration);
    }
    prior.units=units;
    std::vector<Observation> history;
    for (int k=0;k<9;++k) {
        Observation h;h.hour=*options.pollClose+14*24-48+k*6;
        for (auto const& seat:prior.seats) {
            h.counts[{seat,"Absent"}]=100;
            h.counts[{seat,"allocation:absent"}]=100;
        }
        history.push_back(h);
    }
    auto base=prepare(prior,units,{false,false},history,options);
    changed=options;changed.receiptDeadline=*options.receiptDeadline+20*24;
    auto moved=prepare(prior,units,{false,false},history,changed);
    changed.receiptDeadline.reset();
    auto removed=prepare(prior,units,{false,false},history,changed);
    assert(base.unitMeans==moved.unitMeans && base.unitMeans==removed.unitMeans);
    assert(base.evidence[1].strength==removed.evidence[1].strength);
    assert(base.evidence[1].pooledActivity==removed.evidence[1].pooledActivity);
    assert(base.components[0].batchOdds==removed.components[0].batchOdds);
    assert(std::abs(std::exp(base.components[0].usualLogShift)-.5)<1e-12);
    changed.processingTimetables["Absent"]=ProcessingTimetable{16,2};
    auto wa=prepare(prior,units,{false,false},history,changed);
    assert(wa.components[0].usualLogShift>base.components[0].usualLogShift);
    assert(wa.components[0].batchSurvival>base.components[0].batchSurvival);
    history.back().hour+=1./60;
    auto nearby=prepare(prior,units,{false,false},history,changed);
    assert(std::abs(nearby.components[0].processingPhase-wa.components[0].processingPhase)<.001);

    std::ifstream stream(root/"tests/fixtures/turnout/synthetic-turnout-v1.json");json value;stream>>value;
    value["options"]["declaration_schedule_model"]="live-declaration-schedule-1";
    auto legacy=TurnoutModelIO::read(value);
    assert(processingTimetable(legacy.options,"Absent"));
    value["progress_model"]="live-late-counts-4";
    value["options"]["declaration_schedule_model"]="live-declaration-schedule-2";
    auto oldProgress=TurnoutModelIO::read(value);
    assert(oldProgress.prior.counts==legacy.prior.counts);
    value["options"]["processing_timetables"]={{"Absent",{{"days_after_poll_close",16},{"transition_days",2}}},
        {"Provisional",nullptr}};
    auto configured=TurnoutModelIO::read(value);
    assert(processingTimetable(configured.options,"Absent")->referenceHour==*legacy.options.pollClose+16*24);
    assert(!processingTimetable(configured.options,"Provisional"));
    value["options"]["processing_timetables"]["Postal"]={{"days_after_poll_close",13},{"transition_days",2}};
    configured=TurnoutModelIO::read(value);
    auto postal=processingTimetable(configured.options,"Postal");
    assert(postal && postal->separatePostalProcessing && !postal->countingClock);
    assert(postal->referenceHour==*configured.options.pollClose+13*24);
    assert(configured.options.receiptDeadline==legacy.options.receiptDeadline);
    value["options"]["processing_timetables"]["Absent"]["transition_days"]=0;
    rejects([&]{TurnoutModelIO::read(value);});
    std::cout<<"Independent category clocks, default/overridden timing and legacy inputs passed.\n";
}

void checkPostalAllocationOrder() {
    using namespace TurnoutModel;
    double infinity=std::numeric_limits<double>::infinity();
    for (double precursor : {200.,2000.,20000.}) {
        auto actual=allocateDeclarationAdditions(10000,{std::log(precursor/8500),std::log(1000./8500)},{100,infinity});
        compare({100,9900./9.5},actual,"discarded postal allowance");
    }
    compare({100,9900./9.5},allocateDeclarationAdditions(10000,{10000,std::log(1000./8500)},{100,infinity}),
        "extreme discarded postal weight");
    double previousTotal=0;
    for (int amount=0;amount<=3000;amount+=3) {
        auto actual=allocateDeclarationAdditions(10000,{std::log(20000./8500),std::log(1000./8500)},
            {double(amount),infinity});
        compare(double(amount),actual[0],"postal finishing amount");
        assert(actual[0]+actual[1]>=previousTotal-1e-9 && actual[0]+actual[1]<=10000);
        previousTotal=actual[0]+actual[1];
    }
    auto ordered=allocateDeclarationAdditions(10000,{4,3,-1},{100,200,infinity});
    auto reversed=allocateDeclarationAdditions(10000,{-1,3,4},{infinity,200,100});
    std::reverse(reversed.begin(),reversed.end());
    compare(ordered,reversed,"multiple postal services independent of order");

    // Exercise the actual iteration sampler as well as the allocation helper.
    // Both versions select the same tiny postal finishing remainder; changing
    // only its discarded routine weight must not move the absent account.
    Result base; base.broad.counted={1000};base.noAdditionProbability={0};
    base.broad.unitCounts={{1500,1000}};base.broad.totals={{2500}};
    std::vector<Unit> units(2);
    units[0].counted=1000;units[0].category="Postal";units[1].category="Absent";
    for (std::size_t j=0;j<2;++j) {
        Component c;c.unit=j;c.previousProbability=1;c.capacity={8500};c.reference={j?1000.:500.};
        c.referenceOdds={j?std::log(1000./8500):5.};
        if (!j) {c.completionWeight=1;c.postalCounted=1000;c.postalRemainderLogOdds=-20;}
        base.components.push_back(c);
    }
    auto changed=base;changed.components[0].referenceOdds={20};
    auto plan=makeDrawPlan(base,units,{11000});
    for (unsigned long long seed=0;seed<2048;++seed) {
        auto a=drawUnitCounts(base,plan,seed),b=drawUnitCounts(changed,plan,seed);
        compare(a,b,"main sampler ignores discarded postal allowance");
        assert(a[0]>=1000 && a[1]>=0 && a[0]+a[1]<=11000);
    }
    std::cout<<"Postal finishing allocation, capacity and direct-sampler checks passed.\n";
}

int main(int argc, char** argv) {
    try {
        auto start = std::chrono::steady_clock::now();
        auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(".");
        checkRevisionRounding();
        checkProcessingTimetables(root);
        checkPostalAllocationOrder();
        checkWaEarlyBatches();
        checkSignedCountRevisions();
        checkOperationalInputs(root);
        checkPreparedComponent(root);
        checkLiveAccounts();
        checkClosedProgress();
        checkCompletedCountAccounts();
        checkDistrictEnrolmentConflicts();
        checkIndependentServiceSizes();
        checkTurnoutTransfer();
        checkCountDraws();
        checkLocalCompletion();
        checkPostalRemainder();
        checkPostalSamplerMeans(root);
        checkRoutineRemainderSampler();
        check(root/"tests/fixtures/turnout/synthetic-turnout-v1.json");
        // Operators can additionally check a private source-backed fixture.
        // CI and ordinary cloned-repository tests never need that archive.
        if (argc == 3) check(argv[2]);
        rejects([] { TurnoutModel::sourceHour("2025-02-30T12:00:00"); });
        rejects([] { TurnoutModel::sourceHour("2025-05-03T18:00:00+10:00"); });
        rejects([] { TurnoutModel::sourceHour("2025-05-03T18:00:00."); });
        compare(.885163/3600,TurnoutModel::sourceHour("2024-10-26T21:15:59.885163")
            -TurnoutModel::sourceHour("2024-10-26T21:15:59"),"fractional feed clock");
        compare(.1,TurnoutModel::reportingFactor(78),"PPVC delay");
        // Processing time progresses at half speed on Sunday, without moving
        // the legal receipt date or jumping at midnight. Reverse replay clocks
        // use the same signed duration.
        auto saturday = TurnoutModel::sourceHour("2025-05-17T00:00:00");
        auto monday = TurnoutModel::sourceHour("2025-05-19T00:00:00");
        compare(36,TurnoutModel::countingHours(saturday,monday),"weekend counting time");
        compare(-36,TurnoutModel::countingHours(monday,saturday),"reverse counting time");
        // Optional local full-election checkpoint: normal CI needs only the
        // committed small fixtures and never depends on the archive directory.
        if (argc >= 4) {
            auto loadStarted = std::chrono::steady_clock::now();
            auto a = TurnoutModelIO::load(argv[2]);
            // Historical comparison artifacts may carry their observations,
            // but the operational prior reader does not load history at all.
            std::ifstream archiveInput(argv[2]); json archive; archiveInput >> archive;
            auto archivedHistory = TurnoutModelIO::history(archive.at("history"));
            std::ifstream input(argv[3]); json checkpoint; input >> checkpoint;
            auto units = TurnoutModelIO::units(checkpoint.at("units"));
            std::vector<bool> finalised(a.prior.seats.size());
            for (auto const& name : checkpoint.at("finalised")) for (std::size_t s = 0; s < finalised.size(); ++s) finalised[s] = finalised[s] || name == a.prior.seats[s];
            auto options = a.options; options.ppvcFactor = checkpoint.at("ppvc_factor");
            auto preparationStarted = std::chrono::steady_clock::now();
            auto result = TurnoutModel::prepare(a.prior,units,finalised,archivedHistory,options);
            double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-preparationStarted).count();
            auto means = TurnoutModel::meanTotals(result);
            compare(checkpoint.at("expected_mean_totals"),means,"full-election mean totals");
            compare(checkpoint.at("expected_unit_means"),result.unitMeans,"full-election unit means");
            auto plan = TurnoutModel::makeDrawPlan(result,units,a.prior.enrolment);
            auto drawStarted = std::chrono::steady_clock::now();
            double checksum = 0; int draws = 200000, narunggaZero = 0, narunggaLarge = 0;
            std::vector<double> drawnMeans(a.prior.seats.size());
            auto narungga = std::size_t(std::find(a.prior.seats.begin(),a.prior.seats.end(),"Narungga")-a.prior.seats.begin());
            for (int k = 0; k < draws; ++k) {
                auto sampled = TurnoutModel::drawUnitCounts(result,plan,k); checksum += sampled.front();
                for (std::size_t j = 0; j < units.size(); ++j) drawnMeans[units[j].seat] += sampled[j]/draws;
                if (narungga < a.prior.seats.size()) {
                    double addition = 0;
                    for (std::size_t j = 0; j < units.size(); ++j)
                        if (units[j].seat == narungga) addition += sampled[j]-units[j].counted;
                    narunggaZero += addition == 0; narunggaLarge += addition > 77;
                }
            }
            std::cout << "Count-only sampling: " << draws << " whole-election draws in "
                << std::chrono::duration<double>(std::chrono::steady_clock::now()-drawStarted).count()
                << " seconds; checksum " << checksum << ".\n";
            if (narungga < a.prior.seats.size()) std::cout << "Narungga count draws: no additions "
                << 100.*narunggaZero/draws << "%; more than the 77-vote margin "
                << 100.*narunggaLarge/draws << "%. These are count probabilities, not winner probabilities.\n";
            double meanDifference = 0; std::size_t largest = 0;
            for (std::size_t s = 0; s < means.size(); ++s) if (std::abs(drawnMeans[s]-means[s]) > meanDifference) {
                meanDifference = std::abs(drawnMeans[s]-means[s]); largest = s;
            }
            std::cout << "Main draws versus short diagnostic sample: largest mean-count difference "
                << meanDifference << " votes in " << a.prior.seats[largest] << ".\n";
            std::cout << a.prior.election << ": " << a.prior.seats.size() << " districts, " << a.prior.totals.size()
                << " preparations x " << options.countDraws << " count draws; compute " << seconds
                << " seconds; load and compute " << std::chrono::duration<double>(std::chrono::steady_clock::now()-loadStarted).count() << " seconds.\n";
        }
        if (argc == 6) checkSavedHandoff(argv[4],argv[5]);
        std::cout << "Maximum Python/C++ difference: " << maximumError << "; seconds: "
            << std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count() << "\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << "\n"; return 1; }
}
