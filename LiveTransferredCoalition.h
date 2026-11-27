#pragma once

// A polling place can move between two districts whose Coalition candidates
// have very different levels of support. Translate its old FP result to the
// receiving ballot before using incoming votes to learn about that district.
#include <optional>

namespace LiveTransferredCoalition {

struct Reference {
    double nationalOddsFactor;
    bool useLiveDistrictSplit;

    // The receiving district supplies the starting LIB and NAT percentages.
    // The result is the fraction of the booth's *Coalition* votes going to NAT.
    double nationalFraction(double districtLiberal, double districtNational) const {
        double const localNational = districtNational * nationalOddsFactor;
        return localNational / (districtLiberal + localNational);
    }
};

struct FormerBallot {
    // Absence means the party did not stand. A present zero is a candidate
    // who received no recorded votes. Totals include declaration votes.
    std::optional<int> liberal, national;

    std::optional<Reference> reference(int boothLiberal, int boothNational) const {
        if (boothLiberal + boothNational <= 0 || (!liberal && !national)) return {};
        // The old single-party ballot measured combined support only. Use
        // the new district split at opening and as new counts update it.
        if (!liberal || !national) return Reference{1., true};
        if (*liberal <= 0 || *national <= 0) return {};
        // Where voters had a choice, retain the booth's local lean. For
        // example, twice the former district's NAT votes per LIB vote gives
        // twice the receiving district's ratio at opening. Half a ballot on
        // each side keeps a small booth's measured zero from ruling a party
        // out. The ordinary live fit can subsequently revise this estimate.
        double const boothOdds = (boothNational + .5) / (boothLiberal + .5);
        return Reference{boothOdds * *liberal / *national, false};
    }
};
}
