#pragma once

#include <string>

struct NewProjectData {
	// The preliminary name of the project.
	std::string projectName;
	bool valid = false; // set to true when user hits "OK"
};