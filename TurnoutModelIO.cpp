#include "TurnoutModelIO.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <tuple>

namespace TurnoutModelIO {
using json = nlohmann::json;
std::filesystem::path priorPath(std::filesystem::path const& root, std::string const& election) {
    return root/"forecasts"/election/"live-inputs"/"turnout-prior.json";
}
std::filesystem::path historyDirectory(std::filesystem::path const& root, std::string const& election) {
    return root/"forecasts"/election/"live-snapshots"/"turnout";
}
Artifact loadForElection(std::filesystem::path const& root, std::string const& election) {
    auto path = priorPath(root, election);
    auto input = load(path);
    if (input.prior.election != election)
        throw std::runtime_error("Turnout prior is for " + input.prior.election + ", but the live run is " + election + ". File: " + path.string());
    return input;
}

void validateDistrictPopulation(Artifact const& artifact,
    std::map<std::string, double> const& reportedCounts) {
    auto active = reportedCounts;
    for (auto const& [name, contest] : artifact.inactiveContests) {
        auto found = active.find(name);
        if (found == active.end()) continue;
        if (found->second != 0)
            throw std::runtime_error("Inactive turnout contest has reported votes: " + name
                + ". Review its configured status before continuing.");
        active.erase(found);
    }
    if (active.size() != artifact.prior.seats.size())
        throw std::runtime_error("Turnout prior and active live district populations differ.");
    for (auto const& name : artifact.prior.seats)
        if (!active.count(name)) throw std::runtime_error("Turnout district is absent: " + name);
}

std::string observationMapping(TurnoutModel::Prior const& prior) {
    // This is an interpretation marker, not a cryptographic source hash.
    // Sorting names makes array reordering harmless. Expected sizes and fitted
    // parameters do not change the meaning of the retained measured counts.
    std::vector<std::tuple<std::string,std::string,std::string,std::string,std::string>> rows;
    for (auto const& u : prior.units)
        rows.emplace_back(prior.seats.at(u.seat),u.name,prior.groups.at(u.group),u.kind,u.category);
    std::sort(rows.begin(),rows.end());
    std::uint64_t hash = 14695981039346656037ULL;
    auto add = [&](std::string const& text) {
        for (unsigned char c : text) { hash ^= c; hash *= 1099511628211ULL; }
        hash ^= '\n'; hash *= 1099511628211ULL;
    };
    add(prior.election);
    for (auto const& [seat,name,group,kind,category] : rows) {
        add(seat); add(name); add(group); add(kind); add(category);
    }
    return std::to_string(hash);
}

std::vector<TurnoutModel::Observation> loadHistory(std::filesystem::path const& directory,
    std::string const& election, std::string const& sourceTime, std::string const& mapping) {
    // Filtering by source time is essential when rerunning an earlier feed:
    // a cache can contain later observations that were unavailable then.
    double now = TurnoutModel::sourceHour(sourceTime);
    std::vector<TurnoutModel::Observation> observations;
    if (!std::filesystem::exists(directory)) return observations;
    for (auto const& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::ifstream stream(entry.path());
        json row; stream >> row;
        if (row.at("election") != election || row.at("schema_version") != 1)
            throw std::runtime_error("Incompatible turnout observation: " + entry.path().string());
        if (TurnoutModel::sourceHour(row.at("source_time")) >= now) continue;
        if (row.value("mapping",std::string{}) != mapping)
            throw std::runtime_error("Turnout observation category mapping changed: " + entry.path().string()
                + ". Rebuild the count history from retained sources using the current mapping.");
        observations.push_back(history(json::array({row})).front());
    }
    std::sort(observations.begin(), observations.end(), [](auto const& a, auto const& b) { return a.hour < b.hour; });
    return observations;
}

namespace {
json encodeReceivedObservation(std::string const& election, std::string const& sourceTime,
    TurnoutModel::Observation const& observation, std::string const& mapping) {
    // Record received measured counts, independently of forecast outputs.
    // Omitted categories remain unknown; represented zero counts are real
    // source observations, not replacement values invented by the forecast.
    if (TurnoutModel::sourceHour(sourceTime) != observation.hour)
        throw std::runtime_error("Turnout observation source time differs from its timestamp.");
    json row{{"schema_version",1},{"election",election},{"source_time",sourceTime},{"mapping",mapping},{"seats",json::object()}};
    for (auto const& [identity, count] : observation.counts) {
        if (!std::isfinite(count) || count < 0) throw std::runtime_error("Invalid turnout observation count.");
        row["seats"][identity.first]["vote_types"][identity.second] = count;
    }
    return row;
}

void writeReceivedObservation(std::filesystem::path const& directory, std::string const& sourceTime,
    json const& row) {
    // Usually each new source time creates one history file. Revisiting a
    // source replaces only that record, including downward count corrections.
    // Write completely before replacing it so a partial JSON cannot enter history.
    auto filename = sourceTime;
    std::replace(filename.begin(), filename.end(), ':', '-');
    auto path = directory/(filename + ".json"), temporary = directory/(filename + ".tmp");
    std::filesystem::create_directories(directory);
    {
        std::ofstream stream(temporary);
        if (!stream || !(stream << row.dump())) throw std::runtime_error("Cannot write turnout observation: " + temporary.string());
        stream.close();
        if (!stream) throw std::runtime_error("Cannot finish turnout observation: " + temporary.string());
    }
    // Windows rename does not replace an existing file. Only this exact source
    // observation is replaced; original prior inputs and other sources remain.
    std::filesystem::remove(path);
    std::filesystem::rename(temporary, path);
}
}

void recordObservation(std::filesystem::path const& directory, std::string const& election,
    std::string const& sourceTime, TurnoutModel::Observation const& observation, std::string const& mapping) {
    auto record = encodeReceivedObservation(election, sourceTime, observation, mapping);
    writeReceivedObservation(directory, sourceTime, record);
}
// Unit identity and availability are static model inputs. A null counted
// value is deliberately rejected here; missing measurements must be omitted
// by their reader rather than converted to a fabricated zero.
std::vector<TurnoutModel::Unit> units(json const& rows) {
    std::vector<TurnoutModel::Unit> result;
    for (auto const& row : rows) {
        TurnoutModel::Unit u;
        u.seat = row.at("seat"); u.group = row.at("group"); u.name = row.at("name");
        u.category = row.at("category"); u.kind = row.at("kind"); u.weight = row.at("weight");
        u.counted = row.value("counted",0.); u.matched = row.at("matched");
        u.eav = row.at("eav"); u.closed = row.at("closed"); u.excluded = row.at("excluded");
        u.closureReason = row.at("closure_reason"); u.exclusionReason = row.at("exclusion_reason");
        if (u.closed != !u.closureReason.empty() || u.excluded != !u.exclusionReason.empty())
            throw std::runtime_error("Turnout exceptions require their explicit recorded reason.");
        result.push_back(std::move(u));
    }
    return result;
}
std::vector<TurnoutModel::Observation> history(json const& rows) {
    std::vector<TurnoutModel::Observation> result;
    for (auto const& row : rows) {
        TurnoutModel::Observation observation;
        observation.hour = TurnoutModel::sourceHour(row.at("source_time"));
        for (auto const& [seat,record] : row.at("seats").items())
            for (auto const& [category,count] : record.at("vote_types").items()) {
                // Null is an unknown measurement, not evidence of zero votes.
                if (count.is_null()) continue;
                double value = count.get<double>();
                if (!std::isfinite(value) || value < 0) throw std::runtime_error("Invalid turnout history measurement.");
                observation.counts[{seat,category}] = value;
            }
        result.push_back(std::move(observation));
    }
    return result;
}
namespace {
void checkArtifactVersions(json const& value) {
    // Standard inputs contain paired pre-election count outcomes generated by
    // the maintained model. Reject incompatible versions before interpreting
    // their arrays, rather than guessing which calculation created them.
    if (value.at("schema_version") != SchemaVersion || value.at("prior_model") != "proportional-prior-6"
        || value.at("count_model") != TurnoutModel::CountVersion || value.at("progress_model") != TurnoutModel::ProgressVersion
        || value.value("allocation_model",std::string{}) != TurnoutModel::AllocationVersion)
        throw std::runtime_error("Unsupported turnout prior version; regenerate the count prior.");
}

TurnoutModel::Prior readCountPrior(json const& value) {
    // Decode the fixed electorate and category counts. Source history belongs
    // to later live updates, so the starting units must contain no counted votes.
    TurnoutModel::Prior result;
    auto const& prior = value.at("prior");
    result.election = value.at("election"); result.seats = prior.at("seats").get<std::vector<std::string>>();
    result.groups = prior.at("groups").get<std::vector<std::string>>();
    result.subdivisions = prior.at("subdivisions").get<std::vector<std::string>>();
    result.enrolment = prior.at("enrolment").get<std::vector<double>>();
    result.totals = prior.at("totals").get<TurnoutModel::Matrix>();
    result.counts = prior.at("counts").get<TurnoutModel::Matrix>();
    result.units = units(value.at("units"));
    for (auto const& u : result.units) if (u.counted != 0) throw std::runtime_error("Frozen turnout origin must contain no counted votes.");
    TurnoutModel::validate(result);
    return result;
}

void readInactiveContests(json const& value, Artifact& a) {
    // Normally every forecast district participates in the count. Explicitly
    // postponed contests remain forecast seats but have no live count account;
    // require a recorded explanation and prevent overlap with active districts.
    for (auto const& row : value.value("inactive_contests", json::array())) {
        std::string name = row.at("seat");
        InactiveContest contest{row.at("status"), row.at("reason")};
        if (name.empty() || contest.status != "postponed" || contest.reason.empty()
            || std::find(a.prior.seats.begin(), a.prior.seats.end(), name) != a.prior.seats.end()
            || !a.inactiveContests.emplace(name, std::move(contest)).second)
            throw std::runtime_error("Invalid or overlapping inactive turnout contest: " + name);
    }
}

void readOperationalSettings(json const& value, Artifact& a) {
    // Read counting dates, sample sizes and the provenance of the fixed prior.
    // These are configured inputs, not election-specific branches in the model.
    auto const& options = value.at("options");
    a.options.countDraws = options.at("count_draws"); a.options.seed = options.at("seed");
    if (!a.options.countDraws || a.options.countDraws > 64) throw std::runtime_error("Unsupported turnout count draw count.");
    if (!options.at("postal_deadline").is_null()) a.options.postalDeadline = TurnoutModel::sourceHour(options.at("postal_deadline"));
    if (!options.at("receipt_deadline").is_null()) {
        if (options.at("declaration_schedule_model") != TurnoutModel::ScheduleVersion)
            throw std::runtime_error("Unsupported declaration schedule; regenerate the turnout artifact.");
        a.options.receiptDeadline = TurnoutModel::sourceHour(options.at("receipt_deadline"));
    }
    if (!options.at("poll_close").is_null()) a.pollClose = TurnoutModel::sourceHour(options.at("poll_close"));
    a.decayPpvc = options.at("ppvc_reporting_decay");
    a.optionalPreferential = options.value("optional_preferential", false);
    if (a.decayPpvc && !a.pollClose) throw std::runtime_error("PPVC decay needs an explicit polling close time.");
    a.provenance = value.at("provenance"); a.sensitivities = value.at("sensitivities");
}
}

Artifact read(json const& value) {
    // Keep the input boundary explicit: compatibility, fixed count account,
    // contest availability, then operational settings and source evidence.
    checkArtifactVersions(value);
    Artifact artifact;
    artifact.prior = readCountPrior(value);
    readInactiveContests(value, artifact);
    readOperationalSettings(value, artifact);
    return artifact;
}
Artifact load(std::filesystem::path const& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot open required turnout prior: " + path.string()
        + ". Prepare this election's live count inputs before running the live simulation.");
    json value; stream >> value; return read(value);
}

