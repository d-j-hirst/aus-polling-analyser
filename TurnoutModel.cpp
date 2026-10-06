#include "TurnoutModel.h"
#include "Date.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

namespace TurnoutModel {
namespace {
double logistic(double x) { return x >= 0 ? 1 / (1 + std::exp(-x)) : std::exp(x) / (1 + std::exp(x)); }
double softplus(double x) { return std::max(x,0.)+std::log1p(std::exp(-std::abs(x))); }
double odds(double x) {
    if (!(x > 0 && x < 1)) throw std::runtime_error("Turnout share must be inside its parent.");
    return std::log(x) - std::log1p(-x);
}
double cdf(double x) { return .5 * std::erfc(-x / std::sqrt(2.)); }
// Log survival avoids underflow when current votes are far above the prior.
// The asymptotic series is used only where erfc loses its double precision range.
double logCdf(double x) {
    if (x >= 0) return std::log1p(-cdf(-x));
    if (x > -10) return std::log(cdf(x));
    double inv = 1 / (x*x), term = 1, sum = 1;
    for (int k = 1; k < 50; ++k) {
        double next = term * -(2*k-1) * inv;
        if (std::abs(next) >= std::abs(term)) break;
        sum += next; term = next;
        if (std::abs(term) < 1e-16 * std::abs(sum)) break;
    }
    return -.5*x*x - std::log(-x) - .5*std::log(2*std::acos(-1.)) + std::log(sum);
}
double inverseLogCdf(double value) {
    double low = -std::max(12., std::sqrt(-2*value)+2), high = 12;
    for (int k = 0; k < 64; ++k) {
        double mid = (low+high)/2;
        if (logCdf(mid) < value) low = mid; else high = mid;
    }
    return (low+high)/2;
}
double mean(std::vector<double> const& v) { return std::accumulate(v.begin(), v.end(), 0.) / v.size(); }
Matrix zeros(std::size_t rows, std::size_t columns) { return Matrix(rows, std::vector<double>(columns)); }

// Fit one original marginal in log odds and map each outcome above its current
// lower bound. The fit is never taken from a previously updated snapshot.
std::vector<double> condition(std::vector<double> const& shares, std::vector<double> const& lower) {
    std::vector<double> transformed;
    for (double x : shares) transformed.push_back(odds(x));
    double centre = mean(transformed), variance = 0;
    for (double x : transformed) variance += (x-centre)*(x-centre);
    double spread = std::sqrt(variance/shares.size());
    auto result = shares;
    for (std::size_t r = 0; r < shares.size(); ++r) {
        if (!(lower[r] >= 0 && lower[r] < 1)) throw std::runtime_error("Counted votes reach their turnout parent.");
        if (lower[r] == 0) continue;
        if (spread < 1e-12) {
            if (shares[r] <= lower[r]) throw std::runtime_error("Deterministic turnout prior conflicts with counted votes.");
            continue;
        }
        double threshold = (odds(lower[r])-centre)/spread;
        double position = (transformed[r]-centre)/spread;
        result[r] = logistic(centre-spread*inverseLogCdf(logCdf(-threshold)+logCdf(-position)));
    }
    return result;
}
std::pair<double, double> reserve(double base, double requested, double declaration, double budget) {
    double reference = base+declaration > 0 ? base/(base+declaration) : 0;
    if (reference == 0) return {0, budget};
    if (reference == 1) return {budget, 0};
    double ratio = budget > 0 ? requested/budget/reference : 0;
    double shifted = odds(reference)+std::log(ratio)/(1-reference);
    return {budget*logistic(shifted), budget*logistic(-shifted)};
}
bool eav(Unit const& u) { return u.eav || u.name.rfind("EAV ", 0) == 0; }

// Stable per-unit permutations provide a balanced quantile grid without tying
// the C++ implementation to NumPy's random generator or booth traversal order.
unsigned long long step(unsigned long long& state) {
    state += 0x9e3779b97f4a7c15ULL;
    auto value = state;
    value = (value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value = (value^(value>>27))*0x94d049bb133111ebULL;
    return value^(value>>31);
}
std::vector<double> uniforms(std::string const& key, std::size_t count, unsigned long long seed) {
    unsigned long long state = 14695981039346656037ULL;
    for (unsigned char c : key) state = (state^c)*1099511628211ULL;
    state ^= seed;
    std::vector<std::size_t> positions(count);
    std::iota(positions.begin(), positions.end(), 0);
    for (std::size_t n = count; n > 1; --n) std::swap(positions[n-1], positions[step(state)%n]);
    std::vector<double> result;
    for (auto p : positions) result.push_back((p+.5)/count);
    return result;
}
double mixture(double ref, double small, double batch, double previous, double smallProbability, double batchProbability, double uniform) {
    auto continuous = [&](double x) { return smallProbability*cdf(x-small)+batchProbability*cdf((x-batch)/1.5); };
    double below = continuous(ref), above = below+previous;
    if (uniform >= below && uniform <= above) return ref;
    double target = uniform < below ? uniform : uniform-previous;
    double low = std::min(small-12, batch-18), high = std::max(small+12, batch+18);
    for (int k = 0; k < 40; ++k) {
        double middle = (low+high)/2;
        if (continuous(middle) < target) low = middle; else high = middle;
    }
    return (low+high)/2;
}
}

void validate(Prior const& p) {
    auto fail = [] { throw std::runtime_error("Invalid frozen turnout prior dimensions or counts."); };
    auto n = p.seats.size(), g = p.groups.size();
    if (!n || !g || p.totals.size() < 2 || p.counts.size() != p.totals.size() || p.enrolment.size() != n || p.subdivisions.size() != n) fail();
    if (std::set<std::string>(p.seats.begin(),p.seats.end()).size() != n || std::set<std::string>(p.groups.begin(),p.groups.end()).size() != g) fail();
    for (std::size_t r = 0; r < p.totals.size(); ++r) {
        if (p.totals[r].size() != n || p.counts[r].size() != n*g) fail();
        for (std::size_t s = 0; s < n; ++s) {
            if (!std::isfinite(p.enrolment[s]) || !std::isfinite(p.totals[r][s]) || !(p.totals[r][s] > 0 && p.totals[r][s] < p.enrolment[s])) fail();
            double sum = 0;
            for (std::size_t k = 0; k < g; ++k) {
                double x = p.counts[r][s*g+k];
                if (!std::isfinite(x) || x < 0) fail();
                sum += x;
            }
            if (std::abs(sum-p.totals[r][s]) > 1e-6) fail();
        }
    }
    std::set<std::pair<std::size_t,std::string>> identities;
    for (auto const& u : p.units) {
        if (u.seat >= n || u.group >= g || !std::isfinite(u.weight) || u.weight < 0 || u.weight > 1 || u.name.empty()
            || (u.kind != "ordinary" && u.kind != "ppvc" && u.kind != "declaration") || !identities.emplace(u.seat,u.name).second) fail();
    }
}

Broad update(Prior const& p, std::vector<Unit> const& units, std::vector<bool> const& finalised, double ppvcFactor) {
    validate(p);
    auto m = p.totals.size(), n = p.seats.size(), g = p.groups.size(), u = units.size();
    if (finalised.size() != n || !(ppvcFactor > 0 && ppvcFactor <= 1)) throw std::runtime_error("Invalid turnout snapshot options.");
    Broad out;
    out.totals = zeros(m,n); out.counts = zeros(m,n*g); out.unitCounts = zeros(m,u); out.remaining = zeros(m,u);
    out.counted.assign(n,0); out.complete.resize(u); out.compensation.assign(u,0);
    Matrix expected = zeros(m,u), original = zeros(m,u), additions = zeros(m,u), aggregate = zeros(m,n*g);
    std::vector<std::vector<std::size_t>> columns(n*g);
    std::vector<double> groupCounted(n*g), centre(u);
    std::vector<bool> pure(n*g,true), open(n*g);
    for (std::size_t j = 0; j < u; ++j) {
        auto const& v = units[j];
        if (v.seat >= n || v.group >= g || !std::isfinite(v.counted) || v.counted < 0 || !std::isfinite(v.weight) || v.weight < 0 || v.weight > 1)
            throw std::runtime_error("Invalid current turnout unit.");
        auto k = v.seat*g+v.group;
        columns[k].push_back(j); groupCounted[k] += v.counted; out.counted[v.seat] += v.counted;
        out.complete[j] = (v.kind != "declaration" && v.counted > 0) || v.closed || finalised[v.seat];
        pure[k] = pure[k] && v.kind != "declaration";
        open[k] = open[k] || !out.complete[j];
        for (std::size_t r = 0; r < m; ++r) expected[r][j] = original[r][j] = p.counts[r][k]*v.weight;
    }
    for (std::size_t s = 0; s < n; ++s) {
        std::vector<double> shares(m), bound(m,out.counted[s]/p.enrolment[s]);
        for (std::size_t r = 0; r < m; ++r) shares[r] = p.totals[r][s]/p.enrolment[s];
        auto values = condition(shares,bound);
        for (std::size_t r = 0; r < m; ++r) out.totals[r][s] = finalised[s] ? out.counted[s] : values[r]*p.enrolment[s];
    }
    // EAV services have a separate historical size; a generic unmatched PPVC
    // fallback can be thousands of votes too large even before live reporting.
    for (std::size_t r = 0; r < m; ++r) {
        std::vector<double> references;
        for (std::size_t j = 0; j < u; ++j) if (eav(units[j]) && units[j].matched && !units[j].closed) references.push_back(expected[r][j]);
        std::sort(references.begin(), references.end());
        auto z = references.size();
        double replacement = z ? (references[(z-1)/2]+references[z/2])/2 : 25;
        for (std::size_t j = 0; j < u; ++j) if (eav(units[j]) && !units[j].matched) expected[r][j] = replacement;
    }
    for (std::size_t j = 0; j < u; ++j) for (std::size_t r = 0; r < m; ++r) centre[j] += expected[r][j]/m;
    // Only reliable, completed public centres supply compensation evidence.
    // Small and unmatched centres retain their own expectation; the historical
    // exploratory slope is attenuated by how much of the other vote is observed.
    for (std::size_t j = 0; j < u; ++j) {
        auto const& v = units[j];
        if (out.complete[j] || v.kind != "ppvc" || !v.matched || eav(v) || v.excluded) continue;
        double coefficient = centre[j] >= 8000 ? .63 : centre[j] >= 4000 ? 1.17 : centre[j] >= 2000 ? .14 : 0;
        double other = 0, reported = 0, observed = 0;
        for (std::size_t k = 0; k < u; ++k) {
            auto const& w = units[k];
            if (w.kind != "ppvc" || w.seat != v.seat || w.group != v.group) continue;
            if (k != j) other += centre[k];
            if (out.complete[k] && w.matched && !eav(w) && w.counted > 0 && !w.closed && !w.excluded) { reported += centre[k]; observed += w.counted; }
        }
        if (coefficient && reported > 0 && other > 0)
            out.compensation[j] = -coefficient*reported/other*(odds(observed/p.enrolment[v.seat])-odds(reported/p.enrolment[v.seat]));
    }
    for (std::size_t j = 0; j < u; ++j) {
        auto const& v = units[j];
        double shift = out.compensation[j];
        if (v.kind == "ppvc" && !out.complete[j] && !eav(v)) shift += std::log(ppvcFactor);
        for (std::size_t r = 0; r < m; ++r) {
            if (shift != 0) expected[r][j] = p.enrolment[v.seat]*logistic(odds(expected[r][j]/p.enrolment[v.seat])+shift);
            additions[r][j] = out.complete[j] ? 0 : expected[r][j];
        }
        if (!out.complete[j] && v.counted > 0) {
            std::vector<double> shares(m), bound(m);
            for (std::size_t r = 0; r < m; ++r) { shares[r] = expected[r][j]/p.totals[r][v.seat]; bound[r] = v.counted/out.totals[r][v.seat]; }
            auto values = condition(shares,bound);
            for (std::size_t r = 0; r < m; ++r) additions[r][j] = out.totals[r][v.seat]*values[r]-v.counted;
        }
    }
    // Mixed historical groups keep their supported aggregate expectation.
    // Groups made exclusively of booths instead use the unfinished booths'
    // own sizes, avoiding the old forced shortfall allocation to a small EAV.
    for (std::size_t k = 0; k < n*g; ++k) if (open[k] && !pure[k]) {
        auto s = k/g;
        std::vector<double> shares(m), bound(m);
        for (std::size_t r = 0; r < m; ++r) { shares[r] = p.counts[r][k]/p.totals[r][s]; bound[r] = groupCounted[k]/out.totals[r][s]; }
        auto values = condition(shares,bound);
        for (std::size_t r = 0; r < m; ++r) aggregate[r][k] = out.totals[r][s]*values[r]-groupCounted[k];
    }
    for (std::size_t s = 0; s < n; ++s) for (std::size_t r = 0; r < m; ++r) {
        double base = 0, request = 0, combined = 0;
        for (std::size_t k = s*g; k < (s+1)*g; ++k) {
            combined += aggregate[r][k];
            if (pure[k]) for (auto j : columns[k]) if (!out.complete[j]) { base += original[r][j]; request += additions[r][j]; }
        }
        if (!finalised[s] && base+combined <= 0) throw std::runtime_error("Unfinished turnout district has no remaining supported units.");
        auto [boothBudget, aggregateBudget] = reserve(base,request,combined,out.totals[r][s]-out.counted[s]);
        for (std::size_t k = s*g; k < (s+1)*g; ++k) {
            if (pure[k]) {
                for (auto j : columns[k]) if (!out.complete[j]) out.remaining[r][j] = request > 0 ? additions[r][j]*boothBudget/request : 0;
                continue;
            }
            if (!open[k]) continue;
            double budget = combined > 0 ? aggregateBudget*aggregate[r][k]/combined : 0;
            double b = 0, d = 0, originalB = 0;
            for (auto j : columns[k]) if (!out.complete[j]) {
                if (units[j].kind == "declaration") d += additions[r][j];
                else { b += additions[r][j]; originalB += original[r][j]; }
            }
            auto [bBudget,dBudget] = reserve(originalB,b,d,budget);
            for (auto j : columns[k]) if (!out.complete[j]) out.remaining[r][j] = units[j].kind == "declaration"
                ? (d > 0 ? additions[r][j]*dBudget/d : 0) : (b > 0 ? additions[r][j]*bBudget/b : 0);
        }
    }
    for (std::size_t j = 0; j < u; ++j) for (std::size_t r = 0; r < m; ++r) {
        out.unitCounts[r][j] = units[j].counted+out.remaining[r][j];
        out.counts[r][units[j].seat*g+units[j].group] += out.unitCounts[r][j];
    }
    // Retain each count-conditioned estimate before the aggregate allowances
    // rescale it. Reusing this calculation avoids repeating marginal fits when
    // whole-group progress later allows an excessive allowance to recede.
    out.ownRemaining = std::move(additions);
    return out;
}

std::vector<Evidence> progress(Prior const& p, std::vector<Unit> const& units,
    std::vector<Observation> const& history, std::optional<double> deadline,
    bool scheduleAware, double eventScale) {
    std::vector<Evidence> result(units.size());
    if (history.empty()) return result;
    std::map<double, Observation const*> ordered;
    for (auto const& h : history) ordered[h.hour] = &h;
    double now = ordered.rbegin()->first;
    struct Row { Evidence e; double current = 0, sharedActivity = 0, sharedVolume = 0; std::string state; };
    std::map<std::pair<std::string,std::string>, Row> rows;
    std::set<std::string> categories;
    for (auto const& u : units) if (u.kind == "declaration") categories.insert(u.category);
    for (auto const& [key,value] : ordered.rbegin()->second->counts) {
        if (!categories.count(key.second)) continue;
        if (value < 0 || !std::isfinite(value)) throw std::runtime_error("Invalid turnout progress count.");
        Row row; row.current = value;
        auto seat = std::find(p.seats.begin(),p.seats.end(),key.first);
        if (seat != p.seats.end()) row.state = p.subdivisions[seat-p.seats.begin()];
        double activity = 0, coverage = 0, volume = 0, sharedActivity = 0, sharedVolume = 0;
        double smallSupport = 0, changedSupport = 0, tolerance = std::hypot(10.,eventScale*value);
        for (auto it = ordered.begin(); it != ordered.end(); ++it) {
            auto next = std::next(it); if (next == ordered.end()) break;
            auto old = it->second->counts.find(key), current = next->second->counts.find(key);
            if (old == it->second->counts.end() || current == next->second->counts.end()) continue;
            if (old->second < 0 || current->second < 0 || !std::isfinite(old->second) || !std::isfinite(current->second)) throw std::runtime_error("Invalid turnout history count.");
            double hours = scheduleAware ? countingHours(it->first,next->first) : next->first-it->first;
            double age = scheduleAware ? countingHours(next->first,now) : now-next->first, decay = std::exp(-age/24);
            double delta = std::abs(current->second-old->second), ratio = delta/tolerance;
            activity += ratio*ratio/(1+ratio*ratio)*decay; volume += delta*decay;
            // A late backlog is strong evidence about this seat, but weaker
            // evidence that another seat still has routine counting to do.
            double shared = scheduleAware && deadline ? logistic(-((next->first-*deadline)/24)) : 1;
            sharedActivity += ratio*ratio/(1+ratio*ratio)*decay*shared;
            sharedVolume += delta*decay*shared;
            coverage += std::exp(-age/48)*(-std::expm1(-hours/48))/(1+(hours/48)*(hours/48));
            if (scheduleAware && deadline) {
                // Small positive batches after receipt closes support an end
                // to routine processing. Large batches and downward rechecks
                // provide less support, without declaring any batch final.
                double post = logistic((it->first-*deadline)/12);
                double event = post*std::exp(-age/72)*delta*delta/(delta*delta+50*50);
                changedSupport += event;
                if (current->second > old->second)
                    smallSupport += event/(1+std::pow(delta/(.05*(value+.5)),2));
            }
        }
        row.e.activity = -std::expm1(-activity); row.e.volume = volume; row.e.coverage = coverage; row.e.started = value/(value+10);
        row.e.smallBatchSupport = smallSupport/(1+changedSupport);
        row.sharedActivity = -std::expm1(-sharedActivity); row.sharedVolume = sharedVolume;
        rows[key] = row;
    }
    auto pooled = [](std::vector<Row const*> const& pool) {
        Evidence e;
        auto central = [&](auto measure) {
            std::vector<double> values; for (auto r : pool) values.push_back(measure(*r));
            std::sort(values.begin(),values.end()); std::size_t trim = std::min<std::size_t>(2,values.size()/10);
            return std::accumulate(values.begin()+trim,values.end()-trim,0.)/(values.size()-2*trim);
        };
        e.pooledActivity = central([](Row const& r) { return r.sharedActivity; });
        e.pooledStarted = central([](Row const& r) { return r.e.started; });
        e.pooledVolume = central([](Row const& r) { return r.sharedVolume/(r.current+.5); });
        return e;
    };
    std::map<std::pair<std::string,std::string>,double> currentUnits;
    for (auto const& u : units) if (u.kind == "declaration") currentUnits[{p.seats[u.seat],u.category}] += u.counted;
    for (std::size_t j = 0; j < units.size(); ++j) {
        auto const& u = units[j]; auto key = std::make_pair(p.seats[u.seat],u.category);
        auto local = rows.find(key);
        if (u.kind != "declaration" || local == rows.end()) continue;
        auto const& row = local->second;
        if (currentUnits.at(key) != row.current) throw std::runtime_error("Turnout history and current category counts differ.");
        std::vector<Row const*> national, regional;
        for (auto const& [k,v] : rows) if (k.second == key.second) { national.push_back(&v); if (v.state == row.state) regional.push_back(&v); }
        Evidence e = row.e, broad = pooled(national);
        e.stateWeight = row.state.empty() ? 0 : double(regional.size())/(regional.size()+20);
        if (e.stateWeight) {
            auto state = pooled(regional);
            broad.pooledActivity += e.stateWeight*(state.pooledActivity-broad.pooledActivity);
            broad.pooledStarted += e.stateWeight*(state.pooledStarted-broad.pooledStarted);
            broad.pooledVolume += e.stateWeight*(state.pooledVolume-broad.pooledVolume);
        }
        e.pooledActivity = broad.pooledActivity; e.pooledStarted = broad.pooledStarted; e.pooledVolume = broad.pooledVolume;
        auto quiet = [](double value,double scale) { return 1/(1+(value/scale)*(value/scale)); };
        e.strength = e.coverage*e.started*quiet(e.activity,.1)*quiet(1-e.pooledStarted,.1)
            *(.75*quiet(e.pooledActivity,.1)+.25*quiet(e.pooledVolume,.005));
        if (u.category == "Postal" && deadline) { e.postalReceiptSupport = logistic((now-*deadline)/48); e.strength *= e.postalReceiptSupport; }
        result[j] = e;
    }
    return result;
}

// Expected booth sizes, rather than the number of reporting booths, determine
// how much independent evidence the completed ordinary/PPVC account supplies.
// Known closures contribute no voter-behaviour evidence. Match the EAV origin
// treatment before weighting, so an unmatched tiny service cannot dominate.
std::vector<double> boothCompletion(Prior const& p, std::vector<Unit> const& units) {
    std::vector<double> total(p.seats.size()), reported(p.seats.size());
    for (std::size_t r = 0; r < p.counts.size(); ++r) {
        std::vector<double> references;
        for (auto const& u : units) if (eav(u) && u.matched && !u.closed)
            references.push_back(p.counts[r][u.seat*p.groups.size()+u.group]*u.weight);
        std::sort(references.begin(),references.end()); auto z = references.size();
        double replacement = z ? (references[(z-1)/2]+references[z/2])/2 : 25;
        for (auto const& u : units) if (u.kind != "declaration" && !u.closed) {
            double expected = eav(u) && !u.matched ? replacement : p.counts[r][u.seat*p.groups.size()+u.group]*u.weight;
            total[u.seat] += expected/p.counts.size();
            if (u.counted > 0) reported[u.seat] += expected/p.counts.size();
        }
    }
    for (std::size_t s = 0; s < total.size(); ++s) total[s] = total[s] > 0 ? reported[s]/total[s] : 0;
    return total;
}

// Blend the earlier sensitive activity measurement with a more conservative
// two-percent measurement in probability space. This retains caution during
// pauses while recognising small late batches after most booths have reported.
std::vector<Evidence> scheduledEvidence(Prior const& p, std::vector<Unit> const& units,
    std::vector<Observation> const& history, double deadline, std::vector<double> const& completed) {
    auto original = progress(p,units,history,deadline,true,.001);
    auto result = progress(p,units,history,deadline,true,.02);
    auto quiet = [](double value,double scale) { return 1/(1+std::pow(value/scale,2)); };
    for (std::size_t j = 0; j < units.size(); ++j) {
        auto& e = result[j];
        double shared = quiet(e.pooledActivity,.3);
        double strength = e.coverage*e.started*quiet(1-e.pooledStarted,.1)*quiet(e.activity,.3)
            *(.5+.5*shared)*e.postalReceiptSupport;
        double oldWeight = -std::expm1(-std::pow(original[j].strength/.3,2));
        double proposedWeight = -std::expm1(-std::pow(strength/.3,2))*shared;
        double weight = .5*(oldWeight+proposedWeight);
        double extra = .8*e.smallBatchSupport*std::pow(completed[units[j].seat],2)*e.started;
        weight += (1-weight)*extra;
        e.strength = .3*std::sqrt(-std::log1p(-weight));
    }
    return result;
}

// An allowance can remain large when the larger booths in its group have
// finished but a small declaration category is still open. Measure progress
// on the entire supported group, then gradually relax that imposed allowance.
// Category-specific completion and exceptional batches remain a separate step.
void releaseAllocation(Prior const& p, std::vector<Unit> const& units,
    std::vector<Observation> const& history, std::optional<double> deadline, Result& result,
    std::vector<double> const& completed = {}) {
    auto& broad = result.broad;
    auto m = p.totals.size(), n = p.seats.size(), g = p.groups.size();
    std::vector<Unit> groups(n*g);
    for (std::size_t s = 0; s < n; ++s) for (std::size_t k = 0; k < g; ++k) {
        auto& group = groups[s*g+k];
        group.seat = s; group.group = k; group.kind = "declaration";
        group.category = p.groups[k] == "postal" ? "Postal" : "allocation:"+p.groups[k];
        group.name = group.category;
    }
    for (auto const& unit : units) groups[unit.seat*g+unit.group].counted += unit.counted;
    auto evidence = progress(p,groups,history,deadline);
    result.balancedRemainingMeans.assign(units.size(),0);
    result.allocationStrength.assign(units.size(),0);
    result.allocationWeight.assign(units.size(),0);
    bool active = false;
    for (std::size_t j = 0; j < units.size(); ++j) {
        for (auto const& row : broad.remaining) result.balancedRemainingMeans[j] += row[j]/m;
        if (units[j].kind != "declaration" || broad.complete[j]) continue;
        double strength = evidence[units[j].seat*g+units[j].group].strength;
        double weight = -std::expm1(-std::pow(strength/AllocationProgressScale,2));
        if (!completed.empty()) {
            // Completed booths can help release an old total allowance only
            // where the historical group supports one actual declaration
            // category. Mixed SA groups retain their existing evidence rule.
            auto const& unit = units[j]; bool compatible = true; double counted = 0, own = 0;
            for (std::size_t k = 0; k < units.size(); ++k) if (units[k].seat == unit.seat && units[k].group == unit.group) {
                compatible = compatible && units[k].kind == "declaration" && units[k].category == unit.category;
                counted += units[k].counted;
                for (auto const& row : broad.ownRemaining) own += row[k]/m;
            }
            double maturity = counted+own > 0 ? counted/(counted+own) : 0;
            double extra = compatible ? .5*std::pow(completed[unit.seat]*maturity,2) : 0;
            weight += (1-weight)*extra;
            strength = AllocationProgressScale*std::sqrt(-std::log1p(-weight));
        }
        result.allocationStrength[j] = strength;
        result.allocationWeight[j] = weight;
        active = active || result.allocationWeight[j] != 0;
    }
    // No group history, or no slowing evidence, retains the original account
    // exactly. In particular the genuine no-results prior is unchanged.
    if (!active) return;
    for (std::size_t s = 0; s < n; ++s) {
        std::vector<std::size_t> columns;
        bool localActive = false;
        for (std::size_t j = 0; j < units.size(); ++j) if (units[j].seat == s && units[j].kind == "declaration" && !broad.complete[j]) {
            columns.push_back(j); localActive = localActive || result.allocationWeight[j] != 0;
        }
        if (!localActive) continue;
        for (std::size_t r = 0; r < m; ++r) {
            double slack = p.enrolment[s]-broad.totals[r][s], capacity = slack, maximum = 0;
            if (!(slack > 0)) throw std::runtime_error("Allocation release needs positive unused enrolment.");
            std::vector<double> shifted;
            for (auto j : columns) {
                double base = broad.remaining[r][j], own = broad.ownRemaining[r][j];
                if (base < 0 || own < 0) throw std::runtime_error("Allocation release has a negative category estimate.");
                capacity += base;
                // Machine epsilon only represents subtraction roundoff. It
                // does not replace unknown counts or impose a vote-count floor.
                double oldOdds = std::log((base+std::numeric_limits<double>::epsilon())/slack);
                double ownOdds = std::log((own+std::numeric_limits<double>::epsilon())/slack);
                double difference = ownOdds-oldOdds;
                // A larger individual prior is weak evidence for reinstating
                // votes restrained by the aggregate account. Reduce its weight
                // smoothly while allowing substantial excess allowances to fall.
                double direction = logistic(-difference/AllocationDirectionScale);
                double value = oldOdds+difference*result.allocationWeight[j]*direction;
                shifted.push_back(value); maximum = std::max(maximum,value);
            }
            // Include unused enrolment as a competing share. Removed expected
            // votes can become unused capacity instead of being forced onto a
            // different category. Counted votes and booth estimates stay fixed.
            double denominator = std::exp(-maximum);
            for (double value : shifted) denominator += std::exp(value-maximum);
            for (std::size_t k = 0; k < columns.size(); ++k) {
                auto j = columns[k];
                broad.remaining[r][j] = capacity*std::exp(shifted[k]-maximum)/denominator;
                broad.unitCounts[r][j] = units[j].counted+broad.remaining[r][j];
            }
        }
    }
    broad.counts = zeros(m,n*g);
    broad.totals = zeros(m,n);
    for (std::size_t r = 0; r < m; ++r) {
        for (std::size_t j = 0; j < units.size(); ++j) broad.counts[r][units[j].seat*g+units[j].group] += broad.unitCounts[r][j];
        for (std::size_t s = 0; s < n; ++s)
            broad.totals[r][s] = std::accumulate(broad.counts[r].begin()+s*g,broad.counts[r].begin()+(s+1)*g,0.);
    }
}

Result prepare(Prior const& p, std::vector<Unit> const& units, std::vector<bool> const& finalised,
    std::vector<Observation> const& history, Options const& options, Matrix const& suppliedUniforms) {
    if (!options.countDraws) throw std::runtime_error("Turnout preparation needs at least one cheap count draw.");
    Result result;
    result.broad = update(p,units,finalised,options.ppvcFactor);
    auto completed = options.receiptDeadline ? boothCompletion(p,units) : std::vector<double>{};
    result.evidence = options.receiptDeadline ? scheduledEvidence(p,units,history,*options.receiptDeadline,completed)
        : progress(p,units,history,options.postalDeadline);
    releaseAllocation(p,units,history,options.receiptDeadline ? options.receiptDeadline : options.postalDeadline,result,completed);
    auto m = p.totals.size(), n = p.seats.size(), g = p.groups.size(), count = m*options.countDraws;
    auto const& broad = result.broad;
    // Routine expected additions recede after the receipt cutoff and a two-day
    // processing allowance. The separately possible batch fades much more
    // slowly. Source observations determine the clock; rerunning a forecast
    // later in real time cannot make the same source appear more complete.
    bool scheduled = options.receiptDeadline && !history.empty()
        && std::accumulate(broad.counted.begin(),broad.counted.end(),0.) > 0;
    if (scheduled) {
        double now = std::max_element(history.begin(),history.end(),[](auto const& a,auto const& b) { return a.hour < b.hour; })->hour;
        result.countingHoursAfterDeadline = countingHours(*options.receiptDeadline,now);
        double age = 12*softplus((result.countingHoursAfterDeadline-48)/12);
        result.processingPhase = logistic((result.countingHoursAfterDeadline-48)/12);
        result.batchSurvival = std::exp(-age/(14*24));
    }
    // Conditional branches use the same short quantile grid for every unit
    // and preparation. Calculate that grid once; repeatedly inverting the
    // normal CDF would make these intended cheap evaluations unnecessarily slow.
    std::vector<double> normalQuantiles;
    for (std::size_t k = 0; k < options.countDraws; ++k)
        normalQuantiles.push_back(inverseLogCdf(std::log((k+.5)/options.countDraws)));
    result.unitMeans.assign(units.size(),0);
    for (std::size_t j = 0; j < units.size(); ++j) for (std::size_t r = 0; r < m; ++r) result.unitMeans[j] += broad.unitCounts[r][j]/m;
    for (std::size_t j = 0; j < units.size(); ++j) if (units[j].kind == "declaration" && !broad.complete[j]) {
        Component c; c.unit = j; c.weight = -std::expm1(-std::pow(result.evidence[j].strength/.3,2));
        if (scheduled) {
            double currentCategory = 0;
            for (auto const& u : units) if (u.seat == units[j].seat && u.kind == "declaration" && u.category == units[j].category)
                currentCategory += u.counted;
            double relative = result.evidence[j].volume/(currentCategory+.5);
            double delay = 24*relative*relative/(relative*relative+.02*.02);
            double routine = logistic(-(result.countingHoursAfterDeadline-48-delay)/12);
            double active = relative*relative/(relative*relative+.05*.05);
            c.usualLogShift = std::log(routine+(1-routine)*active);
        }
        for (std::size_t r = 0; r < m; ++r) {
            double base = broad.remaining[r][j], slack = p.enrolment[units[j].seat]-broad.totals[r][units[j].seat];
            if (base < 0 || slack <= 0) throw std::runtime_error("Invalid remaining turnout capacity.");
            c.capacity.push_back(slack); c.reference.push_back(base);
            c.referenceOdds.push_back(std::log((base+std::numeric_limits<double>::epsilon())/slack)+c.usualLogShift);
            c.smallOdds.push_back(std::log(.25/slack));
            double oldBatch = std::log((base+.25+.05*units[j].counted)/slack);
            double lateBatch = std::log((broad.ownRemaining[r][j]+.25+.05*units[j].counted)/slack);
            c.batchOdds.push_back((1-result.processingPhase)*oldBatch+result.processingPhase*lateBatch);
            for (std::size_t k = 0; k < options.countDraws; ++k) {
                double z = normalQuantiles[k];
                c.smallMean += (slack+base)*logistic(c.smallOdds.back()+z)/count;
                c.batchMean += (slack+base)*logistic(c.batchOdds.back()+1.5*z)/count;
            }
        }
        result.components.push_back(std::move(c));
    }
    auto c = result.components.size();
    std::vector<std::vector<std::size_t>> localColumns(n);
    std::vector<bool> activeSeats(n);
    for (std::size_t k = 0; k < c; ++k) {
        auto s = units[result.components[k].unit].seat;
        localColumns[s].push_back(k);
        activeSeats[s] = activeSeats[s] || result.components[k].weight != 0
            || result.components[k].usualLogShift != 0 || result.processingPhase != 0;
    }
    // Keep the probability of no further votes explicitly, rather than hoping
    // independent tiny positive category draws happen to approximate it. Count
    // volume weights the slowing evidence; unreported broad estimates weaken
    // it, and postal receipt support prevents quiet pre-deadline counts from
    // closing the seat prematurely. This is shared exploratory calibration.
    result.noAdditionProbability.assign(n,0);
    for (std::size_t s = 0; s < n; ++s) {
        bool allComplete = true, hasUnits = false;
        for (std::size_t j = 0; j < units.size(); ++j) if (units[j].seat == s) {
            hasUnits = true; allComplete = allComplete && broad.complete[j];
        }
        if (hasUnits && allComplete) { result.noAdditionProbability[s] = 1; continue; }
        double counted = 0, support = 0, nonbatch = 1, receipt = 1, remaining = 0;
        for (auto k : localColumns[s]) {
            auto const& component = result.components[k]; auto const& unit = units[component.unit];
            counted += unit.counted; support += unit.counted*component.weight;
            nonbatch *= 1-.02*component.weight;
            if (unit.category == "Postal") receipt = std::min(receipt,result.evidence[component.unit].postalReceiptSupport);
        }
        if (!counted) continue;
        for (auto const& row : broad.totals) remaining += (row[s]-broad.counted[s])/m;
        double coverage = counted/(counted+remaining);
        result.noAdditionProbability[s] = .95*support/counted*coverage*coverage*nonbatch*receipt;
    }
    // Keep the exact no-addition event paired with the tested progress rule.
    // Separately retain a low chance of a late batch even after routine size
    // has receded. Compress that chance smoothly if very little probability
    // remains outside the shared zero outcome; it never becomes a hard floor.
    for (auto& component : result.components) {
        double zero = result.noAdditionProbability[units[component.unit].seat];
        double positive = 1-zero, requested = .02*result.batchSurvival;
        double possible = positive > 0 ? positive*(-std::expm1(-requested/positive)) : 0;
        component.batchProbability = (1-result.processingPhase)*.02*component.weight+result.processingPhase*possible;
        double smallShare = .98*component.weight/(1-.02*component.weight);
        double remaining = positive-component.batchProbability;
        component.previousProbability = (1-smallShare)*remaining;
        component.smallProbability = smallShare*remaining;
    }
    if (!suppliedUniforms.empty()) {
        if (suppliedUniforms.size() != count) throw std::runtime_error("Invalid supplied turnout quantile count.");
        for (auto const& row : suppliedUniforms) {
            if (row.size() != c) throw std::runtime_error("Invalid supplied turnout quantile columns.");
            for (double x : row) if (!(x > 0 && x < 1)) throw std::runtime_error("Turnout quantiles must be interior.");
        }
    }
    Matrix varied = zeros(count,c);
    for (std::size_t k = 0; k < c; ++k) {
        auto const& component = result.components[k]; auto const& u = units[component.unit];
        double positive = 1-result.noAdditionProbability[u.seat];
        auto quantiles = uniforms(p.election+"/"+p.seats[u.seat]+"/"+u.name,count,options.seed);
        for (std::size_t r = 0; r < count; ++r) {
            auto outer = r/options.countDraws;
            varied[r][k] = mixture(component.referenceOdds[outer],component.smallOdds[outer],component.batchOdds[outer],
                component.previousProbability/positive,component.smallProbability/positive,component.batchProbability/positive,
                suppliedUniforms.empty() ? quantiles[r] : suppliedUniforms[r][k]);
        }
    }
    result.totals = zeros(count,n); result.counts = zeros(count,n*g);
    for (std::size_t r = 0; r < count; ++r) {
        auto outer = r/options.countDraws;
        result.counts[r] = broad.counts[outer]; result.totals[r] = broad.totals[outer];
        for (std::size_t s = 0; s < n; ++s) {
            if (!activeSeats[s]) continue;
            auto const& local = localColumns[s];
            double parent = p.enrolment[s]-broad.totals[outer][s], maximum = 0;
            for (auto k : local) { parent += result.components[k].reference[outer]; maximum = std::max(maximum,varied[r][k]); }
            double denominator = std::exp(-maximum);
            for (auto k : local) denominator += std::exp(varied[r][k]-maximum);
            for (auto k : local) {
                auto j = result.components[k].unit;
                double addition = parent*std::exp(varied[r][k]-maximum)/denominator;
                result.counts[r][s*g+units[j].group] += addition-result.components[k].reference[outer];
                result.unitMeans[j] += (addition-result.components[k].reference[outer])/count;
            }
            result.totals[r][s] = std::accumulate(result.counts[r].begin()+s*g,result.counts[r].begin()+(s+1)*g,0.);
        }
    }
    for (std::size_t j = 0; j < units.size(); ++j)
        result.unitMeans[j] = units[j].counted+(1-result.noAdditionProbability[units[j].seat])*(result.unitMeans[j]-units[j].counted);
    return result;
}

DrawPlan makeDrawPlan(Result const& result, std::vector<Unit> const& units,
    std::vector<double> const& enrolment) {
    DrawPlan plan; plan.enrolment = enrolment;
    plan.componentsBySeat.resize(enrolment.size()); plan.active.resize(enrolment.size());
    for (auto const& u : units) { plan.counted.push_back(u.counted); plan.seats.push_back(u.seat); }
    for (std::size_t k = 0; k < result.components.size(); ++k) {
        auto s = units.at(result.components[k].unit).seat;
        plan.componentsBySeat.at(s).push_back(k);
        plan.active[s] = plan.active[s] || result.components[k].weight != 0
            || result.components[k].usualLogShift != 0 || result.processingPhase != 0;
    }
    if (result.broad.unitCounts.empty() || result.broad.unitCounts.front().size() != units.size()
        || result.noAdditionProbability.size() != enrolment.size())
        throw std::runtime_error("Invalid turnout draw plan.");
    return plan;
}

std::vector<double> drawUnitCounts(Result const& result, DrawPlan const& plan,
    unsigned long long seed) {
    // Draw a single shared prior row, retaining its relationships between seats
    // and between turnout, formality, early voting and postal applications.
    // Late progress is a separate seat event: exactly no additions, or a
    // positive outcome with previous expectations, tiny additions or a batch.
    auto state = seed;
    auto outer = step(state) % result.broad.unitCounts.size();
    auto counts = result.broad.unitCounts[outer];
    auto uniform = [](unsigned long long& key) {
        // Midpoints of a 52-bit grid never reach a logarithm's endpoints.
        return (double(step(key) >> 12) + .5) / 4503599627370496.;
    };
    std::vector<bool> zero(plan.enrolment.size());
    for (std::size_t s = 0; s < zero.size(); ++s) {
        // Seat keys keep other seats' random draws unchanged if this seat
        // selects its no-addition branch or acquires another reporting unit.
        auto seatKey = seed ^ ((s+1)*0x9e3779b97f4a7c15ULL);
        zero[s] = uniform(seatKey) < result.noAdditionProbability[s];
        if (zero[s] || !plan.active[s]) continue;
        auto const& local = plan.componentsBySeat[s];
        std::vector<double> odds; odds.reserve(local.size());
        double parent = plan.enrolment[s]-result.broad.totals[outer][s], maximum = 0;
        for (auto k : local) {
            auto const& c = result.components[k];
            auto unitKey = seed ^ ((c.unit+1)*0xd1b54a32d192ed03ULL);
            double selector = uniform(unitKey)*(1-result.noAdditionProbability[s]);
            double value = c.referenceOdds[outer];
            if (selector >= c.previousProbability) {
                // Direct mixture sampling avoids solving an inverse mixture
                // CDF in every iteration, while preserving the same component
                // distributions used by the offline prototype.
                double z = std::sqrt(-2*std::log(uniform(unitKey)))
                    * std::cos(6.2831853071795864769*uniform(unitKey));
                bool small = selector < c.previousProbability+c.smallProbability;
                value = small ? c.smallOdds[outer]+z : c.batchOdds[outer]+1.5*z;
            }
            odds.push_back(value); maximum = std::max(maximum,value);
            parent += c.reference[outer];
        }
        // All unfinished categories share a finite parent and leave a slack
        // component for non-voters/informal votes. Joint log weights prevent
        // endpoint caps and preserve the enrolment account in a large batch.
        double denominator = std::exp(-maximum);
        for (double value : odds) denominator += std::exp(value-maximum);
        for (std::size_t k = 0; k < local.size(); ++k) {
            auto j = result.components[local[k]].unit;
            counts[j] = plan.counted[j]+parent*std::exp(odds[k]-maximum)/denominator;
        }
    }
    for (std::size_t j = 0; j < counts.size(); ++j)
        if (zero[plan.seats[j]]) counts[j] = plan.counted[j];
    return counts;
}

std::vector<double> meanTotals(Result const& result) {
    // Consumers must include the exact counted branch when reporting means;
    // the stored sample matrix intentionally retains positive outcomes only.
    std::vector<double> means(result.broad.counted.size());
    for (auto const& row : result.totals) for (std::size_t s = 0; s < row.size(); ++s) means[s] += row[s]/result.totals.size();
    for (std::size_t s = 0; s < means.size(); ++s)
        means[s] = result.broad.counted[s]+(1-result.noAdditionProbability[s])*(means[s]-result.broad.counted[s]);
    return means;
}

double reportingFactor(double hours) {
    constexpr double times[] = {48,54,78,102}, factors[] = {1,.25,.1,.02};
    if (hours <= times[0]) return factors[0];
    for (int k = 1; k < 4; ++k) if (hours <= times[k]) {
        double weight = (hours-times[k-1])/(times[k]-times[k-1]);
        return std::exp((1-weight)*std::log(factors[k-1])+weight*std::log(factors[k]));
    }
    return factors[3];
}
double countingHours(double before, double after) {
    if (!std::isfinite(before) || !std::isfinite(after)) throw std::runtime_error("Invalid turnout counting clock.");
    if (after < before) return -countingHours(after,before);
    double total = 0;
    while (before < after) {
        double day = std::floor(before/24), boundary = std::min(after,(day+1)*24);
        // Unix day zero was Thursday. Sunday contributes twelve equivalent
        // hours, with no jump in accumulated time when midnight is crossed.
        int weekday = (static_cast<int>(day)%7+11)%7;
        total += (boundary-before)*(weekday == 0 ? .5 : 1);
        before = boundary;
    }
    return total;
}
double sourceHour(std::string const& stamp) {
    // Match the prototype's local source clock. Zoned or incomplete strings
    // require an explicit convention rather than silently mixing clock bases.
    if (stamp.size() != 19 || stamp[4] != '-' || stamp[7] != '-' || stamp[10] != 'T' || stamp[13] != ':' || stamp[16] != ':')
        throw std::runtime_error("Unsupported turnout source timestamp: "+stamp);
    auto number = [&](std::size_t start,std::size_t length) {
        int value = 0;
        auto first = stamp.data()+start, last = first+length;
        auto parsed = std::from_chars(first,last,value);
        if (parsed.ec != std::errc{} || parsed.ptr != last) throw std::runtime_error("Invalid turnout source timestamp.");
        return value;
    };
    int y = number(0,4), mo = number(5,2), d = number(8,2), h = number(11,2), mi = number(14,2), s = number(17,2);
    auto date = Date::fromYmd(y,mo,d);
    if (!date || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) throw std::runtime_error("Invalid turnout source timestamp.");
    // Reuse the repository's calendar arithmetic. Avoid local timezone APIs:
    // the Python replay measures differences on the feed's own local clock.
    return double(date->modifiedJulianDay()-40587)*24+h+mi/60.+s/3600.;
}
}
