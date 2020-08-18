#pragma once

#include "Seat.h"
#include "SeatCollection.h"

#include <wx/datetime.h>

#include <array>

class Seat;

class Outcome {
public:
	Outcome() {}
	Outcome(Seat::Id seat, double incumbentSwing, double percentCounted, std::size_t boothsIn, std::size_t totalBooths)
		: seat(seat), incumbentSwing(incumbentSwing), percentCounted(percentCounted), boothsIn(boothsIn), totalBooths(totalBooths),
		updateTime(wxDateTime::Now()) {}
	Seat::Id seat;
	double incumbentSwing = 0.0;
	double percentCounted = 0.0;
	int boothsIn = 0;
	int totalBooths = 0;
	wxDateTime updateTime;

	std::string getUpdateTimeString() const {
		if (!updateTime.IsValid()) return "";
		else return updateTime.FormatISODate().ToStdString();
	}

	std::string textReport(SeatCollection const& seats) const {
		std::stringstream report;
		report << "Reporting Outcome: \n";
		report << " Seat: " << seats.view(seat).name << "\n";
		report << " Incumbent Swing: " << incumbentSwing << "\n";
		report << " Percent Counted: " << percentCounted << "\n";
		report << " Booths In: " << boothsIn << "\n";
		report << " Total Booths: " << totalBooths << "\n";
		report << " Update Time: " << getUpdateTimeString() << "\n";
		return report.str();
	}

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