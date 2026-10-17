#include "../LiveTppComposition.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>

using namespace LiveTppComposition;

void checkClose(double actual, double expected, double tolerance = 1e-8) {
    if (std::abs(actual-expected) > tolerance) {
        std::cerr << actual << " != " << expected << '\n';
        std::abort();
    }
}

void testCountScaleAccounting() {
    // A category's growth changes its weight, never supplies a signed vote
    // correction. Recentring preserves every requested total even far from 50%.
    for (double total : {5., 49., 75., 98.}) {
        auto account = centre({65, 30, 45}, {100, 600, 300}, total);
        checkClose(dot(account.shares, account.fractions), total);
        double sum = 0;
        for (double w : account.contrastWeights) sum += w;
        checkClose(sum, 1);
        for (std::size_t d = 0; d < 3; ++d) {
            double residual = 0;
            for (std::size_t c = 0; c < 3; ++c)
                residual += account.fractions[c]*derivative(account.shares[c])*
                    ((c == d ? 1. : 0.)-account.contrastWeights[d]);
            checkClose(residual, 0);
        }
    }
    auto mixed = centre({80,20}, {100,900}, 26);
    checkClose(mixed.shares[0],80); checkClose(mixed.shares[1],20);
    // Linearising a count-scale mixture agrees with finite differences.
    auto account = centre({75,30}, {30,70}, 50);
    auto shifted = centre({75,30}, {30,70}, share(.00001));
    for (int c = 0; c < 2; ++c)
        checkClose((transform(shifted.shares[c])-transform(account.shares[c]))/.00001,
            account.seatLoading, 1e-6);
    CategoryEvidence complete;
    complete.expected = complete.weight = 1000;
    complete.baselineFirst = complete.weightedBaselineShare = 65000;
    complete.weightedObservedShare = 60000;
    checkClose(observedShare(complete),60);
    // A very unrepresentative early batch still produces a bounded estimate.
    complete.weight = 10; complete.weightedBaselineShare = 100;
    complete.weightedObservedShare = 990;
    assert(observedShare(complete) > 99 && observedShare(complete) < 100);
    auto geography = referenceMixtures({{.1,.4},{.3,.2},{0,0}}, {100,200,0}, {1,2,1});
    for (int c=0;c<2;++c) checkClose(100*geography[0][c]+400*geography[1][c],0);
    assert(geography[2] == Vector({0,0})); // Inactive district has no fabricated mix.
}

void testGroupedChannels() {
    auto fed = categoryCovariance({1,104,102,105}, "fed", false);
    auto vic = categoryCovariance({106,102,105}, "vic", false);
    auto nsw = categoryCovariance({106,102,105}, "nsw", false);
    auto local = categoryCovariance({1,104,102,105}, "fed", true);
    // SD tuning scales the WHOLE covariance matrix, including cross-category
    // terms. This preserves correlations for local and shared departures.
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        checkClose(fed[i][j], 1.21*SharedCovariance[i][j]);
        checkClose(local[i][j], 1.21*LocalCovariance[i][j]);
    }
    auto unchangedSeatPrior = jointPrior({{0},{{9}}},fed,{{0,0,0,0}});
    checkClose(unchangedSeatPrior.moments.covariance[0][0],9);
    double meanVariance = (fed[0][0]+2*fed[0][1]+fed[1][1])/4;
    checkClose(vic[0][0], meanVariance+(fed[0][0]-2*fed[0][1]+fed[1][1])/12);
    assert(vic[0][0] > meanVariance); // Unknown hidden split did not become 50:50 certainty.
    checkClose(vic[1][1], fed[2][2]); // VIC Absent is day-only.
    checkClose(nsw[0][0], fed[0][0]); // NSW early is distinct from its combined absent.
    checkClose(nsw[1][1], (fed[1][1]+2*fed[1][2]+fed[2][2])/4+
        (fed[1][1]-2*fed[1][2]+fed[2][2])/12);
    auto wa = categoryCovariance({104,102}, "wa", false,2025);
    checkClose(wa[0][0],fed[0][0]); checkClose(wa[1][1],nsw[1][1]);
    auto oldWa = categoryCovariance({104,102}, "wa", false,2021);
    checkClose(oldWa[0][0],vic[0][0]); checkClose(oldWa[1][1],fed[2][2]);
    Matrix root;
    assert(factor(vic, root) && factor(nsw, root));
    auto unknown = categoryCovariance({2,103}, "fed", false);
    checkClose(unknown[0][0],25); checkClose(unknown[0][1],0);
    // Two report labels can observe the same underlying early channel. Their
    // singular shared covariance is valid; it must not acquire independent noise.
    auto aliases = categoryCovariance({1,104,102}, "wa", false,2025);
    auto aliasedPrior = jointPrior({{0},{{9}}},aliases,{{0,0,0}});
    auto draw = extend(prepareExtension(aliasedPrior.moments,1),{0},[](auto){return 1.;});
    checkClose(draw[1],draw[2]);
}

