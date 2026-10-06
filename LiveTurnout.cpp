#include "LiveTurnout.h"
#include "LiveTurnoutMath.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace LiveTurnout {
std::shared_ptr<Prepared const> Prepared::prepare(TurnoutModelIO::Artifact artifact,
    std::vector<TurnoutModel::Unit> units, std::vector<int> boothIndexes,
    std::size_t boothCount, std::vector<bool> const& finalised,
    std::vector<TurnoutModel::Observation> const& history, std::string const& sourceTime) {
    // Source-clock observations update the immutable prior afresh. A rerun or
    // downward revision never feeds an earlier posterior back into the prior.
    auto data = std::make_shared<Counts>();
    data->artifact = std::move(artifact);
    data->units = std::move(units);
    data->boothIndexes = std::move(boothIndexes);
    if (data->units.size() != data->boothIndexes.size())
        throw std::runtime_error("Turnout unit and booth indexes differ.");
    auto options = data->artifact.options;
    if (data->artifact.decayPpvc)
        options.ppvcFactor = TurnoutModel::reportingFactor(
            TurnoutModel::sourceHour(sourceTime) - *data->artifact.pollClose);
    data->result = TurnoutModel::prepare(data->artifact.prior, data->units, finalised, history, options);
    data->drawPlan = TurnoutModel::makeDrawPlan(data->result, data->units, data->artifact.prior.enrolment);
    data->boothMeans.assign(boothCount, -1);
    for (std::size_t j = 0; j < data->units.size(); ++j) {
        int b = data->boothIndexes[j];
        if (b < 0) continue; // An explicitly closed service can be absent.
        auto& amount = data->boothMeans.at(std::size_t(b));
        if (amount >= 0) throw std::runtime_error("Duplicate turnout booth allocation.");
        amount = data->result.unitMeans[j];
    }
    if (std::find(data->boothMeans.begin(), data->boothMeans.end(), -1) != data->boothMeans.end())
        throw std::runtime_error("A live booth has no turnout allocation.");
    data->sourceTime = sourceTime;
    auto prepared = std::make_shared<Prepared>();
    prepared->counts = std::move(data);
    return prepared;
}

std::shared_ptr<Prepared const> Prepared::withComposition(std::vector<UnitComposition> const& units,
    std::vector<SeatComposition> const& seats) const {
    // Cache base votes (including outstanding preferences on reported FP) and
    // the party mix of future FP additions. The two are deliberately separate:
    // a draw with no new voters must still finish an incomplete preference count.
    auto cache = std::make_shared<Composition>();
    cache->seats = seats;
    cache->baseFp.resize(seats.size()); cache->baseTpp.resize(seats.size()); cache->baseTcp.resize(seats.size());
    std::map<std::size_t, UnitComposition const*> byBooth;
    for (auto const& u : units) if (!byBooth.emplace(u.booth, &u).second)
        throw std::runtime_error("Duplicate live turnout composition booth.");
    for (std::size_t s = 0; s < seats.size(); ++s) {
        if (!cache->indexes.emplace(seats[s].name, s).second)
            throw std::runtime_error("Duplicate live turnout composition district.");
        auto check = [&](auto const& projected, auto const& observed) {
            if (projected.empty()) return; // No usable non-classic pair.
            for (auto const& [p, v] : observed) if (v > 0) {
                auto found = projected.find(p);
                if (found == projected.end()) throw std::runtime_error("Turnout projection lost a counted candidate in " + seats[s].name);
                LiveTurnoutMath::remaining(found->second, v);
            }
        };
        check(seats[s].projectedFp, seats[s].fp);
        check(seats[s].projectedTpp, seats[s].tpp);
        check(seats[s].projectedTcp, seats[s].tcp);
    }
    for (std::size_t j = 0; j < counts->boothIndexes.size(); ++j) {
        int b = counts->boothIndexes[j]; if (b < 0) continue;
        auto const& u = *byBooth.at(std::size_t(b));
        auto fp = LiveTurnoutMath::prepareUnitComposition(u.projectedFp, u.fp, counts->drawPlan.counted[j]);
        auto tpp = LiveTurnoutMath::prepareUnitComposition(u.projectedTpp, u.tpp, counts->drawPlan.counted[j]);
        auto tcp = LiveTurnoutMath::prepareUnitComposition(u.projectedTcp, u.tcp, counts->drawPlan.counted[j]);
        for (auto const& [p, v] : fp.base) cache->baseFp.at(u.seat)[p] += v;
        for (auto const& [p, v] : tpp.base) cache->baseTpp.at(u.seat)[p] += v;
        for (auto const& [p, v] : tcp.base) cache->baseTcp.at(u.seat)[p] += v;
        if (!counts->result.broad.complete[j]) {
            if (fp.additionShares.empty()) throw std::runtime_error("No composition for unfinished turnout unit: " + counts->units[j].name);
            cache->responses.push_back({j, u.seat, std::move(fp.additionShares), std::move(tpp.additionShares), std::move(tcp.additionShares)});
        }
    }
    // The cached representation must reproduce prepared party counts before
    // it is shared with iterations. This also checks unit/district accounting.
    auto check = [&](auto const& bases, auto member, auto projectedMember) {
        auto rebuilt = bases;
        for (auto const& r : cache->responses) {
            double addition = counts->result.unitMeans[r.unit] - counts->drawPlan.counted[r.unit];
            for (auto const& [p, w] : r.*member) rebuilt[r.seat][p] += addition * w;
        }
        for (std::size_t s = 0; s < seats.size(); ++s) {
            auto const& expected = seats[s].*projectedMember;
            for (auto const& [p, v] : expected)
                if (std::abs(rebuilt[s][p] - v) > std::max(.02, double(v) * 2e-6))
                    throw std::runtime_error("Turnout composition does not reconstruct its mean in " + seats[s].name);
            for (auto const& [p, v] : rebuilt[s]) if (!expected.count(p) && v != 0)
                throw std::runtime_error("Unexpected party in turnout composition for " + seats[s].name);
        }
    };
    check(cache->baseFp, &Response::fp, &SeatComposition::projectedFp);
    check(cache->baseTpp, &Response::tpp, &SeatComposition::projectedTpp);
    check(cache->baseTcp, &Response::tcp, &SeatComposition::projectedTcp);
    auto prepared = std::make_shared<Prepared>(*this);
    prepared->composition = std::move(cache);
    return prepared;
}

