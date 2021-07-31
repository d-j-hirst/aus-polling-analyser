#pragma once

#include <cmath>
#include <random>

class RandomGenerator {
public:
	RandomGenerator() {
		randomise();
	}

	template<typename T,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	T uniform(T low = T(0.0), T high = T(1.0)) {
		return std::uniform_real_distribution<T>(low, high)(gen);
	}

	template<typename T,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	T normal(T mean = T(0.0), T sd = T(1.0)) {
		return std::normal_distribution<T>(mean, sd)(gen);
	}

	template<typename T, typename U,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0,
		std::enable_if_t<std::is_integral<U>::value, int> = 0>
	T t_dist(U df, T mean = T(0.0), T sd = T(1.0)) {
		// We want the t-distribution to match observed standard distribution
		// for the purposes of matching observed high-kurtosis distributions,
		// so rescale the standard distribution by this factor so it has
		// the correct standard deviation
		T unscaled_sd = std::sqrt(T(df) / (T(df) - T(2.0)));
		return std::student_t_distribution<T>(df)(gen) * sd / unscaled_sd + mean;
	}

	template<typename T,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	T logistic(T mean = T(0.0), T sd = T(1.0)) {
		T x = uniform<T>();
		return mean + sd * std::log(x / (T(1.0) - x));
	}

	template<typename T,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	T laplace(T mean = T(0.0), T sd = T(1.0)) {
		T x = -std::log(T(1.0) - std::max(T(0.00001), uniform<T>())) * sd;
		x = uniform<float>() < 0.5f ? x : -x;
		return mean + x;
	}

	// A flexible distribution that allows for different variance and kurtosis to
	// apply to both sides of the distribution. Uses a normal distribution for
	// kurtosis <= 3, and mixed t-distributions for higher kurtosis.
	template<typename T,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	T flexible_dist(T mean = T(0.0), T lower_sd = T(1.0), T upper_sd = T(1.0),
		T lower_kurt = T(3.0), T upper_kurt = T(3.0)) {
		// are we looking in the upper or lower helf of the distribution?
		bool upper = uniform(0.0, 1.0) < 0.5;
		if (upper) {
			if (upper_kurt <= T(3.0)) {
				// use normal distribution for low kurtosis
				return std::abs(normal(mean, upper_sd));
			}
			else {
				// map kurtosis value to approximate t-dist degrees of freedom
				T df = T(6.0) / upper_kurt + T(4.0);
				T floor_df = std::floor(df);
				T ceil_factor = df - floor_df;
				if (uniform(T(0.0), T(1.0)) < ceil_factor) {
					return mean + std::abs(t_dist(int(floor_df) + 1, T(0.0), upper_sd));
				}
				else {
					return mean + std::abs(t_dist(int(floor_df), T(0.0), upper_sd));
				}
			}
		}
		else {
			if (lower_kurt <= T(3.0)) {
				// use normal distribution for low kurtosis
				return -std::abs(normal(mean, lower_sd));
			}
			else {
				// map kurtosis value to approximate t-dist degrees of freedom
				T df = T(6.0) / lower_kurt + T(4.0);
				T floor_df = std::floor(df);
				T ceil_factor = df - floor_df;
				if (uniform(T(0.0), T(1.0)) < ceil_factor) {
					return mean - std::abs(t_dist(int(floor_df) + 1, T(0.0), lower_sd));
				}
				else {
					return mean - std::abs(t_dist(int(floor_df), T(0.0), lower_sd));
				}
			}
		}
	}

	void randomise() { gen.seed(rd()); }

	void setSeed(int seed) { gen.seed(seed); }

private:
	static std::random_device rd;
	static std::mt19937 gen;
};