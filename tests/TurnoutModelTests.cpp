#include "../TurnoutModelIO.h"
#include "../LiveTurnoutMath.h"
#include "../LiveTurnout.h"
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
            compare(e.at("usual_log_shift"),a.usualLogShift,label+"/routine size shift");
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
        assert(diagnostic.at("maximum_accounting_error").get<double>() < 1e-6);
        assert(diagnostic.at("minimum_category_addition").get<double>() >= -1e-7);
        auto plan = TurnoutModel::makeDrawPlan(actual,units,artifact.prior.enrolment);
        for (unsigned long long k = 0; k < 256; ++k) {
            auto sampled = TurnoutModel::drawUnitCounts(actual,plan,k);
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
int main(int argc, char** argv) {
    try {
        auto start = std::chrono::steady_clock::now();
        auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(".");
        checkOperationalInputs(root);
        checkPreparedComponent(root);
        checkLiveAccounts();
        checkClosedProgress();
        checkCompletedCountAccounts();
        checkCountDraws();
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
