data {
    int<lower=2> dayCount;
    vector[dayCount] priorMeans;
    vector<lower=0.000001>[dayCount] priorSigmas;
    vector<lower=0.000001>[dayCount - 1] transitionSigmas;

    int<lower=0> pollCount;
    int<lower=1, upper=dayCount> pollDays[pollCount];
    vector[pollCount] pollValues;
    vector<lower=0.000001>[pollCount] pollSigmas;
}

parameters {
    vector[dayCount] latentDaySeries;
}

model {
    latentDaySeries ~ normal(priorMeans, priorSigmas);

    for (day in 1:(dayCount - 1)) {
        latentDaySeries[day + 1] ~ normal(
            latentDaySeries[day],
            transitionSigmas[day]
        );
    }

    for (poll in 1:pollCount) {
        pollValues[poll] ~ normal(
            latentDaySeries[pollDays[poll]],
            pollSigmas[poll]
        );
    }
}
