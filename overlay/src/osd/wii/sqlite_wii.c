// license:BSD-3-Clause
// copyright-holders:MAMEdev Team, MAME4Wii contributors
/*
 * sqlite_wii.c - OS hooks for SQLite built with SQLITE_OS_OTHER on the Wii.
 *
 * No file-backed VFS is registered, so on-disk databases are unavailable
 * (Lua plugins that try to open one will get an error and carry on).
 */

#include "../../../3rdparty/sqlite3/sqlite3.h"

int sqlite3_os_init(void)
{
	return SQLITE_OK;
}

int sqlite3_os_end(void)
{
	return SQLITE_OK;
}
