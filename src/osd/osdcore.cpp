// license:BSD-3-Clause
// copyright-holders:Aaron Giles

#include "osdcore.h"
#include <thread>
#include <chrono>

#if defined(__SWITCH__)
#include "nxperf.h"
#include <switch.h>
#include <algorithm>
#endif

#if defined(SDLMAME_ANDROID)
#include <android/log.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <shellapi.h>
#include "strconv.h"
#endif

static const int MAXSTACK = 10;
static osd_output *m_stack[MAXSTACK];
static int m_ptr = -1;

/*-------------------------------------------------
    osd_output
-------------------------------------------------*/

void osd_output::push(osd_output *delegate)
{
	if (m_ptr < MAXSTACK - 1)
	{
		delegate->m_chain = (m_ptr >= 0 ? m_stack[m_ptr] : nullptr);
		m_ptr++;
		m_stack[m_ptr] = delegate;
	}
}

void osd_output::pop(osd_output *delegate)
{
	int f = -1;
	for (int i = 0; i <= m_ptr; i++)
		if (m_stack[i] == delegate)
		{
			f = i;
			break;
		}
	if (f >= 0)
	{
		if (f < m_ptr)
			m_stack[f+1]->m_chain = m_stack[f]->m_chain;
		m_ptr--;
		for (int i = f; i <= m_ptr; i++)
			m_stack[i] = m_stack[i+1];
	}
}


/***************************************************************************
    OUTPUT MANAGEMENT
***************************************************************************/

/*-------------------------------------------------
    osd_vprintf_error - output an error to the
    appropriate callback
-------------------------------------------------*/

void osd_vprintf_error(util::format_argument_pack<char> const &args)
{
#if defined(SDLMAME_ANDROID)
	__android_log_write(ANDROID_LOG_ERROR, "MAME", util::string_format(args).c_str());
#else
	if (m_ptr >= 0) m_stack[m_ptr]->output_callback(OSD_OUTPUT_CHANNEL_ERROR, args);
#endif
}


/*-------------------------------------------------
    osd_vprintf_warning - output a warning to the
    appropriate callback
-------------------------------------------------*/

void osd_vprintf_warning(util::format_argument_pack<char> const &args)
{
#if defined(SDLMAME_ANDROID)
	__android_log_write(ANDROID_LOG_WARN, "MAME", util::string_format(args).c_str());
#else
	if (m_ptr >= 0) m_stack[m_ptr]->output_callback(OSD_OUTPUT_CHANNEL_WARNING, args);
#endif
}


/*-------------------------------------------------
    osd_vprintf_info - output info text to the
    appropriate callback
-------------------------------------------------*/

void osd_vprintf_info(util::format_argument_pack<char> const &args)
{
#if defined(SDLMAME_ANDROID)
	__android_log_write(ANDROID_LOG_INFO, "MAME", util::string_format(args).c_str());
#else
	if (m_ptr >= 0) m_stack[m_ptr]->output_callback(OSD_OUTPUT_CHANNEL_INFO, args);
#endif
}


/*-------------------------------------------------
    osd_vprintf_verbose - output verbose text to
    the appropriate callback
-------------------------------------------------*/

void osd_vprintf_verbose(util::format_argument_pack<char> const &args)
{
#if defined(SDLMAME_ANDROID)
	__android_log_write( ANDROID_LOG_VERBOSE, "MAME", util::string_format(args).c_str());
#else
	if (m_ptr >= 0) m_stack[m_ptr]->output_callback(OSD_OUTPUT_CHANNEL_VERBOSE, args);
#endif
}


/*-------------------------------------------------
    osd_vprintf_debug - output debug text to the
    appropriate callback
-------------------------------------------------*/

void osd_vprintf_debug(util::format_argument_pack<char> const &args)
{
#if defined(SDLMAME_ANDROID)
	__android_log_write(ANDROID_LOG_DEBUG, "MAME", util::string_format(args).c_str());
#else
	if (m_ptr >= 0) m_stack[m_ptr]->output_callback(OSD_OUTPUT_CHANNEL_DEBUG, args);
#endif
}


//============================================================
//  osd_ticks
//============================================================

osd_ticks_t osd_ticks() noexcept
{
#if defined(__SWITCH__)
	// the ARM generic timer: a register read, where the std::chrono clock goes
	// through clock_gettime (the throttle reads this in a loop every frame)
	return nxperf::ticks();
#elif defined(_WIN32)
	LARGE_INTEGER val;
	QueryPerformanceCounter(&val);
	return val.QuadPart;
#else
	return std::chrono::high_resolution_clock::now().time_since_epoch().count();
#endif
}


//============================================================
//  osd_ticks_per_second
//============================================================

