#include "RandomGenerator.h"

std::random_device RandomGenerator::rd = std::random_device();
std::mt19937 RandomGenerator::gen = std::mt19937();