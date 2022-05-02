#pragma once

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
inline void beep() {
	Beep(440, 200);
}
#elif LINUX
#include <stdio.h>
void beep() {
	system("beep -f 5000 -l 50 -r 2");
}
#else
#include <iostream>
void beep() {
	std::cout << "\a" << std::flush;
}
#endif