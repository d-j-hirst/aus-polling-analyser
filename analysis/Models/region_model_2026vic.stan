data {
    // data size
    int<lower=1> pollCount;
    int<lower=1> dayCount;
    int<lower=1, upper=dayCount> pollDay[pollCount]; // day on which polling occurred
    real<lower=0> pollSize[pollCount]; // day on which polling occurred
    real innerMetroDevPoll[pollCount]; // poll data
    real outerMetroDevPoll[pollCount]; // poll data
    real regionalDevPoll[pollCount]; // poll data
    real metroDevPoll[pollCount]; // poll data

}

transformed data {
}

parameters {
    vector[dayCount] innerMetroSwingDev;
    vector[dayCount] outerMetroSwingDev;
    vector[dayCount] regionalSwingDev;
}

model {
    // make sure sum of swing deviations is zero
    innerMetroSwingDev[1:dayCount] * 0.2847 + outerMetroSwingDev[1:dayCount] * 0.369 +
      regionalSwingDev[1:dayCount] * 0.3463 ~ normal(0.0, 0.001);
    // Fairly weak priors, the swing deviations should default to zero, but with a bit of flexibility
    innerMetroSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    outerMetroSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    regionalSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    
    // day-to-day change sampling, excluding discontinuities
    for (day in 1:dayCount-1) {
        real effectiveSigma = 0.25;
        innerMetroSwingDev[day + 1] ~ normal(innerMetroSwingDev[day], effectiveSigma);
        outerMetroSwingDev[day + 1] ~ normal(outerMetroSwingDev[day], effectiveSigma);
        regionalSwingDev[day + 1] ~ normal(regionalSwingDev[day], effectiveSigma);
    }

    // poll observations
    for (poll in 1:pollCount) {
      int day = pollDay[poll]; // scale day for efficiency, and avoid out-of-bounds
      real distSigma = 4.0 / sqrt(pollSize[poll]);
      
      regionalDevPoll[poll] ~ normal(regionalSwingDev[day], distSigma * 1.121);

      if (innerMetroDevPoll[poll] > -100) innerMetroDevPoll[poll] ~ normal(innerMetroSwingDev[day], distSigma * 1.237);
      if (outerMetroDevPoll[poll] > -100) outerMetroDevPoll[poll] ~ normal(outerMetroSwingDev[day], distSigma * 1.087);
      if (metroDevPoll[poll] > -100) {
        metroDevPoll[poll] ~ normal(innerMetroSwingDev[day] * 0.435 + outerMetroSwingDev[day] * 0.565, distSigma);
      }
    }
}

generated quantities {
} 