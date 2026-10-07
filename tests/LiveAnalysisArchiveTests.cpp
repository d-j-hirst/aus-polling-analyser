#include "../LiveAnalysisArchive.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

// Report a failed assertion to console-based runners, including native Windows
// runners where the CRT's assertion dialog can otherwise hide the explanation.
void check(bool condition, int line) {
    if (!condition) throw std::runtime_error("Archive check failed on line " + std::to_string(line));
}
#define CHECK(condition) check(bool(condition), __LINE__)

json expandFields(json const& value, json const& fields) {
    if (value.is_array()) {
        json result = json::array();
        for (auto const& item : value) result.push_back(expandFields(item, fields));
        return result;
    }
    if (!value.is_object()) return value;
    json result = json::object();
    std::string const alphabet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (auto const& item : value.items()) {
        std::size_t index = 0;
        for (char letter : item.key()) {
            auto const digit = alphabet.find(letter);
            CHECK(digit != std::string::npos);
            index = index * alphabet.size() + digit + 1;
        }
        result[fields.at(index - 1).get<std::string>()] = expandFields(item.value(), fields);
    }
    return result;
}

int main(int argc, char** argv) try {
    // This artificial election tests the export contract without storing feeds
    // or running forecast preparation. It also supplies a cross-language sample
    // when an explicit output filename is passed to the test executable.
    json party = {{"party_index", 0}, {"name", "Example Party"}, {"abbreviation", "EX"}, {"value", 1234567}};
    json node = {{"fp_votes_current", json::array({party})},
        {"tcp_shares_baseline", json::array()}, {"tpp_share_baseline", nullptr},
        {"bias", .123456789}, {"fp_completion", std::nextafter(1., 0.)},
        {"small_probability", 1.23456789e-18}, {"exact_integer_float", 1234567.},
        {"model_mean_fp", 119233.2387},
        {"wide_integer", std::uint64_t(18446744073709551600ULL)},
        {"non_finite_example", {{"non_finite", "negative_infinity"}}}};
    json source = {
        {"regions", json::array({{{"name", "Example Region"}}})},
        {"seats", json::array({{{"name", "Example Seat"}, {"region_name", "Example Region"}, {"node", node}}})},
        {"booths", json::array({{{"name", "Example PPVC"}, {"seat_name", "Example Seat"},
            {"vote_type", "Ordinary"}, {"booth_type", "PPVC"}, {"coords", { -33.123456789, 151.123456789}},
            {"same_seat", true}, {"node", node}}})},
        {"turnout", {{"allocation_parameters", {{"coefficient", .12345678901234567}}},
            {"seats", json::array({{{"name", "Example Seat"}, {"mean_total", 119233.2387}}})},
            {"units", json::array({{{"seat", "Example Seat"}, {"name", "Example PPVC"},
                {"balanced_remaining_before_release", 12345.67891}, {"closure_reason", ""}}})}}},
        {"preference_rechecking", {{"seats", json::array({{{"seat", "Example Seat"},
            {"booths", json::array({{{"booth", "Example PPVC"}, {"mean_revision_votes", -2.345678912}}})}}})}}},
        {"unlabelled_index", {{"party_index", 91}, {"value", 3.}}},
        {"unknown_party", {{"party_index", -4}, {"name", nullptr}, {"abbreviation", nullptr}}},
        {"escaped_text", json::parse("\"\\u00c9\\n\\\"quoted\\\"\"")}
    };
    for (int i = 0; i < 70; ++i) source["extra_field_" + std::to_string(i)] = i;
    auto const serialised = LiveAnalysisArchive::serialize(source);
    auto const packet = json::parse(serialised);
    CHECK(packet.at("format") == "polling-analyser-live-analysis");
    CHECK(packet.at("version") == 1);
    auto const data = expandFields(packet.at("data"), packet.at("fields"));
    auto const& identities = data.at("identities");
    CHECK(identities.at("parties").size() == 2);
    CHECK(identities.at("parties").at("0").at("name") == "Example Party");
    CHECK(identities.at("seats").at(0) == "Example Seat");
    CHECK(identities.at("regions").at(0) == "Example Region");
    CHECK(identities.at("booths").at(0).at("seat_ref") == 0);
    CHECK(data.at("seats").at(0).at("name_seat_ref") == 0);
    CHECK(data.at("seats").at(0).at("region_name_ref") == 0);
    CHECK(data.at("booths").at(0).at("booth_identity_ref") == 0);
    CHECK(data.at("turnout").at("units").at(0).at("name_booth_ref") == 0);
    CHECK(data.at("preference_rechecking").at("seats").at(0).at("booths").at(0).at("booth_name_ref") == 0);
    auto const& restoredNode = data.at("seats").at(0).at("node");
    CHECK(restoredNode.at("fp_votes_current").at(0).at("party_ref") == 0);
    CHECK(restoredNode.at("fp_votes_current").at(0).at("value") == 1234567);
    CHECK(restoredNode.at("exact_integer_float") == 1234567);
    CHECK(restoredNode.at("wide_integer") == node.at("wide_integer"));
    CHECK(restoredNode.at("fp_completion") == node.at("fp_completion"));
    CHECK(restoredNode.at("small_probability").get<double>() > 0);
    CHECK(std::abs(restoredNode.at("bias").get<double>() - .123457) < 1e-12);
    CHECK(std::abs(restoredNode.at("model_mean_fp").get<double>() - 119233.24) < 1e-8);
    CHECK(restoredNode.at("tcp_shares_baseline").empty());
    CHECK(restoredNode.at("tpp_share_baseline").is_null());
    CHECK(restoredNode.at("non_finite_example") == node.at("non_finite_example"));
    CHECK(data.at("unlabelled_index") == source.at("unlabelled_index"));
    CHECK(data.at("escaped_text") == source.at("escaped_text"));
    CHECK(data.at("turnout").at("allocation_parameters") == source.at("turnout").at("allocation_parameters"));
    CHECK(std::abs(identities.at("booths").at(0).at("coords").at(1).get<double>() - 151.123456789) < 5e-6);
    CHECK(LiveAnalysisArchive::serialize(source) == serialised);
    CHECK(source.at("seats").at(0).at("node") == node);
    if (argc == 2) {
        std::ofstream output(argv[1], std::ios::binary);
        output << serialised;
        CHECK(output.good());
    }
    std::cout << "Live analysis archive tests passed\n";
}
catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