void testAlignedSyntheticComparisons() {
    // Fictional non-classic history estimates 35%/45% booth TPP, while the
    // configured district prior is 55%. Reporting the expected FP pattern
    // must not manufacture a negative swing, regardless of which booth arrives.
    Vector old{35,45}, sizes{300,700};
    auto aligned = centre(old,sizes,55);
    double shift = transform(aligned.shares[0])-transform(old[0]);
    for (int reporting=0; reporting<2; ++reporting) {
        CategoryEvidence evidence;
        for (int b=0;b<2;++b) {
            double baseline = share(transform(old[b])+shift);
            evidence.baselineFirst += sizes[b]*baseline;
            if (b != reporting) { evidence.add(sizes[b],0,0,0,0,0); continue; }
            auto received = alignInferredReference(*receivedComparison(sizes[b],0,0,
                transform(old[b]),{},false),shift);
            evidence.add(sizes[b],sizes[b],.5,received.transformedShare-transform(baseline),1,5);
            evidence.weightedObservedShare += sizes[b]*.5*share(received.transformedShare);
            evidence.weightedBaselineShare += sizes[b]*.5*baseline;
        }
        checkClose(observedShare(evidence),55);
        checkClose(evidence.weightedDeviation,0);
        // The matched observation must not train either a seat movement or a
        // shared category correction from the reference gap.
        auto prior = jointPrior({{transform(55)},{{9}}},{{4}},{{0}});
        auto update = prepare(prior.moments,
            {{{1,-.4},transform(observedShare(evidence)),evidence.variance()}});
        checkClose(update.posterior.mean[0],transform(55));
        checkClose(update.posterior.mean.back(),0);
    }
    // The completed DIRECT count must override that prior, not acquire its
    // reference shift. Unequal booth sizes ensure the result is vote weighted.
    CategoryEvidence complete;
    for (int b=0;b<2;++b) {
        double baseline = aligned.shares[b];
        auto raw = *receivedComparison(sizes[b],sizes[b],sizes[b]*.48,{}, {},false);
        auto received = alignInferredReference(raw,shift);
        checkClose(received.transformedShare,raw.transformedShare);
        complete.baselineFirst += sizes[b]*baseline;
        complete.add(sizes[b],sizes[b],1,received.transformedShare-transform(baseline),0,5);
        complete.weightedObservedShare += sizes[b]*share(received.transformedShare);
        complete.weightedBaselineShare += sizes[b]*baseline;
    }
    checkClose(observedShare(complete),48,.003); // Quarter-vote endpoint smoothing only.
}

void testPartialReferenceTranslation() {
    // 200 of 1000 FP ballots have a measured TCP (120 Labor); the other 800
    // have an estimated 40% allocation. Move only that estimated component.
    auto original = *receivedComparison(1000,200,120,{},320,false);
    double shift = transform(55)-transform(40);
    auto aligned = alignInferredReference(original,shift);
    checkClose(aligned.inferredFirst,440);
    double first = (aligned.votes+.5)*share(aligned.transformedShare)/100-.25;
    checkClose(first-aligned.inferredFirst,120);
    checkClose(aligned.inferredFraction,.8);
    checkClose(alignInferredReference(original,0).transformedShare,original.transformedShare);
    // OPV exhaustion is not an inferred missing-preference component.
    auto exhausted = *receivedComparison(1000,800,400,{}, {},true);
    checkClose(alignInferredReference(exhausted,shift).transformedShare,exhausted.transformedShare);
    assert(!receivedComparison(0,0,0,{}, {},false));
    for (double firstPending : {0.,800.}) {
        auto endpoint = alignInferredReference(*receivedComparison(1000,200,120,{},firstPending,false),shift);
        assert(std::isfinite(endpoint.transformedShare) && endpoint.samplingVariance>0);
        double endpointFirst = (endpoint.votes+.5)*share(endpoint.transformedShare)/100-.25;
        checkClose(endpointFirst-endpoint.inferredFirst,120);
    }
}

