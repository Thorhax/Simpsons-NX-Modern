// license:BSD-3-Clause
// copyright-holders:Olivier Galibert, R. Belmont
//============================================================
//
//  sdlmain.cpp - main file for SDLMAME.
//
//  SDLMAME by Olivier Galibert and R. Belmont
//
//============================================================

// OSD headers
#include "osdsdl.h"
#include "modules/lib/osdlib.h"
#include "modules/diagnostics/diagnostics_module.h"

// MAME headers
#include "emu.h"
#include "emuopts.h"
#include "main.h"
#include "video.h"

#include "corestr.h"

#include "osdepend.h"
#include "strconv.h"

#include <SDL2/SDL.h>

// only for oslog callback
#include <functional>

#ifdef SDLMAME_UNIX
#if (!defined(SDLMAME_MACOSX)) && (!defined(SDLMAME_EMSCRIPTEN)) && (!defined(SDLMAME_ANDROID)) && (!defined(__SWITCH__))
#ifndef SDLMAME_HAIKU
#include <fontconfig/fontconfig.h>
#endif
#endif
#ifdef SDLMAME_MACOSX
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#include <Carbon/Carbon.h>
#endif
#endif

// standard includes
#if !defined(SDLMAME_WIN32)
#include <unistd.h>
#endif


//============================================================
//  Global variables
//============================================================

#if defined(SDLMAME_UNIX) || defined(SDLMAME_WIN32)
int sdl_entered_debugger;
#endif


//============================================================
//  main
//============================================================

// we do some special sauce on Win32...

#if defined(SDLMAME_WIN32)
/* gee */
extern "C" DECLSPEC void SDLCALL SDL_SetModuleHandle(void *hInst);
#endif

#if defined(__SWITCH__)
#include <switch.h>
#include <sys/stat.h>
#include <malloc.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

extern "C" int fileno(FILE *);
extern "C" u32 __nx_applet_exit_mode;

#include "nxperf.h"
#include "nxpicker.h"
#include <atomic>
#include <unordered_map>
extern "C" int setenv(const char *name, const char *value, int overwrite);

void nxperf::mem_stats(uint64_t &used, uint64_t &heap, uint64_t &borrowed, uint64_t &ipc, uint64_t &device) noexcept
{
	used = heap = borrowed = ipc = device = 0;
	heap = mallinfo().uordblks;
	u64 value = 0;
	if (R_SUCCEEDED(svcGetInfo(&value, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0)))
		used = value;

	// sum memory lent out to other processes/devices
	for (u64 addr = 0; ; )
	{
		MemoryInfo info;
		u32 pageinfo;
		if (R_FAILED(svcQueryMemory(&info, &pageinfo, addr)))
			break;
		if (info.attr & MemAttr_IsBorrowed)
			borrowed += info.size;
		if (info.attr & MemAttr_IsIpcMapped)
			ipc += info.size;
		if (info.attr & MemAttr_IsDeviceMapped)
			device += info.size;
		u64 const next = info.addr + info.size;
		if (next <= addr)
			break;
		addr = next;
	}
}

//============================================================
//  sampling profiler (opt-in: sdmc:/switch/simpsons-arcade/profile.txt)
//
//  A thread on core 2 pauses the emulation thread ~1000 times a
//  second, records its PC and LR, and resumes it.  Histograms are
//  written to profile_pc.txt every 15 seconds; the addresses are
//  resolved offline against the ELF (base = nxperf::mem_stats).
//============================================================

namespace {

struct nx_profiler
{
	Handle target = INVALID_HANDLE;
	Thread thread;
	std::atomic<bool> stop{ false };
	std::unordered_map<u64, u32> pcs, lrs;
	u64 samples = 0, failures = 0;

	void dump()
	{
		FILE *f = fopen("sdmc:/switch/simpsons-arcade/profile_pc.tmp", "w");
		if (!f)
			return;
		fprintf(f, "base %llx samples %llu failures %llu\n", (unsigned long long)(uintptr_t)&nxperf::mem_stats, (unsigned long long)samples, (unsigned long long)failures);
		for (auto const &e : pcs)
			fprintf(f, "pc %llx %u\n", (unsigned long long)e.first, e.second);
		for (auto const &e : lrs)
			fprintf(f, "lr %llx %u\n", (unsigned long long)e.first, e.second);
		fclose(f);
		remove("sdmc:/switch/simpsons-arcade/profile_pc.txt");
		rename("sdmc:/switch/simpsons-arcade/profile_pc.tmp", "sdmc:/switch/simpsons-arcade/profile_pc.txt");
	}

