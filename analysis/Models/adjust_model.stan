data {
  int<lower=1> sampleCount;
  int<lower=0> dayCount;
  real<lower=0.0> pollTrend[sampleCount]; // poll data
  real<lower=0.0> result[sampleCount]; // poll trend data
  int<lower=0, upper=1> federal[sampleCount]; // array of 0s and 1s indicating federal elections
  real<lower=0.0> fundamentals[sampleCount]; // fundamentals data
  int<lower=0, upper=1> incumbencies[sampleCount]; // array of 0s and 1s indicating ALP incumbencies
  int<lower=1> days[sampleCount];
  int<lower=0, upper=100> leftoverTime[sampleCount];
  int<lower=0> yearsPrior[sampleCount];

  real<lower=0.1> obsPriorSigma;
  real<lower=0.1> federalObsPriorSigma;
  real<lower=0, upper=1> federalStateDifferenceSigma;
  real<lower=0> pollWeightChangeSigma;
  real<lower=0> sigmaChangeSigma;
  real<lower=0> biasChangeSigma;
  // real<lower=0> incumbencyBiasChangeSigma;
  real<lower=0> recencyBiasHalfLife;
  int<lower=1> sampleWeight;
}

transformed data {
  real<lower=0> recencyBiases[sampleCount];
  int<lower=0> sampleWeights[sampleCount] = rep_array(0, sampleCount);

  for (a in 1:sampleCount) {
    recencyBiases[a] = 0.5 ^ (days[a] / recencyBiasHalfLife);
    // This effectively rounds up to the nearest whole number
    while (sampleWeights[a] <= sampleWeight * recencyBiases[a]) {
      sampleWeights[a] += 1;
    }
  }
}

parameters {
  real<lower=-0.5, upper=1.5> pollWeight[dayCount];
  real<lower=0.1, upper=10> sigma[dayCount];
  real<lower=-0.5, upper=1.5> federalPollWeight[dayCount];
  real<lower=0.1, upper=10> federalSigma[dayCount];
  real bias[dayCount];
  real federalBias[dayCount];
  //real incumbencyBias[dayCount];
}

model {
  real trendEstimate = 0.0;
  real effectivePollWeight = 0.0;
  real thisPollWeight = 0.0;
  real dayWeight = 0.0;
  real effectiveSigma = 0.0;
  int samples = 0;
  int weight1 = 0;
  int weight2 = 0;
  int day = 0;

  //print(sigma);
  for (a in 1:dayCount) {
    sigma[a] ~ chi_square(3);
    pollWeight[a] / 2 + 0.25 ~ beta(1, 1);
    federalSigma[a] ~ chi_square(3);
    federalPollWeight[a] / 2 + 0.25 ~ beta(1, 1);
    bias[a] ~ logistic(0, obsPriorSigma);
    federalBias[a] ~ logistic(0, obsPriorSigma);
    federalBias[a] ~ logistic(bias[a], federalStateDifferenceSigma);
    //incumbencyBias[a] ~ normal(0, obsPriorSigma);
    if (a > 1) {
      for (b in 1:sampleWeight) {
        pollWeight[a] ~ logistic(pollWeight[a - 1], pollWeightChangeSigma);
        sigma[a] ~ logistic(sigma[a - 1], sigmaChangeSigma);
        federalPollWeight[a] ~ logistic(federalPollWeight[a - 1], pollWeightChangeSigma);
        federalSigma[a] ~ logistic(federalSigma[a - 1], sigmaChangeSigma);
        bias[a] ~ logistic(bias[a - 1], biasChangeSigma);
        federalBias[a] ~ logistic(federalBias[a - 1], biasChangeSigma);
        //incumbencyBias[a] ~ normal(incumbencyBias[a - 1], biasChangeSigma);
      }
    }
  }
  
  // Likelihood
  for (sample in 1:sampleCount) {
    if (days[sample] < dayCount) {
      weight2 = leftoverTime[sample] * sampleWeights[sample] / 100;
      weight1 = sampleWeights[sample] - weight2;
    } else {
      weight1 = sampleWeights[sample];
      weight2 = 0;
    }
    if (weight1 > 0) {
      day = days[sample];
      trendEstimate = pollTrend[sample]
        + federalBias[day] * federal[sample]
        + bias[day] * (1 - federal[sample]);
      //  + incumbencyBias[day] * incumbencies[sample];
      effectivePollWeight = federalPollWeight[day] * federal[sample] + pollWeight[day] * (1 - federal[sample]);
      effectiveSigma = federalSigma[day] * federal[sample] + sigma[day] * (1 - federal[sample]);
      for (a in 1:weight1) {
        result[sample] ~ normal(
          trendEstimate * effectivePollWeight + fundamentals[sample] * (1 - effectivePollWeight), effectiveSigma
        );
      }
    }
    if (weight2 > 0) {
      day = days[sample] + 1;
      trendEstimate = pollTrend[sample]
        + federalBias[day] * federal[sample]
        + bias[day] * (1 - federal[sample]);
      //  + incumbencyBias[day] * incumbencies[sample];
      effectivePollWeight = federalPollWeight[day] * federal[sample] + pollWeight[day] * (1 - federal[sample]);
      effectiveSigma = federalSigma[day] * federal[sample] + sigma[day] * (1 - federal[sample]);
      for (a in 1:weight2) {
        result[sample] ~ normal(
          trendEstimate * effectivePollWeight + fundamentals[sample] * (1 - effectivePollWeight), effectiveSigma
        );
      }
    }
  }
}

generated quantities {
  // real bias_output = bias;
  // real sigma_output = sigma;
} 