void testBudgetAndCorrelatedDraws() {
    Moments baseline{{2,-1},{{4,2},{2,4}}};
    auto prior = jointPrior(baseline, {{4,1},{1,9}}, {{.2,-.1},{-.2,.1}});
    checkClose(prior.budgetScale,1);
    auto extension = prepareExtension(prior.moments,2);
    Accumulator measured;
    // Exact orthogonal ensemble verifies all variances/covariances without
    // Monte Carlo tolerances. Actual baseline draws are retained unchanged.
    for (int axis = 0; axis < 4; ++axis) for (double sign : {-1.,1.}) {
        Vector z(4); z[axis] = sign*std::sqrt(4.);
        Vector original{2+2*z[0],-1+z[0]+std::sqrt(3.)*z[1]};
        auto state = extend(extension,original,[&](std::size_t i){return z[2+i];});
        assert(state[0] == original[0] && state[1] == original[1]);
        measured.add(state);
    }
    auto actual = measured.finish();
    for (std::size_t i = 0; i < actual.mean.size(); ++i) {
        checkClose(actual.mean[i],prior.moments.mean[i]);
        for (std::size_t j = 0; j < actual.mean.size(); ++j)
            checkClose(actual.covariance[i][j],prior.moments.covariance[i][j]);
    }
    auto limited = jointPrior(baseline,{{1000}},{{1},{-1}});
    assert(limited.budgetScale < .01 && limited.budgetScale > 0);
    auto stillValid = prepareExtension(limited.moments,2);
    assert(!stillValid.root.empty());
    for (int i = 0; i < 2; ++i) for (int j = 0; j < 2; ++j)
        checkClose(limited.moments.covariance[i][j],baseline.covariance[i][j]);
    // A deterministic baseline seat must not receive additional uncertainty.
    auto fixed = jointPrior({{0,0},{{0,0},{0,1}}},{{4}},{{0},{0}});
    auto fixedDraw = extend(prepareExtension(fixed.moments,2),{0,1},[](auto){return 1.;});
    assert(fixedDraw[0] == 0);
    auto noBudget = jointPrior({{0},{{0}}},{{4}},{{1}});
    assert(noBudget.budgetScale == 0);
    assert(extend(prepareExtension(noBudget.moments,1),{0},[](auto){return 1.;})[0] == 0);
}

void testLocalCancellationAndCompletion() {
    Moments baseline{{0},{{9}}};
    auto joint = jointPrior(baseline,{{4}},{{0}});
    // 60% ordinary and 40% early: local differences are negatively correlated
    // and cancel in the full-seat total, even though neither variance vanishes.
    Matrix exposure{{-.4},{.6}};
    auto local = mappedCovariance(exposure,{{16}});
    assert(local[0][1] < 0);
    std::vector<Observation> raw{{{1,-.4},-2,.0001},{{1,.6},3,.0001}};
    for (int i=0;i<2;++i) local[i][i] += raw[i].variance;
    auto update = prepare(joint.moments,decorrelate(raw,local));
    checkClose(update.posterior.mean[0],0,1e-5); // .6*(-2)+.4*3 = 0.
    assert(update.posterior.covariance[0][0] < .001);
    auto ordinary = prepare(joint.moments,decorrelate({raw[0]},{{local[0][0]}}));
    assert(ordinary.posterior.covariance[0][0] < 9);
    assert(ordinary.posterior.covariance[0][0] > update.posterior.covariance[0][0]);
    assert(ordinary.posterior.covariance[1][1] > 2); // One category does not identify its contrast.
    std::reverse(raw.begin(),raw.end());
    Matrix reverse{{local[1][1],local[1][0]},{local[0][1],local[0][0]}};
    auto reversed = prepare(joint.moments,decorrelate(raw,reverse));
    checkClose(reversed.posterior.mean[0],update.posterior.mean[0]);
    checkClose(reversed.posterior.covariance[0][0],update.posterior.covariance[0][0]);
}

