// license:BSD-3-Clause
// copyright-holders:MAMEdev Team, MAME4Wii contributors
//============================================================
//
//  wiiinput.cpp - Wii input through libwiiinput
//
//  Joysticks:  Wii Remote (sideways / with Nunchuk), Classic
//              Controller, GameCube pads (console ports and the
//              Wii U adapter), Wii U Pro, USB pads, ...
//  Keyboard:   USB keyboards
//  Mouse:      USB mice
//  Light gun:  Wii Remote IR pointer
//
//============================================================

#include "modules/input/input_module.h"
#include "modules/osdmodule.h"

#if defined(OSD_WII)

#include "modules/input/assignmenthelper.h"
#include "modules/input/input_common.h"

#include "osdwii.h"

#include "emu.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <gccore.h>
#include <wiiuse/wpad.h>

// libwiiinput's input_device struct would clash with MAME's class of that name
#define input_device wii_input_device
#include <input_wii.h>
#undef input_device


namespace {

//============================================================
//  shared controller state, refreshed once per frame
//============================================================

constexpr int WII_MAX_PLAYERS = 4;

enum
{
	WB_1 = 0, WB_2, WB_3, WB_4, WB_5, WB_6, WB_7, WB_8,
	WB_START, WB_SELECT, WB_HOME, WB_OSK,
	WB_UP, WB_DOWN, WB_LEFT, WB_RIGHT,
	WB_COUNT
};

struct wii_pad_state
{
	s32 axis[6];        // X, Y, RX, RY, Z (left trigger), RZ (right trigger)
	u8 buttons[WB_COUNT];
	s32 gun_x, gun_y;
	u8 gun_buttons[2];
	bool gun_valid;
	bool connected;
	char name[32];
};

wii_pad_state s_pads[WII_MAX_PLAYERS];
bool s_hw_initialized = false;
bool s_suspended = false;   // while the on-screen keyboard has the controllers


inline s32 scale_axis(float v)
{
	if (std::isnan(v))
		return 0;
	v = std::clamp(v, -1.0f, 1.0f);
	if (std::fabs(v) < 0.12f)
		return 0;
	return s32(v * float(osd::input_device::ABSOLUTE_MAX));
}

inline s32 scale_trigger(float v)
{
	// 0..1 -> MAX..MIN, the convention for pedals (released = MAX)
	v = std::clamp(v, 0.0f, 1.0f);
	return s32(osd::input_device::ABSOLUTE_MAX - v * 2.0f * osd::input_device::ABSOLUTE_MAX);
}


void map_classic(u32 b, wii_pad_state &st, bool &u, bool &d, bool &l, bool &r)
{
	st.buttons[WB_1] |= (b & WPAD_CLASSIC_BUTTON_B) ? 1 : 0;
	st.buttons[WB_2] |= (b & WPAD_CLASSIC_BUTTON_A) ? 1 : 0;
	st.buttons[WB_3] |= (b & WPAD_CLASSIC_BUTTON_Y) ? 1 : 0;
	st.buttons[WB_4] |= (b & WPAD_CLASSIC_BUTTON_X) ? 1 : 0;
	st.buttons[WB_5] |= (b & WPAD_CLASSIC_BUTTON_FULL_L) ? 1 : 0;
	st.buttons[WB_6] |= (b & WPAD_CLASSIC_BUTTON_FULL_R) ? 1 : 0;
	st.buttons[WB_7] |= (b & WPAD_CLASSIC_BUTTON_ZL) ? 1 : 0;
	st.buttons[WB_8] |= (b & WPAD_CLASSIC_BUTTON_ZR) ? 1 : 0;
	bool const plus = (b & WPAD_CLASSIC_BUTTON_PLUS) != 0;
	bool const minus = (b & WPAD_CLASSIC_BUTTON_MINUS) != 0;
	if (plus && minus)
		st.buttons[WB_OSK] = 1;
	else
	{
		st.buttons[WB_START] |= plus ? 1 : 0;
		st.buttons[WB_SELECT] |= minus ? 1 : 0;
	}
	st.buttons[WB_HOME] |= (b & WPAD_CLASSIC_BUTTON_HOME) ? 1 : 0;
	u |= (b & WPAD_CLASSIC_BUTTON_UP) != 0;
	d |= (b & WPAD_CLASSIC_BUTTON_DOWN) != 0;
	l |= (b & WPAD_CLASSIC_BUTTON_LEFT) != 0;
	r |= (b & WPAD_CLASSIC_BUTTON_RIGHT) != 0;
}


void map_gamecube(u32 b, wii_pad_state &st, bool &u, bool &d, bool &l, bool &r)
{
	st.buttons[WB_1] |= (b & PAD_BUTTON_A) ? 1 : 0;
	st.buttons[WB_2] |= (b & PAD_BUTTON_B) ? 1 : 0;
	st.buttons[WB_3] |= (b & PAD_BUTTON_X) ? 1 : 0;
	st.buttons[WB_4] |= (b & PAD_BUTTON_Y) ? 1 : 0;
	st.buttons[WB_5] |= (b & PAD_TRIGGER_L) ? 1 : 0;
	st.buttons[WB_6] |= (b & PAD_TRIGGER_R) ? 1 : 0;
	bool const z = (b & PAD_TRIGGER_Z) != 0;
	bool const start = (b & PAD_BUTTON_START) != 0;
	if (z && start)
		st.buttons[WB_HOME] = 1;                    // Z + Start: MAME menu
	else if (z && (b & PAD_TRIGGER_R))
	{
		st.buttons[WB_OSK] = 1;                     // Z + R: on-screen keyboard
		st.buttons[WB_6] = 0;
	}
	else
	{
		st.buttons[WB_SELECT] |= z ? 1 : 0;         // Z: coin
		st.buttons[WB_START] |= start ? 1 : 0;
	}
	u |= (b & PAD_BUTTON_UP) != 0;
	d |= (b & PAD_BUTTON_DOWN) != 0;
	l |= (b & PAD_BUTTON_LEFT) != 0;
	r |= (b & PAD_BUTTON_RIGHT) != 0;
}


// folded WPAD_BUTTON_* layout used by libwiiinput for every controller
void map_folded(u32 b, wii_pad_state &st, bool &u, bool &d, bool &l, bool &r)
{
	st.buttons[WB_1] |= (b & WPAD_BUTTON_A) ? 1 : 0;
	st.buttons[WB_2] |= (b & WPAD_BUTTON_B) ? 1 : 0;
	st.buttons[WB_3] |= (b & WPAD_BUTTON_1) ? 1 : 0;
	st.buttons[WB_4] |= (b & WPAD_BUTTON_2) ? 1 : 0;
	st.buttons[WB_5] |= (b & INPUT_BUTTON_Z) ? 1 : 0;
	st.buttons[WB_6] |= (b & INPUT_BUTTON_C) ? 1 : 0;
	bool const plus = (b & WPAD_BUTTON_PLUS) != 0;
	bool const minus = (b & WPAD_BUTTON_MINUS) != 0;
	if (plus && minus)
		st.buttons[WB_OSK] = 1;
	else
	{
		st.buttons[WB_START] |= plus ? 1 : 0;
		st.buttons[WB_SELECT] |= minus ? 1 : 0;
	}
	st.buttons[WB_HOME] |= (b & WPAD_BUTTON_HOME) ? 1 : 0;
	u |= (b & WPAD_BUTTON_UP) != 0;
	d |= (b & WPAD_BUTTON_DOWN) != 0;
	l |= (b & WPAD_BUTTON_LEFT) != 0;
	r |= (b & WPAD_BUTTON_RIGHT) != 0;
}


void read_device(const wii_input_device &dev, wii_pad_state &st)
{
	bool u = false, d = false, l = false, r = false;
	float lx = dev.lx, ly = dev.ly;

	switch (dev.kind)
	{
	case INPUT_DEV_WIIMOTE:
		if (!std::strcmp(dev.name, "Classic"))
		{
			map_classic(dev.raw_ext, st, u, d, l, r);
			st.buttons[WB_HOME] |= (dev.raw & WPAD_BUTTON_HOME) ? 1 : 0;
		}
		else if (!std::strcmp(dev.name, "Nunchuk"))
		{
			// Wii Remote upright with the Nunchuk
			u32 const b = dev.raw;
			st.buttons[WB_1] |= (b & WPAD_BUTTON_A) ? 1 : 0;
			st.buttons[WB_2] |= (b & WPAD_BUTTON_B) ? 1 : 0;
			st.buttons[WB_3] |= (dev.raw_ext & WPAD_NUNCHUK_BUTTON_Z) ? 1 : 0;
			st.buttons[WB_4] |= (dev.raw_ext & WPAD_NUNCHUK_BUTTON_C) ? 1 : 0;
			st.buttons[WB_5] |= (b & WPAD_BUTTON_1) ? 1 : 0;
			st.buttons[WB_6] |= (b & WPAD_BUTTON_2) ? 1 : 0;
			bool const plus = (b & WPAD_BUTTON_PLUS) != 0;
			bool const minus = (b & WPAD_BUTTON_MINUS) != 0;
			if (plus && minus)
				st.buttons[WB_OSK] = 1;
			else
			{
				st.buttons[WB_START] |= plus ? 1 : 0;
				st.buttons[WB_SELECT] |= minus ? 1 : 0;
			}
			st.buttons[WB_HOME] |= (b & WPAD_BUTTON_HOME) ? 1 : 0;
			u |= (b & WPAD_BUTTON_UP) != 0;
			d |= (b & WPAD_BUTTON_DOWN) != 0;
			l |= (b & WPAD_BUTTON_LEFT) != 0;
			r |= (b & WPAD_BUTTON_RIGHT) != 0;
		}
		else
		{
			// Wii Remote on its own, held sideways (D-pad on the left)
			u32 const b = dev.raw;
			st.buttons[WB_1] |= (b & WPAD_BUTTON_2) ? 1 : 0;
			st.buttons[WB_2] |= (b & WPAD_BUTTON_1) ? 1 : 0;
			st.buttons[WB_3] |= (b & WPAD_BUTTON_A) ? 1 : 0;
			st.buttons[WB_4] |= (b & WPAD_BUTTON_B) ? 1 : 0;
			bool const plus = (b & WPAD_BUTTON_PLUS) != 0;
			bool const minus = (b & WPAD_BUTTON_MINUS) != 0;
			if (plus && minus)
				st.buttons[WB_OSK] = 1;
			else
			{
				st.buttons[WB_START] |= plus ? 1 : 0;
				st.buttons[WB_SELECT] |= minus ? 1 : 0;
			}
			st.buttons[WB_HOME] |= (b & WPAD_BUTTON_HOME) ? 1 : 0;
			u |= (b & WPAD_BUTTON_RIGHT) != 0;
			d |= (b & WPAD_BUTTON_LEFT) != 0;
			l |= (b & WPAD_BUTTON_UP) != 0;
			r |= (b & WPAD_BUTTON_DOWN) != 0;
		}
		if (dev.pointer)
		{
			st.gun_valid = true;
			st.gun_x = s32((dev.px / 640.0f) * 2.0f * osd::input_device::ABSOLUTE_MAX) - osd::input_device::ABSOLUTE_MAX;
			st.gun_y = s32((dev.py / 480.0f) * 2.0f * osd::input_device::ABSOLUTE_MAX) - osd::input_device::ABSOLUTE_MAX;
		}
		st.gun_buttons[0] = (dev.raw & WPAD_BUTTON_B) ? 1 : 0;
		st.gun_buttons[1] = (dev.raw & WPAD_BUTTON_A) ? 1 : 0;
		break;

	case INPUT_DEV_PRO:
		map_classic(dev.raw, st, u, d, l, r);
		break;

	case INPUT_DEV_GAMECUBE:
	case INPUT_DEV_GCADAPTER:
	case INPUT_DEV_USBPAD:
		map_gamecube(dev.raw, st, u, d, l, r);
		break;

	default:
		map_folded(dev.held, st, u, d, l, r);
		break;
	}

	s32 x = scale_axis(lx);
	s32 y = -scale_axis(ly);
	// the D-pad also drives the main stick while it is centred
	if (!x && !y)
	{
		x = l ? osd::input_device::ABSOLUTE_MIN : r ? osd::input_device::ABSOLUTE_MAX : 0;
		y = u ? osd::input_device::ABSOLUTE_MIN : d ? osd::input_device::ABSOLUTE_MAX : 0;
	}
	st.axis[0] = x;
	st.axis[1] = y;
	st.axis[2] = scale_axis(dev.rx);
	st.axis[3] = -scale_axis(dev.ry);
	st.axis[4] = scale_trigger(dev.lt);
	st.axis[5] = scale_trigger(dev.rt);
	st.buttons[WB_UP] = u ? 1 : 0;
	st.buttons[WB_DOWN] = d ? 1 : 0;
	st.buttons[WB_LEFT] = l ? 1 : 0;
	st.buttons[WB_RIGHT] = r ? 1 : 0;
	std::strncpy(st.name, dev.name ? dev.name : "", sizeof(st.name) - 1);
	st.connected = true;
}

} // anonymous namespace


