data {
    // data size
    int<lower=1> pollCount;
    int<lower=1> dayCount;
    int<lower=1, upper=dayCount> pollDay[pollCount]; // day on which polling occurred
    real<lower=0> pollSize[pollCount]; // day on which polling occurred
    real isSwingDevPoll[pollCount]; // poll data
    real osSwingDevPoll[pollCount]; // poll data
    real coSwingDevPoll[pollCount]; // poll data
    real reSwingDevPoll[pollCount]; // poll data
    real crSwingDevPoll[pollCount]; // poll data
    real seSwingDevPoll[pollCount]; // poll data
    real ceSwingDevPoll[pollCount]; // poll data
    real fnSwingDevPoll[pollCount]; // poll data
    real rexSwingDevPoll[pollCount]; // poll data
    real ruSwingDevPoll[pollCount]; // poll data
}

transformed data {
}

parameters {
    vector[dayCount] isSwingDev;
    vector[dayCount] osSwingDev;
    vector[dayCount] coreSwingDev;
    vector[dayCount] seruSwingDev;
    vector[dayCount] cereSwingDev;
    vector[dayCount] ceruSwingDev;
    vector[dayCount] fnreSwingDev;
    vector[dayCount] fnruSwingDev;
}

model {
    // make sure sum of swing deviations is zero
    isSwingDev[1:dayCount] * 0.1398 + osSwingDev[1:dayCount] * 0.3011 +
        coreSwingDev[1:dayCount] * 0.2151 + seruSwingDev[1:dayCount] * 0.0323 + 
        cereSwingDev[1:dayCount] * 0.0860 + ceruSwingDev[1:dayCount] * 0.0860 +
        fnreSwingDev[1:dayCount] * 0.0753 + fnruSwingDev[1:dayCount] * 0.0645 ~ normal(0.0, 0.001);
    // Fairly weak priors, the swing deviations should default to zero, but with a bit of flexibility
    isSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    osSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    coreSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    seruSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    cereSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    ceruSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    fnreSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    fnruSwingDev[1:dayCount] ~ normal(0.0, 10.0);
    
    // day-to-day change sampling, excluding discontinuities
    for (day in 1:dayCount-1) {
        real effectiveSigma = 0.25;
        isSwingDev[day + 1] ~ normal(isSwingDev[day], effectiveSigma);
        osSwingDev[day + 1] ~ normal(osSwingDev[day], effectiveSigma);
        coreSwingDev[day + 1] ~ normal(coreSwingDev[day], effectiveSigma);
        seruSwingDev[day + 1] ~ normal(seruSwingDev[day], effectiveSigma);
        cereSwingDev[day + 1] ~ normal(cereSwingDev[day], effectiveSigma);
        ceruSwingDev[day + 1] ~ normal(ceruSwingDev[day], effectiveSigma);
        fnreSwingDev[day + 1] ~ normal(fnreSwingDev[day], effectiveSigma);
        fnruSwingDev[day + 1] ~ normal(fnruSwingDev[day], effectiveSigma);
    }

    // poll observations
    for (poll in 1:pollCount) {
      int day = pollDay[poll]; // scale day for efficiency, and avoid out-of-bounds
      real distSigma = 4.0 / sqrt(pollSize[poll]);
      
      if (isSwingDevPoll[poll] > -100) isSwingDevPoll[poll] ~ normal(isSwingDev[day], distSigma);
      if (osSwingDevPoll[poll] > -100) osSwingDevPoll[poll] ~ normal(osSwingDev[day], distSigma);
      if (coSwingDevPoll[poll] > -100) coSwingDevPoll[poll] ~ normal(coreSwingDev[day], distSigma);
      if (reSwingDevPoll[poll] > -100) {
        reSwingDevPoll[poll] ~ normal(
          seruSwingDev[day] * 0.0938 + cereSwingDev[day] * 0.25 + ceruSwingDev[day] * 0.25 +
          fnreSwingDev[day] * 0.2188 + fnruSwingDev[day] * 0.1875,
          distSigma
        );
      }
      if (crSwingDevPoll[poll] > -100) {
        crSwingDevPoll[poll] ~ normal(
          coreSwingDev[day] * 0.3846 + seruSwingDev[day] * 0.0577 + cereSwingDev[day] * 0.1538 +
          ceruSwingDev[day] * 0.1538 + fnreSwingDev[day] * 0.1346 + fnruSwingDev[day] * 0.1154,
          distSigma
        );
      }
      if (seSwingDevPoll[poll] > -100) {
        seSwingDevPoll[poll] ~ normal(
          isSwingDev[day] * 0.2031 + osSwingDev[day] * 0.4375 + coreSwingDev[day] * 0.3125 +
          seruSwingDev[day] * 0.0469,
          distSigma
        );
      }
      if (ceSwingDevPoll[poll] > -100) {
        ceSwingDevPoll[poll] ~ normal(
          cereSwingDev[day] * 0.5 + ceruSwingDev[day] * 0.5,
          distSigma
        );
      }
      if (fnSwingDevPoll[poll] > -100) {
        fnSwingDevPoll[poll] ~ normal(
          fnreSwingDev[day] * 0.5385 + fnruSwingDev[day] * 0.4615,
          distSigma
        );
      }
      if (rexSwingDevPoll[poll] > -100) {
        rexSwingDevPoll[poll] ~ normal(
          coreSwingDev[day] * 0.5714 + cereSwingDev[day] * 0.2286 + fnreSwingDev[day] * 0.2,
          distSigma
        );
      }
      if (ruSwingDevPoll[poll] > -100) {
        ruSwingDevPoll[poll] ~ normal(
          seruSwingDev[day] * 0.1765 + ceruSwingDev[day] * 0.4706 + fnruSwingDev[day] * 0.3529,
          distSigma
        );
      }
    }
}

generated quantities {
} 