void testRegionalMissDoesNotIdentifyUnseenPostals() {
    // Fictional regions differ by four points and by postal size. An ordinary
    // count must identify that regional miss, not invent extreme postal swings.
    int const seats = 20;
    Moments baseline{Vector(seats),Matrix(seats,Vector(seats,1))};
    Matrix mix(seats,Vector(2));
    for (int s=0;s<seats;++s) {
        for (int t=0;t<seats;++t) if ((s<10)==(t<10)) baseline.covariance[s][t] += 4;
        baseline.covariance[s][s] += 1;
        mix[s] = {0,s<10 ? -.1 : .1};
    }
    auto prior = jointPrior(baseline,{{4,1},{1,6.25}},mix);
    std::vector<Observation> ordinary, paired;
    for (int s=0;s<seats;++s) {
        double postal = s<10 ? .1 : .3;
        Observation o{Vector(seats+2),s<10 ? -2. : 2.,1};
        o.h[s]=1; o.h[seats]=-.3; o.h[seats+1]=-postal;
        ordinary.push_back(o);
        auto early=o; early.value += 2; early.h[seats] += 1;
        paired.push_back(o); paired.push_back(early);
    }
    auto first = prepare(prior.moments,ordinary);
    assert(std::abs(first.posterior.mean[seats+1]) < .1);
    auto oldConnection = jointPrior(baseline,{{4,1},{1,6.25}},Matrix(seats,Vector(2)));
    auto oldResult = prepare(oldConnection.moments,ordinary);
    assert(std::abs(oldResult.posterior.mean[seats+1]) > .1);
    auto later = prepare(prior.moments,paired);
    assert(std::abs(later.posterior.mean[seats+1]) < 2);
    double firstMean=0,laterMean=0,firstVar=0,laterVar=0;
    for (int s=0;s<seats;++s) {
        firstMean+=first.posterior.mean[s]/seats; laterMean+=later.posterior.mean[s]/seats;
        for (int t=0;t<seats;++t) {
            firstVar+=first.posterior.covariance[s][t]/(seats*seats);
            laterVar+=later.posterior.covariance[s][t]/(seats*seats);
        }
    }
    assert(laterMean>firstMean); // Favourable early counts must not drive a negative postal cascade.
    assert(laterVar<firstVar && firstVar<3.1);
    std::cout << "Fictional regional miss: postal " << first.posterior.mean[seats+1]
        << " -> " << later.posterior.mean[seats+1] << "; mean " << firstMean << " -> " << laterMean << '\n';
}

void testInferredPrecision() {
    // The explicit conversion covariance owns preference uncertainty. FP and
    // TCP comparisons with the same quality have identical sampling/scatter
    // precision; the retained half comparison weight affects coverage only.
    for (double coverage : {.001,.01,.1,.5}) {
        CategoryEvidence direct, inferred;
        direct.add(10000,10000*coverage,1,0,0,5,1);
        inferred.add(10000,10000*coverage,.5,0,1,5,1);
        checkClose(inferred.variance()/direct.variance(),
            (1-.5*coverage)/(1-coverage));
    }
    // Uncertain preferences must not double reporting-location heterogeneity.
    CategoryEvidence scattered, inferredScatter;
    for (double movement : {-20.,20.}) {
        scattered.add(10000,100,1,movement,0,0,0);
        inferredScatter.add(10000,100,1,movement,1,0,0);
    }
    checkClose(inferredScatter.variance(),scattered.variance());
    double previous = 0;
    for (int step=0;step<=100;++step) {
        double fraction=step/100.;
        CategoryEvidence partial;
        partial.add(10000,1000,1-.5*fraction,0,fraction,5,1);
        assert(partial.variance() >= previous);
        previous=partial.variance();
    }
}

void testPooledPreferenceEvidence() {
    // PPVC and declaration means already use the ordinary preference fit.
    // Their uncertainty must credit that same evidence, while retaining the
    // local floor even with many matches. Empty categories invent no matches.
    std::map<int, CategoryEvidence> categories;
    categories[0].matchedPreferenceUnits = 20;
    categories[1].matchedPreferenceUnits = 2;
    categories[105].matchedPreferenceUnits = 0;
    checkClose(pooledPreferenceUnits(categories),22);
    Observation ordinary{{1},3,1}, ppvc = ordinary, postal = ordinary, measured = ordinary;
    ordinary.category=0; ppvc.category=1; postal.category=105;
    for (auto row : {ordinary,ppvc,postal}) {
        row.inferredFraction=1; row.unresolvedPreferenceFraction=.3;
        row.localMatchedUnits=pooledPreferenceUnits(categories);
        row.sharedMatchedUnits=1000;
        ObservationBlock block{{row,measured},{{1,0},{0,1}}};
        allowForConversionUncertainty(block,.01);
        checkClose(block.rows[0].localConversionSd,
            std::sqrt(35*.3*localConversionRetention(22)));
        checkClose(block.rows[0].sharedConversionSd,std::sqrt(35*.3/1001));
        checkClose(block.rows[0].value,3);
        checkClose(block.covariance[1][1],1); // Recorded TCP retains its precision.
    }
    categories[1].matchedPreferenceUnits += 3;
    checkClose(pooledPreferenceUnits(categories),25);
    assert(localConversionRetention(25) >= LocalConversionVarianceFloor);
}

