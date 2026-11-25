#pragma once

// Distribute a district's expected home early vote across its named centres.
// The historical evidence and validation are described in
// analysis/reports/2026vic-early-centres/README.md. The neighbours supply
// differences between venues; the current election supplies the district level.
#include "TurnoutModelIO.h"
#include "LiveNewVotes.h"
#include "LiveOrdinaryUpdate.h"

namespace LiveVecCentreGeography {
using Groups = std::array<double,3>; // Labor, both Coalition parties together, Greens.
using Centre = TurnoutModelIO::EarlyCentre;
using Shares = LiveNewVotes::Shares;

inline int group(int party, int nationals, int greens) {
    if (party == 0) return 0;
    if (party == 1 || (nationals >= 0 && party == nationals)) return 1;
    if (greens >= 0 && party == greens) return 2;
    return -1;
}

// Reserve the district share of every other candidate. Divide only the
// three named groups' combined vote, retaining the within-Coalition split.
// Multipliers can be reused when the ordinary count revises the district mix.
inline Shares allocate(Shares votes, Groups const& factors, int nationals, int greens,
    int oneNation = -1, double oneNationContrast = 0.) {
    double available = 0, adjusted = 0;
    for (auto [party,n] : votes) {
        int j = group(party,nationals,greens);
        if (j >= 0) { available += n; adjusted += n*factors[j]; }
    }
    if (adjusted > 0) for (auto& [party,n] : votes) {
        int j = group(party,nationals,greens);
        if (j >= 0) n *= factors[j]*(available/adjusted);
    }
    // In Narracan the federal and supplementary polls also locate ON support.
    // If its district share is p, move the centre by contrast * p * (1-p).
    // This approximates a small shift in log odds while preserving the district
    // share exactly at every starting level: the size-weighted contrasts sum
    // to zero. Other candidates share the adjustment in their existing ratios.
    if (oneNationContrast != 0. && oneNation >= 0 && votes.contains(oneNation)) {
        double total = 0.;
        for (auto const& entry : votes) total += entry.second;
        if (total > 0.) {
            double p = votes.at(oneNation)/total;
            for (auto& [party,n] : votes)
                n *= party == oneNation ? 1.+oneNationContrast*(1.-p) : 1.-oneNationContrast*p;
        }
    }
    return votes;
}

inline std::vector<Groups> primaryFactors(std::vector<Centre> const& sites, Shares const& baseline, int nationals, int greens) {
    std::vector<Groups> factors(sites.size(),{1.,1.,1.});
    LiveOrdinaryUpdate::Mix target(3), sizes;
    for (auto [id,n] : baseline) {
        int j = group(id,nationals,greens);
        if (j >= 0) target[j] += n;
    }
    double mass = LiveOrdinaryUpdate::sum(target);
    if (mass <= 0) return factors;
    std::vector<LiveOrdinaryUpdate::Mix> rows;
    for (auto const& site : sites) if (site.fp) {
        LiveOrdinaryUpdate::Mix row(3);
        for (size_t j=0; j<3; ++j) if (target[j]>0)
            row[j] = std::pow((*site.fp)[j]+.5,.8);
        rows.push_back(std::move(row));
        sizes.push_back(site.volume);
    }
    if (rows.size()<2) return factors;
    double size = LiveOrdinaryUpdate::sum(sizes);
    for (double& n : target) n *= size/mass;
    auto fit = LiveOrdinaryUpdate::fit(rows,sizes,target);
    if (!fit) throw std::runtime_error("Could not balance Victorian early-centre FP shares.");
    size_t row = 0;
    for (size_t i=0; i<sites.size(); ++i) if (sites[i].fp) {
        auto shares = LiveOrdinaryUpdate::tilted(rows[row++],*fit);
        for (size_t j=0; j<3; ++j) if (target[j]>0) factors[i][j] = shares[j]*size/target[j];
    }
    return factors;
}

// A direct historical Labor–Coalition count adds information beyond local
// primaries. Fit its 0.8395-strength log-odds contrast around the original
// FP-implied EVC TPP. A finalist keeps every own FP ballot, so cap each
// centre at what its transferable votes can deliver before balancing. Where
// history has no direct TPP, keep the district preference rates on local FP.
inline std::vector<double> preferenceOffsets(std::vector<Centre> const& sites, std::vector<Shares> const& fp,
    LivePartialCount::Flows const& flows, double districtOffset) {
    std::vector<double> offsets(sites.size(),districtOffset);
    struct Range { double odds, low, high; };
    std::vector<Range> bounds(sites.size());
    double target = 0;
    size_t supported = 0;
    // Finite endpoints avoid introducing exact zero or one for uncertain
    // transfers. They are numerical limits on log odds, not observed rates.
    constexpr double limit = 24;
    for (size_t i=0; i<sites.size(); ++i) if (auto const& tcp = sites[i].tpp) {
        ++supported;
        bounds[i] = {.8395*std::log(((*tcp)[0]+.5)/((*tcp)[1]+.5)),
            LiveNewVotes::firstShare(fp[i],flows,-limit),LiveNewVotes::firstShare(fp[i],flows,limit)};
        target += sites[i].volume*LiveNewVotes::firstShare(fp[i],flows,districtOffset);
    }
    if (!supported) return offsets;
    auto share = [&](size_t i,double shift) {
        auto const& b = bounds[i];
        return std::clamp(LivePartialCount::inverseLogOdds(b.odds+shift),b.low,b.high);
    };
    double lo=-2*limit, hi=2*limit;
    for (int k=0; k<60; ++k) {
        double m=(lo+hi)*.5, votes=0;
        for (size_t i=0; i<sites.size(); ++i) if (sites[i].tpp) votes += sites[i].volume*share(i,m);
        if (votes<target) lo=m; else hi=m;
    }
    double common=(lo+hi)*.5;
    for (size_t i=0; i<sites.size(); ++i) if (sites[i].tpp && bounds[i].high-bounds[i].low>1e-12) {
        double desired=share(i,common);
        lo=-limit; hi=limit;
        for (int k=0; k<55; ++k) {
            double m=(lo+hi)*.5;
            if (LiveNewVotes::firstShare(fp[i],flows,m)<desired) lo=m; else hi=m;
        }
        offsets[i]=(hi+lo)*.5;
    }
    return offsets;
}

// Compare sibling centres' counted ballots with expectations for those same
// locations. A centre reporting its expected mix supplies no shared shift,
// even if it is unusually strong for one party. The existing early-batch
// weight limits extrapolation while most district EVC votes remain.
inline Shares fromOtherCentres(Shares context, Shares const& counted, Shares const& expected, double districtSize) {
    double n = 0;
    for (auto [id,v] : counted) n += v;
    if (n<=0 || context.empty()) return context;
    double w = LiveNewVotes::observedWeight(n,districtSize,LiveNewVotes::Category::AggregatedEarly);
    for (auto& [id,share] : context) {
        double measured = counted.contains(id) ? counted.at(id) : 0;
        double reference = expected.contains(id) ? expected.at(id) : 0;
        share *= std::pow((measured+.5)/(reference+.5),w);
    }
    return LiveNewVotes::normalize(std::move(context));
}
}
