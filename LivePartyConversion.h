#pragma once

#include "LivePartialCount.h"
#include "LiveTppIntegration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// District-level candidate preference destinations inform FP-to-TPP conversion.
// These are historical priors, not current counted TPP or a final-pair forecast.
namespace LivePartyConversion {
constexpr double GreensStrengthSlope = .45;
constexpr double GreensHistoryRetention = .5;
constexpr double StrengthPriorVotes = 5000;

inline double logit(double p) {
    p = std::clamp(p, 1e-6, 1-1e-6);
    return std::log(p/(1-p));
}
inline double logistic(double z) { return 1/(1+std::exp(-z)); }

// Require the complete normalised name, never surname-only or fuzzy matches.
// Sorting accommodates the commission's surname-first display convention.
inline std::string candidateKey(std::string name) {
    for (char& c : name) c = std::isalnum(static_cast<unsigned char>(c))
        ? char(std::tolower(static_cast<unsigned char>(c))) : ' ';
    std::istringstream input(name);
    std::vector<std::string> tokens;
    for (std::string token; input >> token;) tokens.push_back(token);
    std::sort(tokens.begin(), tokens.end());
    std::string result;
    for (auto const& token : tokens) result += (result.empty() ? "" : " ") + token;
    return result;
}

struct Record {
    std::string seat, party, candidate;
    double fp = 0, formal = 0, alp = 0;
    bool classic = false;
    // Normally retain the district residual at the usual global retention.
    // An exceptional campaign can make it unsuitable for the NEXT election.
    // This weight changes forward reuse only, never the historical counts,
    // measured flow or reconstruction of the previous booth/reference account.
    double forwardResidualWeight = 1;
    double strength() const { return logit(fp/formal); }
    double flow() const { return logit((alp+.5)/(fp+1)); }
};

struct History {
    std::vector<Record> records;
    double greensLevel = 0, greensStrength = 0;
    bool hasGreens = false;

    Record const* find(std::string const& seat, std::string const& party,
        std::string const& candidate = {}) const {
        Record const* match = nullptr;
        for (auto const& row : records) {
            if (row.seat != seat || row.party != party ||
                (party == "IND" && row.candidate != candidate)) continue;
            if (match) return nullptr; // Ambiguous identity is not usable history.
            match = &row;
        }
        return match;
    }

    double greensResidual(Record const& row) const {
        return row.flow()-greensLevel-GreensStrengthSlope*(row.strength()-greensStrength);
    }

    double forwardGreensResidual(Record const& row) const {
        return row.forwardResidualWeight*greensResidual(row);
    }
};

inline void centreHistory(History& history) {
    // One classic district is one observation of the national level. Individual
    // voters do not provide independent examples of district behaviour.
    double count = 0;
    for (auto const& row : history.records) if (row.party == "GRN" && row.classic) {
        history.greensLevel += row.flow();
        history.greensStrength += row.strength();
        ++count;
    }
    history.hasGreens = count > 0;
    if (count > 0) {
        history.greensLevel /= count;
        history.greensStrength /= count;
    }
}

inline History readHistory(std::istream& input, std::string const& previousTerm,
    std::string const& currentTerm) {
    History history;
    // History must be the configured comparison election in the same electoral
    // system, strictly earlier than the current election. Never pick the newest
    // table on disk: it may contain the final result of this replay itself.
    if (previousTerm.size() < 5 || currentTerm.size() < 5 ||
        previousTerm.substr(4) != currentTerm.substr(4) ||
        previousTerm.substr(0,4) >= currentTerm.substr(0,4)) return history;
    std::string line;
    std::getline(input, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string const originalHeader = "term,seat,party,candidate_key,fp,formal,alp,classic";
    bool hasResidualWeight = line == originalHeader + ",forward_residual_weight";
    if (line != originalHeader && !hasResidualWeight)
        throw std::runtime_error("Invalid historical party-flow header.");
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::istringstream cells(line);
        std::vector<std::string> values;
        for (std::string cell; std::getline(cells, cell, ',');) values.push_back(cell);
        if (values.size() != (hasResidualWeight ? 9u : 8u))
            throw std::runtime_error("Invalid historical party-flow row.");
        if (values[0] != previousTerm) continue;
        Record row{values[1], values[2], candidateKey(values[3]),
            std::stod(values[4]), std::stod(values[5]), std::stod(values[6]), values[7] == "1"};
        if (hasResidualWeight) {
            std::size_t parsed = 0;
            row.forwardResidualWeight = std::stod(values[8], &parsed);
            if (parsed != values[8].size() || !std::isfinite(row.forwardResidualWeight)
                || row.forwardResidualWeight < 0 || row.forwardResidualWeight > 1
                || (row.party != "GRN" && row.forwardResidualWeight != 1))
                throw std::runtime_error("Invalid historical preference residual weight.");
        }
        if ((row.party != "GRN" && row.party != "IND") || (values[7] != "0" && values[7] != "1") || row.seat.empty() ||
            row.candidate.empty() || !std::isfinite(row.fp) || !std::isfinite(row.formal) ||
            !std::isfinite(row.alp) || row.fp <= 0 || row.formal < row.fp || row.alp < 0 || row.alp > row.fp ||
            std::trunc(row.fp) != row.fp || std::trunc(row.formal) != row.formal || std::trunc(row.alp) != row.alp)
            throw std::runtime_error("Invalid historical party-flow account.");
        history.records.push_back(std::move(row));
    }
    centreHistory(history);
    return history;
}

inline double phasedStrength(double priorShare, double votes, double total) {
    // District strength is used for every booth. Start from the pre-election
    // estimate and replace it smoothly; one tiny booth cannot redefine a party's
    // preference electorate across the district. Shares and flows are bounded.
    double weight = total > 0 ? total/(total+StrengthPriorVotes) : 0;
    return (1-weight)*logit(priorShare) + weight*logit((votes+.5)/(total+1));
}

inline double greensFlow(double base, double strength, double residual, History const& history) {
    if (!history.hasGreens || base <= 0 || base >= 1) return base;
    return logistic(logit(base) + GreensStrengthSlope*(strength-history.greensStrength)
        + GreensHistoryRetention*residual);
}

inline double returningIndependentFlow(Record const& history, double currentBase, double previousBase) {
    // Same-candidate history carries its different voter mix forward, while the
    // ordinary election-wide correction remains free to learn current movement.
    return logistic(history.flow()+logit(currentBase)-logit(previousBase));
}

// A district is one example of preference behaviour, even if it supplies many
// matched booths. These working allowances keep a large classic count from
// claiming that non-classic campaigns must have exactly the same residual.
constexpr double CalibrationDistrictFlowSd = 5; // preference percentage points
constexpr double CalibrationOtherFlowSd = 5; // unidentified other-party movement
constexpr double TransferVarianceFloor = .5; // fraction of original shared variance

struct MatchedAccount {
    double pool = 0, expected = 0, observed = 0, mismatch = 0;
    std::array<double,3> groups{}; // Greens, independents, all other non-finalists
    int locations = 0;

