#pragma once

#include "Seat.h"

#include <wx/time.h>

class Seat;

class Result {
public:
	Result(Seat* seat, double incumbentSwing, double percentCounted, std::size_t boothsIn, std::size_t totalBooths)
		: seat(seat), incumbentSwing(incumbentSwing), percentCounted(percentCounted), boothsIn(boothsIn), totalBooths(totalBooths),
		updateTime(wxDateTime::Now()) {}
	Seat* seat;
	double incumbentSwing = 0.0;
	double percentCounted = 0.0;
	int boothsIn = 0;
	int totalBooths = 0;
	wxDateTime updateTime;

	float getPercentCountedEstimate() const { 
		if (percentCounted) {
			return percentCounted;
		}
		else if (boothsIn) {
			return float(boothsIn) / float(totalBooths) * (0.5 + 0.5 * (float(boothsIn) / float(totalBooths))) * 100.0f;
		}
		return 0.0f;
	}
};