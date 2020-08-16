#pragma once

#include "FileOpeningState.h"

#include <string>

class PollingProject;
class SaveFileInput;
class SaveFileOutput;

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

	// Returns true if the filename should be opened or saved in a detailed (binary) format,
	// and false if it should be saved in classic (text) format.
	static bool isDetailedFormat(std::string filename);

	// Save this project with the detailed format, which stores
	// much more information in a compressed binary format.
	// Returns 0 if successful, and 1 if saving failed.
	int saveDetailed(std::string filename);

	// Open this project with the detailed format, which stores
	// much more information in a compressed binary format.
	// Returns 0 if successful, and 1 if opening failed.
	int openDetailed(std::string filename);

	void saveParties(SaveFileOutput& saveOutput);

	void loadParties(SaveFileInput& saveInput, int versionNum);

	void savePollsters(SaveFileOutput& saveOutput);

	void loadPollsters(SaveFileInput& saveInput, int versionNum);

	// Opens the project saved at the given filename.
	// Returns false if the end of the file is reached (marked by "#End").
	bool processFileLine(std::string line, FileOpeningState& fos);

	PollingProject& project;
};