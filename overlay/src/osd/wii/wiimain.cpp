// license:BSD-3-Clause
// copyright-holders:MAMEdev Team, MAME4Wii contributors
//============================================================
//
//  wiimain.cpp - Nintendo Wii main program and OSD object
//
//============================================================

#include "osdwii.h"

#include "emu.h"
#include "emuopts.h"
#include "main.h"
#include "render.h"
#include "ui/uimain.h"
#include "uiinput.h"
#include "unicode.h"
#include "language.h"

#include "modules/lib/osdlib.h"
#include "osdcore.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fat.h>
#include <ogc/usbgecko.h>
#include <gccore.h>
#include <ogc/lwp.h>
#include <wiiuse/wpad.h>

// libwiiinput's input_device struct would clash with MAME's class of that name
#define input_device wii_input_device
#include <input_wii.h>
#include <wiiosk.h>
#include <wiiosk_input_wii.h>
#undef input_device
#include <wiiupdate.h>


#ifndef MAME4WII_VERSION
#define MAME4WII_VERSION "2.289.0"
#endif
#ifndef MAME4WII_UPDATE_OWNER
#define MAME4WII_UPDATE_OWNER "quatric"
#endif
#ifndef MAME4WII_UPDATE_REPO
#define MAME4WII_UPDATE_REPO "MAME4Wii"
#endif


// implemented in wiivideo.cpp / wiiinput.cpp
void wii_video_show_console();
GXRModeObj *wii_video_mode();
void wii_input_hw_init();
void wii_input_poll_hardware();
bool wii_input_any_home_pressed();
void wii_input_suspend(bool suspend);
void *wii_audio_dma_memory();


//============================================================
//  globals
//============================================================

namespace {

volatile bool s_reset_pressed = false;
volatile bool s_power_pressed = false;

wii_options *s_options = nullptr;

void reset_callback(u32 irq, void *ctx)
{
	s_reset_pressed = true;
}

void power_callback()
{
	s_power_pressed = true;
}

void wiimote_power_callback(s32 chan)
{
	s_power_pressed = true;
}

} // anonymous namespace


bool wii_video_filter_enabled()
{
	return s_options ? s_options->filter() : true;
}

float wii_video_safe_area()
{
	return s_options ? s_options->safe_area() : 1.0f;
}

bool wii_video_wait_vsync()
{
	return s_options ? s_options->wait_vsync() : false;
}


//============================================================
//  options
//============================================================

const options_entry wii_options::s_option_entries[] =
{
	{ nullptr,                    nullptr,  core_options::option_type::HEADER,  "WII VIDEO OPTIONS" },
	{ WIIOPTION_SAFEAREA "(0.5-1.0)", "1.0", core_options::option_type::FLOAT,   "fraction of the TV picture to use (shrink to fit overscan)" },
	{ WIIOPTION_WIDESCREEN,       "auto",   core_options::option_type::STRING,  "treat the TV as 16:9 (1), 4:3 (0) or follow the console setting (auto)" },
	{ WIIOPTION_RENDERSCALE "(0-4)", "0",   core_options::option_type::INTEGER, "software render scale relative to native resolution (0 = automatic)" },
	{ nullptr }
};

wii_options::wii_options() : osd_options()
{
	add_entries(s_option_entries);
}


//============================================================
//  wii_osd_interface
//============================================================

wii_osd_interface::wii_osd_interface(wii_options &options) :
	osd_common_t(options),
	m_options(options),
	m_target(nullptr),
	m_osk(nullptr),
	m_widescreen(false)
{
	s_options = &options;
}

wii_osd_interface::~wii_osd_interface()
{
	s_options = nullptr;
}

bool wii_osd_interface::reset_requested()
{
	return s_reset_pressed;
}

bool wii_osd_interface::power_requested()
{
	return s_power_pressed;
}


void wii_osd_interface::init(running_machine &machine)
{
	osd_common_t::init(machine);

	// benchmarking support
	int const bench = options().bench();
	if (bench > 0)
	{
		options().set_value(OPTION_SLEEP, false, OPTION_PRIORITY_MAXIMUM);
		options().set_value(OPTION_THROTTLE, false, OPTION_PRIORITY_MAXIMUM);
		options().set_value(OSDOPTION_SOUND, "none", OPTION_PRIORITY_MAXIMUM);
		options().set_value(OSDOPTION_VIDEO, "none", OPTION_PRIORITY_MAXIMUM);
		options().set_value(OPTION_SECONDS_TO_RUN, bench, OPTION_PRIORITY_MAXIMUM);
	}

	osd_common_t::init_subsystems();
}


