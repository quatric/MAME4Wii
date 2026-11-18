// license:BSD-3-Clause
// copyright-holders:MAMEdev Team, MAME4Wii contributors
//============================================================
//
//  wiivideo.cpp - GX video output
//
//  MAME's render primitives are rasterised in software into
//  an RGB565 buffer at (close to) native resolution, which is
//  then uploaded as a GX texture and scaled to the TV by the
//  GPU.
//
//============================================================

#include "emu.h"
#include "render.h"
#include "rendersw.hxx"

#include "osdwii.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <gccore.h>
#include <malloc.h>


namespace {

constexpr u32 GX_FIFO_SIZE = 256 * 1024;
constexpr int MAX_TEX_DIM = 1024;

GXRModeObj *s_vmode = nullptr;
void *s_xfb[2] = { nullptr, nullptr };
int s_curfb = 0;
bool s_hw_ready = false;
bool s_gx_ready = false;
void *s_gp_fifo = nullptr;

u16 *s_linear = nullptr;     // software render target (RGB565, linear)
u16 *s_texture = nullptr;    // GX tiled RGB565 copy
int s_buf_w = 0;             // allocated dimensions (multiples of 4)
int s_buf_h = 0;
GXTexObj s_texobj;


void gx_setup()
{
	if (s_gx_ready)
		return;

	if (!s_gp_fifo)
		s_gp_fifo = memalign(32, GX_FIFO_SIZE);
	std::memset(s_gp_fifo, 0, GX_FIFO_SIZE);
	GX_Init(s_gp_fifo, GX_FIFO_SIZE);

	GXColor background = { 0, 0, 0, 0xff };
	GX_SetCopyClear(background, GX_MAX_Z24);

	GX_SetViewport(0, 0, s_vmode->fbWidth, s_vmode->efbHeight, 0, 1);
	f32 const yscale = GX_GetYScaleFactor(s_vmode->efbHeight, s_vmode->xfbHeight);
	u32 const xfbHeight = GX_SetDispCopyYScale(yscale);
	GX_SetScissor(0, 0, s_vmode->fbWidth, s_vmode->efbHeight);
	GX_SetDispCopySrc(0, 0, s_vmode->fbWidth, s_vmode->efbHeight);
	GX_SetDispCopyDst(s_vmode->fbWidth, xfbHeight);
	GX_SetCopyFilter(s_vmode->aa, s_vmode->sample_pattern, GX_TRUE, s_vmode->vfilter);
	GX_SetFieldMode(s_vmode->field_rendering, ((s_vmode->viHeight == 2 * s_vmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));

	if (s_vmode->aa)
		GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
	else
		GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

	GX_SetCullMode(GX_CULL_NONE);
	GX_SetDispCopyGamma(GX_GM_1_0);

	GX_InvVtxCache();
	GX_InvalidateTexAll();

	s_gx_ready = true;
}


// render state for the textured full-screen quad; re-applied every frame
// because overlays (the on-screen keyboard) clobber it
void gx_set_state()
{
	GX_SetViewport(0, 0, s_vmode->fbWidth, s_vmode->efbHeight, 0, 1);
	GX_SetScissor(0, 0, s_vmode->fbWidth, s_vmode->efbHeight);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);

	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetAlphaUpdate(GX_TRUE);
	GX_SetColorUpdate(GX_TRUE);

	Mtx44 proj;
	guOrtho(proj, 0, s_vmode->efbHeight, 0, s_vmode->fbWidth, 0, 1000.0f);
	GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);

	Mtx mv;
	guMtxIdentity(mv);
	guMtxTransApply(mv, mv, 0.0f, 0.0f, -50.0f);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetNumIndStages(0);
	GX_SetTevDirect(GX_TEVSTAGE0);
}


bool ensure_buffers(int width, int height)
{
	int const w = (width + 3) & ~3;
	int const h = (height + 3) & ~3;
	if (w <= s_buf_w && h <= s_buf_h && s_linear && s_texture)
		return true;

	free(s_linear);
	free(s_texture);
	s_linear = static_cast<u16 *>(memalign(32, w * h * 2));
	s_texture = static_cast<u16 *>(memalign(32, w * h * 2));
	if (!s_linear || !s_texture)
	{
		free(s_linear);
		free(s_texture);
		s_linear = s_texture = nullptr;
		s_buf_w = s_buf_h = 0;
		return false;
	}
	s_buf_w = w;
	s_buf_h = h;
	return true;
}


// convert a linear RGB565 image into GX 4x4 tiles
void tile_rgb565(const u16 *src, u16 *dst, int width, int height, int pitch)
{
	u64 *out = reinterpret_cast<u64 *>(dst);
	for (int ty = 0; ty < height; ty += 4)
	{
		const u16 *const row0 = src + ty * pitch;
		for (int tx = 0; tx < width; tx += 4)
		{
			const u16 *p = row0 + tx;
			out[0] = *reinterpret_cast<const u64 *>(p);
			out[1] = *reinterpret_cast<const u64 *>(p + pitch);
			out[2] = *reinterpret_cast<const u64 *>(p + 2 * pitch);
			out[3] = *reinterpret_cast<const u64 *>(p + 3 * pitch);
			out += 4;
		}
	}
}

} // anonymous namespace


//============================================================
//  hardware bring-up (also used for the boot console)
//============================================================

