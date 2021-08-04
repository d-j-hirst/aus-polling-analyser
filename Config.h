#pragma once

#include <string>

class Config {
public:
	Config(std::string const& filename);
	int getNumThreads() const { return numThreads; }
private:

	int numThreads = 1;
};