bool wii_osd_interface::video_init()
{
	wii_video_hw_init();
	m_target = machine().render().target_alloc();
	return m_target != nullptr;
}


bool wii_osd_interface::window_init()
{
	return true;
}


void wii_osd_interface::video_exit()
{
	if (m_target)
	{
		machine().render().target_free(m_target);
		m_target = nullptr;
	}
}


void wii_osd_interface::window_exit()
{
}


void wii_osd_interface::update(bool skip_redraw)
{
	osd_common_t::update(skip_redraw);

	if (s_reset_pressed || s_power_pressed)
	{
		machine().schedule_exit();
		return;
	}

	if (skip_redraw || !m_target)
		return;

	if (strcmp(options().video(), "none") == 0)
		return;

	// work out the software render size
	s32 minw = 0, minh = 0;
	m_target->compute_minimum_size(minw, minh);
	int width, height;
	if ((minw <= 0) || (minh <= 0) || (machine().system().name == std::string_view("___empty")))
	{
		// no screens (e.g. system selection menu): render at TV resolution
		width = 640;
		height = 480;
	}
	else
	{
		int scale = options().render_scale();
		if (scale <= 0)
		{
			// automatic: smallest integer scale that gives at least 400 lines
			// (keeps the UI text legible) without exceeding 640x480 too much
			scale = 1;
			while ((minh * (scale + 1) <= 512) && (minw * (scale + 1) <= 768) && (minh * scale < 400))
				++scale;
		}
		width = std::min<int>(minw * scale, 1024);
		height = std::min<int>(minh * scale, 1024);
	}

	bool widescreen;
	char const *const ws = options().widescreen();
	if (!strcmp(ws, "1") || !strcmp(ws, "yes") || !strcmp(ws, "16:9"))
		widescreen = true;
	else if (!strcmp(ws, "0") || !strcmp(ws, "no") || !strcmp(ws, "4:3"))
		widescreen = false;
	else
		widescreen = wii_video_is_widescreen();
	m_widescreen = widescreen;
	float const screen_aspect = widescreen ? (16.0f / 9.0f) : (4.0f / 3.0f);
	float const pixel_aspect = screen_aspect * float(height) / float(width);

	m_target->set_bounds(width, height, pixel_aspect);
	render_primitive_list &primlist = m_target->get_primitives();

	if (m_osk)
	{
		osk_input in;
		OSK_ReadInputWii(&in);
		osk_status const st = OSK_Update(m_osk, &in);
		if (st != OSK_STATUS_EDITING)
		{
			close_osk(st == OSK_STATUS_DONE);
			wii_video_present(primlist, width, height);
		}
		else
		{
			wii_video_present(primlist, width, height, &wii_osd_interface::draw_osk, m_osk);
		}
	}
	else
	{
		wii_video_present(primlist, width, height);
	}
}


void wii_osd_interface::input_update(bool relative_reset)
{
	wii_input_poll_hardware();

	// forward typed characters to MAME's UI (search boxes, file names, ...)
	if (!m_osk && m_target)
	{
		input_key_event ev;
		while (INPUT_KeyPop(&ev))
		{
			if (ev.code == 0x2a)
				m_target->push_char_event(8);
			else if (ev.ch)
				m_target->push_char_event(char32_t(u8(ev.ch)));
		}
	}

	poll_input_modules(relative_reset);
}


void wii_osd_interface::process_events()
{
}


void wii_osd_interface::output_callback(osd_output_channel channel, const util::format_argument_pack<char> &args)
{
	osd_common_t::output_callback(channel, args);
	if ((channel == OSD_OUTPUT_CHANNEL_DEBUG) || ((channel == OSD_OUTPUT_CHANNEL_VERBOSE) && !verbose()))
		return;
	std::ostringstream text;
	util::stream_format(text, args);
	wii_debug_log(text.str().c_str());
}


void wii_osd_interface::check_osd_inputs()
{
	// OSD 1: on-screen keyboard for MAME's text fields
	if (!m_osk && machine().ui_input().pressed(IPT_OSD_1))
		open_osk();
}