void testReportingOrderProgress() {
    ReportingProgress empty, tiny, sparse, complete;
    checkClose(reportingOrderSd(empty,false),0);
    tiny.add(80,80,0); tiny.add(100000,0,0);
    assert(reportingOrderSd(tiny,false) > .99*ReportingOrderInitialSd);
    sparse.add(10000,1000,.8);
    checkClose(sparse.coverage(false),.1); checkClose(sparse.coverage(true),.02);
    assert(reportingOrderSd(sparse,true) > reportingOrderSd(sparse,false));
    complete.add(10000,10000,0);
    checkClose(reportingOrderSd(complete,false),0);
    checkClose(reportingOrderSd(complete,true),0);
    // Broad booth coverage alone cannot make a tiny fraction of votes precise.
    ReportingProgress smallBooths;
    for (int i=0;i<100;++i) smallBooths.add(10,10,0);
    smallBooths.add(100000,0,0);
    assert(smallBooths.coverage(true) < .01);
    double previous = ReportingOrderInitialSd;
    for (int i=0;i<=100;++i) {
        ReportingProgress progress; progress.add(10000,i*100.,0);
        assert(reportingOrderSd(progress,true) <= previous);
        previous=reportingOrderSd(progress,true);
    }
    auto together=tiny; together.include(sparse);
    checkClose(together.expectedVotes,tiny.expectedVotes+sparse.expectedVotes);
    checkClose(together.tcpVotes,tiny.tcpVotes+sparse.tcpVotes);
    Observation early{{1,0,0},-2,1}; early.category=1;
    applyReportingOrder(early,sparse,1);
    assert(early.h == Vector({1,0,0})); // No new early/postal category noise.
}

void testSharedReportingOrderConditioning() {
    Moments baseline{{0},{{4}}};
    auto prior=withReportingOrder(baseline);
    checkClose(prior.covariance[0][0],4);
    checkClose(prior.covariance[0][1],0);
    checkClose(prior.covariance[1][2],ReportingOrderCorrelation);
    assert(prepare(prior,{}).posterior.covariance==prior.covariance);
    ReportingProgress sparse; sparse.add(10000,500,.8);
    Observation row{{1,0,0},-3,1}; row.inferredLocations=true;
    applyReportingOrder(row,sparse,1);
    double orderVariance=std::pow(reportingOrderSd(sparse,false),2);
    // Copying the same sample pattern across many districts cannot average
    // away the shared reporting error. Independent residual noise can shrink.
    for (int locations : {1,10,100}) {
        std::vector<Observation> rows(locations,row);
        auto updated=prepare(prior,rows);
        double residual=orderVariance+1./locations;
        checkClose(updated.posterior.mean[0],-12/(4+residual));
        checkClose(updated.posterior.covariance[0][0],4*residual/(4+residual));
        assert(updated.posterior.covariance[0][0] <= 4);
    }
    // A typical advancing count narrows smoothly even though the TCP-ready
    // subset lags FP. No post-conditioning draw error can inflate these bands.
    double lastVariance=4;
    for (int step=1;step<=100;++step) {
        double fp=step/100., tcp=fp*fp;
        ReportingProgress progress; progress.add(10000,10000*fp,1-tcp/fp);
        Observation inferred{{1,0,0},-1,1/fp}, measured{{1,0,0},-3,1/tcp};
        inferred.inferredLocations=true;
        applyReportingOrder(inferred,progress,1); applyReportingOrder(measured,progress,1);
        auto result=prepare(prior,{inferred,measured});
        assert(result.posterior.covariance[0][0] <= lastVariance+1e-10);
        lastVariance=result.posterior.covariance[0][0];
    }
    // Extension and conditioning must produce the same moments used by the
    // displayed diagnostics. Orthogonal draws avoid Monte Carlo tolerances.
    auto extension=prepareExtension(prior,1);
    auto update=prepare(prior,{row});
    Accumulator draws;
    for (int axis=0;axis<4;++axis) for (double sign : {-1.,1.}) {
        Vector z(4); z[axis]=2*sign;
        auto state=extend(extension,{2*z[0]},[&](std::size_t i){return z[1+i];});
        draws.add(condition(update,state,[&](std::size_t){return z[3];}));
    }
    auto actual=draws.finish();
    for (std::size_t i=0;i<3;++i) {
        checkClose(actual.mean[i],update.posterior.mean[i]);
        for (std::size_t j=0;j<3;++j)
            checkClose(actual.covariance[i][j],update.posterior.covariance[i][j]);
    }
}

