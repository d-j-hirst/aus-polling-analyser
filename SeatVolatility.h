#pragma once

#include <cmath>
#include <istream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Prepared local TPP scales are election-specific. Unknown seats use the
// same election's regularised generic scale; a missing/wrong file is a
// configuration error, never permission to read future, all-years statistics.
namespace SeatVolatility {
struct Parameters {
    double elasticity = 1;
    double sd = 2;
    bool matched = false;
};

struct Table {
    double genericSd = 0;
    double fallbackSd = 0;
    double kurtosis = 0;
    std::map<std::string, Parameters> seats;

    Parameters lookup(std::string const& name, std::string const& previous,
        std::string const& borrowed) const {
        // The current name wins over either historical alias, independently
        // of file ordering. Renamed seats can still use their known history.
        for (auto const& key : { name, previous, borrowed }) {
            auto found = seats.find(key);
            if (!key.empty() && found != seats.end()) return found->second;
        }
        return { 1, fallbackSd, false };
    }
};

inline double number(std::string const& value) {
    std::size_t used = 0;
    double result = std::stod(value, &used);
    if (used != value.size() || !std::isfinite(result))
        throw std::runtime_error("Invalid seat volatility number: " + value);
    return result;
}

inline std::vector<std::string> fields(std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> result;
    std::istringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) result.push_back(field);
    return result;
}

inline void addRow(Table& table, std::set<std::string>& metadata,
    std::vector<std::string> const& row, std::string const& election) {
    if (row.size() == 4 && row[0] == "seat") {
        Parameters p{number(row[2]), number(row[3]), true};
        if (row[1].empty() || p.elasticity <= 0 || p.sd < 2 ||
            !table.seats.emplace(row[1], p).second)
            throw std::runtime_error("Invalid or duplicate seat volatility row.");
        return;
    }
    if (row.size() != 2 || !metadata.insert(row[0]).second)
        throw std::runtime_error("Invalid or duplicate seat volatility metadata.");
    if (row[0] == "format" && row[1] == "1") return;
    if (row[0] == "election" && row[1] == election) return;
    if (row[0] == "generic_sd") table.genericSd = number(row[1]);
    else if (row[0] == "fallback_sd") table.fallbackSd = number(row[1]);
    else if (row[0] == "kurtosis") table.kurtosis = number(row[1]);
    else throw std::runtime_error("Wrong election, format or field in seat volatility file.");
}

inline Table read(std::istream& input, std::string const& election) {
    Table table;
    std::set<std::string> metadata;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line != "\r") addRow(table, metadata, fields(line), election);
    }
    if (input.bad() || metadata != std::set<std::string>{
        "format", "election", "generic_sd", "fallback_sd", "kurtosis" } ||
        table.genericSd < 2 || table.fallbackSd < 2 || table.kurtosis < 3)
        throw std::runtime_error("Missing or invalid election-specific seat volatility parameters.");
    return table;
}
}
