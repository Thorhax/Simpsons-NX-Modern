// The output hold the Thorhax intro (thorhax_intro/) takes while it plays.
// MAME starts and runs the game behind it: the SDL window is only made on the
// first screen update after the intro ends (window.cpp), the game's sound is
// replaced by silence (sdl_sound.cpp) and the pads read as untouched, with a
// button still held at the end muted until released (input_sdl.cpp)
#pragma once

#include <atomic>

namespace nxintro {

inline std::atomic<bool> held{false};

// Returns once the intro has ended
void wait();

} // namespace nxintro