void wii_osd_interface::open_osk()
{
	osk_config cfg;
	OSK_DefaultConfig(&cfg);
	OSK_UseSystemLanguage(&cfg);
	cfg.title = "Type text for MAME";
	cfg.max_chars = 256;
	cfg.flags |= OSK_FLAG_NO_ENTER;
	GXRModeObj *const mode = wii_video_mode();
	cfg.screen_width = mode ? mode->fbWidth : 640;
	cfg.screen_height = mode ? mode->efbHeight : 480;
	if (m_widescreen)
		cfg.flags |= OSK_FLAG_WIDESCREEN;
	m_osk = OSK_Create(&cfg);
	if (m_osk)
		wii_input_suspend(true);
}


void wii_osd_interface::close_osk(bool accepted)
{
	if (!m_osk)
		return;
	if (accepted && m_target)
	{
		std::string_view text(OSK_GetText(m_osk));
		while (!text.empty())
		{
			char32_t ch;
			int const len = uchar_from_utf8(&ch, text);
			if (len <= 0)
				break;
			m_target->push_char_event(ch);
			text.remove_prefix(len);
		}
	}
	OSK_Destroy(m_osk);
	m_osk = nullptr;
	wii_input_suspend(false);
}


void wii_osd_interface::draw_osk(void *param)
{
	OSK_Draw(reinterpret_cast<osk_keyboard *>(param));
}


void wii_osd_interface::customize_input_type_list(std::vector<input_type_entry> &typelist)
{
	osd_common_t::customize_input_type_list(typelist);

	for (input_type_entry &entry : typelist)
	{
		if (entry.type() == IPT_OSD_1)
		{
			// Minus + Plus (GameCube: Z + R) on a controller, Ctrl + K on a keyboard
			entry.configure_osd("SHOW_OSK", N_p("input-name", "Show On-Screen Keyboard"));
			entry.defseq(SEQ_TYPE_STANDARD).set(KEYCODE_K, KEYCODE_LCONTROL);
		}
	}
}


void wii_osd_interface::osd_exit()
{
	close_osk(false);
	osd_common_t::osd_exit();
}


//============================================================
//  OSD-specific option and focus hooks used by the core
//============================================================

void osd_setup_osd_specific_emu_options(emu_options &opts)
{
	opts.add_entries(osd_options::s_option_entries);
	opts.add_entries(wii_options::s_option_entries);
}

void osd_set_aggressive_input_focus(bool aggressive_focus)
{
	// only one "window" on the Wii
}


//============================================================
//  storage helpers
//============================================================

