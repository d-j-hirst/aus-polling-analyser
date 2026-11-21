#pragma once

// Choose the starting local Greens first-preference share when the party did
// not stand at the comparison election. Preparation makes this choice before
// the iteration adds placeholders for parties which could stand this time.
#include "json.h"

#include <cmath>
#include <istream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace GreensFpPrior {
using DistrictMedians = std::map<std::string, double>;

// These are modelling assumptions for a contested seat without a recent
// result. At a median of 2% of formal votes, a normal draw with this spread
// on the model's scale (25 times log odds) puts the middle 90% at about
// 0.5–7.7%. An older local estimate shifts the median but retains the wider
// spread because the candidate, boundaries and political circumstances can change.
inline constexpr double DefaultMedianPercent = 2.0;
inline constexpr double TransformedSd = 21.5;

// An election may supply older, comparable lower-house shares. Use current
// district names so an operator can account for redistributions explicitly.
// Missing files are normal. An invalid supplied estimate should be visible.
inline DistrictMedians readDistrictMedians(std::istream& input)
{
	DistrictMedians medians;
	if (!input) return medians;
	auto const data = nlohmann::json::parse(input);
	auto const& districts = data.at("districts");
	if (!districts.is_object()) throw std::runtime_error("Expected a districts object.");
	for (auto const& [district, value] : districts.items()) {
		if (!value.is_number()) throw std::runtime_error("Expected a Greens FP percentage for " + district + ".");
		double const share = value.get<double>();
		if (!std::isfinite(share) || share <= 0.0 || share >= 100.0)
			throw std::runtime_error("Greens FP percentage outside (0, 100) for " + district + ".");
		medians.emplace(district, share);
	}
	return medians;
}

// A recorded share, even one rounded to zero, belongs to the ordinary
// historical calculation. Only an absent result needs a replacement median.
inline std::optional<double> missingResultMedian(bool hadResult,
	std::string const& district, DistrictMedians const& olderEstimates)
{
	if (hadResult) return std::nullopt;
	auto const older = olderEstimates.find(district);
	if (older != olderEstimates.end()) return older->second;
	return DefaultMedianPercent;
}
}
