#pragma once

#include "LiveBoothGeography.h"
#include "LiveData.h"
#include "LiveNewVotes.h"
#include "json.h"
#include <filesystem>
#include <fstream>
#include <string_view>

// Estimate One Nation against all other candidates among voters who cast
// an absent or postal ballot. The preceding federal election supplies the
// difference between that group and in-person voters in the same district.
// The state count supplies the district level and each group's own returns.
// Shares are fractions of formal first preferences. The prior changes only
// the expected allocation of the outstanding ballots.
namespace LiveFederalDeclarations {
using LiveData::VoteType;
using Prior = std::map<VoteType, double>; // Weighted changes in natural log odds relative to in-person.

// The SA adapter labels away early votes PrePoll. The same label in another
// jurisdiction can describe a different population, so opt in by election.
// The watcher fingerprints the same versioned prior that the calculation reads.
inline std::optional<std::filesystem::path> inputPath(std::filesystem::path const& root, std::string_view term) {
    if (term != "2026sa" && term != "2026vic") return {};
    return root / "forecasts" / term / "one-nation-declaration-prior.json";
}

// Each state's file measures differences within its own federal divisions.
// The early-away category transfers to SA PrePoll and VEC Other early.
// Victoria's zero weight gives these remaining voters their district's
// in-person share until the category itself reports.
inline Prior readPrior(std::filesystem::path const& path, std::string_view election) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot read One Nation declaration prior: " + path.string());
    auto file = nlohmann::json::parse(input);
    if ((election != "2026sa" && election != "2026vic") || file.at("schema_version") != 1 ||
        file.at("election").get<std::string>() != election || file.at("federal_election") != "2025fed")
        throw std::runtime_error("Incompatible One Nation declaration prior: " + path.string());
    Prior result;
    auto early = election == "2026vic" ? VoteType::OtherEarly : VoteType::PrePoll;
    for (auto [mode, key] : {std::pair{VoteType::Absent,"Absent"}, {early,"PrePoll"}, {VoteType::Postal,"Postal"}}) {
        double shift = file.at("federal_log_odds").at(key);
        auto weightName = mode == VoteType::OtherEarly ? "early" : mode == VoteType::Postal ? "postal" : "absent";
        double weight = file.at("weights").at(weightName);
        if (!std::isfinite(shift) || !std::isfinite(weight) || weight < 0 || weight > 1)
            throw std::runtime_error("Invalid federal voting-type difference or transfer weight: " + path.string());
        result[mode] = weight * shift;
    }
    return result;
}

inline bool homeInPerson(std::string_view election, VoteType mode, LiveData::BoothType booth, std::string_view name) {
    // SA names mobile teams but gives them the same booth classification as
    // an ordinary polling place. Physical early centres belong in the
    // reference because federal ordinary includes home-division early voting.
    bool saMobile = election == "2026sa" && name.ends_with(" Mobile");
    return mode == VoteType::EvcEarly || (mode == VoteType::Ordinary && (booth == LiveData::BoothType::Ppvc ||
        (booth == LiveData::BoothType::Normal && !saMobile)));
}

struct HomeVote {
    double on = 0, formal = 0; // Accepted plus projected outstanding votes across the district's physical booths.
    double counted = 0;       // Accepted formal votes only, establishing whether this district has reported.
};

struct Estimate {
    int party;
    double context;  // In-person share shifted by the historical difference, before blending any local batch.
    double original; // Unadjusted model share of the outstanding ballots at the prepared count.
    double future;   // New central share after incorporating this category's own count once.

    // Keep the local uncertainty already sampled around the model's central
    // forecast. Its change in log odds carries across to the new centre.
    // A shift in the median introduces no second draw or uncertainty scale.
    double shareFor(double originalDraw, bool varied) const {
        if (!varied || originalDraw == original) return future;
        double movement = LivePartialCount::logOdds(originalDraw) - LivePartialCount::logOdds(original);
        return LivePartialCount::inverseLogOdds(LivePartialCount::logOdds(future) + movement);
    }
};

inline std::optional<Estimate> estimateFromContext(int party, double context,
    double countedOn, double countedFormal, double expectedFormal, double original, VoteType mode) {
    if (expectedFormal <= countedFormal) return {};
    double localShare = (countedOn + .5) / (countedFormal + 1);
    // The two half ballots make a genuine observed zero usable on a small
    // count. Early-away batches can arrive from different places over time
    // and retain the live model's slower rate of learning.
    auto category = LiveNewVotes::compositionCategory(false, LiveData::aggregatedEarly(mode));
    double weight = LiveNewVotes::observedWeight(countedFormal, expectedFormal, category);
    return Estimate{party, context, original, (1 - weight) * context + weight * localShare};
}

inline std::optional<Estimate> estimate(int party, HomeVote const& home, double shift,
    double countedOn, double countedFormal, double expectedFormal, double original, VoteType mode) {
    // Once home voting has reported, use the estimated complete in-person
    // vote, so the first small booth cannot set the level for the district.
    if (home.counted <= 0 || home.formal <= 0) return {};
    double homeShare = (home.on + .5) / (home.formal + 1);
    double context = LivePartialCount::inverseLogOdds(LivePartialCount::logOdds(homeShare) + shift);
    return estimateFromContext(party, context, countedOn, countedFormal, expectedFormal, original, mode);
}

// Divide the outstanding ballot total once. The other candidates retain
// their relative shares. A unanimously projected future pool has no relative
// shares for opponents, so use their smoothed reported counts in that case.
// Counted ballots themselves are added back by the ordinary FP account.
inline void allocate(LiveNewVotes::Shares& remaining, double total, Estimate const& adjustment,
    LivePartialCount::Counts const& counted, bool varied) {
    double share = adjustment.shareFor(remaining.at(adjustment.party) / total, varied);
    bool hasOtherVotes = std::any_of(remaining.begin(), remaining.end(), [&](auto const& candidate) {
        return candidate.first != adjustment.party && candidate.second > 0;
    });
    if (!hasOtherVotes)
        for (auto& [id, votes] : remaining) if (id != adjustment.party)
            votes = .5 + LivePartialCount::value(counted, id);
    LiveBoothGeography::allocate(remaining, adjustment.party, share, total);
}
}
