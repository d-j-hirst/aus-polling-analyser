## Guide to the Polling Analysis project

(This readme is under construction and may be incomplete)

This project contains the complete code for the analysis of polling trends and election simulations based on those trends. The overall methodology is covered at [https://www.aeforecasts.com/methodology] In terms of the implementation, for a given election forecast, there are four stages to this process:
 * Calculate historical trends for overall, regional and seat-specific outcomes excluding any data from the election being forecasted. This is performed in Python using scripts in the `/analysis` folder.
 * Calculate the poll trend for forecasted election (without historical bias/error adjustments) using the Python script `/analysis/fp_model.py`
 * Combine results of the previous two analyses to create a distribution of overall vote shares for the forecasted election, fully adjusted for historical bias and error. This is calculated in the C++ part of the program.
 * Use this distribution to perform Monte Carlo simulations of the forecasted election, again in C++ using multithreading to maximise throughput.