#pragma once

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
		std::enable_if_t<std::is_floating_point<T>::value, int> = 0>
		T t_dist(U df, T mean = T(0.0), T sd = T(1.0)) {
		return std::student_t_distribution<T>(df)(gen) * sd + mean;
	}

	void randomise() { gen.seed(rd()); }

	void setSeed(int seed) { gen.seed(seed); }

private:
	static std::random_device rd;
	static std::mt19937 gen;
};