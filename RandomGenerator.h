#pragma once

#include "Log.h"

#include <boost/random/beta_distribution.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <random>
#include <vector>

class RandomGenerator {
public:
	RandomGenerator() {
		randomise();
	}

	template<typename T = float,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
		static T uniform(T low = T(0.0), T high = T(1.0)) {
		return std::uniform_real_distribution<T>(low, high)(gen);
	}

	template<typename T = float,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
		static T uniform(std::pair<T, T> range) {
		return std::uniform_real_distribution<T>(range.first, range.second)(gen);
	}

	template<typename T = int,
		std::enable_if_t<std::is_integral<T>::value, int> = 0>
	static T uniform_int(T low = T(0), T high = T(2)) {
		return std::clamp(T(floor(uniform(float(low), float(high)))), low, high - T(1));
	}

	template<typename T,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	static T normal(T mean = T(0.0), T sd = T(1.0)) {
		return std::normal_distribution<T>(mean, sd)(gen);
	}

	template<typename T, typename U,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0,
		std::enable_if_t<std::is_integral<U>::value, int> = 0>
	static T scaledTdist(U df, T mean = T(0.0), T sd = T(1.0)) {
		// We want the t-distribution to match observed standard distribution
		// for the purposes of matching observed high-kurtosis distributions,
		// so rescale the standard distribution by this factor so it has
		// the correct standard deviation
		T unscaled_sd = std::sqrt(T(df) / (T(df) - T(2.0)));
		return std::student_t_distribution<T>(df)(gen) * sd / unscaled_sd + mean;
	}

	template<typename T,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	static T logistic(T mean = T(0.0), T sd = T(1.0)) {
		T x = uniform<T>();
		return mean + sd * std::log(x / (T(1.0) - x));
	}

	template<typename T,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	static T laplace(T mean = T(0.0), T sd = T(1.0)) {
		T x = -std::log(T(1.0) - std::max(T(0.00001), uniform<T>())) * sd;
		x = uniform<float>() < 0.5f ? x : -x;
		return mean + x;
	}

	template<typename T,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
		static T beta(T alpha = T(1.0), T beta = T(1.0)) {
		boost::random::beta_distribution dist(alpha, beta);
		return dist(gen);
	}

	template<typename T,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
		static T gamma(T alpha = T(1.0), T beta = T(1.0)) {
		std::gamma_distribution dist(alpha, beta);
		return dist(gen);
	}



	// Gets an quantile from the normalised t-dist distribution (scaled so that standard
	// deviation is always 1) for a particular cumulative probability
	// For example, if you want the median, use quantile=0.5, for 10% percentile use quantile=0.1
	template<typename T, typename U,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0,
		std::enable_if_t<std::is_integral<U>::value, int> = 0>
		static T scaledTdistQuantile(U df, T quantile, T mean = T(0.0), T sd = T(1.0)) {
		quantile = std::clamp(quantile, T(0.0), T(1.0));
		int lookupIndex = int(std::floor(quantile * T(LookupSize - 1)));
		lookupIndex = std::min(lookupIndex, LookupSize - 2);
		T mixFactor = quantile - T(lookupIndex) / T(LookupSize - 1);
		df = std::clamp(df, 0, MaxDf);
		prepareTdistLookup(df);
		T lowerVal = tdistLookup[df][lookupIndex];
		T upperVal = tdistLookup[df][lookupIndex + 1];
		T normalised = upperVal * mixFactor + lowerVal * (T(1.0) - mixFactor);
		return normalised * sd + mean;
	}

	// Gets an quantile from the normalised t-dist distribution (scaled so that standard
	// deviation is always 1) for a particular cumulative probability
	// For example, if you want the median, use quantile=0.5, for 10% percentile use quantile=0.1
	template<typename T,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	static T scaledNormalQuantile(T quantile, T mean = T(0.0), T sd = T(1.0)) {
		quantile = std::clamp(quantile, T(0.0), T(1.0));
		int lookupIndex = int(std::floor(quantile * T(LookupSize - 1)));
		lookupIndex = std::min(lookupIndex, LookupSize - 2);
		T mixFactor = quantile - T(lookupIndex) / T(LookupSize - 1);
		prepareNormalLookup();
		T lowerVal = normalLookup[lookupIndex];
		T upperVal = normalLookup[lookupIndex + 1];
		T normalised = upperVal * mixFactor + lowerVal * (T(1.0) - mixFactor);
		return normalised * sd + mean;
	}

