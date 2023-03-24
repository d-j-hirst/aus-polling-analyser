#pragma once

#include <array>

// Note percentCounted should be an actual percentage (e.g. 52.7)
inline float stdDevSingleSeat(float percentCounted) {
	// actual observed formula was 4.2106 * x^-0.522
	// but we multiply by the extra 2.0f here to account for pre-polling changes
	// and differences between state and federal elections
	return 4.2106f * 2.0f * pow(percentCounted, -0.522f);
}

// Note percentCounted should be an actual percentage (e.g. 52.7)
inline float stdDevOverall(float percentCounted) {
	// actual observed formula was 0.5403 * x^-0.391
	// but we multiply by the extra 5.0f here to account for pre-polling changes
	// and differences between state and federal elections
	return 0.5403f * 4.0f * pow(percentCounted, -0.391f);
}