void testSparseSampleUncertainty() {
    // A completed booth is still a sparse sample of an uncounted district.
    // More received votes reduce the extrapolation allowance continuously;
    // complete category evidence removes it entirely.
    double previous = 1e100;
    for (double votes : {1., 55., 110., 500., 1000., 5000., 10000., 50000.}) {
        CategoryEvidence evidence;
        evidence.add(50000, votes, 1, -10, 0, 10, 0);
        double base = evidence.variance(), added = evidence.sparseSampleVariance(base);
        checkClose(added, base*std::pow(5000*(1-votes/50000)/votes,2));
        assert(base+added < previous);
        previous = base+added;
        if (votes == 50000) checkClose(added, 0);
    }
    // Coverage counts real received ballots, without halving FP observations
    // a second time. Preference conversion keeps its separate uncertainty.
    CategoryEvidence fp, tcp;
    fp.add(50000,5000,.5,0,1,10,0);
    tcp.add(50000,5000,1,0,0,10,0);
    checkClose(fp.sampleCoverage(),tcp.sampleCoverage());
    checkClose(fp.sparseSampleVariance(10),tcp.sparseSampleVariance(10));

    // Fictional correlated districts demonstrate the practical contract: a
    // tiny sample can move its own district and others, but only modestly.
    // The prior is unchanged, and conditioning still reduces its covariance.
    CategoryEvidence tiny;
    tiny.add(50000,110,1,-12,0,10,0);
    Moments prior{{0,0},{{9,6},{6,9}}};
    Observation old{{1,0},-12,tiny.variance()}, limited=old;
    limited.variance += tiny.sparseSampleVariance(old.variance);
    auto before=prepare(prior,{old}), after=prepare(prior,{limited});
    assert(std::abs(after.posterior.mean[0]) < .01*std::abs(before.posterior.mean[0]));
    assert(std::abs(after.posterior.mean[1]) < .01*std::abs(before.posterior.mean[1]));
    assert(after.posterior.covariance[0][0] <= prior.covariance[0][0]);
    assert(after.posterior.covariance[1][1] <= prior.covariance[1][1]);
}

void testConversionBudget() {
    // Three districts with unequal target sizes, mixed measured/inferred
    // categories and an unreported district. The enlarged block covariance
    // must cover a fully shared conversion error in EVERY linear combination.
    Vector budgets{.2,.3,.4}, fractions{1,.3,.8,0,1,.5};
    Matrix bound(6,Vector(6));
    for (std::size_t s=0;s<budgets.size();++s) {
        ObservationBlock block{{Observation{},Observation{}},Matrix(2,Vector(2))};
        for (int c=0;c<2;++c) {
            block.rows[c].inferredFraction=fractions[2*s+c];
            block.rows[c].unresolvedPreferenceFraction=fractions[2*s+c];
        }
        allowForConversionUncertainty(block,budgets[s]);
        for (int c=0;c<2;++c) for (int d=0;d<2;++d)
            bound[2*s+c][2*s+d]=block.covariance[c][d];
        checkClose(block.rows[0].conversionVariance,35*fractions[2*s]*(1+1/budgets[s]));
    }
    Matrix remainder=bound, root;
    for (int i=0;i<6;++i) for (int j=0;j<6;++j) remainder[i][j]-=35*std::sqrt(fractions[i]*fractions[j]);
    assert(factor(remainder,root));
    checkClose(bound[3][3],0); // A measured category gets no conversion error.

    // Many districts cannot average away the national error. Equivalent
    // shared uncertainty survives splitting; independent local error averages down.
    for (int districts : {1,10,100}) {
        std::vector<Observation> observations;
        for (int s=0;s<districts;++s) {
            Observation row{{1},2,0}; row.inferredFraction=1;
            row.unresolvedPreferenceFraction=1;
            ObservationBlock block{{row},{{.1*districts}}};
            allowForConversionUncertainty(block,1./districts);
            auto rows=decorrelate(block.rows,block.covariance);
            observations.insert(observations.end(),rows.begin(),rows.end());
        }
        auto update=prepare({{0},{{4}}},observations);
        double residual = 35.1 + 35./districts;
        checkClose(update.posterior.mean[0],8/(4+residual));
        checkClose(update.posterior.covariance[0][0],4*residual/(4+residual));
    }
    // Splitting one district into categories must not duplicate the allowance.
    Observation row{{1},2,0}; row.inferredFraction=1;
    row.unresolvedPreferenceFraction=1;
    ObservationBlock split{{row,row},{{.2,0},{0,.2}}};
    allowForConversionUncertainty(split,1);
    auto splitResult=prepare({{0},{{4}}},decorrelate(split.rows,split.covariance));
    checkClose(splitResult.posterior.covariance[0][0],4*70.1/74.1);
}

