// STAN: Primary Vote Intention Model

data {
    // data size
    int<lower=1> n_polls;
    int<lower=1> n_days;
    int<lower=1> n_houses;
    real<lower=0> pseudoSampleSigma;
    
    // poll data
    real obs_y[n_polls]; // poll data
    int<lower=0, upper=1> missing_y[n_polls]; // 1 is data is missing otherwise zero
    int<lower=1,upper=n_houses> house[n_polls]; // polling house
    int<lower=1,upper=n_days> poll_day[n_polls]; // day on which polling occurred
    vector<lower=0> [n_polls] poll_qual_adj; // poll quality adjustment

    //exclude final n parties from the sum-to-zero constraint for houseEffects
    int<lower=0> n_exclude;
    
    // period of discontinuity and subsequent increased volatility event
    // uncomment if actually used
    //int<lower=1,upper=n_days> discontinuity; // start with a discontinuity
    //int<lower=1,upper=n_days> stability; // end - stability restored
    
    // day-to-day change
    real<lower=0> sigma;
    real<lower=0> sigma_volatile;
}

transformed data {
    int<lower=1> n_include = (n_houses - n_exclude);
    vector[n_polls] missing_data;
    vector[n_polls] sigma_adj;
    vector[n_polls] transformed_obs_y;
    
    for (poll in 1:n_polls) {
        // transform via the logit function
        transformed_obs_y[poll] = log((obs_y[poll] / 100) 
            / (1 - obs_y[poll] / 100)) * 25 + 50;
        missing_data[poll] = (missing_y[poll] > 0 ? 1000000.0 : 1.0);
        // make adjustments to the sigma of poll results to account
        // for the logit/logistic transformation
        sigma_adj[poll] = 1;
        if (obs_y[poll] > 0) {
            // get the derivative of the logit function at this point
            sigma_adj[poll] = 25 / obs_y[poll] + 0.25 / (1 - 0.01 * obs_y[poll]);
        }
    }
}

parameters {
    vector[n_days] centre_track;
    vector[n_houses] pHouseEffects;
}

transformed parameters {
    vector[n_houses] houseEffects;
    houseEffects[1:n_houses] = pHouseEffects[1:n_houses] - 
        mean(pHouseEffects[1:n_include]);
}

model{
    // -- house effects model
    pHouseEffects ~ normal(0, 8.0); // weakly informative PRIOR
    
    // -- temporal model - with a discontinuity followed by increased volatility
    //centre_track[1, p] ~ normal(center, 15); // weakly informative PRIOR
    //centre_track[2:(discontinuity-1), p] ~ 
    //    normal(centre_track[1:(discontinuity-2), p], sigma);
    //centre_track[discontinuity, p] ~ normal(center, 15); // weakly informative PRIOR
    //centre_track[(discontinuity+1):stability, p] ~ 
    //    normal(centre_track[discontinuity:(stability-1), p], sigma_volatile);
    centre_track[2:n_days] ~ 
        normal(centre_track[1:(n_days-1)], sigma);

    // -- observational model
    transformed_obs_y ~ normal(houseEffects[house] + 
        centre_track[poll_day], 
        missing_data .* (sigma_adj * pseudoSampleSigma + poll_qual_adj));
}

generated quantities {
    vector[n_days]  hidden_vote_share;
    
    // un-transform back to actual vote share via the logistic function
    for (day in 1:n_days) {
        hidden_vote_share[day] = 100 / (1 + exp(-0.04 * (centre_track[day] - 50)));
    }
    
} 