//============================================================
//  hardware access shared with the rest of the OSD
//============================================================

void wii_input_hw_init()
{
	if (s_hw_initialized)
		return;
	INPUT_Init();
	INPUT_SetScreenSize(640, 480);
	INPUT_SetCursorMode(INPUT_CURSOR_IR | INPUT_CURSOR_MOUSE);
	s_hw_initialized = true;
}

// stop the input library's background threads and release the devices;
// they must not outlive the USB and Bluetooth stacks at exit
void wii_input_hw_shutdown()
{
	if (!s_hw_initialized)
		return;
	INPUT_Shutdown();
	s_hw_initialized = false;
}

// read every controller; called once per emulated frame
void wii_input_poll_hardware()
{
	wii_input_hw_init();
	INPUT_Update();

	for (auto &st : s_pads)
	{
		std::memset(&st, 0, sizeof(st));
		st.axis[4] = st.axis[5] = osd::input_device::ABSOLUTE_MAX;
	}

	int const count = std::min(INPUT_DeviceCount(), WII_MAX_PLAYERS);
	for (int i = 0; i < count; ++i)
	{
		wii_input_device dev;
		if (INPUT_DeviceGet(i, &dev))
			read_device(dev, s_pads[i]);
	}
}

bool wii_input_any_home_pressed()
{
	for (auto const &st : s_pads)
		if (st.buttons[WB_HOME])
			return true;
	return false;
}

