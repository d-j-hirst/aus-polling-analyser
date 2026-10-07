#include "LiveAnalysisArchive.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace LiveAnalysisArchive {
namespace {
using json = nlohmann::json;
using BoothKey = std::pair<std::string, std::string>;
using NameIndexes = std::map<std::string, std::size_t>;

void packParties(json& value, json& catalog) {
    // Every party-valued node previously repeated its labels. A marked reference
    // distinguishes these records from unrelated objects containing an index.
    if (value.is_object()) {
        if (value.contains("party_index") && value.contains("name") && value.contains("abbreviation")) {
            auto const index = value.at("party_index").get<int>();
            auto const key = std::to_string(index);
            json identity = {{"name", value.at("name")}, {"abbreviation", value.at("abbreviation")}};
            if (catalog.contains(key) && catalog.at(key) != identity)
                throw std::runtime_error("Inconsistent party identity in live analysis.");
            catalog[key] = std::move(identity);
            value.erase("party_index");
            value.erase("name");
            value.erase("abbreviation");
            value["party_ref"] = index;
        }
        for (auto& item : value.items()) packParties(item.value(), catalog);
    }
    else if (value.is_array()) for (auto& item : value) packParties(item, catalog);
}

void replaceName(json& record, char const* key, char const* reference, NameIndexes const& indexes) {
    if (!record.contains(key) || !record.at(key).is_string()) return;
    auto const found = indexes.find(record.at(key).get<std::string>());
    if (found == indexes.end()) return;
    record.erase(key);
    record[reference] = found->second;
}

void packReferences(json& value, NameIndexes const& seats, NameIndexes const& regions,
    std::map<BoothKey, std::size_t> const& booths, std::string_view parent = {}, std::string seat = {}) {
    // Turnout units and preference checks refer to the same booth records as the
    // main diagnostic tree. Retain which original label each reference replaces,
    // so expansion restores all section-specific field names without guessing.
    if (value.is_object()) {
        char const* seatKey = value.contains("seat") ? "seat" : "seat_name";
        char const* boothKey = value.contains("booth") ? "booth" : "name";
        if (value.contains(seatKey) && value.at(seatKey).is_string())
            seat = value.at(seatKey).get<std::string>();
        else if (parent == "seats" && value.contains("name") && value.at("name").is_string()
            && seats.contains(value.at("name").get<std::string>()))
            seat = value.at("name").get<std::string>();
        // Preference diagnostics nest their booths inside a seat record. Carry
        // that seat identity down rather than requiring its name on every row.
        if (!seat.empty() && value.contains(boothKey) && value.at(boothKey).is_string()) {
            auto const found = booths.find({seat, value.at(boothKey).get<std::string>()});
            if (found != booths.end()) {
                value.erase(boothKey);
                value[std::string_view(boothKey) == "booth" ? "booth_name_ref" : "name_booth_ref"] = found->second;
            }
        }
        replaceName(value, "seat", "seat_ref", seats);
        replaceName(value, "seat_name", "seat_name_ref", seats);
        replaceName(value, "region_name", "region_name_ref", regions);
        if (parent == "seats") replaceName(value, "name", "name_seat_ref", seats);
        for (auto& item : value.items()) packReferences(item.value(), seats, regions, booths, item.key(), seat);
    }
    else if (value.is_array()) for (auto& item : value) packReferences(item, seats, regions, booths, parent, seat);
}

void packIdentities(json& document) {
    json identities = {{"parties", json::object()}, {"seats", json::array()},
        {"regions", json::array()}, {"booths", json::array()}};
    packParties(document, identities["parties"]);
    NameIndexes seats, regions;
    if (document.contains("seats")) for (auto const& seat : document.at("seats")) {
        auto const name = seat.at("name").get<std::string>();
        if (!seats.emplace(name, seats.size()).second)
            throw std::runtime_error("Duplicate seat identity in live analysis: " + name);
        identities["seats"].push_back(name);
    }
    if (document.contains("regions")) for (auto& region : document.at("regions")) {
        auto const name = region.at("name").get<std::string>();
        auto const index = regions.size();
        if (!regions.emplace(name, index).second)
            throw std::runtime_error("Duplicate region identity in live analysis: " + name);
        identities["regions"].push_back(name);
        region.erase("name");
        region["name_region_ref"] = index;
    }
    std::map<BoothKey, std::size_t> booths;
    if (document.contains("booths")) for (auto& booth : document.at("booths")) {
        BoothKey const key{booth.at("seat_name").get<std::string>(), booth.at("name").get<std::string>()};
        auto const index = booths.size();
        if (!booths.emplace(key, index).second)
            throw std::runtime_error("Duplicate booth identity in live analysis: " + key.first + "/" + key.second);
        json identity = {{"seat_ref", seats.at(key.first)}, {"name", key.second}};
        booth.erase("name");
        booth.erase("seat_name");
        for (auto const* field : {"vote_type", "booth_type", "coords", "same_seat"}) {
            if (booth.contains(field)) {
                identity[field] = std::move(booth[field]);
                booth.erase(field);
            }
        }
        identities["booths"].push_back(std::move(identity));
        booth["booth_identity_ref"] = index;
    }
    packReferences(document, seats, regions, booths);
    document["identities"] = std::move(identities);
}

void countKeys(json const& value, std::map<std::string, std::size_t>& counts) {
    // Give the most repeated field names the shortest codes. This also works
    // for new diagnostic fields without maintaining a separate fixed schema.
    if (value.is_object()) for (auto const& item : value.items()) {
        ++counts[item.key()];
        countKeys(item.value(), counts);
    }
    else if (value.is_array()) for (auto const& item : value) countKeys(item, counts);
}

std::string shortKey(std::size_t index) {
    // The file's ordered field table defines the codes; no permanent global
    // dictionary needs updating when another diagnostic field is introduced.
    constexpr std::string_view alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string result;
    for (;;) {
        result.insert(result.begin(), alphabet[index % alphabet.size()]);
        if (index < alphabet.size()) return result;
        index = index / alphabet.size() - 1;
    }
}

struct Precision {
    bool counts = false;
    bool coordinates = false;
    bool full = false;
};

Precision precisionFor(Precision parent, std::string_view key) {
    // A parent such as fp_votes_projected contains party records whose numeric
    // field is only called "value". Carry its count precision into those rows.
    // Keep parameter tables exact so an archive can still identify its tuning.
    parent.coordinates = parent.coordinates || key == "coords";
    parent.full = parent.full || key.find("parameters") != std::string_view::npos;
    parent.counts = parent.counts || key.find("count") != std::string_view::npos
        || key.find("votes") != std::string_view::npos || key.find("remaining") != std::string_view::npos
        || key.find("total") != std::string_view::npos || key == "model_mean_fp"
        || key == "model_mean_tcp" || key == "model_mean_tpp" || key == "projected_fp"
        || key == "projected_tcp" || key == "projected_tpp";
    return parent;
}

void appendNumber(std::string& out, json const& value, Precision precision) {
    double const number = value.get<double>();
    if (precision.full || !std::isfinite(number)) { out += value.dump(); return; }
    char buffer[64];
    // Integral floats also represent measured counts. Preserve their exact
    // numerical value instead of applying significant-figure rounding to them.
    if (std::floor(number) == number) {
        if (std::abs(number) <= 9007199254740991.) {
            auto const result = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<std::int64_t>(number));
            if (result.ec == std::errc{}) { out.append(buffer, result.ptr); return; }
        }
        out += value.dump();
        return;
    }
    auto const result = std::to_chars(buffer, buffer + sizeof(buffer), number,
        std::chars_format::general, precision.counts || precision.coordinates ? 8 : 6);
    if (result.ec != std::errc{}) { out += value.dump(); return; }
    double rounded = 0;
    auto const parsed = std::from_chars(buffer, result.ptr, rounded);
    if (parsed.ec != std::errc{}
        || (number > 0 && number < 1 && (rounded == 0 || rounded == 1))) {
        // A diagnostic must not falsely report a strict subset as exactly its
        // parent, or erase a very small probability. Keep extra digits there.
        out += value.dump();
    }
    else out.append(buffer, result.ptr);
}