	// A flexible distribution that allows for different variance and kurtosis to
	// apply to both sides of the distribution. Uses a normal distribution for
	// kurtosis <= 3, and mixed t-distributions for higher kurtosis.
	template<typename T,
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
	static T flexibleDist(T mean = T(0.0), T lower_sd = T(1.0), T upper_sd = T(1.0),
		T lower_kurt = T(3.0), T upper_kurt = T(3.0), T quantile = -1.0) {
		if (quantile < 0.0) quantile = uniform(0.0, 1.0);
		T upperVal = 0.0;

		// If doing a deterministic calculation, we need to have a deterministic
		// calculation for the high-kurtosis branches
		// This gets a uniform variable from the quantile without it correlating to
		// the quantile's value.
		auto auxUniformFromQuantile = [](T q, std::uint64_t salt) {
			std::uint64_t key = 0;
			static_assert(sizeof(T) <= sizeof(std::uint64_t));
			std::memcpy(&key, &q, sizeof(T)); // use <cstring>
			key ^= salt;
			return uniform01_from_key(key);   // uses splitmix64 inside
		};

		// are we looking in the upper or lower half of the distribution?
		if (upper_kurt <= T(3.0)) {
			// use normal distribution for low kurtosis
			upperVal = scaledNormalQuantile(quantile, mean, upper_sd);
		}
		else {
			// map kurtosis value to approximate t-dist degrees of freedom
			T df = T(6.0) / (upper_kurt - 3.0) + T(4.0);
			T floor_df = std::floor(df);
			T ceil_factor = df - floor_df;
			T u = (quantile >= T(0.0))
				? T(auxUniformFromQuantile(quantile, 0x9e3779b97f4a7c15ULL))
				: uniform(T(0.0), T(1.0));
			if (u < ceil_factor) {
				upperVal = mean + scaledTdistQuantile(int(floor_df) + 1, quantile, T(0.0), upper_sd);
			}
			else {
				upperVal = mean + scaledTdistQuantile(int(floor_df), quantile, T(0.0), upper_sd);
			}
		}
		T lowerVal = 0.0;
		if (lower_kurt <= T(3.0)) {
			// use normal distribution for low kurtosis
			lowerVal = scaledNormalQuantile(quantile, mean, lower_sd);
		}
		else {
			// map kurtosis value to approximate t-dist degrees of freedom
			T df = T(6.0) / (lower_kurt - 3.0) + T(4.0);
			T floor_df = std::floor(df);
			T ceil_factor = df - floor_df;
			T u = (quantile >= T(0.0))
				? T(auxUniformFromQuantile(quantile, 0xbf58476d1ce4e5b9ULL))
				: uniform(T(0.0), T(1.0));
			if (u < ceil_factor) {
				lowerVal = mean + scaledTdistQuantile(int(floor_df) + 1, quantile, T(0.0), lower_sd);
			}
			else {
				lowerVal = mean + scaledTdistQuantile(int(floor_df), quantile, T(0.0), lower_sd);
			}
		}
		T mixFactor = quantile;
		return upperVal * mixFactor + lowerVal * (T(1.0) - mixFactor);
	}

	static void randomise() { gen.seed(rd()); }

	static void setSeed(int seed) { gen.seed(seed); }

	static inline std::uint64_t splitmix64(std::uint64_t x) {
		x += 0x9e3779b97f4a7c15ULL;
		x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
		x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
		return x ^ (x >> 31);
	}

	static inline std::uint64_t mixKey(std::uint64_t seed, std::uint64_t v) {
		return splitmix64(seed ^ v);
	}

	static inline float uniform01_from_key(std::uint64_t key) {
		// Use 53 bits -> (0,1). Avoid 0/1 exact by shifting + 1.
		std::uint64_t x = splitmix64(key);
		const double denom = 9007199254740992.0; // 2^53
		return static_cast<float>(((x >> 11) + 1) / (denom + 2.0));
	}

	static inline float normal_from_key(std::uint64_t key, float mean, float sd) {
		// Box-Muller
		float u1 = uniform01_from_key(key);
		float u2 = uniform01_from_key(key + 0x9e3779b97f4a7c15ULL);
		float r = std::sqrt(-2.0f * std::log(u1));
		float theta = 6.28318530718f * u2;
		return mean + sd * (r * std::cos(theta));
	}

	static inline std::uint64_t combinePartyIds(int a, int b) {
		return (std::uint64_t(std::uint32_t(a)) << 32) | std::uint32_t(b);
	}

private:

	static void prepareTdistLookup(int df);

	static void prepareNormalLookup();

	static std::random_device rd;
	static std::mt19937 gen;

	static constexpr int MaxDf = 100;
	static constexpr int LookupSize = 500000;

	static std::mutex tdistGeneralMutex;
	static std::array<std::mutex, MaxDf + 1> tdistMutex;
	static std::map<int, std::vector<double>> tdistLookup;
	static std::array<bool, MaxDf + 1> tdistReady;

	static std::mutex normalMutex;
	static std::vector<double> normalLookup;
	static bool normalReady;
};