double Prepared::fpTarget(std::size_t booth) const { return counts->boothMeans.at(booth); }

std::vector<Projection> Prepared::draw(unsigned long long seed) const {
    // Only the finite count draw runs in the full simulation. No fits, source
    // reads or booth-level party recompositions occur in this hot path.
    auto amounts = TurnoutModel::drawUnitCounts(counts->result, counts->drawPlan, seed);
    auto fp = composition->baseFp, tpp = composition->baseTpp, tcp = composition->baseTcp;
    for (auto const& r : composition->responses) {
        double addition = LiveTurnoutMath::remaining(amounts[r.unit], counts->drawPlan.counted[r.unit]);
        for (auto const& [p, w] : r.fp) fp[r.seat][p] += addition * w;
        for (auto const& [p, w] : r.tpp) tpp[r.seat][p] += addition * w;
        for (auto const& [p, w] : r.tcp) tcp[r.seat][p] += addition * w;
    }
    std::vector<Projection> result(composition->seats.size());
    auto copy = [](auto const& source, auto& target) { for (auto const& [p, v] : source) target[p] = float(v); };
    for (std::size_t s = 0; s < result.size(); ++s) {
        copy(fp[s], result[s].fp); copy(tpp[s], result[s].tpp); copy(tcp[s], result[s].tcp);
    }
    return result;
}

Projected Prepared::vary(LiveData::CountKind kind, std::size_t seat,
    Projected const& sampled, std::map<int, double> const& changes) const {
    return LiveTurnoutMath::varySampledRemaining(sampled, mean(kind, seat), counted(kind, seat),
        changes, kind == LiveData::CountKind::Fp);
}
Projected const& Prepared::mean(LiveData::CountKind kind, std::size_t seat) const {
    auto const& s = composition->seats.at(seat);
    if (kind == LiveData::CountKind::Fp) return s.projectedFp;
    if (kind == LiveData::CountKind::Tpp) return s.projectedTpp;
    return s.projectedTcp;
}
Counted const& Prepared::counted(LiveData::CountKind kind, std::size_t seat) const {
    auto const& s = composition->seats.at(seat);
    if (kind == LiveData::CountKind::Fp) return s.fp;
    if (kind == LiveData::CountKind::Tpp) return s.tpp;
    return s.tcp;
}
float Prepared::confidence(LiveData::CountKind kind, std::size_t seat) const {
    auto const& s = composition->seats.at(seat);
    if (kind == LiveData::CountKind::Fp) return s.fpConfidence;
    if (kind == LiveData::CountKind::Tpp) return s.tppConfidence;
    return s.tcpConfidence;
}
std::size_t Prepared::seatIndex(std::string const& name) const { return composition->indexes.at(name); }
nlohmann::json Prepared::diagnostic() const {
    auto value = TurnoutModelIO::diagnostic(counts->artifact, counts->units, counts->result, counts->sourceTime);
    value["composition_responses"] = composition ? composition->responses.size() : 0;
    return value;
}
}