bool wii_input_osk_pressed()
{
	for (auto const &st : s_pads)
		if (st.buttons[WB_OSK])
			return true;
	return false;
}

void wii_input_suspend(bool suspend)
{
	s_suspended = suspend;
}


namespace osd {

namespace {

//============================================================
//  joystick device
//============================================================

class wii_joystick_device : public device_info, protected joystick_assignment_helper
{
public:
	wii_joystick_device(std::string &&name, std::string &&id, input_module &module, int index) :
		device_info(std::move(name), std::move(id), module),
		m_index(index)
	{
		reset();
	}

	virtual void poll(bool relative_reset) override
	{
		if (s_suspended)
		{
			reset();
			return;
		}
		wii_pad_state const &st = s_pads[m_index];
		std::copy(std::begin(st.axis), std::end(st.axis), std::begin(m_axes));
		for (int i = 0; i < WB_COUNT; ++i)
			m_buttons[i] = st.buttons[i] ? 0x80 : 0x00;
	}

	virtual void reset() override
	{
		std::fill(std::begin(m_axes), std::end(m_axes), 0);
		m_axes[4] = m_axes[5] = input_device::ABSOLUTE_MAX;
		std::fill(std::begin(m_buttons), std::end(m_buttons), 0);
	}

	virtual void configure(input_device &device) override
	{
		static const char *const axis_names[] = { "Left Stick X", "Left Stick Y", "Right Stick X", "Right Stick Y", "Left Trigger", "Right Trigger" };
		static const input_item_id axis_ids[] = { ITEM_ID_XAXIS, ITEM_ID_YAXIS, ITEM_ID_RXAXIS, ITEM_ID_RYAXIS, ITEM_ID_ZAXIS, ITEM_ID_RZAXIS };
		for (int i = 0; i < 6; ++i)
			device.add_item(axis_names[i], std::string_view(), axis_ids[i], generic_axis_get_state<s32>, &m_axes[i]);

		input_device::assignment_vector assignments;
		input_item_id button_ids[8];
		for (int i = 0; i < 8; ++i)
		{
			button_ids[i] = device.add_item(
					util::string_format("Button %d", i + 1),
					std::string_view(),
					input_item_id(ITEM_ID_BUTTON1 + i),
					generic_button_get_state<u8>,
					&m_buttons[WB_1 + i]);
			assignments.emplace_back(
					ioport_type(IPT_BUTTON1 + i),
					SEQ_TYPE_STANDARD,
					input_seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_NONE, button_ids[i])));
		}

