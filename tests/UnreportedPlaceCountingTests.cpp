// Portable checks for delayed physical returns, including their separation
// from declaration completion and from a diagnostic turnout deficit.
#include "../TurnoutModelIO.h"
#include "../LiveTurnout.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>

namespace {
using namespace TurnoutModel;
using UnreportedPlaceCounting::Role;
void near(double a, double b, double tolerance = 1e-8) { assert(std::abs(a-b) <= tolerance); }

void checkCurves() {
    constexpr double ages[] = {1,2,3,5,7,10,14,21,28};
    constexpr double publicValues[] = {.95,.85,.7,.5,.4,.3,.15,.05,.01};
    constexpr double officeValues[] = {.15,.08,.06,.05,.04,.02,.005,.001,.0002};
    for (int k = 0; k < 9; ++k) {
        double age = ages[k];
        near(UnreportedPlaceCounting::returnProbability(Role::Public,true,age),publicValues[k]);
        near(UnreportedPlaceCounting::returnProbability(Role::Office,false,age),officeValues[k]);
        double p = publicValues[k];
        near(UnreportedPlaceCounting::returnProbability(Role::Public,false,age),p/(20-19*p));
        for (auto type : {Role::Public,Role::Office})
            near(UnreportedPlaceCounting::returnProbability(type,true,age-1e-7),
                UnreportedPlaceCounting::returnProbability(type,true,age+1e-7),1e-6);
    }
    near(UnreportedPlaceCounting::returnProbability(Role::Public,true,0),1);
    near(UnreportedPlaceCounting::returnProbability(Role::Office,true,0),1);
    near(UnreportedPlaceCounting::returnProbability(Role::Public,true,35),.01/1.99);
    assert(UnreportedPlaceCounting::returnProbability(Role::Public,true,365) < 1e-15);
    assert(UnreportedPlaceCounting::role("ppvc","New centre",false,false) == Role::Public);
    assert(UnreportedPlaceCounting::role("ordinary","AEC Divisional Office",false,false) == Role::Office);
    assert(UnreportedPlaceCounting::role("ppvc","BLV Centre",false,false) == Role::None);
    assert(UnreportedPlaceCounting::role("ppvc","EAV Centre",false,false) == Role::None);
    assert(UnreportedPlaceCounting::role("ppvc","Centre",true,false) == Role::None);
    assert(UnreportedPlaceCounting::role("ppvc","Centre",false,true) == Role::None);
    for (double capacity : {0.,1.,40.,100.,30000.})
        for (double z : {-10.,-2.,0.,2.,10.}) {
            double size = UnreportedPlaceCounting::officeSize(capacity,z);
            assert(size >= 0 && size <= capacity);
        }
}

Prior fixture() {
    Prior prior;
    prior.election = "2016fed"; prior.seats = {"Example"}; prior.subdivisions = {"X"};
    prior.groups = {"physical","postal","absent"}; prior.enrolment = {30000};
    for (int k = 0; k < 64; ++k) {
        double change = 1+.004*(k-31.5);
        prior.counts.push_back({12000*change,3000*change,1000*change});
        prior.totals.push_back({16000*change});
    }
    Unit booth; booth.name = "Reported booth"; booth.category = "Ordinary";
    booth.kind = "ordinary"; booth.weight = .5; booth.matched = true;
    Unit publicPlace = booth; publicPlace.name = "Late Public PPVC"; publicPlace.kind = "ppvc"; publicPlace.weight = .4;
    Unit office = booth; office.name = "AEC Divisional Office"; office.weight = .1;
    Unit postal; postal.name = postal.category = "Postal"; postal.kind = "declaration"; postal.group = 1; postal.weight = 1;
    Unit absent = postal; absent.name = absent.category = "Absent"; absent.group = 2;
    prior.units = {booth,publicPlace,office,postal,absent};
    return prior;
}

Result prepareAt(Prior const& prior, std::vector<Unit> const& units, double days, bool enabled) {
    Options options; options.unreportedPlaces = enabled; options.countDraws = 4;
    options.pollClose = sourceHour("2016-07-02T18:00:00");
    options.ppvcFactor = reportingFactor(days*24);
    Observation observation; observation.hour = *options.pollClose+24*days;
    observation.counts[{{"Example"},{"Postal"}}] = units[3].counted;
    observation.counts[{{"Example"},{"Absent"}}] = units[4].counted;
    return prepare(prior,units,{false},{observation},options);
}

void checkMixtureAndAccounting() {
    auto prior = fixture(), original = prior;
    auto units = prior.units; units[0].counted = 6000; units[3].counted = 1000;
    auto legacy = prepareAt(prior,units,10,false), result = prepareAt(prior,units,10,true);
    assert(result.unreportedPlaces.size() == 2);
    near(result.unreportedPlaces[0].probability,.3);
    near(result.unreportedPlaces[1].probability,.02);
    assert(result.unreportedPlaces[0].conditionalMean > 3000); // Normal centre, not a delay-shrunk batch.
    assert(result.unreportedPlaces[1].conditionalMean > 95 && result.unreportedPlaces[1].conditionalMean < 110);
    assert(result.unreportedPlaces[0].extraSizeSd > .5);
    near(result.noAdditionProbability[0],0);
    for (auto j : {0,3,4}) near(legacy.unitMeans[j],result.unitMeans[j]);
    near(meanTotals(result)[0],std::accumulate(result.unitMeans.begin(),result.unitMeans.end(),0.));
    auto oldPlan = makeDrawPlan(legacy,units,prior.enrolment), plan = makeDrawPlan(result,units,prior.enrolment);
    std::vector<double> means(units.size()); int returned = 0;
    constexpr int draws = 120000;
    for (int k = 0; k < draws; ++k) {
        auto before = drawUnitCounts(legacy,oldPlan,k), after = drawUnitCounts(result,plan,k);
        for (auto j : {0,3,4}) near(before[j],after[j]); // Same draws, not only the same average.
        assert(std::accumulate(after.begin(),after.end(),0.) <= prior.enrolment[0]+1e-8);
        for (std::size_t j = 0; j < units.size(); ++j) { assert(after[j] >= units[j].counted); means[j] += after[j]/draws; }
        returned += after[1] > 0;
    }
    near(double(returned)/draws,.3,.005);
    near(means[1],result.unitMeans[1],35);
    near(means[2],result.unitMeans[2],.3);
    for (std::size_t r = 0; r < result.totals.size(); ++r) {
        near(std::accumulate(result.counts[r].begin(),result.counts[r].end(),0.),result.totals[r][0]);
        assert(result.totals[r][0] <= prior.enrolment[0]+1e-8);
    }
    assert(prior.counts == original.counts && prior.totals == original.totals);

    TurnoutModelIO::Artifact artifact; artifact.prior = prior; artifact.decayPpvc = true;
    auto diagnostic = TurnoutModelIO::diagnostic(artifact,units,result,"2016-07-12T18:00:00");
    auto const& seat = diagnostic.at("seats").at(0);
    near(seat.at("mean_total"),meanTotals(result)[0]);
    near(seat.at("physical_count_diagnostic").at("frozen_mean"),12000);
    near(seat.at("physical_count_diagnostic").at("shortfall"),6000);
    assert(!seat.at("physical_count_diagnostic").at("used_to_adjust_forecast").get<bool>());
    near(diagnostic.at("units").at(1).at("unreported_place").at("return_probability"),.3);
    // Changing unused descriptive provenance cannot change any calculation.
    artifact.provenance = {{"physical_shortfall",1000000}};
    assert(prepareAt(prior,units,10,true).unitMeans == result.unitMeans);

    // Even if all old counting is finished, a late place can still report.
    auto independent = result;
    independent.originalNoAdditionProbability = {1}; independent.noAdditionProbability = {0};
    auto independentPlan = makeDrawPlan(independent,units,prior.enrolment);
    bool sawReturn = false, sawNoReturn = false;
    for (int k = 0; k < 1000; ++k) {
        auto sample = drawUnitCounts(independent,independentPlan,k);
        near(sample[3],units[3].counted); near(sample[4],0);
        sawReturn = sawReturn || sample[1] > 0; sawNoReturn = sawNoReturn || sample[1] == 0;
    }
    assert(sawReturn && sawNoReturn);
    // A vanishingly rare Office return still contributes to the stored mean.
    auto rare = prepareAt(prior,units,28,true);
    assert(rare.unitMeans[2] > .015 && rare.unitMeans[2] < .025);
    near(rare.exactNoAdditionProbability[0],rare.originalNoAdditionProbability[0]*.99*.9998);
    std::cout << "PPVC and Office means, main draws, sibling isolation and diagnostic accounting passed.\n";
}

void checkEligibilityAndInitialTransition() {
    auto prior = fixture(); auto units = prior.units;
    units[0].counted = 6000; units[3].counted = 1000;
    auto checkExcluded = [&](auto change) {
        auto changed = units; change(changed[1]);
        auto oldResult = prepareAt(prior,changed,10,false), newResult = prepareAt(prior,changed,10,true);
        assert(newResult.unreportedPlaces.size() == 1 && newResult.unreportedPlaces[0].unit == 2);
        near(oldResult.unitMeans[1],newResult.unitMeans[1]);
    };
    checkExcluded([](auto& u) { u.counted = 4000; });
    checkExcluded([](auto& u) { u.receivedFpTotals = {0,4000,0}; });
    checkExcluded([](auto& u) { u.closed = true; u.closureReason = "Unused"; });
    checkExcluded([](auto& u) { u.eav = true; });
    checkExcluded([](auto& u) { u.incremental = true; });
    checkExcluded([](auto& u) { u.excluded = true; u.exclusionReason = "Not comparable"; });
    auto unconfirmed = units; unconfirmed[1].matched = false;
    near(prepareAt(prior,unconfirmed,10,true).unreportedPlaces[0].probability,.3/(20-19*.3));
    unconfirmed[1].returnConfirmed = true;
    near(prepareAt(prior,unconfirmed,10,true).unreportedPlaces[0].probability,.3);
    Options options; options.unreportedPlaces = true;
    assert(prepare(prior,units,{false},{},options).unreportedPlaces.empty()); // Unknown date.
    options.pollClose = sourceHour("2016-07-02T18:00:00");
    Observation observation; observation.hour = *options.pollClose+24*10;
    assert(prepare(prior,prior.units,{false},{observation},options).unreportedPlaces.empty()); // No count.
    assert(prepare(prior,units,{true},{observation},options).unreportedPlaces.empty());
    auto zero = prepareAt(prior,units,0,true), early = prepareAt(prior,units,1e-6,true);
    near(zero.unitMeans[1],early.unitMeans[1],.001);
    near(zero.unitMeans[2],early.unitMeans[2],.001);
    // The two-day size transition is continuous, while the initial prior's
    // already-wide public distribution receives no duplicated uncertainty.
    auto before = prepareAt(prior,units,2-1e-7,true), after = prepareAt(prior,units,2+1e-7,true);
    near(before.unitMeans[1],after.unitMeans[1],.01);
    auto completed = units; completed[1].counted = 4000; completed[2].closed = true;
    auto untouched = prepareAt(prior,completed,10,false), enabled = prepareAt(prior,completed,10,true);
    assert(enabled.unreportedPlaces.empty() && enabled.totals == untouched.totals && enabled.counts == untouched.counts);
    auto wide = prior;
    for (std::size_t r = 0; r < wide.counts.size(); ++r) {
        double factor = r%2 ? .1 : 1.5;
        for (auto& value : wide.counts[r]) value *= factor;
        wide.totals[r][0] *= factor;
    }
    assert(prepareAt(wide,units,10,true).unreportedPlaces[0].extraSizeSd == 0);
    // A count far above its original expectation can leave very little room.
    // Competing positive returns must fit without altering another category.
    auto tight = prior; tight.enrolment = {20000};
    auto highCounts = units; highCounts[0].counted = 17000; highCounts[3].counted = 2000;
    auto constrained = prepareAt(tight,highCounts,10,true);
    auto plan = makeDrawPlan(constrained,highCounts,tight.enrolment);
    for (int k = 0; k < 4096; ++k) {
        auto counts = drawUnitCounts(constrained,plan,k);
        assert(std::accumulate(counts.begin(),counts.end(),0.) <= 20000+1e-7);
        for (std::size_t j = 0; j < counts.size(); ++j) assert(counts[j] >= highCounts[j].counted);
    }
    std::ifstream file("tests/fixtures/turnout/synthetic-turnout-v1.json");
    nlohmann::json fixture; file >> fixture;
    fixture["count_model"] = "live-counts-15"; // Frozen priors remain compatible.
    fixture["options"]["unreported_places"] = true;
    fixture["units"][0]["return_confirmed"] = true;
    auto artifact = TurnoutModelIO::read(fixture);
    assert(artifact.unreportedPlaces == true && artifact.prior.units[0].returnConfirmed);
    fixture["options"]["unreported_places"] = false;
    assert(TurnoutModelIO::read(fixture).unreportedPlaces == false);
}

void checkLiveComposition() {
    // Exercise the actual count-to-party boundary, with default operational
    // activation and an explicit legacy opt-out. One count draw must supply
    // identical total quantities to FP, classic TPP and a different TCP pair.
    auto prior = fixture(); auto units = prior.units;
    units[0].counted = 6000; units[3].counted = 1000;
    TurnoutModelIO::Artifact input; input.prior = prior; input.decayPpvc = true;
    input.pollClose = sourceHour("2016-07-02T18:00:00"); input.options.pollClose = input.pollClose;
    Observation observation; observation.hour = *input.pollClose+240;
    std::vector<int> indexes{0,1,2,3,4};
    auto prepared = LiveTurnout::Prepared::prepare(input,units,indexes,units.size(),{false},
        {observation},"2016-07-12T18:00:00");
    assert(prepared->diagnostic().at("units").at(1).contains("unreported_place"));
    std::vector<LiveTurnout::UnitComposition> compositions;
    LiveTurnout::SeatComposition seat; seat.name = "Example";
    for (std::size_t j = 0; j < units.size(); ++j) {
        LiveTurnout::UnitComposition unit; unit.booth = j;
        double counted = units[j].counted, target = prepared->fpTarget(j);
        unit.fp = unit.tpp = {{0,.6*counted},{1,.4*counted}};
        unit.tcp = {{10,.6*counted},{11,.4*counted}};
        unit.projectedFp = unit.projectedTpp = {{0,float(.6*target)},{1,float(.4*target)}};
        unit.projectedTcp = {{10,float(.6*target)},{11,float(.4*target)}};
        for (auto const& [party,votes] : unit.fp) { seat.fp[party] += votes; seat.tpp[party] += votes; }
        for (auto const& [party,votes] : unit.tcp) seat.tcp[party] += votes;
        for (auto const& [party,votes] : unit.projectedFp) { seat.projectedFp[party] += votes; seat.projectedTpp[party] += votes; }
        for (auto const& [party,votes] : unit.projectedTcp) seat.projectedTcp[party] += votes;
        compositions.push_back(unit);
    }
    auto ready = prepared->withComposition(compositions,{seat});
    for (int k = 0; k < 4096; ++k) {
        auto draw = ready->draw(k)[0];
        near(draw.fp.at(0)+draw.fp.at(1),draw.tpp.at(0)+draw.tpp.at(1),.01);
        near(draw.fp.at(0)+draw.fp.at(1),draw.tcp.at(10)+draw.tcp.at(11),.01);
        assert(draw.fp.at(0) >= 4200 && draw.fp.at(1) >= 2800);
    }
    input.unreportedPlaces = false;
    auto old = LiveTurnout::Prepared::prepare(input,units,indexes,units.size(),{false},
        {observation},"2016-07-12T18:00:00");
    assert(!old->diagnostic().at("units").at(1).contains("unreported_place"));
    std::cout << "Operational activation, opt-out and shared FP/TCP/TPP count draws passed.\n";
}
}
int main() {
    checkCurves(); checkMixtureAndAccounting(); checkEligibilityAndInitialTransition(); checkLiveComposition();
    std::cout << "Unreported-place curve, eligibility, continuity and bounded-size checks passed.\n";
}
