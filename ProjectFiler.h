#pragma once

#include "FileOpeningState.h"

#include <string>

class PollingProject;
class SaveFileInput;
class SaveFileOutput;

class ProjectFiler {
public:
	ProjectFiler(PollingProject& project);

	// Save this project to the given filename.
	void save(std::string filename);

	// Opens the project saved at the given filename.
	void open(std::string filename);
private:

	void saveParties(SaveFileOutput& saveOutput);

	void loadParties(SaveFileInput& saveInput, int versionNum);

	void savePollsters(SaveFileOutput& saveOutput);

	void loadPollsters(SaveFileInput& saveInput, int versionNum);

	void savePolls(SaveFileOutput& saveOutput);

	void loadPolls(SaveFileInput& saveInput, int versionNum);

	void loadEvents(SaveFileInput& saveInput, int versionNum); // for legacy files

	void saveModels(SaveFileOutput& saveOutput);

	void loadModels(SaveFileInput& saveInput, int versionNum);

	void saveProjections(SaveFileOutput& saveOutput);

	void loadProjections(SaveFileInput& saveInput, int versionNum);

	void saveRegions(SaveFileOutput& saveOutput);

	void loadRegions(SaveFileInput& saveInput, int versionNum);

	void saveSeats(SaveFileOutput& saveOutput);

	void loadSeats(SaveFileInput& saveInput, int versionNum);

	void saveSimulations(SaveFileOutput& saveOutput);

	void loadSimulations(SaveFileInput& saveInput, int versionNum);

	void saveOutcomes(SaveFileOutput& saveOutput);

	void loadOutcomes(SaveFileInput& saveInput, int versionNum);

	void saveElections(SaveFileOutput& saveOutput);

	void loadElections(SaveFileInput& saveInput, int versionNum);

	// Opens the project saved at the given filename.
	// Returns false if the end of the file is reached (marked by "#End").
	bool processFileLine(std::string line, FileOpeningState& fos);

	PollingProject& project;
};