		input_item_id const start = device.add_item("Start", std::string_view(), ITEM_ID_START, generic_button_get_state<u8>, &m_buttons[WB_START]);
		input_item_id const select = device.add_item("Select", std::string_view(), ITEM_ID_SELECT, generic_button_get_state<u8>, &m_buttons[WB_SELECT]);
		input_item_id const home = device.add_item("Home", std::string_view(), ITEM_ID_BUTTON16, generic_button_get_state<u8>, &m_buttons[WB_HOME]);
		input_item_id const osk = device.add_item("Keyboard", std::string_view(), ITEM_ID_BUTTON15, generic_button_get_state<u8>, &m_buttons[WB_OSK]);
		input_item_id const up = device.add_item("D-pad Up", std::string_view(), ITEM_ID_HAT1UP, generic_button_get_state<u8>, &m_buttons[WB_UP]);
		input_item_id const down = device.add_item("D-pad Down", std::string_view(), ITEM_ID_HAT1DOWN, generic_button_get_state<u8>, &m_buttons[WB_DOWN]);
		input_item_id const left = device.add_item("D-pad Left", std::string_view(), ITEM_ID_HAT1LEFT, generic_button_get_state<u8>, &m_buttons[WB_LEFT]);
		input_item_id const right = device.add_item("D-pad Right", std::string_view(), ITEM_ID_HAT1RIGHT, generic_button_get_state<u8>, &m_buttons[WB_RIGHT]);

