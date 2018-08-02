#pragma once

#include <string>
#include <fstream>
#include <time.h>
#include <Windows.h>

void ResetTimer();
void ResetDebug();
void PrintDebug(std::string StringToPrint);
void PrintDebugLine(std::string StringToPrint);
void PrintDebugNewLine();
void PrintDebugInt(int IntToPrint);
void PrintDebugClock();
void PrintDebugFloat(float FloatToPrint);
void PrintDebugDouble(double DoubleToPrint);
void PrintDebugError(std::string StringToPrint, int IntToPrint);