	static void run(void *arg)
	{
		auto &p = *reinterpret_cast<nx_profiler *>(arg);
		u64 last_dump = armGetSystemTick();
		while (!p.stop.load())
		{
			svcSleepThread(1'000'000);
			ThreadContext ctx;
			bool ok = false;
			if (R_SUCCEEDED(svcSetThreadActivity(p.target, ThreadActivity_Paused)))
			{
				ok = R_SUCCEEDED(svcGetThreadContext3(&ctx, p.target));
				svcSetThreadActivity(p.target, ThreadActivity_Runnable);
			}
			// only touch the maps (malloc) once the target runs again
			if (ok)
			{
				p.pcs[ctx.pc.x]++;
				p.lrs[ctx.lr]++;
				p.samples++;
			}
			else
				p.failures++;
			if (armTicksToNs(armGetSystemTick() - last_dump) > 15'000'000'000ULL)
			{
				p.dump();
				last_dump = armGetSystemTick();
			}
		}
		p.dump();
	}

	void start()
	{
		// a real handle for this thread (CUR_THREAD_HANDLE only means "me")
		target = threadGetCurHandle();
		if (R_SUCCEEDED(threadCreate(&thread, &nx_profiler::run, this, nullptr, 0x10000, 0x20, 2)))
		{
			threadStart(&thread);
			printf("profiler: sampling the emulation thread\n");
		}
		else
			printf("profiler: could not create thread\n");
	}
};

nx_profiler s_nx_profiler;

} // anonymous namespace

static void nx_background_thread() noexcept
{
	// cores 1/2 only (core 0 runs the emulation), below the emulation (0x2C)
	// and audio (0x2B) threads so it only uses otherwise idle time
	svcSetThreadCoreMask(CUR_THREAD_HANDLE, 2, (1 << 1) | (1 << 2));
	svcSetThreadPriority(CUR_THREAD_HANDLE, 0x30);
}

extern "C" void userAppInit(void)
{
	romfsInit();
	mkdir("sdmc:/switch", 0777);
	mkdir("sdmc:/switch/simpsons-arcade", 0777);
	mkdir("sdmc:/switch/simpsons-arcade/roms", 0777);
	mkdir("sdmc:/switch/simpsons-arcade/roms/simpsons", 0777);
	mkdir("sdmc:/switch/simpsons-arcade/ini", 0777);
	mkdir("sdmc:/switch/simpsons-arcade/cfg", 0777);
	chdir("sdmc:/switch/simpsons-arcade");

	setenv("HOME", "sdmc:/switch/simpsons-arcade", 1);
	osd_setenv("HOME", "sdmc:/switch/simpsons-arcade", 1);

	// keep the last few sessions' logs: simpsons.log -> simpsons.1.log -> ... -> simpsons.4.log
	remove("sdmc:/switch/simpsons-arcade/simpsons.4.log");
	rename("sdmc:/switch/simpsons-arcade/simpsons.3.log", "sdmc:/switch/simpsons-arcade/simpsons.4.log");
	rename("sdmc:/switch/simpsons-arcade/simpsons.2.log", "sdmc:/switch/simpsons-arcade/simpsons.3.log");
	rename("sdmc:/switch/simpsons-arcade/simpsons.1.log", "sdmc:/switch/simpsons-arcade/simpsons.2.log");
	rename("sdmc:/switch/simpsons-arcade/simpsons.log", "sdmc:/switch/simpsons-arcade/simpsons.1.log");

	// Redirect stdout to persistent log file on SD card
	FILE *fout = freopen("sdmc:/switch/simpsons-arcade/simpsons.log", "w", stdout);
	if (fout)
	{
		setvbuf(stdout, nullptr, _IOLBF, 4096);
		// Mirror stderr to stdout so all MAME errors and exceptions appear in simpsons.log
		dup2(fileno(stdout), STDERR_FILENO);
		setvbuf(stderr, nullptr, _IOLBF, 4096);
	}

	// Set preferred core 0 with full 3-core affinity mask so worker/audio threads can spawn onto Cores 1 & 2
	svcSetThreadCoreMask(CUR_THREAD_HANDLE, 0, (1 << 0) | (1 << 1) | (1 << 2));

	// Also attempt nxlink if launched via netloader
	if (R_SUCCEEDED(socketInitializeDefault()))
	{
		nxlinkStdio();
	}

	nxperf::background_thread_hook = &nx_background_thread;

	// per-second performance logging is opt-in
	{
		struct stat st;
		nxperf::enabled = (stat("sdmc:/switch/simpsons-arcade/perf.txt", &st) == 0);
	}

	// local time zone offset so perf lines match capture filenames
	u64 now = 0;
	TimeCalendarTime caltime;
	TimeCalendarAdditionalInfo calinfo;
	if (R_SUCCEEDED(timeGetCurrentTime(TimeType_UserSystemClock, &now)) &&
			R_SUCCEEDED(timeToCalendarTimeWithMyRule(now, &caltime, &calinfo)))
	{
		nxperf::utc_offset = calinfo.offset;
		printf("=== MAME-NX The Simpsons Log Started %04u-%02u-%02u %02u:%02u:%02u ===\n",
				caltime.year, caltime.month, caltime.day, caltime.hour, caltime.minute, caltime.second);
		if (nxperf::enabled)
			printf("perf logging enabled (perf.txt present)\n");
	}
	else
	{
		printf("=== MAME-NX The Simpsons Log Started ===\n");
	}
	fflush(stdout);
}

extern "C" void userAppExit(void)
{
	printf("=== MAME-NX The Simpsons Exiting ===\n");
	fflush(stdout);
	fflush(stderr);
	socketExit();
	romfsExit();
}
#endif

int main(int argc, char** argv)
{
	std::vector<std::string> args = osd_get_command_line(argc, argv);
	int res = 0;

#if !defined(__SWITCH__)
	// disable I/O buffering
	setvbuf(stdout, (char *) nullptr, _IONBF, 0);
	setvbuf(stderr, (char *) nullptr, _IONBF, 0);
#endif

	// Initialize crash diagnostics
	diagnostics_module::get_instance()->init_crash_diagnostics();

#if defined(SDLMAME_ANDROID)
	/* Enable standard application logging */
	SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);
#endif

