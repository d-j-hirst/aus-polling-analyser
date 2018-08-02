#include "Debug.h"

int debugTimerBase = 0;

void ResetTimer() {
	debugTimerBase = timeGetTime();
}

void ResetDebug() {
	using namespace std;
	ResetTimer();
	ofstream FtaDebugFStream("PALog.log", ios::trunc);
	FtaDebugFStream << "--- Logging initialized ---" << endl;
	FtaDebugFStream.close();
}

void PrintDebugLine(std::string StringToPrint) {
	using namespace std;
	ofstream FtaDebugFStream("PALog.log", ios::app);
	FtaDebugFStream << StringToPrint << endl;
	FtaDebugFStream.close();
}

void PrintDebugNewLine() {
	using namespace std;
	ofstream FtaDebugFStream("PALog.log", ios::app);
	FtaDebugFStream << endl;
	FtaDebugFStream.close();
}

void PrintDebug(std::string StringToPrint) {
	using namespace std;
	ofstream FtaDebugFStream("PALog.log", ios::app);
	FtaDebugFStream << StringToPrint;
	FtaDebugFStream.close();
}

void PrintDebugInt(int IntToPrint) {
	using namespace std;
	ofstream FtaDebugFStream("PALog.log", ios::app);
	FtaDebugFStream << IntToPrint << " ";
	FtaDebugFStream.close();
}

void PrintDebugClock() {
	using namespace std;
	ofstream FtaDebugFStream("PALog.log", ios::app);
	FtaDebugFStream << timeGetTime() - debugTimerBase << " ";
	FtaDebugFStream.close();
}

void PrintDebugFloat(float FloatToPrint) {
	using namespace std;
	ofstream FtaDebugFStream("PALog.log", ios::app);
	FtaDebugFStream << FloatToPrint << " ";
	FtaDebugFStream.close();
}

void PrintDebugDouble(double DoubleToPrint) {
	using namespace std;
	ofstream FtaDebugFStream("PALog.log", ios::app);
	FtaDebugFStream << DoubleToPrint << " ";
	FtaDebugFStream.close();
}

void PrintDebugError(std::string StringToPrint, int IntToPrint) {
	using namespace std;
	ofstream FtaDebugFStream("PALog.log", ios::app);
	FtaDebugFStream << StringToPrint << " " << IntToPrint << endl;
	FtaDebugFStream.close();
}