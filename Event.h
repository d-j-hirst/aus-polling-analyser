#pragma once
#include <string>
#include <wx/datetime.h>
#include "General.h"


struct Event {
	enum class Type {
		None,
		Election,
		Discontinuity,
		EndOfPeriod,
		Max,
	};

	typedef int Id;
	constexpr static Id InvalidId = -1;

	std::string name = "Enter event name here";
	Type eventType = Type::None;
	wxDateTime date = wxDateTime::Now();
	float vote = 50.0f;

	std::string getDateString() const {
		if (!date.IsValid()) return "";
		else return date.FormatISODate().ToStdString();
	}

	static std::string eventTypeString(Type eventType) {
		switch (eventType) {
		case Type::None: return "None";
		case Type::Election: return "Election";
		case Type::Discontinuity: return "Discontinuity";
		case Type::EndOfPeriod: return "End Of Period";
		default: return "";
		}
	}

	static std::vector<Type> allEventTypes() {
		std::vector<Type> types;
		for (int typeIndex = 0; typeIndex < int(Type::Max); ++typeIndex) {
			types.push_back(Type(typeIndex));
		}
		return types;
	}

	std::string getEventTypeString() const {
		return eventTypeString(eventType);
	}

	std::string getVoteString() const {
		if (eventType == Type::Election) return formatFloat(vote, 2);
		return "";
	}

	std::string textReport() const {
		std::stringstream report;
		report << "Reporting Event: \n";
		report << " Name: " << name << "\n";
		report << " Event Type: " << getEventTypeString() << "\n";
		report << " Date: " << getDateString() << "\n";
		report << " Vote: " << getVoteString() << "\n";
		return report.str();
	}
};