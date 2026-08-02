functions {
    real exponential_tail_share(real latentShare) {
        if (latentShare < 0.5) {
            return 0.5 * exp(latentShare - 0.5);
        }
        if (latentShare > 99.5) {
            return 100.0 - 0.5 * exp(99.5 - latentShare);
        }
        return latentShare;
    }

    real smooth_logit_share(real latentShare) {
        return 100.0 * inv_logit(latentShare);
    }

    real exponential_tail_log_jacobian(real latentShare) {
        if (latentShare < 0.5) {
            return log(0.5) + latentShare - 0.5;
        }
        if (latentShare > 99.5) {
            return log(0.5) + 99.5 - latentShare;
        }
        return 0.0;
    }

    real smooth_logit_log_jacobian(real latentShare) {
        return (
            log(100.0)
            - log1p_exp(-latentShare)
            - log1p_exp(latentShare)
        );
    }
}

data {
    int<lower=1, upper=3> approach;
    real<lower=0.0, upper=100.0> priorMean;
    real<lower=0.000001> priorSigma;
    int<lower=0> pollCount;
    vector<lower=0.0, upper=100.0>[pollCount] pollValues;
    vector<lower=0.000001>[pollCount] pollSigmas;
}

parameters {
    real latentShare;
}

transformed parameters {
    real modelShare;

    if (approach == 1) {
        modelShare = latentShare;
    } else if (approach == 2) {
        modelShare = exponential_tail_share(latentShare);
    } else {
        modelShare = smooth_logit_share(latentShare);
    }
}

model {
    if (approach == 1) {
        latentShare ~ normal(priorMean, priorSigma);
    } else {
        modelShare ~ normal(priorMean, priorSigma);
        if (approach == 2) {
            target += exponential_tail_log_jacobian(latentShare);
        } else {
            target += smooth_logit_log_jacobian(latentShare);
        }
    }

    pollValues ~ normal(modelShare, pollSigmas);
}

generated quantities {
    real reportedShare;

    if (approach == 1) {
        // This reproduces the current raw likelihood plus transformed output.
        reportedShare = exponential_tail_share(latentShare);
    } else {
        reportedShare = modelShare;
    }
}