namespace {
struct DiagnosticChecks {
    double maximumError = 0;
    double minimumAddition = std::numeric_limits<double>::infinity();
};

json describeCountSetup(Artifact const& a, TurnoutModel::Result const& r, std::string const& sourceTime) {
    // Identify the installed model and its counting timetable so an observer
    // can interpret the following estimates against their actual source time.
    json value{{"schema_version",SchemaVersion},{"election",a.prior.election},
        {"count_model",TurnoutModel::CountVersion},{"progress_model",TurnoutModel::ProgressVersion},
        {"allocation_model",TurnoutModel::AllocationVersion},
        {"allocation_parameters",{{"progress_scale",TurnoutModel::AllocationProgressScale},{"direction_scale",TurnoutModel::AllocationDirectionScale}}},
        {"source_time",sourceTime},{"provenance",a.provenance},
        {"preparation_samples",r.broad.totals.size()},{"count_draws",a.options.countDraws},
        {"frozen_prior_count_sensitivities",a.sensitivities}};
    value["inactive_contests"] = json::array();
    for (auto const& [name, contest] : a.inactiveContests)
        value["inactive_contests"].push_back({{"seat", name}, {"status", contest.status}, {"reason", contest.reason}});
    value["declaration_schedule"] = {{"model",TurnoutModel::ScheduleVersion},
        {"active",bool(a.options.receiptDeadline)},
        {"receipt_deadline",a.options.receiptDeadline ? json(*a.options.receiptDeadline) : json(nullptr)},
        {"deadline_units","Hours since the epoch on the feed's local source clock"},
        {"counting_hours_after_deadline",r.countingHoursAfterDeadline},
        {"processing_phase",r.processingPhase},{"batch_survival",r.batchSurvival},
        {"processing_allowance_hours",48},{"transition_hours",12},
        {"sunday_counting_fraction",.5},{"exceptional_starting_probability",.02},
        {"exceptional_decay_counting_days",14},{"routine_activity_retention_scale",.05}};
    return value;
}

// Count percentiles at the website's colour breakpoints complement the
// central 95% interval. A rare batch can lie beyond 97.5% yet still be
// represented at 99/99.9%. These are count diagnostics, not party-share
// display bands; the full simulator supplies the latter.
template<class Quantile>
json labelledPercentiles(Quantile quantile) {
    json rows = json::object();
    for (auto const& [label,probability] : std::vector<std::pair<std::string,double>>{
        {"0.1",.001},{"1",.01},{"5",.05},{"25",.25},{"50",.5},
        {"75",.75},{"95",.95},{"97.5",.975},{"99",.99},{"99.9",.999}})
        rows[label] = quantile(probability);
    return rows;
}

DiagnosticChecks describeDistrictCounts(json& value, Artifact const& a,
    std::vector<TurnoutModel::Unit> const& units, TurnoutModel::Result const& r) {
    // Compare the ordinary prior-backed prediction with the progress-adjusted
    // count distribution. Means and percentiles include the chance that no
    // further votes arrive; positive outcomes alone would overstate the mean.
    auto n = a.prior.seats.size(), g = a.prior.groups.size();
    DiagnosticChecks checks;
    value["seats"] = json::array();
    for (std::size_t s = 0; s < n; ++s) {
        std::vector<double> outcomes;
        double total = 0, broad = 0, zero = r.noAdditionProbability[s];
        for (auto const& row : r.totals) { outcomes.push_back(row[s]); total += row[s]/r.totals.size(); }
        for (auto const& row : r.broad.totals) broad += row[s]/r.broad.totals.size();
        total = r.broad.counted[s]+(1-zero)*(total-r.broad.counted[s]);
        std::sort(outcomes.begin(),outcomes.end());
        auto quantile = [&](double p) {
            if (p <= zero || zero == 1) return r.broad.counted[s];
            double index = (p-zero)/(1-zero)*(outcomes.size()-1); auto low = std::size_t(index);
            return outcomes[low]+(index-low)*(outcomes[std::min(low+1,outcomes.size()-1)]-outcomes[low]);
        };
        json seat{{"name",a.prior.seats[s]},{"counted",r.broad.counted[s]},
            {"broad_mean_total",broad},{"no_addition_probability",zero},{"mean_total",total},{"mean_remaining",total-r.broad.counted[s]},
            {"total_95_lower",quantile(.025)},{"total_95_upper",quantile(.975)}};
        seat["total_count_percentiles"] = labelledPercentiles(quantile);
        seat["groups"] = json::array();
        for (std::size_t k = 0; k < g; ++k) {
            double count = 0, observed = 0;
            std::vector<double> groupOutcomes;
            for (auto const& u : units) if (u.seat == s && u.group == k) observed += u.counted;
            for (auto const& row : r.counts) {
                count += row[s*g+k]/r.counts.size(); checks.minimumAddition = std::min(checks.minimumAddition,row[s*g+k]-observed);
                groupOutcomes.push_back(row[s*g+k]);
            }
            count = observed+(1-zero)*(count-observed);
            std::sort(groupOutcomes.begin(),groupOutcomes.end());
            auto groupQuantile = [&](double p) {
                if (p <= zero || zero == 1) return observed;
                double index = (p-zero)/(1-zero)*(groupOutcomes.size()-1); auto low = std::size_t(index);
                return groupOutcomes[low]+(index-low)*(groupOutcomes[std::min(low+1,groupOutcomes.size()-1)]-groupOutcomes[low]);
            };
            seat["groups"].push_back({{"name",a.prior.groups[k]},{"counted",observed},{"mean_total",count},
                {"count_percentiles",labelledPercentiles(groupQuantile)}});
        }
        value["seats"].push_back(std::move(seat));
        for (std::size_t sample = 0; sample < r.totals.size(); ++sample) {
            double sum = std::accumulate(r.counts[sample].begin()+s*g,r.counts[sample].begin()+(s+1)*g,0.);
            checks.maximumError = std::max(checks.maximumError,std::abs(sum-r.totals[sample][s]));
        }
    }
    return checks;
}

void describeUnitAllocations(json& value, Artifact const& a,
    std::vector<TurnoutModel::Unit> const& units, TurnoutModel::Result const& r) {
    // Follow a service's estimate through balancing and progress adjustments.
    // Normal reported booths retain their measured totals; exception reasons
    // explain any closure or exclusion instead of presenting it as behaviour.
    value["units"] = json::array();
    for (std::size_t j = 0; j < units.size(); ++j) {
        double own = 0, released = 0;
        for (auto const& row : r.broad.ownRemaining) own += row[j]/r.broad.ownRemaining.size();
        for (auto const& row : r.broad.remaining) released += row[j]/r.broad.remaining.size();
        value["units"].push_back({{"seat",a.prior.seats[units[j].seat]},
        {"name",units[j].name},{"category",units[j].category},{"counted",units[j].counted},
        {"complete",bool(r.broad.complete[j])},{"mean_total",r.unitMeans[j]},
        {"closure_reason",units[j].closureReason},{"calibration_exclusion_reason",units[j].exclusionReason},
        {"closure_count_conflict",units[j].closed && units[j].counted > 0},
        {"compensation_log_odds_shift",r.broad.compensation[j]},
        {"own_remaining_before_balancing",own},{"balanced_remaining_before_release",r.balancedRemainingMeans[j]},
        {"remaining_after_release",released},{"allocation_group_evidence",r.allocationStrength[j]},
        {"allocation_progress_weight",r.allocationWeight[j]}});
    }
}

void describeLateComponents(json& value, Artifact const& a,
    std::vector<TurnoutModel::Unit> const& units, TurnoutModel::Result const& r) {
    // Show the separate possible additions for each unfinished declaration
    // service. Small/batch means describe that branch if selected, not the
    // overall prediction; their unconditional probabilities supply its weight.
    value["late_components"] = json::array();
    for (auto const& c : r.components) {
        auto const& e = r.evidence[c.unit];
        value["late_components"].push_back({{"unit",c.unit},{"seat",a.prior.seats[units[c.unit].seat]},
            {"name",units[c.unit].name},{"slowing_evidence",e.strength},{"slowing_weight",c.weight},
            {"probabilities",{{"no_additions",r.noAdditionProbability[units[c.unit].seat]},
                {"previous",c.previousProbability},{"small",c.smallProbability},{"batch",c.batchProbability}}},
            {"small_mean_if_other_counts_fixed",c.smallMean},{"batch_mean_if_other_counts_fixed",c.batchMean},
            {"usual_size_factor",std::exp(c.usualLogShift)},
            {"postal_receipt_support",e.postalReceiptSupport}});
    }
}
}

json diagnostic(Artifact const& artifact, std::vector<TurnoutModel::Unit> const& units,
    TurnoutModel::Result const& result, std::string const& sourceTime) {
    // Report the same finite count account used by the live forecast: setup,
    // district distributions, service allocations and exceptional late batches.
    // Accounting checks accompany the results without changing predictions.
    auto value = describeCountSetup(artifact, result, sourceTime);
    auto checks = describeDistrictCounts(value, artifact, units, result);
    describeUnitAllocations(value, artifact, units, result);
    describeLateComponents(value, artifact, units, result);
    value["maximum_accounting_error"] = checks.maximumError;
    value["minimum_category_addition"] = checks.minimumAddition;
    return value;
}
}
