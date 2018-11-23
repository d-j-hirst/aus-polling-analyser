#pragma once

#include "Seat.h"

class Seat;

class Result {
public:
	Seat* seat;
	double incumbentSwing = 0.0;
	double percentCounted = 0.0;
	std::size_t boothsCounted = 0;
	std::size_t totalBooths = 0;
};