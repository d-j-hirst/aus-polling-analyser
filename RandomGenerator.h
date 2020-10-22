#pragma once

#include <random>

class RandomGenerator {
public:
	RandomGenerator() {
		gen.seed(rd());
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

private:
	static std::random_device rd;
	static std::mt19937 gen;
};