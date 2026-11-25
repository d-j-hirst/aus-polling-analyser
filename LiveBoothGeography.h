#pragma once

#include "LivePartialCount.h"
#include "LiveData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>

// One Nation's district forecast sets the level of support. Nearby 2025
// House booths describe differences between ordinary polling locations.
// This arithmetic is shared by the blind reference and the interpretation
// of election-night returns. All shares here are fractions of formal FP.
namespace LiveBoothGeography {
using Shares = std::map<int,double>;

struct Point { double odds, weight; }; // Natural log odds and formal ballot weight.
struct Place {
    std::string mode;
    double size = 0; // Frozen pre-count expected formal ballots at this unit.
    std::optional<double> federal; // Strength times the pooled federal log odds, ordinary sites only.
    double previousOn = 0, previousFormal = 0; // Genuine same-district evidence about voting modes.
};
struct Reference {
    bool ordinary = false, districtAverage = false;
    double share = 0, modeShare = 0;
    double absoluteShare = 0; // Destination share inferred from the ordinary returns.
};
struct District {
    Shares opponentFactors;
    std::vector<Point> located; // Pre-count mix represented by a remote ordinary booth.
    double locatedSize = 0;
    std::optional<double> ordinaryMovement; // Log-odds shift fitted to accepted ordinary returns.
    struct OlderPrimaries {
        int greens;
        std::map<int,std::array<int,3>> counts; // Historical ALP, GRN and remaining FP at a matched ordinary site.
        std::map<int,Shares> factors;
        bool covers(int party) const { return party==0 || party==greens; }
    };
    std::optional<OlderPrimaries> olderPrimaries;
    Shares const& factorsFor(int booth) const {
        if (olderPrimaries) {
            auto it=olderPrimaries->factors.find(booth);
            if (it!=olderPrimaries->factors.end()) return it->second;
        }
        return opponentFactors;
    }
};
struct Prepared {
    int party = -1;
    std::vector<std::optional<Reference>> units;
    std::vector<District> districts;
};

// Find the common movement that makes the sum of predicted party ballots
// match the specified proportion of formal ballots. On election night the
// weights are the votes actually reported at those particular locations.
inline double commonShift(std::vector<Point> const& points, double target) {
    double low = 0, high = 0, total = 0;
    double const centre = LivePartialCount::logOdds(target);
    for (auto const& p : points) if (p.weight > 0) {
        double shift = centre - p.odds;
        low = total > 0 ? std::min(low,shift) : shift;
        high = total > 0 ? std::max(high,shift) : shift;
        total += p.weight;
    }
    if (total <= 0) throw std::logic_error("A geographic comparison needs positive formal weight.");
    for (int i = 0; i < 60 && high-low > 1e-13; ++i) {
        double mid = (low+high)*.5, votes = 0;
        for (auto const& p : points) votes += p.weight*LivePartialCount::inverseLogOdds(p.odds+mid);
        if (votes < target*total) low = mid;
        else high = mid;
    }
    return (low+high)*.5;
}

struct Mode { double size = 0, on = 0, formal = 0, share = 0; };
using ModeShifts = std::map<std::string,double>;

// The Victorian district median includes all voting methods. Reserve the
// absent, postal and other-early premiums around a single home-voter level.
// Give each group its expected number of formal ballots and solve for the
// level that returns the original district median. The remaining categories
// carry forward their relative historical levels when these are available.
inline void centreDeclarationModes(std::map<std::string,Mode>& modes, ModeShifts const& shifts, double target) {
    auto home = [](std::string const& name) {
        return name == "ordinary" || name == LiveData::voteTypeName(LiveData::VoteType::EvcEarly);
    };
    std::vector<Point> homeModes, allModes;
    double homeVotes = 0, homeSize = 0;
    for (auto const& [name,m] : modes) if (home(name) && m.size > 0) {
        homeModes.push_back({LivePartialCount::logOdds(m.share),m.size});
        homeVotes += m.share*m.size;
        homeSize += m.size;
    }
    if (homeSize <= 0) throw std::runtime_error("No in-person turnout for the Victorian declaration reference.");
    double homeOdds = LivePartialCount::logOdds(homeVotes/homeSize);
    std::map<std::string,double> offsets;
    for (auto const& [name,m] : modes) {
        auto shift = shifts.find(name);
        double odds = home(name) ? homeOdds : LivePartialCount::logOdds(m.share);
        if (shift != shifts.end()) odds = homeOdds + shift->second;
        offsets[name] = odds;
        allModes.push_back({odds,m.size});
    }
    double districtShift = commonShift(allModes,target);
    // The district's ordinary booths and home early centres may themselves
    // have voted differently. Retain that difference inside the newly
    // centred home pool. Their combined share is the exact federal reference.
    double homeShare = LivePartialCount::inverseLogOdds(homeOdds + districtShift);
    double homeShift = commonShift(homeModes,homeShare);
    for (auto& [name,m] : modes) {
        double odds = home(name) ? LivePartialCount::logOdds(m.share) + homeShift : offsets.at(name) + districtShift;
        m.share = LivePartialCount::inverseLogOdds(odds);
    }
}

// Estimate voting-mode levels first, then vary ordinary sites around their
// own level. A missing former candidacy supplies no contrast between modes.
// A remote ordinary booth also gets its mode average. In both steps only
// locations with evidence need an intercept to preserve the supplied mean.
inline std::vector<Reference> baseline(std::vector<Place> const& places, double seatShare,
    ModeShifts const& declarationShifts = {}) {
    std::map<std::string,Mode> modes;
    for (auto const& place : places) {
        auto& m = modes[place.mode];
        m.size += place.size; m.formal += place.previousFormal; m.on += place.previousOn;
    }
    std::vector<Point> observedModes, locatedBooths;
    auto odds = [](Mode const& m) { return std::log((m.on+.5)/(m.formal-m.on+.5)); };
    for (auto const& [name,m] : modes) if (m.formal > 0 && m.size > 0)
        observedModes.push_back({odds(m),m.size});
    double modeShift = observedModes.empty() ? 0 : commonShift(observedModes,seatShare);
    for (auto& [name,m] : modes) m.share = m.formal > 0 && !observedModes.empty()
        ? LivePartialCount::inverseLogOdds(odds(m)+modeShift) : seatShare;
    if (!declarationShifts.empty()) centreDeclarationModes(modes,declarationShifts,seatShare);
    for (auto const& p : places) if (p.federal && p.size > 0)
        locatedBooths.push_back({*p.federal,p.size});
    double localShift = locatedBooths.empty() ? 0 : commonShift(locatedBooths,modes.at("ordinary").share);
    std::vector<Reference> result;
    for (auto const& p : places) {
        double modeShare = modes.at(p.mode).share;
        double share = p.federal && !locatedBooths.empty()
            ? LivePartialCount::inverseLogOdds(*p.federal+localShift) : modeShare;
        bool ordinary = p.mode == "ordinary";
        result.push_back({ordinary,ordinary && !p.federal,share,modeShare,share});
    }
    return result;
}

// Count an observed zero as a result. Only an all-zero or unanimous pooled
// return needs a half-ballot correction to give finite odds. An empty set of
// returns supplies no movement. A booth meeting its expectation supplies zero.
inline std::optional<double> movement(std::vector<Point> const& reported, double on, double formal) {
    if (formal <= 0) return {};
    double share = on > 0 && on < formal ? on/formal : (on+.5)/(formal+1);
    return commonShift(reported,share);
}

// After a genuine swing the district average is the weighted mean of the
// shifted shares. Applying the swing to the old mean directly gives a
// different answer on log odds. Give remote ordinary places this same
// representative mix whether they report first or are still outstanding.
inline double shiftedShare(Reference const& reference, District const& district, double shift) {
    if (shift == 0) return reference.share;
    if (!reference.districtAverage || district.locatedSize <= 0)
        return LivePartialCount::inverseLogOdds(LivePartialCount::logOdds(reference.share)+shift);
    double on = 0;
    for (auto const& p : district.located)
        on += p.weight*LivePartialCount::inverseLogOdds(p.odds+shift);
    return on/district.locatedSize;
}

inline void includeRemoteReturns(std::vector<Point>& reported, District const& district,
    double remoteVotes, double ordinaryShare) {
    if (remoteVotes <= 0) return;
    if (district.locatedSize == 0) reported.push_back({LivePartialCount::logOdds(ordinaryShare),remoteVotes});
    else for (auto const& p : district.located)
        reported.push_back({p.odds,remoteVotes*p.weight/district.locatedSize});
}

// Fit the district movement to the actual volumes at reporting ordinary
// booths. One extra percentage point at a large booth represents more votes
// than at a small one. Carry that fitted movement to each destination, where
// its effect depends on the destination's own starting share. Early and
// postal batches can subsequently update their own separate estimates.
struct Return { double on = 0, formal = 0; };
inline std::vector<Reference> prepareDistrict(std::vector<Place> const& places,
    std::vector<Return> const& accepted, double seatShare, District& district, ModeShifts const& declarationShifts = {}) {
    auto refs = baseline(places,seatShare,declarationShifts);
    district = District{};
    std::vector<Point> reported;
    double remoteVotes = 0, ordinaryShare = seatShare;
    Return total;
    for (size_t j = 0; j < refs.size(); ++j) {
        auto const& ref = refs[j];
        if (!ref.ordinary) continue;
        ordinaryShare = ref.modeShare;
        double odds = LivePartialCount::logOdds(ref.share);
        if (!ref.districtAverage && places[j].size > 0) {
            district.located.push_back({odds,places[j].size});
            district.locatedSize += places[j].size;
        }
        auto const& count = accepted.at(j);
        if (count.formal <= 0) continue;
        if (ref.districtAverage) remoteVotes += count.formal;
        else reported.push_back({odds,count.formal});
        total.on += count.on;
        total.formal += count.formal;
    }
    includeRemoteReturns(reported,district,remoteVotes,ordinaryShare);
    district.ordinaryMovement = movement(reported,total.on,total.formal);
    for (auto& ref : refs)
        ref.absoluteShare = shiftedShare(ref,district,district.ordinaryMovement.value_or(0.));
    return refs;
}

// Reserve the chosen party share exactly. The other candidates divide the
// remaining ballots in their existing proportions, optionally tilted by the
// factors that preserve their district totals when building the baseline.
// The output has the requested number of ballots (or one for a share table).
inline void allocate(Shares& votes, int party, double share, double size, Shares const& factors = {}) {
    if (!votes.contains(party)) throw std::logic_error("Geographic party missing from the candidate account.");
    double others = 0;
    for (auto& [id,v] : votes) if (id != party) {
        if (auto f = factors.find(id); f != factors.end()) v *= f->second;
        others += v;
    }
    if (!(others > 0) && share < 1) throw std::runtime_error("No other candidate in the geographic FP allocation.");
    for (auto& [id,v] : votes) v = id == party ? size*share : (others > 0 ? size*(1-share)*(v/others) : 0);
}
}
