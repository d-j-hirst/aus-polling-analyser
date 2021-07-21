// STAN: Primary Vote Intention Model

data {
    // data size
    int<lower=1> pollCount;
    int<lower=1> dayCount;
    int<lower=1> houseCount;
    int<lower=0> discontinuityCount;
    real<lower=0.0, upper=100.0> priorResult;
    
    // poll data
    real<lower=0.0, upper=100.0> pollObservations[pollCount]; // poll data
    int<lower=0, upper=1> missingObservations[pollCount]; // 1 is data is missing otherwise zero
    int<lower=0, upper=houseCount> pollHouse[pollCount]; // polling house for each poll
    int<lower=1, upper=dayCount> pollDay[pollCount]; // day on which polling occurred
    
    // day of all discontinuities in term
    // dummy value of 0 is used to indicate no discontinuities since Stan doesn't like zero-size arrays
    int<lower=0, upper=dayCount> discontinuities[discontinuityCount]; 
    
    real<lower=0.0> sigmas[pollCount]; // poll quality adjustment

    //exclude final n parties from the sum-to-zero constraint for houseEffects
    int<lower=0> excludeCount;
    
    int<lower=1> electionDay;
    
    // day-to-day change, higher in a campaign and especially in the final two weeks
    real<lower=0.0> dailySigma;
    real<lower=0.0> campaignSigma;
    real<lower=0.0> finalSigma;
    
    // calibration settings
    real<lower=0.0001> houseEffectSigma;
    real<lower=0.0001> houseEffectSumSigma;
    real<lower=0.0001> priorVoteShareSigma;
}

transformed data {
    int<lower=0> firstDay = min(pollDay);
    real adjustedPriorResult = priorResult;
    int<lower=1> includeCount = (houseCount - excludeCount);
    int<lower=0> housePollCount[includeCount] = rep_array(0, includeCount);
    vector<lower=0.0, upper=1.0>[includeCount] houseWeight;
    real totalHouseWeight;
    for (poll in 1:pollCount) {
        if (pollHouse[poll] <= includeCount) {
            housePollCount[pollHouse[poll]] = housePollCount[pollHouse[poll]] + 1;
        }
    }
    for (house in 1:includeCount) {
        houseWeight[house] = min([1.0, housePollCount[house] * 0.2]);
    }
    totalHouseWeight = sum(houseWeight);
    houseWeight = houseWeight / totalHouseWeight;
    if (priorResult < 0.5) {
        adjustedPriorResult = log(priorResult * 2.0) + 0.5;
    }
}

parameters {
    vector[dayCount] preliminaryVoteShare;
    vector[houseCount] pHouseEffects;
}

model {
    // using this distribution encourages house effects not to be too large but
    // doesn't penalise too heavily if a large house effect is really called for
    pHouseEffects ~ double_exponential(0.0, houseEffectSigma);
    // keep sum of house effects constrained to zero, or near enough
    sum(pHouseEffects[1:includeCount] .* houseWeight) ~ normal(0.0, houseEffectSumSigma);
    // very broad prior distribution, this shouldn't affect the model much unless
    // there is absolutely no data nearby
    preliminaryVoteShare[1:dayCount] ~ normal(adjustedPriorResult, priorVoteShareSigma);
    
    // day-to-day change sampling, excluding discontinuities
    for (day in firstDay:dayCount-1) {
        int isDisc = 0;
        for (discontinuity in discontinuities) {
            if (discontinuity == day) {
                isDisc = 1;
            }
        }
        if (isDisc == 0) {
            // increased volatility during campaign
            real effectiveSigma = dailySigma;
            if (day >= electionDay - 14) { // maximum volatility two weeks before result
                effectiveSigma = finalSigma;
            }
            else if (day >= electionDay - 42) { // heightened volatility six weeks before result
                effectiveSigma = campaignSigma;
            }
            preliminaryVoteShare[day + 1] ~ 
                normal(preliminaryVoteShare[day], effectiveSigma);
        }
    }

    // poll observations
    for (poll in 1:pollCount) {
        if (!missingObservations[poll]) {
            
            real obs = pollObservations[poll];
            real distMean = preliminaryVoteShare[pollDay[poll]] + pHouseEffects[pollHouse[poll]];
            real distSigma = sigmas[poll];
            
            obs ~ normal(distMean, distSigma);
        }
    }
}

generated quantities {
    vector[dayCount] adjustedVoteShare;
    
    // modifiy values near to or beyond edge cases so that they're still valid vote shares
    for (day in firstDay:dayCount) {
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