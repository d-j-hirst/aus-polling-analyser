#pragma once

#include "Date.h"
#include "Seat.h"
#include "SeatCollection.h"

#include <array>

class Seat;

class Outcome {
public:
	Outcome() {}
	Outcome(Seat::Id seat, double partyOneSwing, double percentCounted, double tcpSwingBasis, std::size_t boothsIn, std::size_t totalBooths)
		: seat(seat), partyOneSwing(partyOneSwing), percentCounted(percentCounted), tcpSwingBasis(tcpSwingBasis),
		boothsIn(boothsIn), totalBooths(totalBooths), updateTime(Timestamp::now()) {}
	Seat::Id seat = -1;
	double partyOneSwing = 0.0;
	double percentCounted = 0.0;
	double tcpSwingBasis = 0.0;
	int boothsIn = 0;
	int totalBooths = 0;
	Timestamp updateTime;

	std::string getUpdateTimeString() const {
		if (!updateTime.isValid()) return "";
		return updateTime.formatIsoDateLocal();
	}

	std::string textReport(SeatCollection const& seats) const {
		std::stringstream report;
		report << "Reporting Outcome: \n";
		report << " Seat: " << seats.view(seat).name << "\n";
		report << " Party One Swing: " << partyOneSwing << "\n";
		report << " Percent Counted: " << percentCounted << "\n";
		report << " Booths In: " << boothsIn << "\n";
		report << " Total Booths: " << totalBooths << "\n";
		report << " Update Time: " << getUpdateTimeString() << "\n";
		return report.str();
	}

	float getPercentCountedEstimate() const { 
		if (percentCounted) {
			return static_cast<float>(percentCounted);
		}
		else if (boothsIn) {
			float const boothProportion =
				float(boothsIn) / float(totalBooths);
			return boothProportion *
				(0.5f + 0.5f * boothProportion) * 100.0f;
		}
		return 0.0f;
	}
};