    void add(LivePartialCount::Counts const& fp, LivePartialCount::Counts const& tcp,
        LivePartialCount::Flows const& flows, double offset, int coalition,
        int greens, int independentStart, double quality) {
        auto match = LivePartialCount::measureTransfer(fp,tcp,0,coalition);
        if (!match || quality <= 0) return;
        double prediction = LivePartialCount::expectedFirst(fp,flows,offset)
            - LivePartialCount::value(fp,0);
        pool += quality*match->votes;
        expected += quality*prediction;
        observed += quality*match->received;
        mismatch += quality*std::abs(LivePartialCount::total(fp)-LivePartialCount::total(tcp));
        for (auto const& [party,votes] : fp) if (party != 0 && party != coalition)
            groups[party == greens ? 0 : (party >= independentStart ? 1 : 2)] += quality*votes;
        ++locations;
    }

    LiveTppIntegration::Observation observation(std::array<double,3> const& scales) const {
        // Compare the SAME primary cohort against its source-specific expected
        // flow. An expected/observed 83% therefore supplies zero error, without
        // changing a strong-Greens destination's separate 88% expectation.
        double p = (expected+.5)/(pool+1), actual = (observed+.5)/(pool+1);
        double jacobian = 1/(4*p*(1-p)); // flow percentage points -> 25*log odds
        LiveTppIntegration::Observation row;
        row.value = 25*(logit(p)-logit(actual)); // positive = prediction too ALP
        row.h.resize(3);
        for (std::size_t i = 0; i < 3; ++i) row.h[i] = groups[i]/pool*scales[i]*jacobian;
        row.variance = std::pow(CalibrationDistrictFlowSd*jacobian,2)
            + 625*(1/(observed+.5)+1/(pool-observed+.5))
            + std::pow(100*mismatch/pool*jacobian,2);
        return row;
    }
};

struct Calibration {
    LiveTppIntegration::Moments factors{{0,0},{{1,0},{0,1}}};
    std::vector<MatchedAccount> districts;
};

inline Calibration calibrate(std::vector<MatchedAccount> accounts, double greensSd, double independentSd) {
    using namespace LiveTppIntegration;
    // Other-party uncertainty is a nuisance variable, not a Greens error.
    // Mixed pools cannot uniquely identify group flows: joint fitting retains
    // the resulting covariance instead of assigning the entire miss to Greens.
    Moments prior{{0,0,0},{{1-TransferVarianceFloor,0,0},{0,1-TransferVarianceFloor,0},{0,0,1}}};
    std::vector<Observation> observations;
    for (auto const& account : accounts) if (account.pool > 0)
        observations.push_back(account.observation({greensSd,independentSd,CalibrationOtherFlowSd}));
    auto fit = prepare(std::move(prior), observations);
    Calibration result;
    result.districts = std::move(accounts);
    for (std::size_t i = 0; i < 2; ++i) {
        result.factors.mean[i] = fit.posterior.mean[i];
        for (std::size_t j = 0; j < 2; ++j)
            result.factors.covariance[i][j] = fit.posterior.covariance[i][j]
                + (i == j ? TransferVarianceFloor : 0);
    }
    return result;
}

inline LiveTppIntegration::Moments applyCalibration(LiveTppIntegration::Moments prior,
    Calibration const& calibration) {
    // The final two states are independent of the seat/category prior and
    // expressed in the original flow-error units used by observation loadings.
    if (prior.mean.size() < 2) throw std::logic_error("Missing conversion calibration states.");
    auto start = prior.mean.size()-2;
    for (std::size_t i = 0; i < 2; ++i) {
        prior.mean[start+i] = calibration.factors.mean[i];
        for (std::size_t j = 0; j < 2; ++j)
            prior.covariance.at(start+i).at(start+j) = calibration.factors.covariance[i][j];
    }
    return prior;
}
} // namespace LivePartyConversion
