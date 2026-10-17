#include "../RegionalSwingMath.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
	// These fixtures also run against Python's mean rule. In particular, both
	// intercepts must participate: the old half-state-intercept rule gave 2.9.
	assert(std::abs(RegionalSwingMath::pollingDeviation(0.8, 1.0, 0.4, -0.2, 3.0) - 2.2) < 1e-12);
	assert(std::abs(RegionalSwingMath::pollingDeviation(0.8, 1.0, 0.4, -0.2, 0.0) - 0.4) < 1e-12);
	assert(std::abs(RegionalSwingMath::pollingDeviation(0.8, 1.0, 0.4, -0.2, -3.0) + 1.4) < 1e-12);
	assert(RegionalSwingMath::pollingWeight(1.2, 0.1, 0.0) == 1.0);
	assert(RegionalSwingMath::pollingWeight(-0.1, 0.1, 0.0) == 0.0);
	assert(RegionalSwingMath::pollingWeight(0.8, 0.1, -1.0) == 0.8);
	assert(RegionalSwingMath::pollingWeight(0.8, 0.1, 4.0) < 0.8);
	assert(RegionalSwingMath::quartersUntilElection(-30.0) == 0.0);
	assert(RegionalSwingMath::quartersUntilElection(0.0) == 0.0);
	assert(RegionalSwingMath::quartersUntilElection(91.315) == 1.0);
	std::cout << "Regional swing mean parity and bounded mixture checks passed.\n";
}
