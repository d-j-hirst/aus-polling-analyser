#pragma once

#include "General.h"
#include <optional>
#include <string_view>

// Preparation measures local FP and non-classic TCP variation from identical
// deterministic samples. Cache the unchanged comparison account once; retain
// the original float summation order, zero handling and sorted RMS reduction.
namespace LiveVariability {
using Counts = std::map<int, float>;

inline float positiveVoteTotal(
  std::map<int, float> const& votes,
  std::string_view context) {
  float total = 0.0f;
  for (auto const& [partyId, partyVotes] : votes) {
    if (!std::isfinite(partyVotes) || partyVotes < 0.0f) {
      throw std::runtime_error(
        std::string(context) + " has invalid votes for party "
        + std::to_string(partyId) + ".");
    }
    total += partyVotes;
  }
  if (!std::isfinite(total) || total <= 0.0f) {
    throw std::runtime_error(
      std::string(context) + " has no positive finite vote total.");
  }
  return total;
}

inline void addProjectedVotes(
  std::map<int, float>& totals,
  std::map<int, float> const& votes,
  std::string_view context) {
  for (auto const& [partyId, partyVotes] : votes) {
    if (!std::isfinite(partyVotes) || partyVotes < 0.0f) {
      throw std::runtime_error(
        std::string(context) + " has invalid votes for party "
        + std::to_string(partyId) + ".");
    }
    totals[partyId] += partyVotes;
  }
}

inline float transformedPartyShare(
  std::map<int, float> const& votes,
  int partyId,
  float total,
  std::string_view context) {
  if (!votes.contains(partyId)) {
    throw std::runtime_error(
      std::string(context) + " is missing party "
      + std::to_string(partyId) + ".");
  }
  float const share = votes.at(partyId) / total * 100.0f;
  if (!std::isfinite(share) || share <= 0.0f || share >= 100.0f) {
    throw std::runtime_error(
      std::string(context) + " has an untransformable share for party "
      + std::to_string(partyId) + ".");
  }
  float const transformedShare = transformVoteShare(share);
  if (!std::isfinite(transformedShare)) {
    throw std::runtime_error(
      std::string(context) + " produced a non-finite transformed share for party "
      + std::to_string(partyId) + ".");
  }
  return transformedShare;
}

inline float deterministicRootMeanSquare(
  std::vector<float>& samples,
  std::string_view context) {
  if (samples.empty()) {
    throw std::runtime_error(
      std::string(context) + " has no variability samples.");
  }
  for (float sample : samples) {
    if (!std::isfinite(sample)) {
      throw std::runtime_error(
        std::string(context) + " contains a non-finite variability sample.");
    }
  }
  // Worker completion order is nondeterministic; sort before floating-point
  // reduction so identical inputs produce identical prepared parameters.
  std::sort(samples.begin(), samples.end());
  float const squaredTotal = std::accumulate(
    samples.begin(), samples.end(), 0.0f,
    [](float sum, float sample) { return sum + sample * sample; });
  return std::sqrt(squaredTotal / samples.size());
}


struct Baseline {
    Counts fp;
    Counts fpTransformed;
    float fpTotal = 0;
    // TCP can be absent even when the seat has a final pair: no comparable
    // booth-level account exists after some pairing changes.
    std::optional<std::pair<int, float>> tcp;
    std::optional<float> tpp;
};

struct SeatSample {
    Counts fp;
    std::optional<float> tcp, tpp;
};
using Sample = std::vector<SeatSample>;

inline Baseline prepareBaseline(Counts fp, Counts const& tcp,
    std::optional<Counts> const& tpp, std::string const& context) {
    Baseline result;
    float total = positiveVoteTotal(fp, context + " base FP projection");
    result.fpTotal = total;
    for (auto const& [party, votes] : fp) {
        // Zero parties remain in the identity account but need no log-odds.
        if (votes == 0) continue;
        // Preserve the old delayed endpoint validation: a party whose varied
        // count is zero is skipped before transforming its baseline. Usually
        // every positive baseline is interior and can be cached immediately.
        float share = votes / total * 100.0f;
        if (share <= 0 || share >= 100) continue;
        result.fpTransformed[party] = transformedPartyShare(fp, party, total,
            context + " base FP projection");
    }
    result.fp = std::move(fp);
    if (tcp.size() >= 2) {
        int party = tcp.begin()->first;
        float pairTotal = positiveVoteTotal(tcp, context + " base TCP projection");
        result.tcp = {party, transformedPartyShare(tcp, party, pairTotal,
            context + " base TCP projection")};
    }
    if (tpp) {
        float pairTotal = positiveVoteTotal(*tpp, context + " base TPP projection");
        result.tpp = transformedPartyShare(*tpp, 0, pairTotal, context + " base TPP projection");
    }
    return result;
}

inline Counts fpDifferences(Baseline const& baseline, Counts const& varied,
    std::string const& context) {
    float total = positiveVoteTotal(varied, context + " varied FP projection");
    Counts changes;
    for (auto const& [party, votes] : baseline.fp) {
        if (!varied.contains(party)) throw std::runtime_error(
            context + " varied FP projection is missing party " + std::to_string(party) + ".");
        if (votes == 0 || varied.at(party) == 0) continue;
        float base = baseline.fpTransformed.contains(party) ? baseline.fpTransformed.at(party)
            : transformedPartyShare(baseline.fp, party, baseline.fpTotal, context + " base FP projection");
        changes[party] = transformedPartyShare(varied, party, total,
            context + " varied FP projection") - base;
    }
    return changes;
}

inline float pairDifference(Counts const& varied, int party, float baseline,
    std::string const& context) {
    float total = positiveVoteTotal(varied, context);
    return transformedPartyShare(varied, party, total, context) - baseline;
}
} // namespace LiveVariability
