#pragma once

#include "json.h"
#include <cmath>

// The history viewer needs a few scalar diagnostics, not the joint state's
// seat vectors, covariance loadings or reporting-unit observations. Keep this
// boundary explicit so browsing a long sequence retains bounded per-run data.
namespace LiveTppDiagnostics {
inline nlohmann::json viewerSummary(nlohmann::json const& integration) {
    nlohmann::json summary = nlohmann::json::object();
    if (!integration.is_object()) return summary;
    struct Field { char const* source; char const* target; double scale; };
    constexpr Field fields[] = {
        {"weighted_posterior_centre", "tpp_joint_centre", 1},
        {"weighted_prior_sd", "tpp_prior_sd", 1},
        {"weighted_posterior_sd", "tpp_posterior_sd", 1},
        {"inferred_evidence_percent", "tpp_inferred_percent", 1},
        {"conversion_uncertainty_sd", "tpp_conversion_sd", 1},
        {"conversion_pool_variance", "tpp_conversion_pool_variance", 1},
        {"conversion_pool_percent", "tpp_conversion_pool_percent", 1},
        {"conversion_local_sd", "tpp_conversion_local_sd", 1},
        {"conversion_shared_sd", "tpp_conversion_shared_sd", 1},
        {"conversion_matched_units", "tpp_conversion_matched_units", 1},
        {"ordinary_fp_order_sd", "tpp_fp_order_sd", 1},
        {"ordinary_tcp_order_sd", "tpp_tcp_order_sd", 1},
        {"ordinary_fp_order_coverage", "tpp_fp_order_coverage_percent", 100},
        {"ordinary_tcp_order_coverage", "tpp_tcp_order_coverage_percent", 100},
        {"composition_covariance_budget_scale", "tpp_category_budget_percent", 100}
    };
    for (auto const& field : fields) {
        auto value = integration.find(field.source);
        if (value == integration.end() || !value->is_number()) continue;
        double number = value->get<double>() * field.scale;
        if (std::isfinite(number)) summary[field.target] = number;
    }
    // An absent or deterministic prior has no meaningful retained-variance
    // ratio. Leave it blank rather than manufacturing a zero-percent reading.
    if (summary.contains("tpp_prior_sd") && summary.contains("tpp_posterior_sd")) {
        double prior = summary["tpp_prior_sd"], posterior = summary["tpp_posterior_sd"];
        if (prior > 0 && posterior >= 0) {
            double ratio = 100 * std::pow(posterior / prior, 2);
            if (std::isfinite(ratio)) summary["tpp_variance_retained_percent"] = ratio;
        }
    }
    return summary;
}
}