		add_button_assignment(assignments, IPT_START, { start });
		add_button_assignment(assignments, IPT_SELECT, { select });
		add_button_assignment(assignments, IPT_UI_MENU, { home });
		add_button_assignment(assignments, IPT_OSD_1, { osk });
		add_button_assignment(assignments, IPT_UI_SELECT, { button_ids[0] });
		add_button_assignment(assignments, IPT_UI_BACK, { button_ids[1] });
		add_button_assignment(assignments, IPT_UI_CLEAR, { button_ids[2] });
		add_button_assignment(assignments, IPT_UI_HELP, { button_ids[3] });
		add_button_pair_assignment(assignments, IPT_UI_PREV_GROUP, IPT_UI_NEXT_GROUP, button_ids[4], button_ids[5]);
		add_directional_assignments(assignments, ITEM_ID_XAXIS, ITEM_ID_YAXIS, left, right, up, down);
		add_axis_pair_assignment(assignments, IPT_UI_PAGE_UP, IPT_UI_PAGE_DOWN, ITEM_ID_RYAXIS);

		device.set_default_assignments(std::move(assignments));
	}

private:
	int m_index;
	s32 m_axes[6];
	u8 m_buttons[WB_COUNT];
};


//============================================================
//  light gun device (Wii Remote IR pointer)
//============================================================

class wii_lightgun_device : public device_info
{
public:
	wii_lightgun_device(std::string &&name, std::string &&id, input_module &module, int index) :
		device_info(std::move(name), std::move(id), module),
		m_index(index)
	{
		reset();
	}

	virtual void poll(bool relative_reset) override
	{
		if (s_suspended)
		{
			reset();
			return;
		}
		wii_pad_state const &st = s_pads[m_index];
		if (st.gun_valid)
		{
			m_x = st.gun_x;
			m_y = st.gun_y;
		}
		else
		{
			// pointing away from the screen: off-screen shots reload
			m_x = input_device::ABSOLUTE_MIN;
			m_y = input_device::ABSOLUTE_MIN;
		}
		m_buttons[0] = st.gun_buttons[0] ? 0x80 : 0;
		m_buttons[1] = st.gun_buttons[1] ? 0x80 : 0;
	}

	virtual void reset() override
	{
		m_x = m_y = 0;
		m_buttons[0] = m_buttons[1] = 0;
	}

	virtual void configure(input_device &device) override
	{
		device.add_item("X", std::string_view(), ITEM_ID_XAXIS, generic_axis_get_state<s32>, &m_x);
		device.add_item("Y", std::string_view(), ITEM_ID_YAXIS, generic_axis_get_state<s32>, &m_y);
		device.add_item("B (Trigger)", std::string_view(), ITEM_ID_BUTTON1, generic_button_get_state<u8>, &m_buttons[0]);
		device.add_item("A", std::string_view(), ITEM_ID_BUTTON2, generic_button_get_state<u8>, &m_buttons[1]);
	}

private:
	int m_index;
	s32 m_x, m_y;
	u8 m_buttons[2];
};


//============================================================
//  USB keyboard
//============================================================

struct hid_key { u8 code; input_item_id id; const char *name; };

