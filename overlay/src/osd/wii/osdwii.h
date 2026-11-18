// license:BSD-3-Clause
// copyright-holders:MAMEdev Team, MAME4Wii contributors
//============================================================
//
//  osdwii.h - Nintendo Wii OSD interface
//
//============================================================
#ifndef MAME_OSD_WII_OSDWII_H
#define MAME_OSD_WII_OSDWII_H

#pragma once

#include "modules/lib/osdobj_common.h"
#include "modules/osdmodule.h"

#include <cstdint>
#include <memory>


class render_primitive_list;
struct osk_keyboard;


//============================================================
//  options
//============================================================

#define WIIOPTION_SAFEAREA      "safearea"
#define WIIOPTION_WIDESCREEN    "widescreen"
#define WIIOPTION_RENDERSCALE   "renderscale"

class wii_options : public osd_options
{
public:
	wii_options();

	float safe_area() const { return float_value(WIIOPTION_SAFEAREA); }
	const char *widescreen() const { return value(WIIOPTION_WIDESCREEN); }
	int render_scale() const { return int_value(WIIOPTION_RENDERSCALE); }

	static const options_entry s_option_entries[];
};


//============================================================
//  OSD interface
//============================================================

class wii_osd_interface : public osd_common_t
{
public:
	wii_osd_interface(wii_options &options);
	virtual ~wii_osd_interface();

	// general overridables
	virtual void init(running_machine &machine) override;
	virtual void update(bool skip_redraw) override;
	virtual void input_update(bool relative_reset) override;
	virtual void check_osd_inputs() override;

	// input overridables
	virtual void customize_input_type_list(std::vector<input_type_entry> &typelist) override;

	virtual bool video_init() override;
	virtual bool window_init() override;
	virtual void video_exit() override;
	virtual void window_exit() override;

	virtual void process_events() override;
	virtual void output_callback(osd_output_channel channel, const util::format_argument_pack<char> &args) override;
	virtual bool has_focus() const override { return true; }

	virtual wii_options &options() override { return m_options; }

	// Wii-specific
	static bool reset_requested();
	static bool power_requested();

private:
	virtual void osd_exit() override;

	void open_osk();
	void close_osk(bool accepted);
	static void draw_osk(void *param);

	wii_options &m_options;
	render_target *m_target;
	osk_keyboard *m_osk;
	bool m_widescreen;
};


//============================================================
//  global helpers implemented in wiimain.cpp / wiivideo.cpp
//============================================================

// early hardware/video bring-up used for the boot console
void wii_video_hw_init();
// shut down video before exiting to the loader
void wii_video_hw_shutdown();
// render a render_primitive_list to the TV
void wii_video_present(render_primitive_list &primlist, int width, int height, void (*overlay)(void *) = nullptr, void *param = nullptr);
// whether the console/TV is configured for 16:9
bool wii_video_is_widescreen();
// append to debug.log (when enabled by a "debuglog" file); survives crashes
void wii_debug_log(const char *text);

#endif // MAME_OSD_WII_OSDWII_H
