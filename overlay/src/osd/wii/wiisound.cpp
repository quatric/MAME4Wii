// license:BSD-3-Clause
// copyright-holders:MAMEdev Team, MAME4Wii contributors
//============================================================
//
//  wiisound.cpp - Wii audio interface (AI DMA, 48 kHz stereo)
//
//============================================================

#include "modules/sound/sound_module.h"
#include "modules/osdmodule.h"

#if defined(OSD_WII)

#include "modules/lib/osdobj_common.h"
#include "osdcore.h"

#include <cstring>
#include <memory>
#include <mutex>

#include <gccore.h>
#include <malloc.h>
#include <ogc/audio.h>
#include <ogc/lwp.h>
#include <ogc/semaphore.h>


namespace osd {

namespace {

constexpr uint32_t DMA_TOTAL_BYTES = 3 * 512 * 2 * 2;

} // anonymous namespace

} // namespace osd

// DMA buffers for the audio interface; allocated once at startup (from
// MEM1, before the heap grows into MEM2) and reused for every machine
void *wii_audio_dma_memory()
{
	static void *s_memory = nullptr;
	if (!s_memory)
		s_memory = memalign(32, osd::DMA_TOTAL_BYTES);
	return s_memory;
}

namespace osd {

namespace {

constexpr uint32_t WII_SAMPLE_RATE = 48000;
constexpr uint32_t WII_CHANNELS = 2;
constexpr uint32_t DMA_FRAMES = 512;                         // ~10.7 ms per buffer
constexpr uint32_t DMA_BYTES = DMA_FRAMES * WII_CHANNELS * 2; // multiple of 32
constexpr int DMA_BUFFERS = 3;
static_assert(DMA_BUFFERS * DMA_BYTES == DMA_TOTAL_BYTES);

class sound_wii : public osd_module, public sound_module
{
public:
	sound_wii() : osd_module(OSD_SOUND_PROVIDER, "wii"), sound_module() { }
	virtual ~sound_wii() { }

	virtual int init(osd_interface &osd, const osd_options &options) override;
	virtual void exit() override;

	virtual uint32_t get_generation() override { return 1; }
	virtual osd::audio_info get_information() override;
	virtual uint32_t stream_sink_open(uint32_t node, std::string name, uint32_t rate) override;
	virtual void stream_close(uint32_t id) override;
	virtual void stream_sink_update(uint32_t id, const int16_t *buffer, int samples_this_frame) override;

private:
	static void dma_callback();
	static void *thread_entry(void *arg);
	void fill_buffer(int index);

	static sound_wii *s_instance;

	std::unique_ptr<abuffer> m_buffer;
	std::mutex m_mutex;
	uint32_t m_stream_id = 0;
	float m_latency = 0.0f;

