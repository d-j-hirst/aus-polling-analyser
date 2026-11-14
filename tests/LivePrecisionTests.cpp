// Numerical identities at the live boundary: retain exact unchanged inputs,
// but keep genuine small updates and enforce received-count constraints.
#include "../General.h"
#include "../LiveFpIntegration.h"
#include "../LiveNewVotes.h"
#include "../SimulationAudit.h"

#include <cassert>
#include <iostream>
#include <limits>
#include <type_traits>

void testProjectedVoteShareEndpoints()
{
    // A historical one-vote category can be 0% or 100%. Test the full numeric
    // route from that observed percentage through the log-odds transformation
    // and back to expected counts, including fractional turnout targets which
    // previously produced tiny negative remainders. The production projection
    // uses this same percentage-to-count conversion.
    for (double target : {0.,.1,.7,1.234,100.1,35000.123}) {
        for (double percent : {0.,1e-10,25.,50.,75.,100.-1e-10,100.}) {
            double transformed = transformVoteShare(percent);
            double votes = votesFromPercentage(detransformVoteShare(transformed),target);
            assert(std::isfinite(votes));
            assert(votes >= 0 && votes <= target);
            assert(target-votes >= 0);
            if (percent == 0) assert(votes == 0);
            if (percent == 100) assert(votes == target);
        }
    }
    // Preserve small genuine changes rather than replacing all near-endpoint
    // shares with endpoints, and leave invalid input for the caller to reject.
    assert(votesFromPercentage(100.-1e-10,100.) < 100.);
    assert(votesFromPercentage(1e-10,100.) > 0);
    assert(votesFromPercentage(100.+1e-10,100.) > 100.);
    assert(votesFromPercentage(-1e-10,100.) < 0);
    assert(std::isnan(votesFromPercentage(std::numeric_limits<double>::quiet_NaN(),100.)));
}

int main()
{
    testProjectedVoteShareEndpoints();
    // A 1,000-vote district has 50 counted Others, including 30 for the
    // second independent. The projection gives them 20 of 50 outstanding
    // Others. A draw that assigns 8% to the group gives them 3% fixed and
    // 40% of the remaining 3% in that draw only after subtracting all 5% fixed.
    assert(std::abs(LiveFpIntegration::splitCountedCandidate(8,1000,50,100,30,50) - 4.2) < 1e-10);
    assert(LiveFpIntegration::splitCountedCandidate(5,1000,50,50,30,30) == 3);
    assert(LiveFpIntegration::splitCountedCandidate(8,1000,50,100,0,50) == 0);
    static_assert(std::is_same_v<decltype(LiveTurnoutMath::PartyAccount{}.projected)::mapped_type, double>);
    static_assert(std::is_same_v<decltype(getAt(std::map<int,double>{},0,0.f)), double>);
    std::map<int,double> precise{{0,50.000000000001}};
    assert(getAt(precise,0,0.f) == precise.at(0));
    assert(getAt(precise,1,0.f) == 0);

    LiveTurnoutMath::PartyAccount account{{{0,10},{1,20}},{{0,500},{1,500}}};
    for (int step = 0; step <= 10000; ++step) {
        double share = 10 + step*.008;
        std::map<int,double> target{{0,share},{1,100-share}};
        // This is not a zero-count special case: received votes are present,
        // but this allocation already respects their lower bounds.
        assert(LiveFpIntegration::constrainToCounted(account,target) == target);
        auto shifted = target;
        shifted[0] += 1e-10;
        shifted[1] -= 1e-10;
        auto result = LiveFpIntegration::constrainToCounted(account,shifted);
        assert(result.at(0) > target.at(0));
    }
    auto bounded = LiveFpIntegration::constrainToCounted(account,{{0,.1},{1,99.9}});
    assert(bounded.at(0) == 1);
    assert(bounded.at(1) == 99);
    account.counted = account.projected;
    assert((LiveFpIntegration::constrainToCounted(account,{{0,90},{1,10}})
        == std::map<int,double>{{0,50},{1,50}}));

    LiveData::FpIntegrationInformation unavailableWidths;
    unavailableWidths.evidenceGate = .1;
    LiveTurnoutMath::PartyAccount noLocalWeight{{{0,1},{1,1},{2,0}},{{0,400},{1,600},{2,0}}};
    std::map<int,double> unchangedPrior{{0,32.123456789123},{1,67.876543210877},{2,0}};
    auto identity = [](int candidate) { return candidate; };
    assert(LiveFpIntegration::integrate(noLocalWeight,unchangedPrior,unavailableWidths,identity,identity) == unchangedPrior);

    LiveNewVotes::Shares context{{0,.3},{1,.7},{2,0}};
    auto neutral = LiveNewVotes::composition({},context,1000,LiveNewVotes::Category::General);
    assert(neutral == context); // Includes the genuine zero, not a smoothed floor.
    for (auto const& invalid : {LiveNewVotes::Shares{}, LiveNewVotes::Shares{{0,.2},{1,.3}},
        LiveNewVotes::Shares{{0,std::numeric_limits<double>::quiet_NaN()}}}) {
        bool rejected = false;
        try { LiveNewVotes::composition({},invalid,1000,LiveNewVotes::Category::General); }
        catch (std::runtime_error const&) { rejected = true; }
        assert(rejected);
    }
    for (double change : {-1e-10,1e-10}) {
        auto varied = LiveNewVotes::composition({},context,1000,
            LiveNewVotes::Category::General,{},{{0,change}});
        assert((varied.at(0)-context.at(0))*change > 0);
        assert(varied.at(2) == 0);
    }
    auto observed = LiveNewVotes::composition({{0,1},{1,0},{2,0}},context,
        1000,LiveNewVotes::Category::General);
    assert(observed != context);
    assert(observed.at(2) > 0); // An observed zero has ordinary small-sample smoothing.

    // The blind reference and a declaration record containing only zero counts
    // take the same composition route. A separate extra normalisation on one
    // side would manufacture offsets even with double-precision arithmetic.
    for (int step = 1; step <= 10000; ++step) {
        LiveNewVotes::Shares counts{{0,1234.567+step*.01},{1,7654.321},{2,0}};
        auto fractions = LiveNewVotes::normalize(counts);
        auto reference = LiveNewVotes::project({},fractions,10000.123);
        LivePartialCount::Counts zeros{{0,0},{1,0},{2,0}};
        auto current = LiveNewVotes::project(zeros,LiveNewVotes::composition(
            zeros,fractions,10000.123,LiveNewVotes::Category::General),10000.123);
        assert(reference == current);
    }

    // The opt-in trace round-trips every double, including changes well below
    // display precision. Ordinary report formats need not change to expose it.
    double const next = std::nextafter(50.0,100.0);
    auto encoded = nlohmann::json({{"value",next}}).dump();
    assert(nlohmann::json::parse(encoded).at("value").get<double>() == next);
    std::cout << "Live precision tests passed\n";
}