	// FIXME: this should be done differently

#ifdef SDLMAME_UNIX
	sdl_entered_debugger = 0;
#if (!defined(SDLMAME_MACOSX)) && (!defined(SDLMAME_HAIKU)) && (!defined(SDLMAME_EMSCRIPTEN)) && (!defined(SDLMAME_ANDROID)) && (!defined(__SWITCH__))
	FcInit();
#endif
#endif

#if defined(__SWITCH__)
	// The emulation runs on this thread: keep it on core 0.  With cores 0-2
	// allowed it migrated onto core 1 (shared with the higher priority audio
	// thread), losing its caches.  Worker and audio threads pin themselves to
	// cores 1/2.
	svcSetThreadCoreMask(CUR_THREAD_HANDLE, 0, 1 << 0);
	{
		struct stat st;
		if (stat("sdmc:/switch/simpsons-arcade/profile.txt", &st) == 0)
			s_nx_profiler.start();
	}
	SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG,
		"000038f853776974636820436f6e7400,Switch Controller,a:b1,b:b0,back:b11,dpdown:b15,dpleft:b12,dpright:b14,dpup:b13,leftshoulder:b6,leftstick:b4,lefttrigger:b8,leftx:a0,lefty:a1,rightshoulder:b7,rightstick:b5,righttrigger:b9,rightx:a2,righty:a3,start:b10,x:b3,y:b2,");

	if (args.size() <= 1)
	{
		args.push_back("simpsons");
	}

	// The Simpsons 4-player sets give each cabinet position its own character,
	// so a pad's first coin opens a character picker (input_sdl.cpp); the
	// 2-player sets have an in-game character select instead
	{
		std::string_view const system = (args.size() > 1) && !args[1].starts_with('-') ? std::string_view(args[1]) : std::string_view();
		if (system.starts_with("simpsons") && (system.find("2p") == std::string_view::npos))
		{
			static char const *const s_simpsons[] = { "MARGE", "HOMER", "BART", "LISA" };
			std::copy(std::begin(s_simpsons), std::end(s_simpsons), std::begin(nxpicker::slot_names));
			nxpicker::slot_count = 4;
		}
	}
	// Switch performance defaults
	bool has_video = false;
	bool has_sleep = false;
	bool has_skip_gameinfo = false;
	bool has_numprocessors = false;
	bool has_audio_latency = false;
	bool has_joystickprovider = false;
	bool has_keyboardprovider = false;
	bool has_samplerate = false;
	for (const auto &arg : args)
	{
		if (arg == "-video") has_video = true;
		if (arg == "-sleep" || arg == "-nosleep") has_sleep = true;
		if (arg == "-skip_gameinfo") has_skip_gameinfo = true;
		if (arg == "-numprocessors" || arg == "-np") has_numprocessors = true;
		if (arg == "-audio_latency") has_audio_latency = true;
		if (arg == "-joystickprovider") has_joystickprovider = true;
		if (arg == "-keyboardprovider") has_keyboardprovider = true;
		if (arg == "-samplerate" || arg == "-sr") has_samplerate = true;
	}
	if (!has_video)
	{
		args.push_back("-video");
		args.push_back("accel");
	}
	// No auto-frameskip: as long as the game has headroom,
	// every emulated frame is drawn and a rare hiccup shows as a brief
	// slowdown instead of a skipped frame (-autoframeskip brings it back).
	// Sleep is on: osd_sleep() on Switch sleeps through most of each frame's
	// idle time and spins only for the last 2ms, instead of keeping core 0 at
	// ~100%.  It must be requested here because emuopts.cpp defaults "sleep"
	// to 0 on Switch (from when osd_sleep was a no-op there).
	if (!has_sleep)
	{
		args.push_back("-sleep");
	}
	if (!has_numprocessors)
	{
		args.push_back("-numprocessors");
		args.push_back("3");
	}
	if (!has_audio_latency)
	{
		args.push_back("-audio_latency");
		args.push_back("2");
	}
	if (!has_joystickprovider)
	{
		args.push_back("-joystickprovider");
		args.push_back("switch");
	}
	if (!has_keyboardprovider)
	{
		// the Switch input module's "Switch UI" device has to be the first
		// keyboard to carry the UI controls (input_sdl.cpp)
		args.push_back("-keyboardprovider");
		args.push_back("none");
	}
	if (!has_skip_gameinfo)
	{
		args.push_back("-skip_gameinfo");
	}
	if (!has_samplerate)
	{
		args.push_back("-samplerate");
		args.push_back("48000");
	}
	args.push_back("-verbose");
