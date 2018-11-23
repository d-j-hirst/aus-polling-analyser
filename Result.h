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
};