-- license:BSD-3-Clause
-- copyright-holders:MAMEdev Team, MAME4Wii contributors

dofile('modules.lua')

defines {
	"OSD_WII",
	"NO_USE_MIDI",
	"NO_USE_PORTAUDIO",
	"NO_USE_PULSEAUDIO",
	"NO_USE_PIPEWIRE",
	"USE_QTDEBUG=0",
	"USE_OPENGL=0",
	"__WII__",
	"SDLMAME_NO64BITIO",
}

includedirs {
	"$(DEVKITPRO)/libogc/include",
}
