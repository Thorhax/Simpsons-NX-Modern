// license:BSD-3-Clause
// copyright-holders:Thorhax
//============================================================
//
//  nxpicker.h - Switch character picker shared state
//
//  On games with one fixed character per cabinet position
//  (The Simpsons 4-player sets), the first coin from a pad
//  opens a popup where that pad picks its character; the
//  input module then routes the pad to that cabinet position.
//  The input module owns the logic; the OSD renderer only
//  reads this state to draw the popup.
//
//============================================================

#ifndef MAME_OSD_NXPICKER_H
#define MAME_OSD_NXPICKER_H

#pragma once

#include <atomic>
#include <cstdint>

namespace nxpicker {

constexpr int MAX_SLOTS = 4;

// set once from the input module when the running system supports picking
inline int slot_count = 0;
inline char const *slot_names[MAX_SLOTS] = { nullptr, nullptr, nullptr, nullptr };

// popup state, written by the input module on the emulation thread
inline std::atomic<bool> open{ false };
inline std::atomic<int> controller{ 0 };          // 1-based controller number shown in the title
inline std::atomic<int> selection{ 0 };           // highlighted slot
inline std::atomic<std::uint32_t> taken_mask{ 0 }; // slots claimed by other connected pads

} // namespace nxpicker

#endif // MAME_OSD_NXPICKER_H
