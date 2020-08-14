#include "SaveIO.h"

constexpr int CharLimit = 100000;

SaveFileOutput::SaveFileOutput(std::string filename) {
	saveStream_.open(filename, std::fstream::binary | std::fstream::trunc);
}

SaveFileOutput& operator<<(SaveFileOutput& stream, std::string itemToAdd) {
	int numchars = itemToAdd.length();
	stream << numchars;

	// avoid saving really long strings -if the file gets corrupted it could
	// try to read a really long string, to prevent this have a limit on string length
	if (numchars > CharLimit) return stream;

	for (int pos = 0; pos < numchars; ++pos) {
		stream << itemToAdd[pos];
	}
	return stream;
}

SaveFileInput::SaveFileInput(std::string filename) {
	loadStream_.open(filename, std::fstream::binary);
}

SaveFileInput& operator>>(SaveFileInput& stream, std::string& itemToRead) {
	itemToRead.clear();
	int numchars = 0;
	stream >> numchars;

	// avoid saving really long strings -if the file gets corrupted it could
	// try to read a really long string, to prevent this have a limit on string length
	if (numchars > CharLimit) return stream;

	for (int pos = 0; pos < numchars; ++pos) {
		char ch = '0';
		stream >> ch;
		itemToRead += ch;
	}
	return stream;
}