void testUncertaintyOnlyUpdate() {
    // These fictional districts have independent priors. An extreme direct
    // result in one must not train a signed conversion correction for the
    // other's unchanged FP estimate. Genuine baseline correlations, when
    // present, still propagate ordinary election/regional movements normally.
    Moments prior{{0,0},{{4,0},{0,4}}};
    Observation direct{{1,0},-8,.1}, inferred{{0,1},0,.1};
    inferred.inferredFraction=1;
    inferred.unresolvedPreferenceFraction=.5;
    ObservationBlock block{{inferred},{{.1}}};
    allowForConversionUncertainty(block,.5);
    auto rows=decorrelate(block.rows,block.covariance);
    rows.push_back(direct);
    auto update=prepare(prior,rows);
    checkClose(update.posterior.mean[1],0);
    auto reversed=rows; std::reverse(reversed.begin(),reversed.end());
    auto other=prepare(prior,reversed);
    checkClose(other.posterior.mean[0],update.posterior.mean[0]);
    checkClose(other.posterior.covariance[1][1],update.posterior.covariance[1][1]);
    Matrix reduction=prior.covariance, root;
    for (int i=0;i<2;++i) for (int j=0;j<2;++j) reduction[i][j]-=update.posterior.covariance[i][j];
    assert(factor(reduction,root));

    // Exact orthogonal draws check that scenario conditioning really produces
    // the reported moments, rather than adding unconditioned noise afterwards.
    Accumulator draws;
    for (int axis=0;axis<4;++axis) for (double sign : {-1.,1.}) {
        Vector z(4); z[axis]=2*sign;
        draws.add(condition(update,{2*z[0],2*z[1]},[&](std::size_t i){return z[2+i];}));
    }
    auto measured=draws.finish();
    for (int i=0;i<2;++i) {
        checkClose(measured.mean[i],update.posterior.mean[i]);
        for (int j=0;j<2;++j) checkClose(measured.covariance[i][j],update.posterior.covariance[i][j]);
    }
    // As TCP replaces the inferred part, precision increases smoothly. A
    // complete measured count can determine the result; an all-FP count cannot.
    double previous=4;
    for (int step=0;step<=100;++step) {
        Observation row{{1},2,.0001}; row.inferredFraction=1-step/100.;
        row.unresolvedPreferenceFraction=.5*row.inferredFraction;
        ObservationBlock partial{{row},{{.0001}}};
        allowForConversionUncertainty(partial,.5);
        auto next=prepare({{0},{{4}}},decorrelate(partial.rows,partial.covariance));
        assert(next.posterior.covariance[0][0] <= previous);
        previous=next.posterior.covariance[0][0];
        if (step==100) checkClose(next.posterior.mean[0],2,.0001);
    }
    assert(prepare(prior,{}).posterior.covariance == prior.covariance);
}

void testMatchedConversionPrecision() {
    // Breadth comes from reporting locations, not treating every transferred
    // ballot as an independent estimate of a district's preference behaviour.
    checkClose(matchedPreferenceUnit(500,1),.5);
    checkClose(matchedPreferenceUnit(0,1),0);
    checkClose(matchedPreferenceUnit(500,0),0);
    assert(matchedPreferenceUnit(1e9,1) < 1);
    checkClose(conversionSd(3),std::sqrt(35./4));

    Observation inferred{{1},4,1}; inferred.inferredFraction=1;
    inferred.unresolvedPreferenceFraction=.5;
    double previous = 1e9;
    for (double matches : {0.,1.,10.,100.,1000.,4000.}) {
        inferred.sharedMatchedUnits = matches;
        inferred.localMatchedUnits = matches/151;
        ObservationBlock block{{inferred},{{1}}};
        allowForConversionUncertainty(block,1./151);
        assert(block.rows[0].conversionVariance < previous);
        previous = block.rows[0].conversionVariance;
        auto result=prepare({{0},{{9}}},decorrelate(block.rows,block.covariance));
        assert(result.posterior.covariance[0][0] <= 9);
        if (matches == 4000) {
            // Strong measured calibration no longer turns an FP comparison
            // into a roughly 30-point uncertainty simply because there are
            // many other districts in the election.
            assert(std::sqrt(previous) < 3);
            assert(result.posterior.mean[0] > 2);
        }
    }
    // Sparse local calibration retains uncertainty even when the election's
    // shared correction is well established. New counts never fit a new mean.
    inferred.localMatchedUnits=0;
    ObservationBlock sparse{{inferred},{{1}}};
    allowForConversionUncertainty(sparse,1./151);
    assert(sparse.rows[0].conversionVariance > 17.5);
    assert(sparse.rows[0].conversionVariance < 18.5);
    checkClose(sparse.rows[0].value,4);

    // Category-specific precision must still cover a shared residual for
    // every linear combination, not only the all-ordinary national average.
    Matrix bound(4,Vector(4)), root;
    Vector shared;
    for (int district=0; district<2; ++district) {
        Observation a{{1},0,0}, b=a;
        a.inferredFraction=.7; b.inferredFraction=1;
        a.unresolvedPreferenceFraction=.35; b.unresolvedPreferenceFraction=.5;
        a.sharedMatchedUnits=1000; b.sharedMatchedUnits=10;
        a.localMatchedUnits=20; b.localMatchedUnits=2;
        ObservationBlock block{{a,b},Matrix(2,Vector(2))};
        allowForConversionUncertainty(block,district==0 ? .3 : .7);
        for (int i=0; i<2; ++i) {
            auto const& row=block.rows[i];
            shared.push_back(row.sharedConversionSd);
            for (int j=0; j<2; ++j) bound[2*district+i][2*district+j]=block.covariance[i][j];
        }
    }
    for (int i=0; i<4; ++i) for (int j=0; j<4; ++j) bound[i][j]-=shared[i]*shared[j];
    assert(factor(bound,root));
}

