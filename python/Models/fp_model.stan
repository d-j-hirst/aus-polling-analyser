// STAN: Primary Vote Intention Model

data {
    // data size
    int<lower=1> pollCount;
    int<lower=1> dayCount;
    int<lower=1> houseCount;
    int<lower=0> discontinuityCount;
    real<lower=0> pseudoSampleSigma;
    real<lower=0, upper=100> priorResult;
    
    // poll data
    real<lower=0, upper=100> pollObservations[pollCount]; // poll data
    int<lower=0, upper=1> missingObservations[pollCount]; // 1 is data is missing otherwise zero
    int<lower=1, upper=houseCount> pollHouse[pollCount]; // polling house for each poll
    int<lower=1, upper=dayCount> pollDay[pollCount]; // day on which polling occurred
    
    // day of all discontinuities in term
    // dummy value of 0 is used to indicate no discontinuities since Stan doesn't like zero-size arrays
    int<lower=0, upper=dayCount> discontinuities[discontinuityCount]; 
    
    vector<lower=0> [pollCount] pollQualityAdjustment; // poll quality adjustment
    

    //exclude final n parties from the sum-to-zero constraint for houseEffects
    int<lower=0> excludeCount;
    
    // day-to-day change
    real<lower=0> dailySigma;
}

transformed data {
    int<lower=1> includeCount = (houseCount - excludeCount);
    real adjustedPriorResult = priorResult;
    if (priorResult < 0.5) {
        adjustedPriorResult = log(priorResult * 2.0) + 0.5;
    }
}

parameters {
    vector[dayCount] preliminaryVoteShare;
    vector[houseCount] pHouseEffects;
}

model {
    // weakly informative prior for house effects
    pHouseEffects ~ normal(0.0, 5.0);
    // keep sum of house effects constrained to zero, or near enough
    sum(pHouseEffects[1:includeCount]) ~ normal(0.0, 0.001);
    // high-kurtosis prior distribution to allow for chance of sudden change if the numbers demonstrate it
    preliminaryVoteShare[1:dayCount] ~ normal(adjustedPriorResult, 40.0);
    
    // day-to-day change sampling, excluding discontinuities
    for (day in 1:dayCount-1) {
        int isDisc = 0;
        for (discontinuity in discontinuities) {
            if (discontinuity == day) {
                isDisc = 1;
            }
        }
        if (isDisc == 0) {
            // political events have frequent outliers, use the inbuilt distribution
            // with highest kurtosis
            preliminaryVoteShare[day + 1] ~ 
                normal(preliminaryVoteShare[day], dailySigma);
        }
    }

    // poll observations
    for (poll in 1:pollCount) {
        if (!missingObservations[poll]) {
            
            real obs = pollObservations[poll];
            real distMean = preliminaryVoteShare[pollDay[poll]] + pHouseEffects[pollHouse[poll]];
            real distSigma = pseudoSampleSigma + pollQualityAdjustment[poll];
            
            obs ~ normal(distMean, distSigma);
        }
    }
}

generated quantities {
    vector[dayCount] adjustedVoteShare;
    
    // modifiy values near to or beyond edge cases so that they're still valid vote shares
    for (day in 1:dayCount) {
        real share = preliminaryVoteShare[day];
        if (share < 0.5) {
            adjustedVoteShare[day] = 0.5 * exp(share-0.5);
        } else if (share > 99.5) {
            adjustedVoteShare[day] = 100.0 - 0.5 * exp(99.5 - share);
        } else {
            adjustedVoteShare[day] = share;
        }
    }
    
} 