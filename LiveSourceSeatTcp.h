#pragma once

// Estimate what observed TCP swings in transferred territory imply for its
// unreported ordinary booths. All shares below are for the same first finalist.
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace LiveSourceSeatTcp {
struct Venue {
    double size, first, lower, upper; // Projected votes and guaranteed bounds.
    std::string source;               // Empty outside comparable transferred territory.
    double previousShare = 0., counted = 0., countedFirst = 0.;
    bool pending = false;
};

struct Curve {
    // District percentage-point changes indexed by the unadjusted Labor TCP
    // percentage. Interpolation keeps draws continuous and avoids revisiting
    // every venue in every iteration.
    std::array<double,101> points{};
    struct Evidence { std::string source; int in, pending; double votes, remaining, swing; };
    std::vector<Evidence> evidence;
    double change(double percent) const {
        double p = std::clamp(percent,0.,100.);
        int low = std::min(99,int(p));
        return std::lerp(points[low],points[low+1],p-low);
    }
};

inline std::optional<Curve> prepare(std::vector<Venue> const& venues) {
    struct Area {
        double counted = 0., swingVotes = 0., locations = 0., pendingSize = 0.;
        int reported = 0;
        std::vector<Venue const*> pending;
    };
    std::map<std::string,Area> areas;
    double size = 0.,first = 0.,upper = 0.,lower = 0.;
    for (auto const& v : venues) {
        if (v.size<=0) continue;
        if (v.lower>v.upper || v.lower<0 || v.upper>v.size) return {};
        size += v.size;
        first += std::clamp(v.first,v.lower,v.upper);
        upper += v.upper;
        lower += v.lower;
        if (v.source.empty()) continue;
        auto& area = areas[v.source];
        if (v.counted>0) {
            ++area.reported;
            area.counted += v.counted;
            area.swingVotes += v.countedFirst-v.previousShare*v.counted;
            // One small return is less informative than a large one and no
            // single venue counts for more than one independent location.
            area.locations += v.counted/(v.counted+250.);
        } else if (v.pending) {
            area.pending.push_back(&v);
            area.pendingSize += v.size;
        }
    }
    std::erase_if(areas,[](auto const& item) {
        return item.second.counted<=0 || item.second.pending.empty();
    });
    if (areas.empty()) return {};
    Curve result;
    for (auto const& [source,area] : areas)
        result.evidence.push_back({source,area.reported,static_cast<int>(area.pending.size()),
            area.counted,area.pendingSize,100*area.swingVotes/area.counted});
    for (int p=0;p<=100;++p) {
        // A draw with a different seat TCP implies different uncounted booth
        // shares. Allocate its difference from the native booth estimates in
        // proportion to ballots free to move in that direction. This matches
        // the district draw wherever feasible while respecting recorded votes.
        double delta = p*.01*size-first;
        double room = delta>0 ? upper-first : first-lower;
        double fraction = room>0 ? std::clamp(delta/room,-1.,1.) : 0.;
        auto benchmark = [&](Venue const& v) {
            double base = std::clamp(v.first,v.lower,v.upper);
            return base+fraction*(fraction>0 ? v.upper-base : base-v.lower);
        };
        double change = 0.;
        for (auto const& [source,area] : areas) {
            double implied = 0.;
            for (auto v : area.pending) implied += benchmark(*v)-v->size*v->previousShare;
            double gap = area.swingVotes/area.counted-implied/area.pendingSize;
            double movement = gap*area.locations/(1.+area.locations);
            for (auto v : area.pending) {
                double base = benchmark(*v);
                // If 70% of the booth already voted FP for the two finalists,
                // apply the swing correction to at most the other 30%.
                change += std::clamp(base+movement*(v->upper-v->lower),v->lower,v->upper)-base;
            }
        }
        result.points[p] = 100*change/size;
    }
    return result;
}
}