const hid_key s_hid_keys[] =
{
	{ 0x04, ITEM_ID_A, "A" }, { 0x05, ITEM_ID_B, "B" }, { 0x06, ITEM_ID_C, "C" }, { 0x07, ITEM_ID_D, "D" },
	{ 0x08, ITEM_ID_E, "E" }, { 0x09, ITEM_ID_F, "F" }, { 0x0a, ITEM_ID_G, "G" }, { 0x0b, ITEM_ID_H, "H" },
	{ 0x0c, ITEM_ID_I, "I" }, { 0x0d, ITEM_ID_J, "J" }, { 0x0e, ITEM_ID_K, "K" }, { 0x0f, ITEM_ID_L, "L" },
	{ 0x10, ITEM_ID_M, "M" }, { 0x11, ITEM_ID_N, "N" }, { 0x12, ITEM_ID_O, "O" }, { 0x13, ITEM_ID_P, "P" },
	{ 0x14, ITEM_ID_Q, "Q" }, { 0x15, ITEM_ID_R, "R" }, { 0x16, ITEM_ID_S, "S" }, { 0x17, ITEM_ID_T, "T" },
	{ 0x18, ITEM_ID_U, "U" }, { 0x19, ITEM_ID_V, "V" }, { 0x1a, ITEM_ID_W, "W" }, { 0x1b, ITEM_ID_X, "X" },
	{ 0x1c, ITEM_ID_Y, "Y" }, { 0x1d, ITEM_ID_Z, "Z" },
	{ 0x1e, ITEM_ID_1, "1" }, { 0x1f, ITEM_ID_2, "2" }, { 0x20, ITEM_ID_3, "3" }, { 0x21, ITEM_ID_4, "4" },
	{ 0x22, ITEM_ID_5, "5" }, { 0x23, ITEM_ID_6, "6" }, { 0x24, ITEM_ID_7, "7" }, { 0x25, ITEM_ID_8, "8" },
	{ 0x26, ITEM_ID_9, "9" }, { 0x27, ITEM_ID_0, "0" },
	{ 0x28, ITEM_ID_ENTER, "Enter" }, { 0x29, ITEM_ID_ESC, "Esc" }, { 0x2a, ITEM_ID_BACKSPACE, "Backspace" },
	{ 0x2b, ITEM_ID_TAB, "Tab" }, { 0x2c, ITEM_ID_SPACE, "Space" }, { 0x2d, ITEM_ID_MINUS, "-" },
	{ 0x2e, ITEM_ID_EQUALS, "=" }, { 0x2f, ITEM_ID_OPENBRACE, "[" }, { 0x30, ITEM_ID_CLOSEBRACE, "]" },
	{ 0x31, ITEM_ID_BACKSLASH, "\\" }, { 0x32, ITEM_ID_BACKSLASH2, "#" }, { 0x33, ITEM_ID_COLON, ";" },
	{ 0x34, ITEM_ID_QUOTE, "'" }, { 0x35, ITEM_ID_TILDE, "`" }, { 0x36, ITEM_ID_COMMA, "," },
	{ 0x37, ITEM_ID_STOP, "." }, { 0x38, ITEM_ID_SLASH, "/" }, { 0x39, ITEM_ID_CAPSLOCK, "Caps Lock" },
	{ 0x3a, ITEM_ID_F1, "F1" }, { 0x3b, ITEM_ID_F2, "F2" }, { 0x3c, ITEM_ID_F3, "F3" }, { 0x3d, ITEM_ID_F4, "F4" },
	{ 0x3e, ITEM_ID_F5, "F5" }, { 0x3f, ITEM_ID_F6, "F6" }, { 0x40, ITEM_ID_F7, "F7" }, { 0x41, ITEM_ID_F8, "F8" },
	{ 0x42, ITEM_ID_F9, "F9" }, { 0x43, ITEM_ID_F10, "F10" }, { 0x44, ITEM_ID_F11, "F11" }, { 0x45, ITEM_ID_F12, "F12" },
	{ 0x46, ITEM_ID_PRTSCR, "Print Screen" }, { 0x47, ITEM_ID_SCRLOCK, "Scroll Lock" }, { 0x48, ITEM_ID_PAUSE, "Pause" },
	{ 0x49, ITEM_ID_INSERT, "Insert" }, { 0x4a, ITEM_ID_HOME, "Home" }, { 0x4b, ITEM_ID_PGUP, "Page Up" },
	{ 0x4c, ITEM_ID_DEL, "Delete" }, { 0x4d, ITEM_ID_END, "End" }, { 0x4e, ITEM_ID_PGDN, "Page Down" },
	{ 0x4f, ITEM_ID_RIGHT, "Right" }, { 0x50, ITEM_ID_LEFT, "Left" }, { 0x51, ITEM_ID_DOWN, "Down" },
	{ 0x52, ITEM_ID_UP, "Up" }, { 0x53, ITEM_ID_NUMLOCK, "Num Lock" }, { 0x54, ITEM_ID_SLASH_PAD, "Keypad /" },
	{ 0x55, ITEM_ID_ASTERISK, "Keypad *" }, { 0x56, ITEM_ID_MINUS_PAD, "Keypad -" }, { 0x57, ITEM_ID_PLUS_PAD, "Keypad +" },
	{ 0x58, ITEM_ID_ENTER_PAD, "Keypad Enter" }, { 0x59, ITEM_ID_1_PAD, "Keypad 1" }, { 0x5a, ITEM_ID_2_PAD, "Keypad 2" },
	{ 0x5b, ITEM_ID_3_PAD, "Keypad 3" }, { 0x5c, ITEM_ID_4_PAD, "Keypad 4" }, { 0x5d, ITEM_ID_5_PAD, "Keypad 5" },
	{ 0x5e, ITEM_ID_6_PAD, "Keypad 6" }, { 0x5f, ITEM_ID_7_PAD, "Keypad 7" }, { 0x60, ITEM_ID_8_PAD, "Keypad 8" },
	{ 0x61, ITEM_ID_9_PAD, "Keypad 9" }, { 0x62, ITEM_ID_0_PAD, "Keypad 0" }, { 0x63, ITEM_ID_DEL_PAD, "Keypad ." },
	{ 0x64, ITEM_ID_BACKSLASH2, "\\ (ISO)" }, { 0x65, ITEM_ID_MENU, "Menu" }, { 0x67, ITEM_ID_EQUALS_PAD, "Keypad =" },
	{ 0x68, ITEM_ID_F13, "F13" }, { 0x69, ITEM_ID_F14, "F14" }, { 0x6a, ITEM_ID_F15, "F15" },
	{ 0x85, ITEM_ID_COMMA_PAD, "Keypad ," },
	{ 0xe0, ITEM_ID_LCONTROL, "Left Ctrl" }, { 0xe1, ITEM_ID_LSHIFT, "Left Shift" }, { 0xe2, ITEM_ID_LALT, "Left Alt" },
	{ 0xe3, ITEM_ID_LWIN, "Left Win" }, { 0xe4, ITEM_ID_RCONTROL, "Right Ctrl" }, { 0xe5, ITEM_ID_RSHIFT, "Right Shift" },
	{ 0xe6, ITEM_ID_RALT, "Right Alt" }, { 0xe7, ITEM_ID_RWIN, "Right Win" },
};

