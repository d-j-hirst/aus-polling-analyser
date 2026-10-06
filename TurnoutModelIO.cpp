#include "TurnoutModelIO.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace TurnoutModelIO {
using json = nlohmann::json;
Selection select(std::filesystem::path const& workspaceRoot, std::string const& election,
    std::string const& mode, std::optional<std::filesystem::path> const& overridePath) {
    Selection selected;
    selected.mode = mode.empty() ? "counts" : mode;
    if (selected.mode != "counts" && selected.mode != "shadow" && selected.mode != "off")
        throw std::runtime_error("Turnout mode must be counts, shadow or off.");
    if (selected.mode == "off") return selected;
    selected.automatic = !overridePath;
    selected.path = overridePath ? *overridePath : workspaceRoot/"downloads"/"turnout"/"cpp-shadow"/(election+"-shadow.json");
    // Absence is expected for elections whose prior has not been prepared.
    // An explicit override remains strict: missing/invalid files must surface
    // as errors, rather than quietly selecting a different model.
    selected.enabled = !selected.automatic || std::filesystem::is_regular_file(selected.path);
    return selected;
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
Artifact read(json const& value) {
    // Version identifiers describe the calculations that generated the fixed
    // outcomes. Dimension/accounting checks make an incompatible or damaged
    // artifact fail before it enters LiveV2's count projection workflow.
    if (value.at("schema_version") != SchemaVersion || value.at("prior_model") != "proportional-prior-6"
        || value.at("count_model") != TurnoutModel::CountVersion || value.at("progress_model") != TurnoutModel::ProgressVersion
        || value.value("allocation_model",std::string{}) != TurnoutModel::AllocationVersion)
        throw std::runtime_error("Unsupported turnout artifact version; regenerate the shadow artifact.");
    Artifact a;
    auto const& prior = value.at("prior");
    a.prior.election = value.at("election"); a.prior.seats = prior.at("seats").get<std::vector<std::string>>();
    a.prior.groups = prior.at("groups").get<std::vector<std::string>>();
    a.prior.subdivisions = prior.at("subdivisions").get<std::vector<std::string>>();
    a.prior.enrolment = prior.at("enrolment").get<std::vector<double>>();
    a.prior.totals = prior.at("totals").get<TurnoutModel::Matrix>();
    a.prior.counts = prior.at("counts").get<TurnoutModel::Matrix>();
    a.prior.units = units(value.at("units"));
    for (auto const& u : a.prior.units) if (u.counted != 0) throw std::runtime_error("Frozen turnout origin must contain no counted votes.");
    TurnoutModel::validate(a.prior);
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
    if (a.decayPpvc && !a.pollClose) throw std::runtime_error("PPVC decay needs an explicit polling close time.");
    a.history = history(value.at("history"));
    a.provenance = value.at("provenance"); a.sensitivities = value.at("sensitivities");
    return a;
}
Artifact load(std::filesystem::path const& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot open turnout shadow artifact: "+path.string());
    json value; stream >> value; return read(value);
}

// The diagnostic is a compact comparison account, not an implicit change to
// forecast votes. Totals and categories use the same count outcomes. Conditional
// late-batch means are labelled as such, rather than mistaken for predictions.
json diagnostic(Artifact const& a, std::vector<TurnoutModel::Unit> const& units,
    TurnoutModel::Result const& r, std::string const& sourceTime) {
    json value{{"schema_version",SchemaVersion},{"mode","shadow"},{"election",a.prior.election},
        {"count_model",TurnoutModel::CountVersion},{"progress_model",TurnoutModel::ProgressVersion},
        {"allocation_model",TurnoutModel::AllocationVersion},
        {"allocation_parameters",{{"progress_scale",TurnoutModel::AllocationProgressScale},{"direction_scale",TurnoutModel::AllocationDirectionScale}}},
        {"source_time",sourceTime},{"provenance",a.provenance},
        {"preparation_samples",r.broad.totals.size()},{"count_draws",a.options.countDraws},
        {"frozen_prior_count_sensitivities",a.sensitivities}};
    value["declaration_schedule"] = {{"model",TurnoutModel::ScheduleVersion},
        {"active",bool(a.options.receiptDeadline)},
        {"receipt_deadline",a.options.receiptDeadline ? json(*a.options.receiptDeadline) : json(nullptr)},
        {"deadline_units","Hours since the epoch on the feed's local source clock"},
        {"counting_hours_after_deadline",r.countingHoursAfterDeadline},
        {"processing_phase",r.processingPhase},{"batch_survival",r.batchSurvival},
        {"processing_allowance_hours",48},{"transition_hours",12},
        {"sunday_counting_fraction",.5},{"exceptional_starting_probability",.02},
        {"exceptional_decay_counting_days",14},{"routine_activity_retention_scale",.05}};
    auto n = a.prior.seats.size(), g = a.prior.groups.size();
    // Count percentiles at the website's colour breakpoints complement the
    // central 95% interval. A rare batch can lie beyond 97.5% yet still be
    // represented at 99/99.9%. These are count diagnostics, not party-share
    // display bands; the full simulator supplies the latter.
    auto labelledPercentiles = [](auto quantile) {
        json rows = json::object();
        for (auto const& [label,probability] : std::vector<std::pair<std::string,double>>{
            {"0.1",.001},{"1",.01},{"5",.05},{"25",.25},{"50",.5},
            {"75",.75},{"95",.95},{"97.5",.975},{"99",.99},{"99.9",.999}})
            rows[label] = quantile(probability);
        return rows;
    };
    double maximumError = 0, minimumAddition = std::numeric_limits<double>::infinity();
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
                count += row[s*g+k]/r.counts.size(); minimumAddition = std::min(minimumAddition,row[s*g+k]-observed);
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
            maximumError = std::max(maximumError,std::abs(sum-r.totals[sample][s]));
        }
    }
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
    value["maximum_accounting_error"] = maximumError; value["minimum_category_addition"] = minimumAddition;
    return value;
}
}
