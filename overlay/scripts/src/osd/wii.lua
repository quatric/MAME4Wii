-- license:BSD-3-Clause
-- copyright-holders:MAMEdev Team, MAME4Wii contributors

---------------------------------------------------------------------------
--
--   wii.lua
--
--   Rules for building the Nintendo Wii OSD (devkitPPC / libogc)
--
---------------------------------------------------------------------------

dofile("modules.lua")

BASE_TARGETOS = "unix"

_OPTIONS["NO_USE_MIDI"] = "1"
_OPTIONS["NO_USE_PORTAUDIO"] = "1"
_OPTIONS["NO_USE_PULSEAUDIO"] = "1"
_OPTIONS["NO_USE_PIPEWIRE"] = "1"
_OPTIONS["NO_OPENGL"] = "1"
_OPTIONS["USE_QTDEBUG"] = "0"
_OPTIONS["NO_USE_BGFX"] = "1"

WII_LIBS = {
	{ dir = MAME_DIR .. "3rdparty/libwiiinput",  lib = "lib" },
	{ dir = MAME_DIR .. "3rdparty/libwiiupdate", lib = "lib" },
	{ dir = MAME_DIR .. "3rdparty/libwiiosk",    lib = "lib" },
}

function maintargetosdoptions(_target,_subtarget)
	for _, l in ipairs(WII_LIBS) do
		libdirs {
			l.dir .. "/" .. l.lib,
		}
	end
	-- libogc, wiiuse and bte reference each other
	flags {
		"LinkSupportCircularDependencies",
	}
	links {
		"wiiupdate",
		"wiiosk",
		"input-wii",
		"wiikeyboard",
		"fat",
		"wiiuse",
		"bte",
		"ogc",
		"m",
	}
end

project ("qtdbg_" .. _OPTIONS["osd"])
	uuid (os.uuid("qtdbg_" .. _OPTIONS["osd"]))
	kind (LIBTYPE)

	dofile("wii_cfg.lua")
	includedirs {
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/devices",
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "src/osd/modules/render",
		MAME_DIR .. "3rdparty",
	}
	qtdebuggerbuild()

project ("osd_" .. _OPTIONS["osd"])
	uuid (os.uuid("osd_" .. _OPTIONS["osd"]))
	kind (LIBTYPE)

	dofile("wii_cfg.lua")
	osdmodulesbuild()

	includedirs {
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/devices",
		MAME_DIR .. "src/frontend/mame",
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "src/osd/modules/file",
		MAME_DIR .. "src/osd/modules/render",
		MAME_DIR .. "3rdparty",
		MAME_DIR .. "src/osd/wii",
		MAME_DIR .. "3rdparty/libwiiinput/include",
		MAME_DIR .. "3rdparty/libwiiupdate/include",
		MAME_DIR .. "3rdparty/libwiiosk/include",
	}

	files {
		MAME_DIR .. "src/osd/osdepend.h",
		MAME_DIR .. "src/osd/modules/osdwindow.cpp",
		MAME_DIR .. "src/osd/modules/osdwindow.h",
		MAME_DIR .. "src/osd/wii/osdwii.h",
		MAME_DIR .. "src/osd/wii/wiimain.cpp",
		MAME_DIR .. "src/osd/wii/wiivideo.cpp",
		MAME_DIR .. "src/osd/wii/wiisound.cpp",
		MAME_DIR .. "src/osd/wii/wiiinput.cpp",
		MAME_DIR .. "src/osd/wii/wiimonitor.cpp",
	}

project ("ocore_" .. _OPTIONS["osd"])
	uuid (os.uuid("ocore_" .. _OPTIONS["osd"]))
	kind (LIBTYPE)

	removeflags {
		"SingleOutputDir",
	}

	dofile("wii_cfg.lua")

	includedirs {
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "src/osd/wii",
	}

	files {
		MAME_DIR .. "src/osd/osdcore.cpp",
		MAME_DIR .. "src/osd/osdcore.h",
		MAME_DIR .. "src/osd/osdfile.h",
		MAME_DIR .. "src/osd/strconv.cpp",
		MAME_DIR .. "src/osd/strconv.h",
		MAME_DIR .. "src/osd/osdsync.cpp",
		MAME_DIR .. "src/osd/osdsync.h",
		MAME_DIR .. "src/osd/modules/osdmodule.cpp",
		MAME_DIR .. "src/osd/modules/osdmodule.h",
		MAME_DIR .. "src/osd/modules/lib/osdlib_wii.cpp",
		MAME_DIR .. "src/osd/modules/lib/osdlib.h",
		MAME_DIR .. "src/osd/modules/file/posixdir.cpp",
		MAME_DIR .. "src/osd/modules/file/stdfile.cpp",
	}