class wii_keyboard_device : public device_info
{
public:
	wii_keyboard_device(std::string &&name, std::string &&id, input_module &module) :
		device_info(std::move(name), std::move(id), module)
	{
		reset();
	}

	virtual void poll(bool relative_reset) override
	{
		if (s_suspended)
		{
			reset();
			return;
		}
		for (size_t i = 0; i < std::size(s_hid_keys); ++i)
			m_keys[i] = INPUT_KeyHeld(s_hid_keys[i].code) ? 0x80 : 0x00;
	}

	virtual void reset() override
	{
		std::fill(std::begin(m_keys), std::end(m_keys), 0);
	}

	virtual void configure(input_device &device) override
	{
		for (size_t i = 0; i < std::size(s_hid_keys); ++i)
		{
			// two HID codes share BACKSLASH2; only add each item once
			bool dup = false;
			for (size_t j = 0; j < i; ++j)
				if (s_hid_keys[j].id == s_hid_keys[i].id)
					dup = true;
			if (!dup)
				device.add_item(s_hid_keys[i].name, std::string_view(), s_hid_keys[i].id, generic_button_get_state<u8>, &m_keys[i]);
		}
	}

private:
	u8 m_keys[std::size(s_hid_keys)];
};


//============================================================
//  USB mouse
//============================================================

class wii_mouse_device : public device_info
{
public:
	wii_mouse_device(std::string &&name, std::string &&id, input_module &module) :
		device_info(std::move(name), std::move(id), module)
	{
		reset();
	}

	virtual void poll(bool relative_reset) override
	{
		if (s_suspended || !INPUT_MouseConnected())
		{
			reset();
			return;
		}
		int dx = 0, dy = 0, wheel = 0;
		INPUT_MouseDelta(&dx, &dy, &wheel);
		if (relative_reset)
		{
			m_x = dx * input_device::RELATIVE_PER_PIXEL;
			m_y = dy * input_device::RELATIVE_PER_PIXEL;
			m_z = wheel * input_device::RELATIVE_PER_PIXEL;
		}
		else
		{
			m_x += dx * input_device::RELATIVE_PER_PIXEL;
			m_y += dy * input_device::RELATIVE_PER_PIXEL;
			m_z += wheel * input_device::RELATIVE_PER_PIXEL;
		}
		u8 const b = INPUT_MouseButtonsHeld();
		m_buttons[0] = (b & INPUT_MOUSE_LEFT) ? 0x80 : 0;
		m_buttons[1] = (b & INPUT_MOUSE_RIGHT) ? 0x80 : 0;
		m_buttons[2] = (b & INPUT_MOUSE_MIDDLE) ? 0x80 : 0;
	}

	virtual void reset() override
	{
		m_x = m_y = m_z = 0;
		m_buttons[0] = m_buttons[1] = m_buttons[2] = 0;
	}

