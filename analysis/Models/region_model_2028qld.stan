data {
    // data size
    int<lower=1> pollCount;
    int<lower=1> dayCount;
    int<lower=1, upper=dayCount> pollDay[pollCount]; // day on which polling occurred
    real<lower=0> pollSize[pollCount]; // day on which polling occurred
    real metroDevPoll[pollCount]; // poll data
    real seqDevPoll[pollCount]; // poll data
    real regionalDevPoll[pollCount]; // poll data

}

transformed data {
}

parameters {
    vector[dayCount] metroSwingDev;
    vector[dayCount] seqSwingDev;
    vector[dayCount] regionalSwingDev;
}

model {
    // make sure sum of swing deviations is zero
    metroSwingDev[1:dayCount] * 0.4534 + seqSwingDev[1:dayCount] * 0.2841 + regionalSwingDev[1:dayCount] * 0.2625 ~ normal(0.0, 0.001);
    // Fairly weak priors, the swing deviations should default to zero, but with a bit of flexibility
    metroSwingDev[1:dayCount] ~ normal(0.0, 2.0);
    seqSwingDev[1:dayCount] ~ normal(0.0, 2.0);
    regionalSwingDev[1:dayCount] ~ normal(0.0, 2.0);
    
    // day-to-day change sampling, excluding discontinuities
    for (day in 1:dayCount-1) {
        real effectiveSigma = 0.25;
        metroSwingDev[day + 1] ~ normal(metroSwingDev[day], effectiveSigma);
        seqSwingDev[day + 1] ~ normal(seqSwingDev[day], effectiveSigma);
        regionalSwingDev[day + 1] ~ normal(regionalSwingDev[day], effectiveSigma);
    }

    // poll observations
    for (poll in 1:pollCount) {
      int day = pollDay[poll]; // scale day for efficiency, and avoid out-of-bounds
      real distSigma = 4.0 / sqrt(pollSize[poll]);
      
      metroDevPoll[poll] ~ normal(metroSwingDev[day], distSigma);
      seqDevPoll[poll] ~ normal(seqSwingDev[day], distSigma * 1.263);
      regionalDevPoll[poll] ~ normal(regionalSwingDev[day], distSigma * 1.314);
      
    }
}

generated quantities {
} 