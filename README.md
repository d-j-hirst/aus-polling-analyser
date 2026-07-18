# Polling Analyser

This README is under construction and may be incomplete.

This project contains the analysis code used to produce polling trends and
election simulations. The overall approach is described on the
[AE Forecasts methodology page](https://www.aeforecasts.com/methodology).

For a given election forecast, the implementation has four stages:

* Calculate historical trends for overall, regional and seat-specific outcomes
  while excluding data from the election being forecast. Python scripts in
  `analysis/` perform this work.
* Calculate the unadjusted poll trend for the forecast election with
  `analysis/fp_model.py`.
* Combine the trend with historical bias and error estimates to create
  distributions of overall vote shares. The C++ application performs this
  stage.
* Run multithreaded Monte Carlo simulations of the election in the C++
  application.

The application uses paths relative to the repository root, so run it with the
repository root as its working directory. See [analysis/README.md](analysis/README.md)
for the Python environment and data-generation workflow.
