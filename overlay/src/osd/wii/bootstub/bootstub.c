// license:BSD-3-Clause
// copyright-holders:MAME4Wii contributors
//============================================================
//
//  bootstub.c - MEM1 boot stub for MAME4Wii
//
//  The MAME executable is far larger than the executable area
//  that loaders such as the Homebrew Channel accept (MEM1), so
//  it is linked to run from MEM2 instead.  This stub is a normal
//  MEM1 DOL carrying a zlib-compressed copy of that image; it
//  inflates the image into MEM2, hands over the loader's
//  arguments, shuts the system down and jumps to it.
//
//============================================================

#include <gccore.h>
#include <ogc/irq.h>
#include <ogc/system.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>


// payload header written by pack.py (big-endian, like the CPU)
#define PAYLOAD_MAGIC 0x4d345750 // 'M4WP'
#define PAYLOAD_MAX_SECTIONS 18

typedef struct
{
	u32 magic;
	u32 entry;
	u32 bss_addr;
	u32 bss_size;
	u32 count;
	struct { u32 addr, size; } section[PAYLOAD_MAX_SECTIONS];
} payload_header;

// MEM2 range the payload may occupy (below the IOS IPC buffer)
#define MEM2_LO 0x90000800
#define MEM2_HI 0x933e0000
// top of MEM1; the payload's writable data lives just below it
#define MEM1_HI 0x81800000

// end of this stub's own image (its small heap follows)
extern u8 __bss_end[];

extern const u8 payload_z[];
extern const u32 payload_z_size;

static void *xfb;


static void fail(const char *message)
{
	if (!xfb)
	{
		VIDEO_Init();
		GXRModeObj *const mode = VIDEO_GetPreferredMode(NULL);
		xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
		CON_Init(xfb, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
		VIDEO_Configure(mode);
		VIDEO_SetNextFramebuffer(xfb);
		VIDEO_SetBlack(false);
		VIDEO_Flush();
		VIDEO_WaitVSync();
	}
	printf("\x1b[2;0HMAME4Wii boot stub\n\n%s\n\nReturning to the loader in 10 seconds...\n", message);
	for (int i = 0; i < 600; i++)
		VIDEO_WaitVSync();
	exit(1);
}


static int inflate_to(z_stream *zs, void *dst, u32 size)
{
	zs->next_out = (Bytef *)dst;
	zs->avail_out = size;
	while (zs->avail_out)
	{
		int const ret = inflate(zs, Z_SYNC_FLUSH);
		if ((ret != Z_OK) && !((ret == Z_STREAM_END) && !zs->avail_out))
			return 0;
	}
	return 1;
}


// the payload may only be unpacked to MEM2, or to the top of MEM1 well
// clear of this stub (which keeps running until the jump)
static int placeable(u32 addr, u32 size)
{
	u32 const mem1_lo = ((u32)__bss_end + 0x100000) & ~0xfffff;
	if ((addr >= MEM2_LO) && (addr < MEM2_HI))
		return size <= MEM2_HI - addr;
	if ((addr >= mem1_lo) && (addr < MEM1_HI))
		return size <= MEM1_HI - addr;
	return 0;
}


int main(int argc, char *argv[])
{
	payload_header hdr;
	z_stream zs;
	memset(&zs, 0, sizeof(zs));
	zs.next_in = (Bytef *)payload_z;
	zs.avail_in = payload_z_size;
	if (inflateInit(&zs) != Z_OK)
		fail("zlib initialisation failed.");

	// header, then every section's data in order
	if (!inflate_to(&zs, &hdr, sizeof(hdr)) || (hdr.magic != PAYLOAD_MAGIC) || (hdr.count > PAYLOAD_MAX_SECTIONS))
		fail("The embedded MAME image is damaged.");
	for (u32 i = 0; i < hdr.count; i++)
	{
		u32 const addr = hdr.section[i].addr, size = hdr.section[i].size;
		if (!placeable(addr, size))
			fail("The embedded MAME image does not fit in memory.");
		if (!inflate_to(&zs, (void *)addr, size))
			fail("The embedded MAME image is damaged.");
		DCFlushRange((void *)addr, size);
		ICInvalidateRange((void *)addr, size);
	}
	inflateEnd(&zs);
	if (!placeable(hdr.bss_addr, hdr.bss_size) || !placeable(hdr.entry, 0x20))
		fail("The embedded MAME image does not fit in memory.");
	memset((void *)hdr.bss_addr, 0, hdr.bss_size);
	DCFlushRange((void *)hdr.bss_addr, hdr.bss_size);

	// Pass our arguments on.  The command line goes just past the
	// payload's .bss at the top of MEM1, which its heap never uses; its
	// startup code turns it into argc/argv before main() runs.
	struct __argv *const target = (struct __argv *)(hdr.entry + 8);
	memset(target, 0, sizeof(*target));
	if (argc > 0)
	{
		char *const cmdline = (char *)((hdr.bss_addr + hdr.bss_size + 0x3f) & ~0x1f);
		u32 length = 0;
		for (int i = 0; i < argc; i++)
		{
			u32 const len = strlen(argv[i]) + 1;
			// leave room for the argv[] array built after the strings
			if ((u32)cmdline + length + len + 0x400 > MEM1_HI)
				break;
			memcpy(cmdline + length, argv[i], len);
			length += len;
		}
		DCFlushRange(cmdline, length);
		target->argvMagic = ARGV_MAGIC;
		target->commandLine = cmdline;
		target->length = length;
	}
	DCFlushRange(target, sizeof(*target));
	ICInvalidateRange((void *)hdr.entry, 0x40);

	if (xfb)
		VIDEO_SetBlack(true);

	void (*const entry)(void) = (void (*)(void))hdr.entry;
	SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
	IRQ_Disable();
	entry();
	return 0;
}