	virtual void configure(input_device &device) override
	{
		device.add_item("X", std::string_view(), ITEM_ID_XAXIS, generic_axis_get_state<s32>, &m_x);
		device.add_item("Y", std::string_view(), ITEM_ID_YAXIS, generic_axis_get_state<s32>, &m_y);
		device.add_item("Wheel", std::string_view(), ITEM_ID_ZAXIS, generic_axis_get_state<s32>, &m_z);
		device.add_item("Left", std::string_view(), ITEM_ID_BUTTON1, generic_button_get_state<u8>, &m_buttons[0]);
		device.add_item("Right", std::string_view(), ITEM_ID_BUTTON2, generic_button_get_state<u8>, &m_buttons[1]);
		device.add_item("Middle", std::string_view(), ITEM_ID_BUTTON3, generic_button_get_state<u8>, &m_buttons[2]);
	}

private:
	s32 m_x, m_y, m_z;
	u8 m_buttons[3];
};


//============================================================
//  modules
//============================================================

template <typename Device, input_device_class Class>
class wii_input_module : public input_module_impl<device_info, wii_osd_interface>
{
public:
	wii_input_module(const char *type, int count, const char *name_fmt, const char *id_fmt) :
		input_module_impl<device_info, wii_osd_interface>(type, "wii"),
		m_count(count), m_name_fmt(name_fmt), m_id_fmt(id_fmt)
	{
	}

	virtual void input_init(running_machine &machine) override
	{
		input_module_impl<device_info, wii_osd_interface>::input_init(machine);
		wii_input_hw_init();
		for (int i = 0; i < m_count; ++i)
			create_device_for(i);
	}

private:
	void create_device_for(int i)
	{
		if constexpr (std::is_constructible_v<Device, std::string &&, std::string &&, input_module &, int>)
			create_device<Device>(Class, util::string_format(m_name_fmt, i + 1), util::string_format(m_id_fmt, i), i);
		else
			create_device<Device>(Class, util::string_format(m_name_fmt, i + 1), util::string_format(m_id_fmt, i));
	}

	int m_count;
	const char *m_name_fmt;
	const char *m_id_fmt;
};

class wii_joystick_module : public wii_input_module<wii_joystick_device, DEVICE_CLASS_JOYSTICK>
{
public:
	wii_joystick_module() : wii_input_module(OSD_JOYSTICKINPUT_PROVIDER, WII_MAX_PLAYERS, "Wii Controller %d", "wii_pad_%d") { }
};

class wii_lightgun_module : public wii_input_module<wii_lightgun_device, DEVICE_CLASS_LIGHTGUN>
{
public:
	wii_lightgun_module() : wii_input_module(OSD_LIGHTGUNINPUT_PROVIDER, WII_MAX_PLAYERS, "Wii Remote %d IR", "wii_gun_%d") { }
};

class wii_keyboard_module : public wii_input_module<wii_keyboard_device, DEVICE_CLASS_KEYBOARD>
{
public:
	wii_keyboard_module() : wii_input_module(OSD_KEYBOARDINPUT_PROVIDER, 1, "USB Keyboard %d", "wii_kbd_%d") { }
};

class wii_mouse_module : public wii_input_module<wii_mouse_device, DEVICE_CLASS_MOUSE>
{
public:
	wii_mouse_module() : wii_input_module(OSD_MOUSEINPUT_PROVIDER, 1, "USB Mouse %d", "wii_mouse_%d") { }
};

} // anonymous namespace

} // namespace osd

#else // defined(OSD_WII)

namespace osd {
namespace {
MODULE_NOT_SUPPORTED(wii_joystick_module, OSD_JOYSTICKINPUT_PROVIDER, "wii")
MODULE_NOT_SUPPORTED(wii_lightgun_module, OSD_LIGHTGUNINPUT_PROVIDER, "wii")
MODULE_NOT_SUPPORTED(wii_keyboard_module, OSD_KEYBOARDINPUT_PROVIDER, "wii")
MODULE_NOT_SUPPORTED(wii_mouse_module, OSD_MOUSEINPUT_PROVIDER, "wii")
} // anonymous namespace
} // namespace osd

#endif // defined(OSD_WII)

MODULE_DEFINITION(JOYSTICKINPUT_WII, osd::wii_joystick_module)
MODULE_DEFINITION(LIGHTGUNINPUT_WII, osd::wii_lightgun_module)
MODULE_DEFINITION(KEYBOARDINPUT_WII, osd::wii_keyboard_module)
MODULE_DEFINITION(MOUSEINPUT_WII, osd::wii_mouse_module)
