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
    int<lower=1, upper=houseCount> pollHouse[pollCount]; // polling house for each poll
    int<lower=1, upper=dayCount> pollDay[pollCount]; // day on which polling occurred
    
    // day of all discontinuities in term
    // dummy value of 0 is used to indicate no discontinuities since Stan doesn't like zero-size arrays
    int<lower=0, upper=dayCount> discontinuities[discontinuityCount]; 
    
    real<lower=0.0> heWeights[houseCount]; // house effect weightings, for the sum-to-zero contraint
    real<lower=-100.0, upper=100.0> biases[houseCount]; // previously observed biases for each house count
    real<lower=0.0> sigmas[pollCount]; // poll quality adjustment
    
    int<lower=1> electionDay;
    
    // day-to-day change, higher in a campaign and especially in the final two weeks
    real<lower=0.0> dailySigma;
    real<lower=0.0> campaignSigma;
    real<lower=0.0> finalSigma;
    
    // calibration settings
    real<lower=0.0001> houseEffectSigma;
    real<lower=0.0001> houseEffectSumSigma;
    real<lower=0.0001> priorVoteShareSigma;

    // End points between the transition between "new" and "old" house effects
    real<lower=1> houseEffectNew;
    real<lower=1> houseEffectOld;
}

transformed data {
    real adjustedPriorResult = priorResult;
    int<lower=0> housePollCount[houseCount] = rep_array(0, houseCount);
    vector<lower=0.0, upper=1.0>[houseCount] houseWeight;
    real totalHouseWeight = 0.0;
    real weightedBiasSum = 0.0;
    real<lower=-100.0, upper=100.0> weightedBias;
    for (poll in 1:pollCount) {
        housePollCount[pollHouse[poll]] = housePollCount[pollHouse[poll]] + 1;
    }
    for (house in 1:houseCount) {
        houseWeight[house] = min([1.0, housePollCount[house] * 0.2]) * heWeights[house];
        weightedBiasSum += biases[house] * houseWeight[house];
    }
    totalHouseWeight = sum(houseWeight);
    houseWeight = houseWeight / totalHouseWeight;
    weightedBias = weightedBiasSum / totalHouseWeight;
    if (priorResult < 0.5) {
        adjustedPriorResult = log(priorResult * 2.0) + 0.5;
    }
}

parameters {
    vector[dayCount] preliminaryVoteShare;
    vector[houseCount] pHouseEffects;
    vector[houseCount] pOldHouseEffects;
}

model {
    // using this distribution encourages house effects not to be too large but
    // doesn't penalise too heavily if a large house effect is really called for
    pHouseEffects ~ double_exponential(weightedBias, houseEffectSigma);
    pOldHouseEffects ~ double_exponential(weightedBias, houseEffectSigma);
    // Tend to keep old and new house effects similar, but not too much
    pHouseEffects ~ double_exponential(pOldHouseEffects, houseEffectSigma * 2.0);
    // keep sum of house effects constrained to zero, or near enough
    sum(pHouseEffects[1:houseCount] .* houseWeight) ~ normal(weightedBias, houseEffectSumSigma);
    sum(pOldHouseEffects[1:houseCount] .* houseWeight) ~ normal(weightedBias, houseEffectSumSigma);
    // very broad prior distribution, this shouldn't affect the model much unless
    // there is absolutely no data nearby
    preliminaryVoteShare[1:dayCount] ~ normal(adjustedPriorResult, priorVoteShareSigma);
    
    // day-to-day change sampling, excluding discontinuities
    for (day in 1:dayCount-1) {
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
            int daysBeforePresent = dayCount - pollDay[poll];
            real houseEffectFactor = 1.0;
            real effectiveHouseEffect = 0.0;
            real obs = pollObservations[poll];
            real distMean = 0.0;
            real distSigma = 1.0;
            if (daysBeforePresent >= houseEffectOld) {
                houseEffectFactor = 0.0;
            }
            else if (daysBeforePresent >= houseEffectNew) {
                houseEffectFactor = (houseEffectOld - daysBeforePresent) / (houseEffectOld - houseEffectNew);
            }
            effectiveHouseEffect = pHouseEffects[pollHouse[poll]] * houseEffectFactor +
                pOldHouseEffects[pollHouse[poll]] * (1.0 - houseEffectFactor);
            
            distMean = preliminaryVoteShare[pollDay[poll]] + effectiveHouseEffect;
            distSigma = sigmas[poll];
            
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