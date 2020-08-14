#pragma once

#include <string>
#include <memory>
#include <fstream>

// * SaveFileOutput * //
// Stream enabling writing the data to a save file
// Handles the writing of arbitrary binary data to a file in a
// friendly manner (just use stream operators for plain-old-data
// types and strings).
class SaveFileOutput {
public:
	SaveFileOutput(std::string filename);

	template<typename T>
	friend SaveFileOutput& operator<<(SaveFileOutput& stream, T itemToAdd);

	friend SaveFileOutput& operator<<(SaveFileOutput& stream, std::string itemToAdd);

private:
	std::ofstream saveStream_;
};

template<typename T>
SaveFileOutput& operator<<(SaveFileOutput& stream, T itemToAdd) {
	stream.saveStream_.write(reinterpret_cast<char*>(&itemToAdd), sizeof(itemToAdd));
	return stream;
}

SaveFileOutput& operator<<(SaveFileOutput& stream, std::string itemToAdd);

// * SaveFileInput * //
// Stream enabling reading the data from a save file
// Handles the reading of arbitrary binary data from a file in a
// friendly manner (just use stream operators for plain-old-data
// types and strings).
class SaveFileInput {
public:
	SaveFileInput(std::string filename);

	bool valid() { return loadStream_.is_open(); }

	template<typename T>
	friend SaveFileInput& operator>>(SaveFileInput& stream, T& itemToRead);

	friend SaveFileInput& operator>>(SaveFileInput& stream, std::string& itemToRead);

private:
	std::ifstream loadStream_;
};

template<typename T>
SaveFileInput& operator>>(SaveFileInput& stream, T& itemToRead) {
	stream.loadStream_.read(reinterpret_cast<char*>(&itemToRead), sizeof(itemToRead));
	return stream;
}

SaveFileInput& operator>>(SaveFileInput& stream, std::string& itemToRead);