#pragma once

#include "General.h"
#include <optional>
#include <string_view>

// Preparation measures local FP and non-classic TCP variation from identical
// deterministic samples. Cache the unchanged comparison account once; retain
// the original summation order, zero handling and sorted RMS reduction, now
// retaining double precision throughout the working calculation.
namespace LiveVariability {
using Counts = std::map<int, double>;

// Start a reused preparation booth from exactly the same state as a fresh
// copy. A sample varies candidate composition while holding accepted votes,
// historical comparisons, turnout targets and prepared evidence fixed.
//
// The three temporary maps contain that sample's projected FP/TCP/TPP counts.
// futureFpShares describes its genuinely new voters and is also used to infer
// their preferences. Some projection branches leave these maps untouched, so
// restore the prepared contents rather than assuming that clearing is enough.
// Counted/revised pair paths can also write completion and confidence; restore
// those weights before another sample can read them. No other booth fields
// are writable along the randomised preparation path. Keep this boundary in
// sync when adding writes to that path, including the optional TPP diagnostic.
template<class Booth>
void restoreBoothForSample(Booth& working, Booth const& prepared) {
    working.node.tempFpVotesProjected = prepared.node.tempFpVotesProjected;
    working.node.tempTcpVotesProjected = prepared.node.tempTcpVotesProjected;
    working.node.tempTppVotesProjected = prepared.node.tempTppVotesProjected;
    working.futureFpShares = prepared.futureFpShares;
    working.node.tcpCompletion = prepared.node.tcpCompletion;
    working.node.tcpConfidence = prepared.node.tcpConfidence;
    working.node.tppCompletion = prepared.node.tppCompletion;
    working.node.tppConfidence = prepared.node.tppConfidence;
}

inline double positiveVoteTotal(
  std::map<int, double> const& votes,
  std::string_view context) {
  double total = 0.0;
  for (auto const& [partyId, partyVotes] : votes) {
    if (!std::isfinite(partyVotes) || partyVotes < 0.0) {
      throw std::runtime_error(
        std::string(context) + " has invalid votes for party "
        + std::to_string(partyId) + ".");
    }
    total += partyVotes;
  }
  if (!std::isfinite(total) || total <= 0.0) {
    throw std::runtime_error(
      std::string(context) + " has no positive finite vote total.");
  }
  return total;
}

inline void addProjectedVotes(
  std::map<int, double>& totals,
  std::map<int, double> const& votes,
  std::string_view context) {
  for (auto const& [partyId, partyVotes] : votes) {
    if (!std::isfinite(partyVotes) || partyVotes < 0.0) {
      throw std::runtime_error(
        std::string(context) + " has invalid votes for party "
        + std::to_string(partyId) + ".");
    }
    totals[partyId] += partyVotes;
  }
}

inline double transformedPartyShare(
  std::map<int, double> const& votes,
  int partyId,
  double total,
  std::string_view context) {
  if (!votes.contains(partyId)) {
    throw std::runtime_error(
      std::string(context) + " is missing party "
      + std::to_string(partyId) + ".");
  }
  double const share = votes.at(partyId) / total * 100.0;
  if (!std::isfinite(share) || share <= 0.0 || share >= 100.0) {
    throw std::runtime_error(
      std::string(context) + " has an untransformable share for party "
      + std::to_string(partyId) + ".");
  }
  double const transformedShare = transformVoteShare(share);
  if (!std::isfinite(transformedShare)) {
    throw std::runtime_error(
      std::string(context) + " produced a non-finite transformed share for party "
      + std::to_string(partyId) + ".");
  }
  return transformedShare;
}

inline double deterministicRootMeanSquare(
  std::vector<double>& samples,
  std::string_view context) {
  if (samples.empty()) {
    throw std::runtime_error(
      std::string(context) + " has no variability samples.");
  }
  for (double sample : samples) {
    if (!std::isfinite(sample)) {
      throw std::runtime_error(
        std::string(context) + " contains a non-finite variability sample.");
    }
  }
  // Worker completion order is nondeterministic; sort before floating-point
  // reduction so identical inputs produce identical prepared parameters.
  std::sort(samples.begin(), samples.end());
  double const squaredTotal = std::accumulate(
    samples.begin(), samples.end(), 0.0,
    [](double sum, double sample) { return sum + sample * sample; });
  return std::sqrt(squaredTotal / samples.size());
}


struct Baseline {
    Counts fp;
    Counts fpTransformed;
    double fpTotal = 0;
    // TCP can be absent even when the seat has a final pair: no comparable
    // booth-level account exists after some pairing changes.
    std::optional<std::pair<int, double>> tcp;
    std::optional<double> tpp;
};

struct SeatSample {
    Counts fp;
    std::optional<double> tcp, tpp;
};
using Sample = std::vector<SeatSample>;

inline Baseline prepareBaseline(Counts fp, Counts const& tcp,
    std::optional<Counts> const& tpp, std::string const& context) {
    Baseline result;
    double total = positiveVoteTotal(fp, context + " base FP projection");
    result.fpTotal = total;
    for (auto const& [party, votes] : fp) {
        // Zero parties remain in the identity account but need no log-odds.
        if (votes == 0) continue;
        // Preserve the old delayed endpoint validation: a party whose varied
        // count is zero is skipped before transforming its baseline. Usually
        // every positive baseline is interior and can be cached immediately.
        double share = votes / total * 100.0;
        if (share <= 0 || share >= 100) continue;
        result.fpTransformed[party] = transformedPartyShare(fp, party, total,
            context + " base FP projection");
    }
    result.fp = std::move(fp);
    if (tcp.size() >= 2) {
        int party = tcp.begin()->first;
        double pairTotal = positiveVoteTotal(tcp, context + " base TCP projection");
        result.tcp = {party, transformedPartyShare(tcp, party, pairTotal,
            context + " base TCP projection")};
    }
    if (tpp) {
        double pairTotal = positiveVoteTotal(*tpp, context + " base TPP projection");
        result.tpp = transformedPartyShare(*tpp, 0, pairTotal, context + " base TPP projection");
    }
    return result;
}

inline Counts fpDifferences(Baseline const& baseline, Counts const& varied,
    std::string const& context) {
    double total = positiveVoteTotal(varied, context + " varied FP projection");
    Counts changes;
    for (auto const& [party, votes] : baseline.fp) {
        if (!varied.contains(party)) throw std::runtime_error(
            context + " varied FP projection is missing party " + std::to_string(party) + ".");
        if (votes == 0 || varied.at(party) == 0) continue;
        double base = baseline.fpTransformed.contains(party) ? baseline.fpTransformed.at(party)
            : transformedPartyShare(baseline.fp, party, baseline.fpTotal, context + " base FP projection");
        changes[party] = transformedPartyShare(varied, party, total,
            context + " varied FP projection") - base;
    }
    return changes;
}

inline double pairDifference(Counts const& varied, int party, double baseline,
    std::string const& context) {
    double total = positiveVoteTotal(varied, context);
    return transformedPartyShare(varied, party, total, context) - baseline;
}
} // namespace LiveVariability