void wii_video_hw_init()
{
	if (s_hw_ready)
		return;

	VIDEO_Init();
	s_vmode = VIDEO_GetPreferredMode(nullptr);

	VIDEO_Configure(s_vmode);
	s_xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(s_vmode));
	s_xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(s_vmode));
	VIDEO_ClearFrameBuffer(s_vmode, s_xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer(s_vmode, s_xfb[1], COLOR_BLACK);

	CON_Init(s_xfb[0], 20, 20, s_vmode->fbWidth, s_vmode->xfbHeight, s_vmode->fbWidth * VI_DISPLAY_PIX_SZ);

	VIDEO_SetNextFramebuffer(s_xfb[0]);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (s_vmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();

	// allocate the GPU FIFO now, while the heap is still in MEM1
	s_gp_fifo = memalign(32, GX_FIFO_SIZE);

	s_curfb = 0;
	s_hw_ready = true;
}


void wii_video_hw_shutdown()
{
	if (!s_hw_ready)
		return;
	if (s_gx_ready)
	{
		GX_AbortFrame();
		GX_Flush();
	}
	VIDEO_SetBlack(true);
	VIDEO_Flush();
	VIDEO_WaitVSync();
}


bool wii_video_is_widescreen()
{
	return CONF_GetAspectRatio() == CONF_ASPECT_16_9;
}


//============================================================
//  wii_video_present
//============================================================

void wii_video_present(render_primitive_list &primlist, int width, int height, void (*overlay)(void *), void *param)
{
	if (!s_hw_ready)
		wii_video_hw_init();
	gx_setup();

	width = std::clamp(width, 4, MAX_TEX_DIM);
	height = std::clamp(height, 4, MAX_TEX_DIM);
	if (!ensure_buffers(width, height))
		return;

	int const tw = (width + 3) & ~3;
	int const th = (height + 3) & ~3;

	// rasterise MAME's primitives
	primlist.acquire_lock();
	software_renderer<u16, 3,2,3, 11,5,0>::draw_primitives(primlist, s_linear, width, height, s_buf_w);
	primlist.release_lock();

	// clear padding so filtering at the edges stays black
	if (tw != width)
		for (int y = 0; y < height; ++y)
			std::memset(s_linear + y * s_buf_w + width, 0, (tw - width) * 2);
	if (th != height)
		std::memset(s_linear + height * s_buf_w, 0, (th - height) * s_buf_w * 2);

	tile_rgb565(s_linear, s_texture, tw, th, s_buf_w);
	DCFlushRange(s_texture, tw * th * 2);
	GX_InvalidateTexAll();

	extern bool wii_video_filter_enabled();
	u8 const filt = wii_video_filter_enabled() ? GX_LINEAR : GX_NEAR;
	GX_InitTexObj(&s_texobj, s_texture, tw, th, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&s_texobj, filt, filt, 0.0f, 0.0f, 0.0f, GX_DISABLE, GX_DISABLE, GX_ANISO_1);

	extern float wii_video_safe_area();
	float const safe = std::clamp(wii_video_safe_area(), 0.5f, 1.0f);
	float const fbw = s_vmode->fbWidth;
	float const fbh = s_vmode->efbHeight;
	float const hofs = (fbw - fbw * safe) * 0.5f;
	float const vofs = (fbh - fbh * safe) * 0.5f;
	float const x0 = hofs, x1 = fbw - hofs;
	float const y0 = vofs, y1 = fbh - vofs;
	float const s1 = float(width) / float(tw);
	float const t1 = float(height) / float(th);

	gx_set_state();
	GX_LoadTexObj(&s_texobj, GX_TEXMAP0);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	GX_Position3f32(x0, y0, 0.0f); GX_Color4u8(0xff, 0xff, 0xff, 0xff); GX_TexCoord2f32(0.0f, 0.0f);
	GX_Position3f32(x1, y0, 0.0f); GX_Color4u8(0xff, 0xff, 0xff, 0xff); GX_TexCoord2f32(s1, 0.0f);
	GX_Position3f32(x1, y1, 0.0f); GX_Color4u8(0xff, 0xff, 0xff, 0xff); GX_TexCoord2f32(s1, t1);
	GX_Position3f32(x0, y1, 0.0f); GX_Color4u8(0xff, 0xff, 0xff, 0xff); GX_TexCoord2f32(0.0f, t1);
	GX_End();

	if (overlay)
		overlay(param);

	s_curfb ^= 1;
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetColorUpdate(GX_TRUE);
	GX_CopyDisp(s_xfb[s_curfb], GX_TRUE);
	GX_DrawDone();

	VIDEO_SetNextFramebuffer(s_xfb[s_curfb]);
	VIDEO_Flush();

	extern bool wii_video_wait_vsync();
	if (wii_video_wait_vsync())
		VIDEO_WaitVSync();
}


//============================================================
//  console helpers used for fatal error reporting
//============================================================

void wii_video_show_console()
{
	if (!s_hw_ready)
	{
		wii_video_hw_init();
		return;
	}
	// point the console back at the visible buffer
	VIDEO_ClearFrameBuffer(s_vmode, s_xfb[0], COLOR_BLACK);
	CON_Init(s_xfb[0], 20, 20, s_vmode->fbWidth, s_vmode->xfbHeight, s_vmode->fbWidth * VI_DISPLAY_PIX_SZ);
	VIDEO_SetNextFramebuffer(s_xfb[0]);
	VIDEO_Flush();
	VIDEO_WaitVSync();
}


GXRModeObj *wii_video_mode()
{
	return s_vmode;
}
