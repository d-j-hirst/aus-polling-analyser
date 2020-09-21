// STAN: Two-Party Preferred (TPP) Vote Intention Model 

data {
    // data size
    int<lower=1> n_polls;
    int<lower=1> n_days;
    int<lower=1> n_houses;
    
    // assumed standard deviation for all polls
    real<lower=0> pseudoSampleSigma;
    
    // poll data
    vector<lower=0,upper=100>[n_polls] y; // TPP vote share
    int<lower=1> house[n_polls];
    int<lower=1> day[n_polls];
    
    // period of discontinuity event
    // uncomment if actually used
    //int<lower=1,upper=n_days> discontinuity;
    //int<lower=1,upper=n_days> stability;
    
    // exclude final n houses from the house
    // effects sum to zero constraint.
    int<lower=0> n_exclude;
}

transformed data {
    // fixed day-to-day standard deviation
    real sigma = 0.15;
    real sigma_volatile = 0.45;
    int<lower=1> n_include = (n_houses - n_exclude);
}

parameters {
    vector[n_days] hidden_vote_share; 
    vector[n_houses] pHouseEffects;
}

transformed parameters {
    vector[n_houses] houseEffect;
    houseEffect[1:n_houses] = pHouseEffects[1:n_houses] - 
        mean(pHouseEffects[1:n_include]);
}

model {
    // -- temporal model [this is the hidden state-space model]
    hidden_vote_share[1] ~ normal(50, 15); // PRIOR
    
    // Uncomment this if a discontinuity is introduced
    // hidden_vote_share[discontinuity] ~ normal(0.5, 0.15); // PRIOR
    // hidden_vote_share[2:(discontinuity-1)] ~ 
    //     normal(hidden_vote_share[1:(discontinuity-2)], sigma);
    // hidden_vote_share[(discontinuity+1):stability] ~ 
    //     normal(hidden_vote_share[discontinuity:(stability-1)], sigma_volatile);
    //hidden_vote_share[(stability+1):n_days] ~ 
    //    normal(hidden_vote_share[stability:(n_days-1)], sigma);
    
    // Comment this if a discontinuity is introduced
    hidden_vote_share[2:n_days] ~ 
        normal(hidden_vote_share[1:(n_days-1)], sigma);
    
    // -- house effects model
    pHouseEffects ~ normal(0, 8); // PRIOR 

    // -- observed data / measurement model
    y ~ normal(houseEffect[house] + hidden_vote_share[day], 
        pseudoSampleSigma);
}