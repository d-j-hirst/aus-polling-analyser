data {
    // data size
    int<lower=1> pollCount;
    int<lower=1> dayCount;
    int<lower=1, upper=dayCount> pollDay[pollCount]; // day on which polling occurred
    real<lower=0> pollSize[pollCount]; // day on which polling occurred
    real nswSwingDevPoll[pollCount]; // poll data
    real vicSwingDevPoll[pollCount]; // poll data
    real qldSwingDevPoll[pollCount]; // poll data
    real waSwingDevPoll[pollCount]; // poll data
    real saSwingDevPoll[pollCount]; // poll data
    real wstanSwingDevPoll[pollCount]; // poll data

}

transformed data {
}

parameters {
    vector[dayCount] nswSwingDev;
    vector[dayCount] vicSwingDev;
    vector[dayCount] qldSwingDev;
    vector[dayCount] waSwingDev;
    vector[dayCount] saSwingDev;
    vector[dayCount] tanSwingDev;
}

model {
    // make sure sum of swing deviations is zero
    nswSwingDev[1:dayCount] * 0.3173 + vicSwingDev[1:dayCount] * 0.2556 +
        qldSwingDev[1:dayCount] * 0.2018 + waSwingDev[1:dayCount] * 0.1005 + 
        saSwingDev[1:dayCount] * 0.0749 + tanSwingDev[1:dayCount] * 0.05 ~ normal(0.0, 0.001);
    // Fairly weak priors, the swing deviations should default to zero, but with a bit of flexibility
    nswSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    vicSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    qldSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    waSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    saSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    tanSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    
    // day-to-day change sampling, excluding discontinuities
    for (day in 1:dayCount-1) {
        real effectiveSigma = 0.25;
        nswSwingDev[day + 1] ~ normal(nswSwingDev[day], effectiveSigma);
        vicSwingDev[day + 1] ~ normal(vicSwingDev[day], effectiveSigma);
        qldSwingDev[day + 1] ~ normal(qldSwingDev[day], effectiveSigma);
        waSwingDev[day + 1] ~ normal(waSwingDev[day], effectiveSigma);
        saSwingDev[day + 1] ~ normal(saSwingDev[day], effectiveSigma);
        tanSwingDev[day + 1] ~ normal(tanSwingDev[day], effectiveSigma);
    }

    // poll observations
    for (poll in 1:pollCount) {
      int day = pollDay[poll]; // scale day for efficiency, and avoid out-of-bounds
      real distSigma = 4.0 / sqrt(pollSize[poll]);
      
      nswSwingDevPoll[poll] ~ normal(nswSwingDev[day], distSigma);
      vicSwingDevPoll[poll] ~ normal(vicSwingDev[day], distSigma * 1.114);
      qldSwingDevPoll[poll] ~ normal(qldSwingDev[day], distSigma * 1.254);
      if (waSwingDevPoll[poll] > -100) waSwingDevPoll[poll] ~ normal(waSwingDev[day], distSigma * 1.777);
      if (saSwingDevPoll[poll] > -100) saSwingDevPoll[poll] ~ normal(saSwingDev[day], distSigma * 2.058);
      if (wstanSwingDevPoll[poll] > -100) {
        wstanSwingDevPoll[poll] ~ normal(
          waSwingDev[day] * 0.4459 + saSwingDev[day] * 0.3323 + tanSwingDev[day] * 0.2218,
          distSigma * 2.519
        );
      }
    }
}

generated quantities {
} 