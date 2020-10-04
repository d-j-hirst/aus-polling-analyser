// STAN: Primary Vote Intention Model

functions {

    real clamp(real x, real minVal, real maxVal) {
        return min([max([x, minVal]), maxVal]);
    }
    
    real truncationFactor() {
        return 5.0;
    }

    real logit_transform(real x) {
        return log(((x + truncationFactor()) / (100.0 + 2.0 * truncationFactor())) / (1.0 - (x + truncationFactor()) / (100.0 + 2.0 * truncationFactor()))) * (25.0 + 0.5 * truncationFactor()) + 50.0;
    }
    
    real logistic_transform(real x) {
        real trans = (100.0 + truncationFactor() * 2.0) / (1.0 + exp(-(1.0 / (25 + truncationFactor() * 0.5)) * (x - 50.0))) - truncationFactor();
        return clamp(trans, 0.0, 100.0);
    }
}

data {
    // data size
    int<lower=1> n_polls;
    int<lower=1> n_days;
    int<lower=1> n_houses;
    real<lower=0> pseudoSampleSigma;
    real<lower=0, upper=100> priorResult;
    
    // poll data
    real<lower=0, upper=100> obs_y[n_polls]; // poll data
    int<lower=0, upper=1> missing_y[n_polls]; // 1 is data is missing otherwise zero
    int<lower=1,upper=n_houses> house[n_polls]; // polling house
    int<lower=1,upper=n_days> poll_day[n_polls]; // day on which polling occurred
    vector<lower=0> [n_polls] poll_qual_adj; // poll quality adjustment

    //exclude final n parties from the sum-to-zero constraint for houseEffects
    int<lower=0> n_exclude;
    
    // day-to-day change
    real<lower=0> dailySigma;
}

transformed data {
    int<lower=1> n_include = (n_houses - n_exclude);
    vector[n_polls] sigma_adj;
    vector[n_polls] transformed_obs_y;
    real transformedPriorResult = priorResult;
    
    for (poll in 1:n_polls) {
        // transform via the logit function
        transformed_obs_y[poll] = obs_y[poll];
    }
}

parameters {
    vector[n_days] transformed_vote;
    vector[n_houses] pHouseEffects;
}

transformed parameters {
    vector[n_houses] houseEffects;
    houseEffects[1:n_houses] = pHouseEffects[1:n_houses] - 
        mean(pHouseEffects[1:n_include]);
}

model {
    // -- house effects model
    pHouseEffects ~ normal(0.0, 3.0); // weakly informative PRIOR
    
    transformed_vote[1:n_days] ~ normal(transformedPriorResult, 50.0); // weakly informative PRIOR
    for (day in 1:n_days-1) {
        transformed_vote[day + 1] ~ 
            normal(transformed_vote[day], dailySigma);
    }

    // -- observational model
    for (poll in 1:n_polls) {
        if (!missing_y[poll]) {
            
            real obs = transformed_obs_y[poll];
            real dist_mean = transformed_vote[poll_day[poll]] + houseEffects[house[poll]];
            real dist_sigma = pseudoSampleSigma;
            
            obs ~ double_exponential(dist_mean, dist_sigma);
        }
    }
}

generated quantities {
    vector[n_days]  hidden_vote_share;
    
    // un-transform back to actual vote share via the logistic function
    for (day in 1:n_days) {
        if (transformed_vote[day] < 0.5) {
            hidden_vote_share[day] = 0.5 * exp(transformed_vote[day]-0.5);
        } else if (transformed_vote[day] > 99.5) {
            hidden_vote_share[day] = 100.0 - 0.5 * exp(99.5 - transformed_vote[day]);
        } else {
            hidden_vote_share[day] = transformed_vote[day];
        }
    }
    
} 