#endif

	try
	{
		sdl_options options;
		sdl_osd_interface osd(options);
		osd.register_options();
		res = emulator_info::start_frontend(options, osd, args);
		printf("=== MAME frontend returned: %d ===\n", res);
	}
	catch (const std::exception &e)
	{
		printf("=== MAME FATAL EXCEPTION: %s ===\n", e.what());
		res = -1;
	}
	catch (...)
	{
		printf("=== MAME UNKNOWN EXCEPTION CAUGHT ===\n");
		res = -1;
	}
	fflush(stdout);
	fflush(stderr);

#if defined(__SWITCH__)
	// MAME only quits the SDL subsystems it started; make sure SDL's Switch
	// backends release all GPU/audio memory before we hand back to hbl,
	// otherwise hbl can fail (0xD401) reading the next NRO into that memory
	SDL_Quit();

	// Something in the graphics/audio stack leaves ~49MB of heap borrowed
	// even after SDL_Quit, and hbl aborts (0xD401) when it loads the next
	// NRO over it.  Pointing the loader return address at svcExitProcess
	// (what libnx uses when there is no loader) makes libnx do its normal
	// cleanup, including closing the applet session so the system sees an
	// intentional exit, and then end the process instead of jumping to hbl.
	// Note libnx branches to this pointer unconditionally, so it must not be null.
	envSetExitFuncPtr(reinterpret_cast<LoaderReturnFn>(&svcExitProcess));
	// An NRO normally leaves AM notification to hbl; since we end the process
	// ourselves, have libnx send ISelfController::Exit like a real title so
	// the system doesn't report "The software was closed because an error occurred".
	__nx_applet_exit_mode = 1;
	printf("=== MAME-NX The Simpsons Exiting (libnx process exit) ===\n");
	fflush(stdout);
#endif

#ifdef SDLMAME_UNIX
#if (!defined(SDLMAME_MACOSX)) && (!defined(SDLMAME_HAIKU)) && (!defined(SDLMAME_EMSCRIPTEN)) && (!defined(SDLMAME_ANDROID)) && (!defined(__SWITCH__))
	if (!sdl_entered_debugger)
	{
		FcFini();
	}
#endif
#endif

	return res;
}