void appendPacked(std::string& out, json const& value,
    std::map<std::string, std::string> const& keys, Precision precision = {}) {
    // Write the packed JSON directly rather than constructing a second large
    // tree. Only floating-point diagnostics are rounded; other values use the
    // existing JSON writer so strings, integer counts and nulls remain intact.
    if (value.is_number_float()) appendNumber(out, value, precision);
    else if (value.is_array()) {
        out.push_back('[');
        bool first = true;
        for (auto const& item : value) {
            if (!first) out.push_back(',');
            first = false;
            appendPacked(out, item, keys, precision);
        }
        out.push_back(']');
    }
    else if (value.is_object()) {
        out.push_back('{');
        bool first = true;
        for (auto const& item : value.items()) {
            if (!first) out.push_back(',');
            first = false;
            out.push_back('"');
            out += keys.at(item.key());
            out += "\":";
            appendPacked(out, item.value(), keys, precisionFor(precision, item.key()));
        }
        out.push_back('}');
    }
    else out += value.dump();
}
} // namespace

std::string serialize(json document) {
    if (!document.is_object()) throw std::runtime_error("Live analysis must be a JSON object.");
    packIdentities(document);
    std::map<std::string, std::size_t> counts;
    countKeys(document, counts);
    std::vector<std::pair<std::string, std::size_t>> ranked(counts.begin(), counts.end());
    std::sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    json fields = json::array();
    std::map<std::string, std::string> keys;
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        fields.push_back(ranked[i].first);
        keys.emplace(ranked[i].first, shortKey(i));
    }
    // Only the small envelope uses permanent descriptive keys. The dictionary
    // and identity catalogs make each compressed archive independently readable.
    std::string out = "{\"format\":\"polling-analyser-live-analysis\",\"version\":1,\"fields\":";
    out += fields.dump();
    out += ",\"data\":";
    appendPacked(out, document, keys);
    out.push_back('}');
    return out;
}
} // namespace LiveAnalysisArchive