namespace {

int s_log_fd = -1;

bool dir_exists(const char *path)
{
	struct stat st;
	return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

std::string find_base_directory(int argc, char **argv)
{
	// preferred locations, in order
	static const char *const candidates[] = {
		"sd:/mame4wii",
		"usb:/mame4wii",
		"sd:/apps/mame4wii",
		"usb:/apps/mame4wii",
	};
	for (const char *c : candidates)
		if (dir_exists(c))
			return c;

	// fall back to the directory the DOL was launched from
	if (argc > 0 && argv && argv[0])
	{
		std::string path(argv[0]);
		auto const slash = path.find_last_of('/');
		if (slash != std::string::npos)
		{
			path.resize(slash);
			if (dir_exists(path.c_str()))
				return path;
		}
	}

	return "sd:/mame4wii";
}

void wait_for_button(const char *message)
{
	std::printf("\n%s\n", message);
	while (true)
	{
		INPUT_Update();
		if (s_reset_pressed || s_power_pressed)
			break;
		if (INPUT_ButtonsDown() & (WPAD_BUTTON_A | WPAD_BUTTON_HOME))
			break;
		VIDEO_WaitVSync();
	}
}


struct main_args
{
	std::vector<std::string> args;
	int result;
};

void *mame_thread(void *param)
{
	main_args &ma = *reinterpret_cast<main_args *>(param);
	try
	{
		wii_options options;
		wii_osd_interface osd(options);
		osd.register_options();
		wii_debug_log("starting frontend\n");
		ma.result = emulator_info::start_frontend(options, osd, ma.args);
		wii_debug_log("frontend returned\n");
	}
	catch (std::exception const &e)
	{
		wii_debug_log("fatal exception\n");
		std::fprintf(stderr, "Fatal exception: %s\n", e.what());
		ma.result = EMU_ERR_FATALERROR;
	}
	catch (...)
	{
		std::fprintf(stderr, "Fatal unhandled exception\n");
		ma.result = EMU_ERR_FATALERROR;
	}
	return nullptr;
}

} // anonymous namespace


void wii_debug_log(const char *text)
{
	if (s_log_fd < 0)
		return;
	::write(s_log_fd, text, std::strlen(text));
	::fsync(s_log_fd);
}


//============================================================
//  main
//============================================================

int main(int argc, char *argv[])
{
	wii_video_hw_init();
	wii_audio_dma_memory();

	// Throw one exception now, while the heap is empty: the unwinder sorts
	// its frame tables on first use, and if that first use is a bad_alloc
	// thrown with the heap exhausted it cannot get the memory and crashes.
	try { throw 0; } catch (...) { }

	// mirror console output to a USB Gecko in slot B when one is attached
	if (usb_isgeckoalive(EXI_CHANNEL_1))
		CON_EnableGecko(EXI_CHANNEL_1, false);
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	std::setvbuf(stderr, nullptr, _IONBF, 0);

	SYS_SetResetCallback(reset_callback);
	SYS_SetPowerCallback(power_callback);

	std::printf("\x1b[2;0H");
	std::printf("MAME4Wii %s - %s %s\n", MAME4WII_VERSION, emulator_info::get_appname(), emulator_info::get_build_version());
	std::printf("Initialising storage...\n");

	if (!fatInitDefault())
	{
		wait_for_button("Unable to mount SD card or USB storage.\nPress A or HOME to exit.");
		wii_video_hw_shutdown();
		return 1;
	}

	wii_input_hw_init();
	WPAD_SetPowerButtonCallback(wiimote_power_callback);

	std::string const base = find_base_directory(argc, argv);
	if (chdir(base.c_str()) != 0)
	{
		mkdir(base.c_str(), 0777);
		chdir(base.c_str());
	}
	std::printf("Using %s\n", base.c_str());

	// creating a file called "debuglog" copies MAME's output to debug.log
	{
		struct stat st;
		if (stat("debuglog", &st) == 0)
			s_log_fd = open("debug.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
		wii_debug_log("MAME4Wii " MAME4WII_VERSION " debug log\n");
	}

	// check GitHub for a newer release (skip by creating "noupdatecheck" in the base folder)
	{
		struct stat st;
		if (stat("noupdatecheck", &st) != 0)
		{
			WIIUPDATE_SetAppPath((argc > 0) ? argv[0] : nullptr);
			wiiupdate_state const upd = WIIUPDATE_CheckConsole(
					MAME4WII_UPDATE_OWNER, MAME4WII_UPDATE_REPO, MAME4WII_VERSION,
					[] () -> u32 { INPUT_Update(); return INPUT_ButtonsDown(); });
			if (upd == WIIUPDATE_UPDATED)
			{
				wii_video_hw_shutdown();
				return 0;
			}
		}
	}

	// build the MAME command line: argv[0] plus any arguments passed by the loader
	main_args ma;
	ma.result = 0;
	ma.args.emplace_back("mame");
	for (int i = 1; i < argc; ++i)
		if (argv[i] && *argv[i])
			ma.args.emplace_back(argv[i]);

	// extra arguments (e.g. a system to start straight away) from cmdline.txt
	if (FILE *const f = std::fopen("cmdline.txt", "r"))
	{
		char token[256];
		while (std::fscanf(f, "%255s", token) == 1)
			ma.args.emplace_back(token);
		std::fclose(f);
	}

	// run MAME on a thread with a generous stack; the default main stack is small
	constexpr u32 STACK_SIZE = 2 * 1024 * 1024;
	void *const stack = memalign(32, STACK_SIZE);
	lwp_t thread;
	if (!stack || LWP_CreateThread(&thread, mame_thread, &ma, stack, STACK_SIZE, 64) < 0)
	{
		wait_for_button("Unable to start the emulation thread.\nPress A or HOME to exit.");
		wii_video_hw_shutdown();
		return 1;
	}
	LWP_JoinThread(thread, nullptr);
	free(stack);

	if ((ma.result != EMU_ERR_NONE) && !s_reset_pressed && !s_power_pressed)
	{
		wii_video_show_console();
		std::printf("\x1b[2;0H");
		std::printf("MAME exited with error code %d.\n", ma.result);
		std::printf("See %s/mame.log (run with -log) for details.\n", base.c_str());
		wait_for_button("Press A or HOME to exit.");
	}

	// flush everything to the card before returning to the loader
	std::fflush(stdout);
	std::fflush(stderr);
	fatUnmount("sd:");
	fatUnmount("usb:");

	wii_video_hw_shutdown();
	if (s_power_pressed)
		SYS_ResetSystem(SYS_POWEROFF, 0, 0);
	return 0;
}
