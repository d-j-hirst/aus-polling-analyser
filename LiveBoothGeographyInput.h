#pragma once
#include "LiveAutomation.h"
#include "TurnoutModel.h"
#include <cmath>

namespace LiveBoothGeography {
using Key = std::pair<std::string,std::string>;
struct RosterPlace {
    std::string mode;
    double size;
    int sourceId = -1;
    std::optional<double> federal;
};

// Start from the same frozen formal turnout draws and unit weights used by
// the count model. A change in actual turnout is allowed to change the final
// mix of votes. It must not retrospectively change what a booth was expected
// to report. Bind ordinary comparisons by both district and commission ID,
// with the turnout name as a separate check (including shared buildings).
inline std::map<Key,RosterPlace> readRoster(nlohmann::json const& file, TurnoutModel::Prior const& prior) {
    if (file.at("schema_version") != 1 || file.at("election") != prior.election)
        throw std::runtime_error("Federal booth geography has an incompatible election or version.");
    auto const& districts = file.at("districts");
    std::vector<double> means(prior.seats.size()*prior.groups.size());
    for (auto const& draw : prior.counts)
        for (size_t k = 0; k < means.size(); ++k) means[k] += draw.at(k)/prior.counts.size();
    std::map<Key,RosterPlace> places;
    size_t ordinary = 0, supplied = 0;
    for (auto const& district : districts) supplied += district.at("places").size();
    for (auto const& u : prior.units) {
        auto const& seat = prior.seats.at(u.seat);
        RosterPlace p{u.kind == "declaration" ? u.name : u.kind,
            u.closed ? 0. : u.weight*means.at(u.seat*prior.groups.size()+u.group)};
        if (u.kind == "ordinary") {
            ++ordinary;
            auto const& district = districts.at(seat);
            auto const& row = district.at("places").at(u.name);
            double strength = district.at("strength");
            if (!std::isfinite(strength) || strength <= 0 || strength > 1)
                throw std::runtime_error("Invalid federal booth strength for "+seat);
            p.sourceId = row.at("id");
            if (row.value("mobile",false)) p.mode = "mobile";
            if (row.contains("nearest_km")) {
                int on = row.at("one_nation"), formal = row.at("formal");
                double km = row.at("nearest_km");
                if (on < 0 || on > formal || formal <= 0 || !std::isfinite(km) || km < 0 || p.mode != "ordinary")
                    throw std::runtime_error("Invalid federal ordinary counts or distance for "+seat+"/"+u.name);
                // A distant location uses its district ordinary expectation.
                // Half a ballot on either side keeps an observed zero finite.
                if (km <= 10.) p.federal = strength*std::log((on+.5)/(formal-on+.5));
            }
        }
        places.emplace(Key{seat,u.name},p);
    }
    if (supplied != ordinary || districts.size() != prior.seats.size())
        throw std::runtime_error("Federal booth geography and turnout rosters differ. Refresh the prepared input.");
    return places;
}

// The Victorian preparation command installs the operational input. SA uses
// a file in the explicitly selected replay's own directory. Other SA runs
// omit the ordinary-booth file from their inputs and revision fingerprint.
inline std::optional<std::filesystem::path> inputPath(std::filesystem::path const& root,
    std::string const& term, nlohmann::json const& watcher) {
    if (term == "2026vic") return root/"forecasts"/term/"live-inputs"/"one-nation-booths.json";
    if (term != "2026sa" || !watcher.is_object() || !watcher.value("one_nation_booth_geography",false)) return {};
    if (watcher.at("election") != term || watcher.at("kind") != "replay")
        throw std::runtime_error("SA federal booth geography requires its own configured SA replay.");
    std::string id = watcher.at("run_id");
    LiveAutomation::validateRunId(id);
    return root/"live_runs"/id/"live-inputs"/"one-nation-booths.json";
}
}
