#pragma once

// The 2022 supplementary ballot in Narracan lacked Labor. Use the 2018
// Labor and Greens primary pattern to locate their projected 2026 voters.
// Turnout, the relative mix of the other candidates, and One Nation's own
// geographic forecast still come from the usual current-election estimates.
#include "LiveOrdinaryUpdate.h"
#include "json.h"
#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace LiveNarracanPrimaries {
using Mix = LiveOrdinaryUpdate::Mix;
using Counts = std::array<int,3>; // Labor, Greens, remaining formal ballots in 2018.
inline std::filesystem::path historyPath(std::filesystem::path const& root) {
    return root/"analysis"/"Archived"/"Booth Results"/"2018vic.json";
}

// Read candidate identities from the archived roster. Only ordinary sites
// matching the present district and name will be selected by the caller.
inline std::map<std::string,Counts> readHistory(nlohmann::json const& district) {
    std::map<std::string,int> groups;
    bool labor=false, greens=false;
    for (auto const& [id,candidate] : district.at("candidates").items()) {
        auto party = candidate.at("party").get<std::string>();
        int group = party=="AUSTRALIAN LABOR PARTY" ? 0 : party=="AUSTRALIAN GREENS" ? 1 : 2;
        groups.emplace(id,group);
        labor = labor || group==0;
        greens = greens || group==1;
    }
    if (!labor || !greens) throw std::runtime_error("Missing Labor or Greens in 2018 Narracan comparison.");
    std::map<std::string,Counts> history;
    for (auto const& [name,booth] : district.at("booths").items()) {
        Counts counts{};
        for (auto const& [id,votes] : booth.at("fp").items()) {
            int n=votes.get<int>();
            if (n<0) throw std::runtime_error("Negative historical primary count in Narracan/"+name);
            counts.at(groups.at(id))+=n;
        }
        history.emplace(name,counts);
    }
    return history;
}

struct Venue {
    Mix opening; // Expected Labor, Greens and remaining non-ON ballots after fixing local ON.
    std::optional<Counts> history;
};
struct Fit {
    Mix common;
    std::vector<Mix> local;
};

// Match absolute district targets in ballots. First replace the relative
// pattern among matched booths with the 2018 pattern. Start each party at
// its expected number of ballots across those booths as a group, so a year
// in which Labor was stronger overall only informs where its voters live.
// Finally scale the three groups to the current district targets at fixed
// booth turnout. One Nation ballots were set aside before this calculation.
inline std::optional<Fit> fit(std::vector<Venue> const& venues, Mix const& targets) {
    Mix group(3), oldGroup(3), weights;
    for (auto const& v : venues) if (v.history) {
        for (size_t j=0;j<3;++j) {
            group[j]+=v.opening[j];
            oldGroup[j]+=v.history->at(j);
        }
    }
    if (*std::min_element(oldGroup.begin(),oldGroup.end())<=0 ||
        *std::min_element(group.begin(),group.end())<=0) return {};
    std::vector<Mix> seeds;
    Fit result;
    for (auto const& v : venues) {
        auto seed=v.opening;
        Mix scale(3,1.);
        if (v.history && *std::min_element(seed.begin(),seed.end())>0.) {
            for (size_t j=0;j<3;++j) {
                // Half a ballot avoids ruling a party out at a location just
                // because no-one there chose it in the comparison election.
                double old=std::max(.5,static_cast<double>(v.history->at(j)));
                scale[j]=old*group[j]/(oldGroup[j]*seed[j]);
                seed[j]*=scale[j];
            }
        }
        seeds.push_back(std::move(seed));
        weights.push_back(LiveOrdinaryUpdate::sum(v.opening));
        result.local.push_back(std::move(scale));
    }
    auto common=LiveOrdinaryUpdate::fit(seeds,weights,targets);
    if (!common) return {};
    result.common=*common;
    for (auto& scale : result.local)
        for (size_t j=0;j<3;++j) scale[j]*=result.common[j];
    return result;
}
}
