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
    real provincialDevPoll[pollCount]; // poll data
    real ruralDevPoll[pollCount]; // poll data
}

transformed data {
}

parameters {
    vector[dayCount] innerMetroSwingDev;
    vector[dayCount] outerMetroSwingDev;
    vector[dayCount] provincialSwingDev;
    vector[dayCount] ruralSwingDev;
}

model {
    // make sure sum of swing deviations is zero
    innerMetroSwingDev[1:dayCount] * 0.2847 + outerMetroSwingDev[1:dayCount] * 0.3896 +
      provincialSwingDev[1:dayCount] * 0.1269 + ruralSwingDev[1:dayCount] * 0.1988 ~ normal(0.0, 0.001);
    // Fairly weak priors, the swing deviations should default to zero, but with a bit of flexibility
    innerMetroSwingDev[1:dayCount] ~ normal(0.0, 2.0);
    outerMetroSwingDev[1:dayCount] ~ normal(0.0, 2.0);
    provincialSwingDev[1:dayCount] ~ normal(0.0, 2.0);
    ruralSwingDev[1:dayCount] ~ normal(0.0, 2.0);
    
    // day-to-day change sampling, excluding discontinuities
    for (day in 1:dayCount-1) {
        real effectiveSigma = 0.25;
        innerMetroSwingDev[day + 1] ~ normal(innerMetroSwingDev[day], effectiveSigma);
        outerMetroSwingDev[day + 1] ~ normal(outerMetroSwingDev[day], effectiveSigma);
        provincialSwingDev[day + 1] ~ normal(provincialSwingDev[day], effectiveSigma);
        ruralSwingDev[day + 1] ~ normal(ruralSwingDev[day], effectiveSigma);
    }

    // poll observations
    for (poll in 1:pollCount) {
      int day = pollDay[poll]; // scale day for efficiency, and avoid out-of-bounds
      real distSigma = 4.0 / sqrt(pollSize[poll]);

      if (innerMetroDevPoll[poll] > -100) innerMetroDevPoll[poll] ~ normal(innerMetroSwingDev[day], distSigma * 1.539);
      if (outerMetroDevPoll[poll] > -100) outerMetroDevPoll[poll] ~ normal(outerMetroSwingDev[day], distSigma * 1.316);
      if (provincialDevPoll[poll] > -100) provincialDevPoll[poll] ~ normal(provincialSwingDev[day], distSigma * 2.305);
      if (ruralDevPoll[poll] > -100) ruralDevPoll[poll] ~ normal(ruralSwingDev[day], distSigma * 1.842);
      
      if (regionalDevPoll[poll] > -100) {
        regionalDevPoll[poll] ~ normal(provincialSwingDev[day] * 0.39 + ruralSwingDev[day] * 0.61, distSigma);
      }

      if (metroDevPoll[poll] > -100) {
        metroDevPoll[poll] ~ normal(innerMetroSwingDev[day] * 0.435 + outerMetroSwingDev[day] * 0.565, distSigma * 1.439);
      }
    }
}

generated quantities {
} 