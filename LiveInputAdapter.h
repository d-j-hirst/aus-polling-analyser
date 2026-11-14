#pragma once
#include "ElectionData.h"
#include "TurnoutModelIO.h"

// The parser retains commission identities. This boundary prepares the single
// effective account consumed by every part of the live forecast, before parties
// are mapped or projected. It does not calculate future votes.
namespace LiveInputAdapter {
LiveInputRecovery::Snapshot identify(Results2::Election const& election,
    TurnoutModelIO::Artifact const& artifact, std::string const& sourceHash);
// Compare accepted records with the source's current district candidate pair
// before apply() reconstructs summaries from recovered (possibly older) data.
// This returns numerators only; live turnout preparation supplies the common
// expected-final-formal denominator.
LiveData::CountProgress reportedCountProgress(Results2::Election const& source,
    LiveInputRecovery::Snapshot const& effective);
// Rebuild the forecast from recovered counts. SA may temporarily use an
// earlier TCP until the new pair reports. Return those modelling-only records
// so their FP basis can follow them into preference preparation.
std::map<std::string,LiveInputRecovery::Record> apply(Results2::Election& election,
    LiveInputRecovery::Snapshot const& effective, std::filesystem::path const& historyFolder = {});
// Recover the accepted observation while the election still holds the source
// identities and published pair. The caller applies it separately.
LiveInputRecovery::Result recoverSource(Results2::Election const& election,
    TurnoutModelIO::Artifact const& artifact, std::filesystem::path const& root,
    std::filesystem::path const& source);
}