osd_ticks_t osd_ticks_per_second() noexcept
{
#if defined(__SWITCH__)
	return nxperf::TICKS_PER_SEC;
#elif defined(_WIN32)
	LARGE_INTEGER val;
	QueryPerformanceFrequency(&val);
	return val.QuadPart;
#else
	return std::chrono::high_resolution_clock::period::den / std::chrono::high_resolution_clock::period::num;
#endif
}

//============================================================
//  osd_sleep
//============================================================

void osd_sleep(osd_ticks_t duration) noexcept
{
#if defined(__SWITCH__)
	// The throttle asks to sleep up to the frame deadline, but a sleeping
	// thread can wake up late, which would cut the framerate.  So sleep only
	// until a safety margin before the deadline and return early; the
	// throttle then spins on the tick count for the rest, which keeps frame
	// timing exact.  The margin follows how late wake-ups actually are: the
	// worst lateness of the last ~16 seconds of sleeps plus 0.2 ms, between
	// 0.25 and 2 ms (a fixed 2 ms spin cost about 12% of core 0).
	constexpr int64_t MIN_MARGIN_NS = 250'000, MAX_MARGIN_NS = 2'000'000, PAD_NS = 200'000;
	constexpr int WINDOW_SLEEPS = 120, WINDOWS = 8;
	static int64_t s_margin_ns = MAX_MARGIN_NS;
	static int64_t s_window_late[WINDOWS] = { MAX_MARGIN_NS, MAX_MARGIN_NS, MAX_MARGIN_NS, MAX_MARGIN_NS, MAX_MARGIN_NS, MAX_MARGIN_NS, MAX_MARGIN_NS, MAX_MARGIN_NS };
	static int s_window = 0, s_sleeps = 0;

	int64_t const ns = int64_t(duration) * 1'000'000'000LL / int64_t(osd_ticks_per_second());
	if (ns > s_margin_ns)
	{
		int64_t const want = ns - s_margin_ns;
		uint64_t const start = nxperf::ticks();
		{
			nxperf::scope nxperf_scope(nxperf::throttle_sleep);
			svcSleepThread(want);
		}
		int64_t const slept = int64_t((nxperf::ticks() - start) * 1'000'000'000ULL / nxperf::TICKS_PER_SEC);
		int64_t const late = std::max<int64_t>(slept - want, 0);
		nxperf::sleep_late.add(uint64_t(late) * nxperf::TICKS_PER_SEC / 1'000'000'000ULL);

		// the first window of each slot starts at the maximum, so the margin
		// only comes down once ~16 seconds of real measurements are in
		if ((s_sleeps == 0) || (late > s_window_late[s_window]))
			s_window_late[s_window] = (s_sleeps == 0) ? late : std::max(s_window_late[s_window], late);
		if (++s_sleeps == WINDOW_SLEEPS)
		{
			s_sleeps = 0;
			s_window = (s_window + 1) % WINDOWS;
			int64_t worst = 0;
			for (int64_t const w : s_window_late)
				worst = std::max(worst, w);
			s_margin_ns = std::clamp(worst + PAD_NS, MIN_MARGIN_NS, MAX_MARGIN_NS);
			nxperf::spin_margin_us = uint32_t(s_margin_ns / 1000);
		}
	}
#elif defined(_WIN32)
// sleep_for appears to oversleep on Windows with gcc 8
	Sleep(duration / (osd_ticks_per_second() / 1000));
#else
	std::this_thread::sleep_for(std::chrono::high_resolution_clock::duration(duration));
#endif
}


//============================================================
//  osd_get_command_line - returns command line arguments
//  in an std::vector<std::string> in UTF-8
//
//  The real purpose of this call is to hide details necessary
//  on Windows (provided that one wants to avoid using wmain)
//============================================================

std::vector<std::string> osd_get_command_line(int argc, char *argv[])
{
	std::vector<std::string> results;
#ifdef _WIN32
	{
		// Get the command line from Windows
		int count;
		LPWSTR *wide_args = CommandLineToArgvW(GetCommandLineW(), &count);

		// Convert the returned command line arguments to UTF8 std::vector<std::string>
		results.reserve(count);
		for (int i = 0; i < count; i++)
		{
			std::string arg = osd::text::from_wstring(wide_args[i]);
			results.push_back(std::move(arg));
		}

		LocalFree(wide_args);
	}
#else // !_WIN32
	{
		// for non Windows platforms, we are assuming that arguments are
		// already UTF-8; we just need to convert to std::vector<std::string>
		results.reserve(argc);
		for (int i = 0; i < argc; i++)
			results.emplace_back(argv[i]);
	}
#endif // _WIN32
	return results;
}
