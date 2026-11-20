// The ReXGlue SDK's log macros, which the Thorhax intro is written with, on MAME's stdout log (simpsons.log)
#pragma once

#include <cstdio>
#include <format>

#define REXLOG_INFO(...) std::printf("%s\n", std::format(__VA_ARGS__).c_str())
#define REXLOG_WARN(...) std::printf("warning: %s\n", std::format(__VA_ARGS__).c_str())