	int16_t (*m_dma)[DMA_FRAMES * WII_CHANNELS] = nullptr;
	volatile int m_playing = 0;
	volatile bool m_stop = false;
	lwp_t m_thread = LWP_THREAD_NULL;
	sem_t m_sem = LWP_SEM_NULL;
	bool m_running = false;
};

sound_wii *sound_wii::s_instance = nullptr;


int sound_wii::init(osd_interface &osd, const osd_options &options)
{
	m_latency = options.audio_latency();
	m_dma = reinterpret_cast<int16_t (*)[DMA_FRAMES * WII_CHANNELS]>(wii_audio_dma_memory());
	std::memset(m_dma, 0, DMA_BUFFERS * DMA_BYTES);

	AUDIO_Init(nullptr);
	AUDIO_SetDSPSampleRate(AI_SAMPLERATE_48KHZ);
	AUDIO_RegisterDMACallback(dma_callback);

	s_instance = this;
	m_stop = false;
	LWP_SemInit(&m_sem, 0, DMA_BUFFERS);
	if (LWP_CreateThread(&m_thread, thread_entry, this, nullptr, 16 * 1024, 80) < 0)
	{
		osd_printf_error("Audio: could not create mixer thread\n");
		return -1;
	}

	// start the DMA engine on silence; the callback keeps it running
	m_playing = 0;
	DCFlushRange(m_dma, DMA_BUFFERS * DMA_BYTES);
	AUDIO_InitDMA(reinterpret_cast<u32>(m_dma[0]), DMA_BYTES);
	AUDIO_StartDMA();
	m_running = true;
	return 0;
}


void sound_wii::exit()
{
	if (m_running)
	{
		AUDIO_StopDMA();
		AUDIO_RegisterDMACallback(nullptr);
		m_stop = true;
		LWP_SemPost(m_sem);
		LWP_JoinThread(m_thread, nullptr);
		LWP_SemDestroy(m_sem);
		m_thread = LWP_THREAD_NULL;
		m_running = false;
	}
	s_instance = nullptr;
	std::lock_guard<std::mutex> lock(m_mutex);
	m_buffer.reset();
}


osd::audio_info sound_wii::get_information()
{
	osd::audio_info result;
	result.m_generation = 1;
	result.m_default_sink = 1;
	result.m_default_source = 0;
	result.m_nodes.resize(1);
	auto &node = result.m_nodes[0];
	node.m_name = "Wii";
	node.m_display_name = "Wii audio output";
	node.m_id = 1;
	node.m_rate = audio_rate_range{ WII_SAMPLE_RATE, WII_SAMPLE_RATE, WII_SAMPLE_RATE };
	node.m_sinks = WII_CHANNELS;
	node.m_sources = 0;
	node.m_port_names.push_back("FL");
	node.m_port_names.push_back("FR");
	node.m_port_positions.push_back(osd::channel_position::FL());
	node.m_port_positions.push_back(osd::channel_position::FR());
	return result;
}


uint32_t sound_wii::stream_sink_open(uint32_t node, std::string name, uint32_t rate)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_buffer = std::make_unique<abuffer>(WII_CHANNELS, rate);
	if (m_latency > 0.0f)
		m_buffer->set_latency(m_latency);
	return ++m_stream_id;
}


void sound_wii::stream_close(uint32_t id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (id == m_stream_id)
		m_buffer.reset();
}


void sound_wii::stream_sink_update(uint32_t id, const int16_t *buffer, int samples_this_frame)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_buffer && (id == m_stream_id))
		m_buffer->push(buffer, samples_this_frame);
}


void sound_wii::fill_buffer(int index)
{
	int16_t *const dst = m_dma[index];
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_buffer)
			m_buffer->get(dst, DMA_FRAMES);
		else
			std::memset(dst, 0, DMA_BYTES);
	}
	// the AI expects right/left ordered sample pairs
	uint32_t *const pairs = reinterpret_cast<uint32_t *>(dst);
	for (uint32_t i = 0; i < DMA_FRAMES; ++i)
		pairs[i] = (pairs[i] >> 16) | (pairs[i] << 16);
	DCFlushRange(dst, DMA_BYTES);
}


// called from the AI interrupt when the current DMA block has been latched
void sound_wii::dma_callback()
{
	sound_wii *const self = s_instance;
	if (!self)
		return;
	int const next = (self->m_playing + 1) % DMA_BUFFERS;
	self->m_playing = next;
	AUDIO_InitDMA(reinterpret_cast<u32>(self->m_dma[next]), DMA_BYTES);
	LWP_SemPost(self->m_sem);
}


void *sound_wii::thread_entry(void *arg)
{
	sound_wii *const self = reinterpret_cast<sound_wii *>(arg);
	while (true)
	{
		LWP_SemWait(self->m_sem);
		if (self->m_stop)
			break;
		// refill the buffer that will be queued after the one now playing
		int const target = (self->m_playing + 1) % DMA_BUFFERS;
		self->fill_buffer(target);
	}
	return nullptr;
}

} // anonymous namespace

} // namespace osd

#else // defined(OSD_WII)

namespace osd { namespace { MODULE_NOT_SUPPORTED(sound_wii, OSD_SOUND_PROVIDER, "wii") } }

#endif // defined(OSD_WII)

MODULE_DEFINITION(SOUND_WII, osd::sound_wii)
