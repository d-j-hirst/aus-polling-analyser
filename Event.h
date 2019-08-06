#pragma once
#include <string>
#include <wx/datetime.h>
#include "General.h"

enum EventType {
	EventType_None,
	EventType_Election,
	EventType_Discontinuity
};

struct Event {
	typedef int Id;
	constexpr static Id InvalidId = -1;

	std::string name = "Enter event name here";
	EventType eventType = EventType_None;
	wxDateTime date = wxDateTime::Now();
	float vote = 50.0f;

	std::string getDateString() const {
		if (!date.IsValid()) return "";
		else return date.FormatISODate().ToStdString();
	}

	std::string getEventTypeString() const {
		switch (eventType) {
		case EventType_None: return "None";
		case EventType_Election: return "Election";
		case EventType_Discontinuity: return "Discontinuity";
		default: return "";
		}
	}

	std::string getVoteString() const {
		if (eventType == EventType_Election) return formatFloat(vote, 2);
		return "";
	}
};