void testPreferencePoolScale() {
    // Direct major-party FPs do not require an estimated transfer. Both
    // Coalition partners count on the same side of the hypothetical TPP.
    std::map<int,int> fp{{0,300},{1,200},{10,100},{2,250},{101,150}};
    checkClose(preferencePoolFraction(fp,10),.4);
    checkClose(preferencePoolFraction(std::map<int,int>{{0,40},{1,60}},10),0);
    checkClose(preferencePoolFraction(std::map<int,int>{},10),0);
    checkClose(conversionShareJacobian(50),1);
    checkClose(conversionShareJacobian(80),1.5625);
    checkClose(localConversionRetention(0),1);
    checkClose(localConversionRetention(3),.2+.8/7);
    assert(std::sqrt(localConversionRetention(10)) < .5);
    assert(localConversionRetention(1e9) >= .2);

    auto row = [](double pool, double evidence, double percent=50.) {
        Observation value{{1},0,0}; value.inferredFraction=1;
        value.unresolvedPreferenceFraction=pool;
        value.localMatchedUnits=evidence;
        value.sharedMatchedUnits=1000;
        value.conversionJacobian=conversionShareJacobian(percent);
        ObservationBlock block{{value},{{0}}};
        allowForConversionUncertainty(block,1);
        return block.rows[0];
    };
    checkClose(std::pow(row(.5,0).localConversionSd,2),17.5);
    checkClose(std::pow(row(.5,3).localConversionSd,2),5.5);
    // Double the pool doubles variance, not SD; a zero pool receives neither
    // local nor shared conversion noise, even without any calibration matches.
    checkClose(row(.4,0).conversionVariance,2*row(.2,0).conversionVariance);
    checkClose(row(0,0).conversionVariance,0);
    checkClose(row(.5,0,80).conversionVariance,
        row(.5,0).conversionVariance*1.5625*1.5625);
    bool rejected=false;
    try { row(-.1,0); } catch (std::logic_error const&) { rejected=true; }
    assert(rejected);
}

void testMeasuredLocationsRemainInformative() {
    // A fictional category has one 1,000-vote measured booth and then an
    // equally sized FP-only booth. Keep the original measured observation;
    // do not increase its error by pooling the conversion allowance into it.
    CategoryEvidence direct, inferred;
    direct.add(9000,0,0,0,0,0);
    direct.add(1000,1000,1,0,0,5,0);
    inferred.add(9000,0,0,0,0,0);
    inferred.add(1000,1000,.5,0,1,5,0);
    Observation measured{{1},2,direct.variance()}, estimated{{1},2,inferred.variance()};
    estimated.inferredFraction=1;
    estimated.unresolvedPreferenceFraction=.5;
    estimated.localMatchedUnits=.5; estimated.sharedMatchedUnits=.5;
    auto before=prepare({{0},{{9}}},{measured});
    ObservationBlock block{{measured,estimated},{{measured.variance,0},{0,estimated.variance}}};
    allowForConversionUncertainty(block,1./151);
    checkClose(block.covariance[0][0],measured.variance);
    checkClose(block.covariance[0][1],0);
    auto after=prepare({{0},{{9}}},decorrelate(block.rows,block.covariance));
    assert(after.posterior.covariance[0][0] < before.posterior.covariance[0][0]);
    assert(after.posterior.mean[0] > before.posterior.mean[0]);
    // A common scatter floor also protects against unlike reporting subsets
    // looking individually homogeneous merely because we split the rows.
    assert(direct.variance(100) > direct.variance());
}

int main() {
    testCountScaleAccounting();
    testAlignedSyntheticComparisons();
    testPartialReferenceTranslation();
    testGroupedChannels();
    testBudgetAndCorrelatedDraws();
    testLocalCancellationAndCompletion();
    testRegionalMissDoesNotIdentifyUnseenPostals();
    testInferredPrecision();
    testPooledPreferenceEvidence();
    testReportingOrderProgress();
    testSharedReportingOrderConditioning();
    testSparseSampleUncertainty();
    testConversionBudget();
    testUncertaintyOnlyUpdate();
    testMatchedConversionPrecision();
    testPreferencePoolScale();
    testMeasuredLocationsRemainInformative();
    std::cout << "Live TPP composition tests passed.\n";
}
