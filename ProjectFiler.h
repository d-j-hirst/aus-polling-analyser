#pragma once

#include "FileOpeningState.h"

#include <string>

class PollingProject;

class ProjectFiler {
public:
	ProjectFiler(PollingProject & project);

	// Save this project to the given filename.
	// Returns 0 if successful, and 1 if saving failed.
	int save(std::string filename);

	// Opens the project saved at the given filename.
	// Returns 0 if successful, and 1 if opening failed.
	void open(std::string filename);
private:

	// Save this project with the detailed format, which stores
	// much more information in a compressed binary format.
	// Returns 0 if successful, and 1 if saving failed.
	int saveDetailed(std::string filename);

	// Open this project with the detailed format, which stores
	// much more information in a compressed binary format.
	// Returns 0 if successful, and 1 if opening failed.
	int openDetailed(std::string filename);

	// Opens the project saved at the given filename.
	// Returns false if the end of the file is reached (marked by "#End").
	bool processFileLine(std::string line, FileOpeningState& fos);

	PollingProject& project;
};