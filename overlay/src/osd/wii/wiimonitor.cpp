// license:BSD-3-Clause
// copyright-holders:MAMEdev Team, MAME4Wii contributors
//============================================================
//
//  wiimonitor.cpp - single fixed "monitor" (the TV)
//
//============================================================

#include "modules/osdmodule.h"
#include "modules/monitor/monitor_module.h"

#if defined(OSD_WII)

#include "modules/monitor/monitor_common.h"
#include "modules/lib/osdobj_common.h"

#include "osdwii.h"

namespace {

class wii_monitor_info : public osd_monitor_info
{
public:
	wii_monitor_info(monitor_module &module, std::uint64_t handle, const char *monitor_device, float aspect)
		: osd_monitor_info(module, handle, monitor_device, aspect)
	{
		wii_monitor_info::refresh();
	}

private:
	void refresh() override
	{
		m_pos_size = osd_rect(0, 0, 640, 480);
		m_usuable_pos_size = m_pos_size;
		m_is_primary = true;
	}
};


class wii_monitor_module : public monitor_module_base
{
public:
	wii_monitor_module() : monitor_module_base(OSD_MONITOR_PROVIDER, "wii") { }

	std::shared_ptr<osd_monitor_info> monitor_from_rect(const osd_rect &proposed) override
	{
		return m_initialized ? list().front() : nullptr;
	}

	std::shared_ptr<osd_monitor_info> monitor_from_window(const osd_window &window) override
	{
		return m_initialized ? list().front() : nullptr;
	}

protected:
	int init_internal(const osd_options &options) override
	{
		float const aspect = wii_video_is_widescreen() ? (16.0f / 9.0f) : (4.0f / 3.0f);
		add_monitor(std::make_shared<wii_monitor_info>(*this, 0, OSDOPTION_SCREEN "0", aspect));
		return 0;
	}
};

} // anonymous namespace

#else
namespace { MODULE_NOT_SUPPORTED(wii_monitor_module, OSD_MONITOR_PROVIDER, "wii") }
#endif

MODULE_DEFINITION(MONITOR_WII, wii_monitor_module)
