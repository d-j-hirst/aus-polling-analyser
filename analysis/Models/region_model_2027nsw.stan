data {
    // data size
    int<lower=1> pollCount;
    int<lower=1> dayCount;
    int<lower=1, upper=dayCount> pollDay[pollCount]; // day on which polling occurred
    real<lower=0> pollSize[pollCount]; // day on which polling occurred
    real metroDevPoll[pollCount]; // poll data
    real regionalDevPoll[pollCount]; // poll data

}

transformed data {
}

parameters {
    vector[dayCount] metroSwingDev;
    vector[dayCount] regionalSwingDev;
}

model {
    // make sure sum of swing deviations is zero
    metroSwingDev[1:dayCount] * 0.5796 + regionalSwingDev[1:dayCount] * 0.4204 ~ normal(0.0, 0.001);
    // Fairly weak priors, the swing deviations should default to zero, but with a bit of flexibility
    metroSwingDev[1:dayCount] ~ normal(0.0, 2.0);
    regionalSwingDev[1:dayCount] ~ normal(0.0, 2.0);
    
    // day-to-day change sampling, excluding discontinuities
    for (day in 1:dayCount-1) {
        real effectiveSigma = 0.25;
        metroSwingDev[day + 1] ~ normal(metroSwingDev[day], effectiveSigma);
        regionalSwingDev[day + 1] ~ normal(regionalSwingDev[day], effectiveSigma);
    }

    // poll observations
    for (poll in 1:pollCount) {
      int day = pollDay[poll]; // scale day for efficiency, and avoid out-of-bounds
      real distSigma = 4.0 / sqrt(pollSize[poll]);
      
      regionalDevPoll[poll] ~ normal(regionalSwingDev[day], distSigma * 1.174);
      metroDevPoll[poll] ~ normal(metroSwingDev[day], distSigma);
    }
}

generated quantities {
} 