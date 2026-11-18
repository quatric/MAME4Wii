// license:BSD-3-Clause
// copyright-holders:MAMEdev Team, MAME4Wii contributors
//============================================================
//
//  osdlib_wii.cpp - OS library functions for the Nintendo Wii
//
//============================================================

#include "osdcore.h"
#include "osdlib.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>

#include <malloc.h>
#include <unistd.h>

#include <gccore.h>


//============================================================
//  osd_getenv
//============================================================

const char *osd_getenv(const char *name)
{
	return getenv(name);
}


//============================================================
//  osd_setenv
//============================================================

int osd_setenv(const char *name, const char *value, int overwrite)
{
	return setenv(name, value, overwrite);
}


//============================================================
//  osd_process_kill
//============================================================

void osd_process_kill()
{
	std::fflush(stdout);
	std::fflush(stderr);
	std::_Exit(1);
}


//============================================================
//  osd_break_into_debugger
//============================================================

void osd_break_into_debugger(const char *message)
{
	std::fprintf(stderr, "%s", message);
}


//============================================================
//  osd_get_cache_line_size
//============================================================

std::pair<std::error_condition, unsigned> osd_get_cache_line_size() noexcept
{
	// Broadway (PowerPC 750CL) has 32-byte cache lines
	return std::make_pair(std::error_condition(), 32U);
}


//============================================================
//  clipboard (not available)
//============================================================

std::string osd_get_clipboard_text() noexcept
{
	return std::string();
}

std::error_condition osd_set_clipboard_text(std::string_view text) noexcept
{
	return std::errc::io_error;
}


//============================================================
//  popen / pclose (used by Lua's io library; no processes on the Wii)
//============================================================

extern "C" FILE *popen(const char *command, const char *mode)
{
	errno = ENOSYS;
	return nullptr;
}

extern "C" int pclose(FILE *stream)
{
	errno = ENOSYS;
	return -1;
}


//============================================================
//  osd_getpid
//============================================================

int osd_getpid() noexcept
{
	return 1;
}


namespace osd {

namespace {

class dynamic_module_wii_impl : public dynamic_module
{
public:
	dynamic_module_wii_impl(std::vector<std::string> &&libraries) { }

protected:
	virtual generic_fptr_t get_symbol_address(char const *symbol) override
	{
		// no dynamic loading on the Wii
		return nullptr;
	}
};

} // anonymous namespace


bool invalidate_instruction_cache(void const *start, std::size_t size) noexcept
{
	DCFlushRange(const_cast<void *>(start), size);
	ICInvalidateRange(const_cast<void *>(start), size);
	return true;
}


// There is no MMU-backed virtual memory available to applications, so
// "virtual memory" allocations are plain aligned heap allocations and
// access protection is a no-op.
void *virtual_memory_allocation::do_alloc(std::initializer_list<std::size_t> blocks, unsigned intent, std::size_t &size, std::size_t &page_size) noexcept
{
	constexpr std::size_t p = 4096;
	std::size_t s(0);
	for (std::size_t b : blocks)
		s += (b + p - 1) / p;
	s *= p;
	if (!s)
		return nullptr;
	void *const result = memalign(p, s);
	if (!result)
		return nullptr;
	std::memset(result, 0, s);
	size = s;
	page_size = p;
	return result;
}

void virtual_memory_allocation::do_free(void *start, std::size_t size) noexcept
{
	free(start);
}

bool virtual_memory_allocation::do_set_access(void *start, std::size_t size, unsigned access) noexcept
{
	if (access & EXECUTE)
		invalidate_instruction_cache(start, size);
	return true;
}


dynamic_module::ptr dynamic_module::open(std::vector<std::string> &&names)
{
	return std::make_unique<dynamic_module_wii_impl>(std::move(names));
}

} // namespace osd
