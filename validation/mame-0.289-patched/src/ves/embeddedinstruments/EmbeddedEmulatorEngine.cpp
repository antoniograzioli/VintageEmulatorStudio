// license:BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
// copyright-holders:Vintage Emulator Studio contributors

#include "EmbeddedEmulatorEngine.h"

#include "emu.h"

#include "drivenum.h"
#include "emuopts.h"
#include "main.h"
#include "rendlay.h"
#include "render.h"
#include "rendersw.hxx"
#include "modules/lib/osdobj_common.h"
#include "ui/uimain.h"
#include "uiinput.h"
#include "fileio.h"

#include "frontend/mame/audit.h"
#include "frontend/mame/luaengine.h"
#include "frontend/mame/pluginopts.h"

#include "bus/midi/midi.h"
#include "imagedev/midiin.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <functional>
#include <fstream>
#include <filesystem>
#include <memory>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#endif

GAME_EXTERN(ap10);
GAME_EXTERN(cd3000i);
GAME_EXTERN(cd3000xl);
GAME_EXTERN(mpc60);
GAME_EXTERN(mpc3000);
GAME_EXTERN(ct8000);
GAME_EXTERN(ctfk1);
GAME_EXTERN(cz1);
GAME_EXTERN(cz101);
GAME_EXTERN(cz230s);
GAME_EXTERN(dd9);
GAME_EXTERN(dx100);
GAME_EXTERN(esq1);
GAME_EXTERN(fb01);
GAME_EXTERN(fatman);
GAME_EXTERN(linndrum);
GAME_EXTERN(mu2000);
GAME_EXTERN(mu50);
GAME_EXTERN(obdmx);
GAME_EXTERN(psr60);
GAME_EXTERN(psr11);
GAME_EXTERN(psr70);
GAME_EXTERN(psr75);
GAME_EXTERN(psr76);
GAME_EXTERN(psr110);
GAME_EXTERN(psr150);
GAME_EXTERN(psr180);
GAME_EXTERN(pss6);
GAME_EXTERN(pss11);
GAME_EXTERN(pss12);
GAME_EXTERN(pss21);
GAME_EXTERN(pss31);
GAME_EXTERN(prophet5r30);
GAME_EXTERN(rz1);
GAME_EXTERN(s2000);
GAME_EXTERN(s3000);
GAME_EXTERN(s3000xl);
GAME_EXTERN(sd132);
GAME_EXTERN(sixtrak);
GAME_EXTERN(tr707);
GAME_EXTERN(tr727);
GAME_EXTERN(tg100);
GAME_EXTERN(tx81z);
GAME_EXTERN(vfx);
GAME_EXTERN(vfxsd);

const game_driver *const driver_list::s_drivers_sorted[] =
{
	&GAME_NAME(___empty),
	&GAME_NAME(ap10),
	&GAME_NAME(cd3000i),
	&GAME_NAME(cd3000xl),
	&GAME_NAME(ct8000),
	&GAME_NAME(ctfk1),
	&GAME_NAME(cz1),
	&GAME_NAME(cz101),
	&GAME_NAME(cz230s),
	&GAME_NAME(dd9),
	&GAME_NAME(dx100),
	&GAME_NAME(esq1),
	&GAME_NAME(fatman),
	&GAME_NAME(fb01),
	&GAME_NAME(linndrum),
	&GAME_NAME(mpc3000),
	&GAME_NAME(mpc60),
	&GAME_NAME(mu2000),
	&GAME_NAME(mu50),
	&GAME_NAME(obdmx),
	&GAME_NAME(prophet5r30),
	&GAME_NAME(psr11),
	&GAME_NAME(psr110),
	&GAME_NAME(psr150),
	&GAME_NAME(psr180),
	&GAME_NAME(psr60),
	&GAME_NAME(psr70),
	&GAME_NAME(psr75),
	&GAME_NAME(psr76),
	&GAME_NAME(pss11),
	&GAME_NAME(pss12),
	&GAME_NAME(pss21),
	&GAME_NAME(pss31),
	&GAME_NAME(pss6),
	&GAME_NAME(rz1),
	&GAME_NAME(s2000),
	&GAME_NAME(s3000),
	&GAME_NAME(s3000xl),
	&GAME_NAME(sd132),
	&GAME_NAME(sixtrak),
	&GAME_NAME(tg100),
	&GAME_NAME(tr707),
	&GAME_NAME(tr727),
	&GAME_NAME(tx81z),
	&GAME_NAME(vfx),
	&GAME_NAME(vfxsd),
};

std::size_t const driver_list::s_driver_count = std::size(driver_list::s_drivers_sorted);

namespace ves {

namespace {

std::string lifecycle_pointer_string(const void *pointer)
{
	std::ostringstream stream;
	if (pointer == nullptr)
		stream << "null";
	else
		stream << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(pointer);
	return stream.str();
}

void append_engine_lifecycle_log(const std::string &line)
{
	const char *home = std::getenv("HOME");
	if (home == nullptr || *home == '\0')
		return;

	std::ofstream file(std::filesystem::path(home) / "Library/Logs/VES-lifecycle.log", std::ios::app);
	if (file)
		file << line << '\n';
}

std::string lifecycle_log_field(std::string text)
{
	for (std::size_t pos = 0; (pos = text.find('\n', pos)) != std::string::npos; pos += 2)
		text.replace(pos, 1, "\\n");
	for (std::size_t pos = 0; (pos = text.find('\r', pos)) != std::string::npos; pos += 2)
		text.replace(pos, 1, "\\r");
	return text;
}

bool looks_like_media_failure(const std::string &output)
{
	std::string lower = output;
	std::transform(lower.begin(), lower.end(), lower.begin(), [] (unsigned char character)
	{
		return static_cast<char> (std::tolower(character));
	});
	return lower.find("unable to open image") != std::string::npos
		|| lower.find("error opening image") != std::string::npos
		|| lower.find("loading image failed") != std::string::npos
		|| lower.find("cannot open image") != std::string::npos
		|| lower.find("image device") != std::string::npos;
}

std::string crcString(const util::hash_collection &hashes)
{
	uint32_t crc = 0;
	return hashes.crc(crc) ? util::string_format("%08x", crc) : std::string();
}

std::string sha1String(const util::hash_collection &hashes)
{
	util::sha1_t sha1;
	return hashes.sha1(sha1) ? sha1.as_string() : std::string();
}

EmbeddedStartupIssue makeStartupIssue(const media_auditor::audit_record &record)
{
	EmbeddedStartupIssue issue;
	issue.name = record.name();
	if (auto const shared_device = record.shared_device())
		issue.owner = shared_device->shortname();
	issue.expected_crc = crcString(record.expected_hashes());
	issue.expected_sha1 = sha1String(record.expected_hashes());
	issue.actual_crc = crcString(record.actual_hashes());
	issue.actual_sha1 = sha1String(record.actual_hashes());
	issue.expected_length = record.expected_length();
	issue.actual_length = record.actual_length();
	return issue;
}

void appendStartupIssue(EmbeddedStartupDiagnostic &diagnostic, const media_auditor::audit_record &record)
{
	diagnostic.issues.emplace_back(makeStartupIssue(record));
}

EmbeddedStartupDiagnostic auditDriverRomSet(emu_options &options, const game_driver &driver)
{
	driver_enumerator enumerator(options, driver);
	if (!enumerator.next())
	{
		EmbeddedStartupDiagnostic diagnostic;
		diagnostic.category = EmbeddedStartupError::InvalidRomSet;
		diagnostic.summary = "The selected ROM set is incomplete or invalid.";
		diagnostic.recovery = "Please check that you are using a complete MAME 0.289 ROM set for this machine.";
		diagnostic.technical_details = "MAME driver enumerator could not select the requested driver.";
		return diagnostic;
	}

	media_auditor auditor(enumerator);
	const auto summary = auditor.audit_media(AUDIT_VALIDATE_FULL);

	EmbeddedStartupDiagnostic missing;
	missing.category = EmbeddedStartupError::MissingRom;
	missing.summary = "Required ROM files are missing.";
	missing.details = "Missing:";
	missing.recovery = "Please check that you are using a complete MAME 0.289 ROM set for this machine.";

	for (const auto &record : auditor.records())
	{
		switch (record.substatus())
		{
		case media_auditor::audit_substatus::NOT_FOUND:
			appendStartupIssue(missing, record);
			break;

		default:
			break;
		}
	}

	if (!missing.issues.empty())
		return missing;

	if ((summary == media_auditor::NOTFOUND || summary == media_auditor::INCORRECT) && auditor.records().empty())
	{
		EmbeddedStartupDiagnostic invalid;
		invalid.category = EmbeddedStartupError::InvalidRomSet;
		invalid.summary = "The selected ROM set is incomplete or invalid.";
		invalid.recovery = "Please check that you are using a complete MAME 0.289 ROM set for this machine.";
		invalid.technical_details = util::string_format("MAME ROM audit summary: %d", static_cast<int>(summary));
		return invalid;
	}

	return {};
}

template <typename T, std::size_t Capacity>
class spsc_ring
{
public:
	bool push(const T &item)
	{
		const auto head = m_head.load(std::memory_order_relaxed);
		const auto next = increment(head);
		if (next == m_tail.load(std::memory_order_acquire))
			return false;

		m_items[head] = item;
		m_head.store(next, std::memory_order_release);
		return true;
	}

	template <typename Writer>
	std::size_t push_bulk(std::size_t requested, Writer writer)
	{
		const auto head = m_head.load(std::memory_order_relaxed);
		const auto tail = m_tail.load(std::memory_order_acquire);
		const auto writable = writable_from(head, tail);
		const auto count = std::min(requested, writable);
		if (count == 0)
			return 0;

		const auto first = std::min(count, Capacity - head);
		writer(&m_items[head], first, std::size_t(0));

		const auto second = count - first;
		if (second != 0)
			writer(&m_items[0], second, first);

		m_head.store((head + count) % Capacity, std::memory_order_release);
		return count;
	}

	bool pop(T &item)
	{
		const auto tail = m_tail.load(std::memory_order_relaxed);
		if (tail == m_head.load(std::memory_order_acquire))
			return false;

		item = m_items[tail];
		m_tail.store(increment(tail), std::memory_order_release);
		return true;
	}

	std::size_t pop(T *items, std::size_t max_count)
	{
		const auto tail = m_tail.load(std::memory_order_relaxed);
		const auto head = m_head.load(std::memory_order_acquire);
		const auto readable = readable_from(head, tail);
		const auto count = std::min(max_count, readable);
		if (count == 0)
			return 0;

		const auto first = std::min(count, Capacity - tail);
		std::copy_n(&m_items[tail], first, items);

		const auto second = count - first;
		if (second != 0)
			std::copy_n(&m_items[0], second, items + first);

		m_tail.store((tail + count) % Capacity, std::memory_order_release);
		return count;
	}

	std::size_t size() const
	{
		const auto head = m_head.load(std::memory_order_acquire);
		const auto tail = m_tail.load(std::memory_order_acquire);
		return (head >= tail) ? (head - tail) : (Capacity + head - tail);
	}

	std::size_t clear()
	{
		const auto queued = size();
		m_tail.store(m_head.load(std::memory_order_acquire), std::memory_order_release);
		return queued;
	}

	std::size_t discard_oldest(std::size_t count)
	{
		const auto queued = size();
		const auto dropped = std::min(count, queued);
		if (dropped == 0)
			return 0;

		const auto tail = m_tail.load(std::memory_order_relaxed);
		m_tail.store((tail + dropped) % Capacity, std::memory_order_release);
		return dropped;
	}

	static constexpr std::size_t capacity() { return Capacity - 1; }

private:
	static constexpr std::size_t increment(std::size_t value)
	{
		return (value + 1) % Capacity;
	}

	static constexpr std::size_t readable_from(std::size_t head, std::size_t tail)
	{
		return (head >= tail) ? (head - tail) : (Capacity + head - tail);
	}

	static constexpr std::size_t writable_from(std::size_t head, std::size_t tail)
	{
		return tail > head ? (tail - head - 1) : (Capacity - head + tail - 1);
	}

	std::array<T, Capacity> m_items {};
	std::atomic<std::size_t> m_head { 0 };
	std::atomic<std::size_t> m_tail { 0 };
};

struct queued_midi_byte
{
	std::uint8_t value = 0;
	std::uint64_t queued_ms = 0;
};

using midi_byte_queue = spsc_ring<queued_midi_byte, 8192>;
using audio_frame_queue = spsc_ring<StereoFrame, 262144>;
using mouse_event_queue = spsc_ring<EmbeddedMouseEvent, 256>;
using floppy_request_queue = spsc_ring<FloppyChangeRequest, 2>;
using floppy_result_queue = spsc_ring<FloppyChangeResult, 2>;
using state_request_queue = spsc_ring<StateOperationRequest, 2>;
using state_result_queue = spsc_ring<StateOperationResult, 2>;

typedef software_renderer<std::uint32_t, 0,0,0, 16,8,0, false, false> embedded_video_renderer;

class embedded_video_bridge
{
public:
	enum class begin_status
	{
		ready,
		reader_busy,
		allocation_failed
	};

	begin_status beginFrame(int width, int height)
	{
		if (width <= 0 || height <= 0)
			return begin_status::allocation_failed;

		const auto required = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
		if (required == 0 || required > static_cast<std::size_t>(4096) * static_cast<std::size_t>(4096))
			return begin_status::allocation_failed;

		const auto active = m_active_slot.load(std::memory_order_acquire);
		if (m_width[active] == width && m_height[active] == height && m_buffers[active][0].size() == required)
		{
			m_capture_slot = active;
			m_pending_generation = false;
			return begin_status::ready;
		}

		const auto candidate = (m_generation[active] == 0) ? active : 1 - active;
		if (m_reader_index[candidate].load(std::memory_order_acquire) != -1)
			return begin_status::reader_busy;

		try
		{
			for (auto &buffer : m_buffers[candidate])
				buffer.assign(required, 0xff000000U);
		}
		catch (...)
		{
			return begin_status::allocation_failed;
		}

		m_width[candidate] = width;
		m_height[candidate] = height;
		m_published_index[candidate].store(-1, std::memory_order_relaxed);
		m_write_index[candidate] = 0;
		m_capture_slot = candidate;
		m_pending_generation = true;
		return begin_status::ready;
	}

	std::uint32_t *writeBuffer()
	{
		const auto published = m_published_index[m_capture_slot].load(std::memory_order_acquire);
		const auto reader = m_reader_index[m_capture_slot].load(std::memory_order_acquire);
		for (std::size_t offset = 0; offset < m_buffers[m_capture_slot].size(); ++offset)
		{
			const auto index = (m_write_index[m_capture_slot] + offset) % m_buffers[m_capture_slot].size();
			if (static_cast<int>(index) == published || static_cast<int>(index) == reader)
				continue;
			if (!m_buffers[m_capture_slot][index].empty())
			{
				m_write_buffer_index = index;
				return m_buffers[m_capture_slot][index].data();
			}
		}
		return nullptr;
	}

	void publishWrittenBuffer(EngineDiagnostics &diag, std::uint64_t job_token)
	{
		const auto index = m_write_buffer_index;
		const auto generation = ++m_generation_counter;
		const auto timestamp = steadyMs();

		m_metadata[m_capture_slot][index].width = m_width[m_capture_slot];
		m_metadata[m_capture_slot][index].height = m_height[m_capture_slot];
		m_metadata[m_capture_slot][index].generation = generation;
		m_metadata[m_capture_slot][index].timestamp_ms = timestamp;
		m_metadata[m_capture_slot][index].job_token = job_token;

		m_published_index[m_capture_slot].store(static_cast<int>(index), std::memory_order_release);
		m_generation[m_capture_slot] = generation;
		if (m_pending_generation)
		{
			m_active_slot.store(static_cast<int>(m_capture_slot), std::memory_order_release);
			m_pending_generation = false;
		}
		m_write_index[m_capture_slot] = (m_write_index[m_capture_slot] + 1) % m_buffers[m_capture_slot].size();
		if (generation > 1)
			diag.video_frames_replaced.fetch_add(1, std::memory_order_relaxed);

		diag.video_frame_width.store(static_cast<std::uint64_t>(m_width[m_capture_slot]), std::memory_order_relaxed);
		diag.video_frame_height.store(static_cast<std::uint64_t>(m_height[m_capture_slot]), std::memory_order_relaxed);
		diag.video_source_aspect_x1000.store(m_height[m_capture_slot] != 0 ? static_cast<std::uint64_t>((static_cast<double>(m_width[m_capture_slot]) / m_height[m_capture_slot]) * 1000.0) : 0, std::memory_order_relaxed);
		diag.video_frame_generation.store(generation, std::memory_order_relaxed);
		diag.video_last_frame_timestamp_ms.store(timestamp, std::memory_order_relaxed);
		diag.video_frames_produced.fetch_add(1, std::memory_order_relaxed);
		if (diag.state_restore_first_timeslice_completed_ms.load(std::memory_order_acquire) != 0)
		{
			std::uint64_t expected = 0;
			if (diag.state_restore_first_video_ms.compare_exchange_strong(expected, timestamp, std::memory_order_release, std::memory_order_relaxed))
			{
				diag.state_restore_first_video_generation.store(generation, std::memory_order_release);
				osd_printf_verbose("[VES state restore] first post-load video generation=%llu\n", static_cast<unsigned long long>(generation));
			}
		}
	}

	bool copy(VideoFrameSnapshot &snapshot, EngineDiagnostics &diag)
	{
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			const auto slot = m_active_slot.load(std::memory_order_acquire);
			const auto index = m_published_index[slot].load(std::memory_order_acquire);
			if (index < 0 || index >= static_cast<int>(m_buffers[slot].size()))
				return false;

			int expected = -1;
			if (!m_reader_index[slot].compare_exchange_strong(expected, index, std::memory_order_acq_rel))
				return false;

			if (m_active_slot.load(std::memory_order_acquire) != slot)
			{
				m_reader_index[slot].store(-1, std::memory_order_release);
				continue;
			}

			const auto metadata = m_metadata[slot][static_cast<std::size_t>(index)];
			if (metadata.generation == 0 || metadata.width <= 0 || metadata.height <= 0
				|| metadata.job_token < m_minimum_job_token.load(std::memory_order_acquire))
			{
				m_reader_index[slot].store(-1, std::memory_order_release);
				return false;
			}

			snapshot.width = metadata.width;
			snapshot.height = metadata.height;
			snapshot.generation = metadata.generation;
			snapshot.timestamp_ms = metadata.timestamp_ms;
			snapshot.pixels = m_buffers[slot][static_cast<std::size_t>(index)];
			m_reader_index[slot].store(-1, std::memory_order_release);
			diag.video_frames_displayed.fetch_add(1, std::memory_order_relaxed);
			return true;
		}
		return false;
	}

	void invalidatePublishedFrames(EngineDiagnostics &diag)
	{
		for (auto &published : m_published_index)
			published.store(-1, std::memory_order_release);
		diag.video_frame_width.store(0, std::memory_order_relaxed);
		diag.video_frame_height.store(0, std::memory_order_relaxed);
		diag.video_source_aspect_x1000.store(0, std::memory_order_relaxed);
		diag.video_frame_reset_generation.fetch_add(1, std::memory_order_release);
	}

	void setMinimumJobToken(std::uint64_t token)
	{
		m_minimum_job_token.store(token, std::memory_order_release);
	}

private:
	struct metadata
	{
		int width = 0;
		int height = 0;
		std::uint64_t generation = 0;
		std::uint64_t timestamp_ms = 0;
		std::uint64_t job_token = 0;
	};

	std::array<int, 2> m_width {};
	std::array<int, 2> m_height {};
	std::array<std::size_t, 2> m_write_index {};
	std::size_t m_write_buffer_index = 0;
	std::array<std::array<std::vector<std::uint32_t>, 3>, 2> m_buffers;
	std::array<std::array<metadata, 3>, 2> m_metadata {};
	std::array<std::atomic<int>, 2> m_published_index { -1, -1 };
	std::array<std::atomic<int>, 2> m_reader_index { -1, -1 };
	std::array<std::uint64_t, 2> m_generation {};
	std::atomic<int> m_active_slot { 0 };
	std::atomic<std::uint64_t> m_minimum_job_token { 0 };
	std::size_t m_capture_slot = 0;
	bool m_pending_generation = false;
	std::uint64_t m_generation_counter = 0;
};

void max_store(std::atomic<std::uint64_t> &target, std::uint64_t value)
{
	auto current = target.load(std::memory_order_relaxed);
	while (value > current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed))
	{
	}
}

void min_store(std::atomic<std::uint64_t> &target, std::uint64_t value)
{
	auto current = target.load(std::memory_order_relaxed);
	while ((current == 0 || value < current) && !target.compare_exchange_weak(current, value, std::memory_order_relaxed))
	{
	}
}

void update_audio_queue_diagnostics(EngineDiagnostics &diag, std::size_t queued)
{
	const auto queued64 = static_cast<std::uint64_t>(queued);
	const auto sample_rate = diag.mame_sample_rate.load(std::memory_order_relaxed);
	const auto latency_us = sample_rate != 0 ? (queued64 * 1'000'000ULL) / sample_rate : 0;
	diag.audio_current_queued_frames.store(queued64, std::memory_order_relaxed);
	diag.audio_queued_latency_us.store(latency_us, std::memory_order_relaxed);
	diag.audio_queued_observations.fetch_add(1, std::memory_order_relaxed);
	diag.audio_queued_accumulator_frames.fetch_add(queued64, std::memory_order_relaxed);
	max_store(diag.audio_max_queued_frames, queued64);
	max_store(diag.audio_max_queued_latency_us, latency_us);
	min_store(diag.audio_min_queued_frames, queued64);
	const auto written = diag.audio_frames_written.load(std::memory_order_relaxed);
	const auto read = diag.audio_frames_read.load(std::memory_order_relaxed);
	diag.audio_producer_consumer_frame_difference.store(
		static_cast<std::int64_t>(written) - static_cast<std::int64_t>(read),
		std::memory_order_relaxed);
}

constexpr std::uint32_t k_audio_sink_diagnostics_interval = 32;
constexpr std::uint32_t k_audio_read_diagnostics_interval = 32;

class memory_midi_input_port : public osd::midi_input_port
{
public:
	memory_midi_input_port(midi_byte_queue &queue, EngineDiagnostics &diag)
		: m_queue(queue)
		, m_diag(diag)
	{
	}

	bool poll() override
	{
		return m_queue.size() != 0;
	}

	int read(std::uint8_t *output) override
	{
		queued_midi_byte bytes[256] {};
		const auto count = m_queue.pop(bytes, std::size(bytes));
		if (count != 0)
		{
			const auto now = steadyMs();
			const auto started = m_diag.machine_started_ms.load(std::memory_order_relaxed);
			if (m_diag.midi_first_read_ms.load(std::memory_order_relaxed) == 0)
			{
				m_diag.midi_first_read_ms.store(now, std::memory_order_relaxed);
				m_diag.midi_first_read_machine_uptime_ms.store(started != 0 ? now - started : 0, std::memory_order_relaxed);
			}
			m_diag.midi_last_read_ms.store(now, std::memory_order_relaxed);

			for (std::size_t i = 0; i < count; ++i)
			{
				output[i] = bytes[i].value;
				if (bytes[i].queued_ms != 0 && now >= bytes[i].queued_ms)
					max_store(m_diag.midi_max_queue_to_read_ms, now - bytes[i].queued_ms);
			}
			m_diag.midi_bytes_consumed.fetch_add(count, std::memory_order_relaxed);
		}
		return static_cast<int>(count);
	}

private:
	midi_byte_queue &m_queue;
	EngineDiagnostics &m_diag;
};

class memory_midi_output_port : public osd::midi_output_port
{
public:
	void write(std::uint8_t) override {}
};

class embedded_osd : public osd_common_t
{
public:
	embedded_osd(osd_options &options, midi_byte_queue &midi, audio_frame_queue &audio, mouse_event_queue &mouse,
		floppy_request_queue &floppy_requests, floppy_result_queue &floppy_results,
		state_request_queue &state_requests, state_result_queue &state_results,
		std::atomic<bool> &midi_panic_requested,
		std::array<std::atomic<std::uint64_t>, 32> &midi_panic_held_notes,
		std::array<std::atomic<std::uint64_t>, 32> &active_host_midi_notes,
		embedded_video_bridge &video, EngineDiagnostics &diag)
		: osd_common_t(options)
		, m_options(options)
		, m_midi_queue(midi)
		, m_audio_queue(audio)
		, m_mouse_queue(mouse)
		, m_floppy_requests(floppy_requests)
		, m_floppy_results(floppy_results)
		, m_state_requests(state_requests)
		, m_state_results(state_results)
		, m_midi_panic_requested(midi_panic_requested)
		, m_midi_panic_held_notes(midi_panic_held_notes)
		, m_active_host_midi_notes(active_host_midi_notes)
		, m_video_bridge(video)
		, m_diag(diag)
	{
		m_video_worker = std::thread([this] { video_worker_loop(); });
	}

	~embedded_osd() override
	{
		stop_video_worker();
	}

	void output_callback(osd_output_channel channel, const util::format_argument_pack<char> &args) override
	{
		if (channel == OSD_OUTPUT_CHANNEL_ERROR || channel == OSD_OUTPUT_CHANNEL_WARNING)
		{
			std::ostringstream text;
			util::stream_format(text, args);
			std::lock_guard<std::mutex> guard(m_startup_output_mutex);
			if (m_startup_output.size() < 65536)
				m_startup_output.append(text.str(), 0, 65536 - m_startup_output.size());
		}
		osd_common_t::output_callback(channel, args);
	}

	std::string startup_output() const
	{
		std::lock_guard<std::mutex> guard(m_startup_output_mutex);
		return m_startup_output;
	}

	void init(running_machine &machine) override
	{
		osd_common_t::init(machine);
		init_subsystems();

		m_diag.video_initialized.store(true, std::memory_order_relaxed);
		m_diag.video_render_target_available.store(false, std::memory_order_relaxed);
		m_diag.video_capture_enabled.store(true, std::memory_order_relaxed);
		m_diag.video_target_frame_rate.store(1000 / std::max<std::uint64_t>(1, m_diag.video_capture_interval_ms.load(std::memory_order_relaxed)), std::memory_order_relaxed);
		m_diag.mouse_forwarding_enabled.store(true, std::memory_order_relaxed);
		m_diag.video_state.store(static_cast<std::uint64_t>(EmbeddedVideoState::WaitingForMachine), std::memory_order_relaxed);
	}

	void sound_manager_update() override
	{
	}

	void update(bool skip_redraw) override
	{
		process_floppy_requests();
		consume_mouse_events();
		process_embedded_ui_pointer_events();
		osd_common_t::update(skip_redraw);
		m_diag.osd_updates.fetch_add(1, std::memory_order_relaxed);
		if (machine().phase() >= machine_phase::RESET)
		{
			if (!m_diag.video_editor_display_active.load(std::memory_order_relaxed)
				|| !m_diag.video_capture_enabled.load(std::memory_order_relaxed))
			{
				release_video_target();
				m_diag.video_state.store(static_cast<std::uint64_t>(EmbeddedVideoState::Disabled), std::memory_order_relaxed);
			}
			else if (ensure_video_target())
				capture_video_frame(skip_redraw);
			else
				m_diag.video_state.store(static_cast<std::uint64_t>(EmbeddedVideoState::Unavailable), std::memory_order_relaxed);
		}
		if (m_diag.stop_requested.load(std::memory_order_relaxed))
		{
			release_video_target();
			machine().schedule_exit();
		}
	}

	void timeslice_complete() override
	{
		const auto now = steadyMs();
		if (machine().phase() == machine_phase::RUNNING)
		{
			m_diag.machine_running.store(true, std::memory_order_release);
			m_diag.normal_scheduler_iterations.fetch_add(1, std::memory_order_release);
		}
		m_diag.state_timeslice_exit_ms.store(now, std::memory_order_relaxed);
		if (m_restore_waiting_for_clean_timeslice)
		{
			m_restore_waiting_for_clean_timeslice = false;
			m_diag.state_restore_first_timeslice_completed_ms.store(now, std::memory_order_release);
			m_diag.video_deadline_reset_requests.fetch_add(1, std::memory_order_release);
			m_diag.video_capture_requested.fetch_add(1, std::memory_order_release);
		}
		process_state_requests();
		process_midi_panic_request();
	}

	struct static_layout_run
	{
		u32 first_item = 0;
		u32 item_count = 0;
		bool is_static = false;
		int x = 0;
		int y = 0;
		int width = 0;
		int height = 0;
		// RGB is produced by the embedded renderer; the high byte is a cached
		// coverage value recovered during the one-time build.
		std::vector<std::uint32_t> pixels;
	};

	struct immutable_primitive
	{
		render_primitive primitive;
		std::vector<std::uint8_t> texture_pixels;
		std::vector<rgb_t> palette;

		void bind_owned_data()
		{
			primitive.texture.base = texture_pixels.empty() ? nullptr : texture_pixels.data();
			primitive.texture.palette = palette.empty() ? nullptr : palette.data();
			primitive.container = nullptr;
		}
	};

	struct immutable_video_job
	{
		struct run
		{
			bool is_static = false;
			std::uint64_t signature = 0;
			std::vector<immutable_primitive> primitives;
		};

		int width = 0;
		int height = 0;
		std::uint64_t token = 0;
		std::uint64_t snapshot_bytes = 0;
		bool use_static_cache = false;
		std::vector<run> runs;
	};

	struct worker_static_run
	{
		std::uint64_t signature = 0;
		int x = 0;
		int y = 0;
		int width = 0;
		int height = 0;
		std::vector<std::uint32_t> pixels;
	};

	static std::uint64_t snapshot_signature(const immutable_video_job::run &run)
	{
		std::uint64_t hash = 1469598103934665603ULL;
		auto mix = [&hash] (std::uint64_t value) { hash = (hash ^ value) * 1099511628211ULL; };
		for (const auto &snapshot : run.primitives)
		{
			mix(snapshot.primitive.flags);
			mix(snapshot.primitive.texture.unique_id);
			mix(snapshot.primitive.texture.seqid);
			mix(snapshot.texture_pixels.size());
			mix(snapshot.palette.size());
			std::uint32_t bits = 0;
			std::memcpy(&bits, &snapshot.primitive.bounds.x0, sizeof(bits)); mix(bits);
			std::memcpy(&bits, &snapshot.primitive.bounds.y0, sizeof(bits)); mix(bits);
			std::memcpy(&bits, &snapshot.primitive.bounds.x1, sizeof(bits)); mix(bits);
			std::memcpy(&bits, &snapshot.primitive.bounds.y1, sizeof(bits)); mix(bits);
		}
		return hash;
	}

	static std::size_t texture_pixel_size(u32 format)
	{
		return (format == TEXFORMAT_PALETTE16 || format == TEXFORMAT_YUY16) ? sizeof(std::uint16_t) : sizeof(std::uint32_t);
	}

	bool snapshot_primitive(const render_primitive &source, immutable_primitive &destination, std::uint64_t &bytes)
	{
		destination.primitive.type = source.type;
		destination.primitive.bounds = source.bounds;
		destination.primitive.full_bounds = source.full_bounds;
		destination.primitive.color = source.color;
		destination.primitive.flags = source.flags;
		destination.primitive.width = source.width;
		destination.primitive.texture = source.texture;
		destination.primitive.texcoords = source.texcoords;
		destination.primitive.container = nullptr;
		if (source.texture.base != nullptr)
		{
			const auto pixel_size = texture_pixel_size(PRIMFLAG_GET_TEXFORMAT(source.flags));
			const auto row_bytes = static_cast<std::size_t>(source.texture.rowpixels) * pixel_size;
			const auto texture_bytes = row_bytes * static_cast<std::size_t>(source.texture.height);
			if (source.texture.rowpixels == 0 || source.texture.height == 0
				|| texture_bytes > 256ULL * 1024ULL * 1024ULL)
				return false;
			destination.texture_pixels.resize(texture_bytes);
			std::memcpy(destination.texture_pixels.data(), source.texture.base, texture_bytes);
			bytes += texture_bytes;
		}
		if (source.texture.palette != nullptr && source.texture.palette_length != 0)
		{
			destination.palette.assign(source.texture.palette, source.texture.palette + source.texture.palette_length);
			bytes += destination.palette.size() * sizeof(rgb_t);
		}
		destination.bind_owned_data();
		bytes += sizeof(render_primitive);
		return true;
	}

	void invalidate_video_jobs()
	{
		const auto token = m_video_job_token.fetch_add(1, std::memory_order_acq_rel) + 1;
		m_video_bridge.setMinimumJobToken(token);
		std::unique_lock<std::mutex> lock(m_video_worker_mutex, std::try_to_lock);
		if (lock.owns_lock())
		{
			m_pending_video_job.reset();
			m_diag.video_worker_pending_depth.store(0, std::memory_order_relaxed);
		}
	}

	bool submit_video_job(std::unique_ptr<immutable_video_job> job)
	{
		std::unique_lock<std::mutex> lock(m_video_worker_mutex, std::try_to_lock);
		if (!lock.owns_lock())
		{
			m_diag.video_jobs_replaced.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		if (m_pending_video_job)
			m_diag.video_jobs_replaced.fetch_add(1, std::memory_order_relaxed);
		m_pending_video_job = std::move(job);
		m_diag.video_worker_pending_depth.store(1, std::memory_order_relaxed);
		m_diag.video_jobs_submitted.fetch_add(1, std::memory_order_relaxed);
		lock.unlock();
		m_video_worker_condition.notify_one();
		return true;
	}

	worker_static_run build_worker_static_run(immutable_video_job::run &run, int width, int height)
	{
		worker_static_run cached;
		cached.signature = run.signature;
		int left = width;
		int top = height;
		int right = 0;
		int bottom = 0;
		for (const auto &snapshot : run.primitives)
		{
			left = std::min(left, std::clamp(static_cast<int>(std::floor(snapshot.primitive.bounds.x0)) - 1, 0, width));
			top = std::min(top, std::clamp(static_cast<int>(std::floor(snapshot.primitive.bounds.y0)) - 1, 0, height));
			right = std::max(right, std::clamp(static_cast<int>(std::ceil(snapshot.primitive.bounds.x1)) + 1, 0, width));
			bottom = std::max(bottom, std::clamp(static_cast<int>(std::ceil(snapshot.primitive.bounds.y1)) + 1, 0, height));
		}
		if (right <= left || bottom <= top)
			return cached;

		const auto pixel_count = static_cast<std::size_t>(width) * height;
		std::vector<std::uint32_t> black(pixel_count, 0U);
		std::vector<std::uint32_t> white(pixel_count, 0x00ffffffU);
		for (auto &snapshot : run.primitives)
		{
			snapshot.bind_owned_data();
			embedded_video_renderer::draw_primitive(snapshot.primitive, black.data(), width, height, width);
			embedded_video_renderer::draw_primitive(snapshot.primitive, white.data(), width, height, width);
		}
		cached.x = left;
		cached.y = top;
		cached.width = right - left;
		cached.height = bottom - top;
		cached.pixels.resize(static_cast<std::size_t>(cached.width) * cached.height);
		for (int y = 0; y < cached.height; ++y)
			for (int x = 0; x < cached.width; ++x)
			{
				const auto source_index = static_cast<std::size_t>(cached.y + y) * width + cached.x + x;
				const auto black_pixel = black[source_index];
				const auto white_pixel = white[source_index];
				const auto alpha_channel = [] (unsigned b, unsigned w)
				{
					const int difference = static_cast<int>(w) - static_cast<int>(b);
					return difference >= 0 ? 0xffU - std::min(0xff, difference) : 0xffU;
				};
				const auto alpha = std::max({
					alpha_channel((black_pixel >> 16) & 0xffU, (white_pixel >> 16) & 0xffU),
					alpha_channel((black_pixel >> 8) & 0xffU, (white_pixel >> 8) & 0xffU),
					alpha_channel(black_pixel & 0xffU, white_pixel & 0xffU) });
				const auto red = alpha != 0 ? std::min(0xffU, (((black_pixel >> 16) & 0xffU) * 0xffU) / alpha) : 0U;
				const auto green = alpha != 0 ? std::min(0xffU, (((black_pixel >> 8) & 0xffU) * 0xffU) / alpha) : 0U;
				const auto blue = alpha != 0 ? std::min(0xffU, ((black_pixel & 0xffU) * 0xffU) / alpha) : 0U;
				cached.pixels[static_cast<std::size_t>(y) * cached.width + x] =
					(alpha << 24) | (red << 16) | (green << 8) | blue;
			}
		return cached;
	}

	static void composite_worker_static_run(std::uint32_t *destination, int destination_width, const worker_static_run &run)
	{
		for (int y = 0; y < run.height; ++y)
			for (int x = 0; x < run.width; ++x)
			{
				const auto source = run.pixels[static_cast<std::size_t>(y) * run.width + x];
				const auto alpha = (source >> 24) & 0xffU;
				if (alpha == 0)
					continue;
				const auto index = static_cast<std::size_t>(run.y + y) * destination_width + run.x + x;
				const auto dest = destination[index];
				const auto inverse = 0xffU - alpha;
				const auto red = (((source >> 16) & 0xffU) * alpha + ((dest >> 16) & 0xffU) * inverse) / 0xffU;
				const auto green = (((source >> 8) & 0xffU) * alpha + ((dest >> 8) & 0xffU) * inverse) / 0xffU;
				const auto blue = ((source & 0xffU) * alpha + (dest & 0xffU) * inverse) / 0xffU;
				destination[index] = (red << 16) | (green << 8) | blue;
			}
	}

	void video_worker_loop()
	{
#if defined(__APPLE__)
		pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
		for (;;)
		{
			std::unique_ptr<immutable_video_job> job;
			{
				std::unique_lock<std::mutex> lock(m_video_worker_mutex);
				m_video_worker_condition.wait(lock, [this] { return m_video_worker_stop || m_pending_video_job != nullptr; });
				if (m_video_worker_stop)
					return;
				job = std::move(m_pending_video_job);
				m_diag.video_worker_pending_depth.store(0, std::memory_order_relaxed);
			}

			if (!job || job->token != m_video_job_token.load(std::memory_order_acquire))
			{
				m_diag.video_capture_in_progress.store(false, std::memory_order_release);
				continue;
			}
			try
			{
				std::vector<std::uint32_t> pixels(static_cast<std::size_t>(job->width) * job->height, 0xff000000U);
				if (m_worker_cache_token != job->token || m_worker_cache_width != job->width
					|| m_worker_cache_height != job->height || m_worker_static_runs.size() != job->runs.size())
				{
					m_worker_static_runs.clear();
					m_worker_static_runs.resize(job->runs.size());
					m_worker_cache_token = job->token;
					m_worker_cache_width = job->width;
					m_worker_cache_height = job->height;
				}
				for (std::size_t index = 0; index < job->runs.size(); ++index)
				{
					auto &run = job->runs[index];
					if (job->use_static_cache && run.is_static)
					{
						auto &cached = m_worker_static_runs[index];
						if (cached.signature != run.signature || cached.pixels.empty())
							cached = build_worker_static_run(run, job->width, job->height);
						composite_worker_static_run(pixels.data(), job->width, cached);
					}
					else
					{
						for (auto &snapshot : run.primitives)
						{
							snapshot.bind_owned_data();
							embedded_video_renderer::draw_primitive(snapshot.primitive, pixels.data(),
								static_cast<u32>(job->width), static_cast<u32>(job->height), static_cast<u32>(job->width));
						}
					}
				}

				if (job->token != m_video_job_token.load(std::memory_order_acquire))
				{
					m_diag.video_capture_in_progress.store(false, std::memory_order_release);
					continue;
				}
				const auto begin_status = m_video_bridge.beginFrame(job->width, job->height);
				if (begin_status != embedded_video_bridge::begin_status::ready)
				{
					m_diag.video_capture_in_progress.store(false, std::memory_order_release);
					continue;
				}
				auto *const destination = m_video_bridge.writeBuffer();
				if (destination == nullptr)
				{
					m_diag.video_capture_in_progress.store(false, std::memory_order_release);
					continue;
				}
				std::copy(pixels.begin(), pixels.end(), destination);
				if (job->token != m_video_job_token.load(std::memory_order_acquire))
				{
					m_diag.video_capture_in_progress.store(false, std::memory_order_release);
					continue;
				}
				m_video_bridge.publishWrittenBuffer(m_diag, job->token);
				m_diag.video_capture_queue_depth_after.store(m_audio_queue.size(), std::memory_order_relaxed);
				m_diag.video_jobs_completed.fetch_add(1, std::memory_order_relaxed);
				m_diag.video_capture_completed.fetch_add(1, std::memory_order_relaxed);
				m_diag.video_capture_in_progress.store(false, std::memory_order_release);
				m_diag.video_state.store(static_cast<std::uint64_t>(EmbeddedVideoState::Running), std::memory_order_relaxed);
			}
			catch (...)
			{
				m_diag.video_capture_in_progress.store(false, std::memory_order_release);
				m_diag.video_raster_error_code.store(1, std::memory_order_relaxed);
				m_diag.video_last_error_code.store(1, std::memory_order_relaxed);
			}
		}
	}

	void stop_video_worker()
	{
		{
			std::lock_guard<std::mutex> lock(m_video_worker_mutex);
			m_video_worker_stop = true;
			m_pending_video_job.reset();
		}
		m_video_worker_condition.notify_one();
		if (m_video_worker.joinable())
			m_video_worker.join();
	}

	bool static_cache_supported() const
	{
		return m_video_target != nullptr;
	}

	void invalidate_static_cache()
	{
		m_static_cache_valid = false;
		m_static_cache_fallback = false;
		m_static_cache_fallback_reason.clear();
		m_static_runs.clear();
		m_static_cache_width = 0;
		m_static_cache_height = 0;
		m_static_cache_machine_name.clear();
		m_static_cache_view_name.clear();
		m_static_cache_visibility_mask = 0;
		m_static_cache_orientation = 0;
		m_static_cache_pixel_aspect = 0.0f;
		m_static_cache_view_aspect = 0.0f;
		m_static_cache_validation_done = false;
		m_static_cropped_cache_memory_bytes_estimate = 0;
		m_static_full_frame_equivalent_bytes = 0;
		m_static_cache_hits = 0;
		m_static_dynamic_render_total_us = 0;
		m_static_dynamic_render_max_us = 0;
		m_static_dynamic_render_count = 0;
		m_static_composite_total_us = 0;
		m_static_composite_max_us = 0;
		m_static_cached_frame_total_us = 0;
		m_static_cached_frame_max_us = 0;
		m_static_cached_frame_count = 0;
	}

	void stamp_static_cache_context(int width, int height)
	{
		m_static_cache_width = width;
		m_static_cache_height = height;
		m_static_cache_machine_name = machine().basename();
		m_static_cache_view_name = m_video_target != nullptr ? m_video_target->current_view().name() : std::string();
		m_static_cache_visibility_mask = m_video_target != nullptr ? m_video_target->visibility_mask() : 0;
		m_static_cache_orientation = m_video_target != nullptr ? m_video_target->orientation() : 0;
		m_static_cache_pixel_aspect = m_video_target != nullptr ? m_video_target->pixel_aspect() : 0.0f;
		m_static_cache_view_aspect = m_video_target != nullptr ? m_video_target->current_view().effective_aspect() : 0.0f;
	}

	void append_static_cache_log(const std::string &line) const
	{
		const char *home = std::getenv("HOME");
		if (home == nullptr || *home == '\0')
			return;
		const std::string directory = std::string(home) + "/Library/Logs";
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		std::ofstream log(directory + "/VES-static-cache.log", std::ios::app);
		if (log)
			log << line << '\n';
	}

	static bool cacheable_static_blend(int blend_mode)
	{
		return blend_mode == BLENDMODE_ALPHA || blend_mode == BLENDMODE_NONE;
	}

	bool compute_static_run_bounds(static_layout_run &run, int width, int height, std::string &reason)
	{
		auto &run_list = m_video_target->get_primitives(run.first_item, run.item_count, false);
		int min_x = width;
		int min_y = height;
		int max_x = -1;
		int max_y = -1;
		for (const render_primitive *primitive = run_list.first(); primitive != nullptr; primitive = primitive->next())
		{
			if (!std::isfinite(primitive->bounds.x0) || !std::isfinite(primitive->bounds.y0)
				|| !std::isfinite(primitive->bounds.x1) || !std::isfinite(primitive->bounds.y1)
				|| primitive->bounds.x1 < primitive->bounds.x0 || primitive->bounds.y1 < primitive->bounds.y0)
			{
				reason = util::string_format("invalid primitive bounds first_item=%u item_count=%u", run.first_item, run.item_count);
				return false;
			}
			const int left = std::clamp(static_cast<int>(std::floor(primitive->bounds.x0)) - 1, 0, width);
			const int top = std::clamp(static_cast<int>(std::floor(primitive->bounds.y0)) - 1, 0, height);
			const int right = std::clamp(static_cast<int>(std::ceil(primitive->bounds.x1)) + 1, 0, width);
			const int bottom = std::clamp(static_cast<int>(std::ceil(primitive->bounds.y1)) + 1, 0, height);
			if (right <= left || bottom <= top)
				continue;
			min_x = std::min(min_x, left);
			min_y = std::min(min_y, top);
			max_x = std::max(max_x, right);
			max_y = std::max(max_y, bottom);
		}
		if (max_x <= min_x || max_y <= min_y)
		{
			reason = util::string_format("empty primitive bounds first_item=%u item_count=%u", run.first_item, run.item_count);
			return false;
		}
		run.x = min_x;
		run.y = min_y;
		run.width = max_x - min_x;
		run.height = max_y - min_y;
		return true;
	}

	bool compute_static_run_bounds(std::vector<static_layout_run> &runs, int width, int height, std::uint64_t &cropped_cache_memory_bytes, std::string &reason)
	{
		cropped_cache_memory_bytes = 0;
		for (auto &run : runs)
		{
			if (!run.is_static)
				continue;
			if (!compute_static_run_bounds(run, width, height, reason))
				return false;
			cropped_cache_memory_bytes += static_cast<std::uint64_t>(run.width) * static_cast<std::uint64_t>(run.height) * sizeof(std::uint32_t);
		}
		return true;
	}

	static void composite_cached_rgb_region(std::uint32_t *destination, int destination_width, const static_layout_run &run)
	{
		for (int y = 0; y < run.height; ++y)
			for (int x = 0; x < run.width; ++x)
			{
				const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(run.width) + x;
				const auto src = run.pixels[index];
				const auto alpha = (src >> 24) & 0xffU;
				if (alpha == 0)
					continue;
				const auto destination_index = static_cast<std::size_t>(run.y + y) * static_cast<std::size_t>(destination_width) + static_cast<std::size_t>(run.x + x);
				if (alpha == 0xffU)
				{
					destination[destination_index] = src & 0x00ffffffU;
					continue;
				}

				const auto dst = destination[destination_index];
				const auto inverse = 0xffU - alpha;
				const auto red = (((src >> 16) & 0xffU) * alpha + ((dst >> 16) & 0xffU) * inverse) / 0xffU;
				const auto green = (((src >> 8) & 0xffU) * alpha + ((dst >> 8) & 0xffU) * inverse) / 0xffU;
				const auto blue = ((src & 0xffU) * alpha + (dst & 0xffU) * inverse) / 0xffU;
				destination[destination_index] = (red << 16) | (green << 8) | blue;
			}
	}

	bool build_static_cache(int width, int height)
	{
		const bool during_restore = m_diag.state_restore_read_completed_ms.load(std::memory_order_acquire) != 0
			&& m_diag.state_restore_first_video_ms.load(std::memory_order_acquire) == 0;
		if (during_restore)
			m_diag.state_restore_cache_build_start_ms.store(steadyMs(), std::memory_order_release);
		struct restore_cache_build_scope
		{
			EngineDiagnostics &diag;
			bool active;
			~restore_cache_build_scope()
			{
				if (active)
					diag.state_restore_cache_build_end_ms.store(steadyMs(), std::memory_order_release);
			}
		} restore_scope { m_diag, during_restore };
		if (!static_cache_supported())
			return false;
		stamp_static_cache_context(width, height);

		const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
		std::vector<static_layout_run> runs;
		u32 static_items = 0;
		u32 stateful_items = 0;
		u32 display_items = 0;
		u32 unsupported_blend_items = 0;
		u32 item_index = 0;
		for (const auto &item : m_video_target->current_view().visible_items())
		{
			const bool display = item.get().screen() != nullptr;
			const bool unsupported_blend = !display && !cacheable_static_blend(item.get().blend_mode());
			const bool dynamic = display || unsupported_blend || item.get().has_dynamic_dependency();
			const auto classification = display ? "DISPLAY" : (dynamic ? "STATEFUL" : "STATIC");
			osd_printf_verbose("[VES static cache item] index=%u id=%s class=%s bounds=%.6f,%.6f,%.6f,%.6f\n",
				item_index, item.get().id().c_str(), classification,
				item.get().bounds().x0, item.get().bounds().y0,
				item.get().bounds().x1, item.get().bounds().y1);
			if (display)
				++display_items;
			else if (dynamic)
			{
				++stateful_items;
				if (unsupported_blend)
					++unsupported_blend_items;
			}
			else
				++static_items;

			if (runs.empty() || runs.back().is_static != !dynamic)
				runs.push_back({ item_index, 1, !dynamic, {} });
			else
				++runs.back().item_count;
			++item_index;
		}

		u32 static_run_count = 0;
		for (const auto &run : runs)
			if (run.is_static)
				++static_run_count;
		const auto full_frame_equivalent_bytes = static_cast<std::uint64_t>(static_run_count) * static_cast<std::uint64_t>(pixel_count) * sizeof(std::uint32_t);
		std::uint64_t cropped_cache_memory_bytes = 0;
		std::string bounds_failure_reason;
		if (!compute_static_run_bounds(runs, width, height, cropped_cache_memory_bytes, bounds_failure_reason))
		{
			m_static_cache_valid = false;
			m_static_cache_fallback = true;
			m_static_cache_validation_done = true;
			m_static_cropped_cache_memory_bytes_estimate = 0;
			m_static_full_frame_equivalent_bytes = full_frame_equivalent_bytes;
			m_static_cache_fallback_reason = "unsupported " + bounds_failure_reason;
			append_static_cache_log(util::string_format("[VES static cache validation] machine=%s view=\"%s\" static_items=%u stateful_items=%u display_items=%u static_runs=%u old_full_frame_equivalent_bytes=%llu actual_cropped_cache_bytes=0 unsupported_blend_items=%u result=FALLBACK reason=\"%s\"",
				machine().basename(), m_video_target->current_view().name().c_str(), static_items, stateful_items, display_items, static_run_count,
				static_cast<unsigned long long>(full_frame_equivalent_bytes),
				unsupported_blend_items, m_static_cache_fallback_reason.c_str()));
			return false;
		}
		const auto total_items = static_items + stateful_items + display_items;
		const double static_ratio = total_items != 0 ? double(static_items) / double(total_items) : 0.0;
		constexpr std::uint64_t cropped_memory_limit_bytes = 64ULL * 1024ULL * 1024ULL;
		const bool compact_many_runs = static_run_count <= 64
			&& static_ratio >= 0.30
			&& cropped_cache_memory_bytes <= 32ULL * 1024ULL * 1024ULL;
		if (static_items < 3 || static_run_count == 0 || (static_run_count > 32 && !compact_many_runs) || cropped_cache_memory_bytes > cropped_memory_limit_bytes || static_ratio < 0.20)
		{
			m_static_cache_valid = false;
			m_static_cache_fallback = true;
			m_static_cache_validation_done = true;
			m_static_cropped_cache_memory_bytes_estimate = cropped_cache_memory_bytes;
			m_static_full_frame_equivalent_bytes = full_frame_equivalent_bytes;
			m_static_cache_fallback_reason = util::string_format("efficiency static_items=%u static_runs=%u static_ratio=%.4f old_full_frame_equivalent_bytes=%llu actual_cropped_cache_bytes=%llu",
				static_items, static_run_count, static_ratio,
				static_cast<unsigned long long>(full_frame_equivalent_bytes),
				static_cast<unsigned long long>(cropped_cache_memory_bytes));
			append_static_cache_log(util::string_format("[VES static cache validation] machine=%s view=\"%s\" static_items=%u stateful_items=%u display_items=%u static_runs=%u old_full_frame_equivalent_bytes=%llu actual_cropped_cache_bytes=%llu memory_reduction_percent=%.2f unsupported_blend_items=%u result=FALLBACK reason=\"%s\"",
				machine().basename(), m_video_target->current_view().name().c_str(), static_items, stateful_items, display_items, static_run_count,
				static_cast<unsigned long long>(full_frame_equivalent_bytes),
				static_cast<unsigned long long>(cropped_cache_memory_bytes),
				full_frame_equivalent_bytes != 0 ? 100.0 * (1.0 - double(cropped_cache_memory_bytes) / double(full_frame_equivalent_bytes)) : 0.0,
				unsupported_blend_items, m_static_cache_fallback_reason.c_str()));
			return false;
		}

		const auto build_start = std::chrono::steady_clock::now();
		std::uint64_t full_render_equivalent_us = 0;
		try
		{
			// Establish a one-time baseline for the diagnostic without changing
			// the normal path used by any other machine or view.
			const auto full_start = std::chrono::steady_clock::now();
			auto &full_list = m_video_target->get_primitives();
			std::vector<std::uint32_t> baseline(pixel_count, 0xff000000U);
			embedded_video_renderer::draw_primitives(full_list, baseline.data(), static_cast<u32>(width), static_cast<u32>(height), static_cast<u32>(width));
			full_render_equivalent_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - full_start).count());

			u32 static_run_index = 0;
			for (auto &run : runs)
			{
				if (!run.is_static)
					continue;
				std::vector<std::uint32_t> black(pixel_count, 0U);
				std::vector<std::uint32_t> white(pixel_count, 0x00ffffffU);
				auto &run_list = m_video_target->get_primitives(run.first_item, run.item_count, false);
				embedded_video_renderer::draw_primitives(run_list, black.data(), static_cast<u32>(width), static_cast<u32>(height), static_cast<u32>(width));
				embedded_video_renderer::draw_primitives(run_list, white.data(), static_cast<u32>(width), static_cast<u32>(height), static_cast<u32>(width));
				const auto cropped_pixel_count = static_cast<std::size_t>(run.width) * static_cast<std::size_t>(run.height);
				run.pixels.resize(cropped_pixel_count);
				std::size_t covered_pixels = 0;
				for (int y = 0; y < run.height; ++y)
					for (int x = 0; x < run.width; ++x)
					{
						const auto full_index = static_cast<std::size_t>(run.y + y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(run.x + x);
						const auto cropped_index = static_cast<std::size_t>(y) * static_cast<std::size_t>(run.width) + static_cast<std::size_t>(x);
						const auto black_pixel = black[full_index];
						const auto white_pixel = white[full_index];
						const auto alpha_from_channel = [] (unsigned black_channel, unsigned white_channel)
						{
							const int difference = static_cast<int>(white_channel) - static_cast<int>(black_channel);
							return difference >= 0 ? 0xffU - std::min(0xff, difference) : 0xffU;
						};
						const auto alpha_r = alpha_from_channel((black_pixel >> 16) & 0xffU, (white_pixel >> 16) & 0xffU);
						const auto alpha_g = alpha_from_channel((black_pixel >> 8) & 0xffU, (white_pixel >> 8) & 0xffU);
						const auto alpha_b = alpha_from_channel(black_pixel & 0xffU, white_pixel & 0xffU);
						const auto alpha = std::max({ alpha_r, alpha_g, alpha_b });
						const auto red = alpha != 0 ? std::min(0xffU, (((black_pixel >> 16) & 0xffU) * 0xffU) / alpha) : 0U;
						const auto green = alpha != 0 ? std::min(0xffU, (((black_pixel >> 8) & 0xffU) * 0xffU) / alpha) : 0U;
						const auto blue = alpha != 0 ? std::min(0xffU, ((black_pixel & 0xffU) * 0xffU) / alpha) : 0U;
						run.pixels[cropped_index] = (alpha << 24) | (red << 16) | (green << 8) | blue;
						if (alpha != 0)
							++covered_pixels;
					}
				osd_printf_verbose("[VES static cache run] index=%u first_item=%u item_count=%u bbox=%d,%d %dx%d pixels=%u\n",
					static_run_index++, run.first_item, run.item_count, run.x, run.y, run.width, run.height, static_cast<unsigned>(cropped_pixel_count));
				append_static_cache_log(util::string_format("run machine=%s view=\"%s\" first_item=%u item_count=%u bbox=%d,%d %dx%d cropped_pixels=%llu covered_pixels=%llu coverage_percent=%.4f",
					machine().basename(), m_video_target->current_view().name().c_str(),
					run.first_item, run.item_count, run.x, run.y, run.width, run.height,
					static_cast<unsigned long long>(cropped_pixel_count),
					static_cast<unsigned long long>(covered_pixels),
					cropped_pixel_count != 0 ? (100.0 * covered_pixels) / cropped_pixel_count : 0.0));
			}
		}
		catch (...)
		{
			m_static_cache_valid = false;
			m_static_runs.clear();
			m_static_cache_fallback = true;
			m_static_cache_validation_done = true;
			m_static_cache_fallback_reason = "cache build exception";
			append_static_cache_log(util::string_format("[VES static cache validation] machine=%s view=\"%s\" result=FALLBACK reason=\"%s\"",
				m_static_cache_machine_name.c_str(), m_static_cache_view_name.c_str(), m_static_cache_fallback_reason.c_str()));
			return false;
		}

		m_static_runs = std::move(runs);
		m_static_cache_valid = true;
		m_static_cache_fallback = false;
		m_static_items = static_items;
		m_static_stateful_items = stateful_items;
		m_static_display_items = display_items;
		m_static_unsupported_blend_items = unsupported_blend_items;
		m_static_run_count = static_run_count;
		m_static_cache_build_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - build_start).count());
		m_static_full_render_equivalent_us = full_render_equivalent_us;
		m_static_cache_memory_bytes = 0;
		for (const auto &run : m_static_runs)
			m_static_cache_memory_bytes += run.pixels.size() * sizeof(std::uint32_t);
		m_static_full_frame_equivalent_bytes = full_frame_equivalent_bytes;
		m_static_cropped_cache_memory_bytes_estimate = cropped_cache_memory_bytes;
		append_static_cache_log(util::string_format("cache_build machine=%s view=\"%s\" static_items=%u stateful_items=%u display_items=%u static_runs=%u cache_build_us=%llu old_full_frame_equivalent_bytes=%llu actual_cropped_cache_bytes=%llu cropped_cache_memory_bytes_estimate=%llu memory_reduction_percent=%.2f full_render_equivalent_us=%llu unsupported_blend_items=%u",
			m_static_cache_machine_name.c_str(), m_static_cache_view_name.c_str(),
			m_static_items, m_static_stateful_items, m_static_display_items, m_static_run_count,
			static_cast<unsigned long long>(m_static_cache_build_us),
			static_cast<unsigned long long>(m_static_full_frame_equivalent_bytes),
			static_cast<unsigned long long>(m_static_cache_memory_bytes),
			static_cast<unsigned long long>(m_static_cropped_cache_memory_bytes_estimate),
			m_static_full_frame_equivalent_bytes != 0 ? 100.0 * (1.0 - double(m_static_cache_memory_bytes) / double(m_static_full_frame_equivalent_bytes)) : 0.0,
			static_cast<unsigned long long>(m_static_full_render_equivalent_us),
			m_static_unsupported_blend_items));
		return true;
	}

	bool compare_static_cache_frames(const std::uint32_t *cached_pixels, const std::uint32_t *full_pixels, int width, int height, std::uint64_t comparison_us)
	{
		const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
		std::uint64_t different_pixels = 0;
		std::uint32_t maximum_channel_difference = 0;
		std::uint64_t severe_pixels_gt2 = 0;
		std::uint64_t severe_pixels_gt4 = 0;
		std::uint64_t severe_pixels_gt8 = 0;
		std::uint64_t total_channel_absolute_error = 0;
		long double total_channel_squared_error = 0.0L;
		int min_x = width;
		int min_y = height;
		int max_x = -1;
		int max_y = -1;
		for (int y = 0; y < height; ++y)
			for (int x = 0; x < width; ++x)
			{
				const auto cached_pixel = cached_pixels[static_cast<std::size_t>(y) * width + x];
				const auto full_pixel = full_pixels[static_cast<std::size_t>(y) * width + x];
				const auto red_difference = static_cast<unsigned>(std::abs(int((cached_pixel >> 16) & 0xffU) - int((full_pixel >> 16) & 0xffU)));
				const auto green_difference = static_cast<unsigned>(std::abs(int((cached_pixel >> 8) & 0xffU) - int((full_pixel >> 8) & 0xffU)));
				const auto blue_difference = static_cast<unsigned>(std::abs(int(cached_pixel & 0xffU) - int(full_pixel & 0xffU)));
				const auto pixel_difference = std::max({ red_difference, green_difference, blue_difference });
				total_channel_absolute_error += red_difference + green_difference + blue_difference;
				total_channel_squared_error += static_cast<long double>(red_difference * red_difference)
					+ static_cast<long double>(green_difference * green_difference)
					+ static_cast<long double>(blue_difference * blue_difference);
				maximum_channel_difference = std::max(maximum_channel_difference, pixel_difference);
				if (pixel_difference != 0)
				{
					++different_pixels;
					if (pixel_difference > 2)
						++severe_pixels_gt2;
					if (pixel_difference > 4)
						++severe_pixels_gt4;
					if (pixel_difference > 8)
						++severe_pixels_gt8;
					min_x = std::min(min_x, x);
					min_y = std::min(min_y, y);
					max_x = std::max(max_x, x);
					max_y = std::max(max_y, y);
				}
			}
		const double difference_percent = pixel_count != 0 ? (100.0 * different_pixels) / pixel_count : 0.0;
		const double severe_percent_gt2 = pixel_count != 0 ? (100.0 * severe_pixels_gt2) / pixel_count : 0.0;
		const double severe_percent_gt4 = pixel_count != 0 ? (100.0 * severe_pixels_gt4) / pixel_count : 0.0;
		const double severe_percent_gt8 = pixel_count != 0 ? (100.0 * severe_pixels_gt8) / pixel_count : 0.0;
		const auto channel_count = pixel_count * 3;
		const double mean_absolute_channel_error = channel_count != 0 ? static_cast<double>(total_channel_absolute_error) / static_cast<double>(channel_count) : 0.0;
		const double rms_channel_error = channel_count != 0 ? std::sqrt(static_cast<double>(total_channel_squared_error / static_cast<long double>(channel_count))) : 0.0;
		const bool rounding_noise_only = maximum_channel_difference <= 2
			&& severe_pixels_gt2 == 0
			&& mean_absolute_channel_error <= 1.0
			&& rms_channel_error <= 1.5;
		const bool minor_noise_only = maximum_channel_difference <= 8
			&& severe_percent_gt8 <= 0.0001
			&& severe_percent_gt4 <= 0.001
			&& severe_percent_gt2 <= 0.01
			&& mean_absolute_channel_error <= 0.10
			&& rms_channel_error <= 0.50;
		const bool accepted = rounding_noise_only || minor_noise_only;
		osd_printf_verbose("[VES static cache compare] different_pixels=%llu percentage=%.4f bbox=%d,%d-%d,%d max_channel_difference=%u mean_abs_channel_error=%.6f rms_channel_error=%.6f severe_percent_gt2=%.6f severe_percent_gt4=%.6f severe_percent_gt8=%.6f result=%s\n",
			static_cast<unsigned long long>(different_pixels),
			difference_percent,
			min_x, min_y, max_x, max_y, maximum_channel_difference,
			mean_absolute_channel_error, rms_channel_error,
			severe_percent_gt2, severe_percent_gt4, severe_percent_gt8,
			accepted ? "ACCEPTED" : "FALLBACK");
		append_static_cache_log(util::string_format("[VES static cache validation] machine=%s view=\"%s\" static_items=%u stateful_items=%u display_items=%u static_runs=%u different_pixels=%llu difference_percent=%.4f severe_pixels_gt2=%llu severe_percent_gt2=%.6f severe_pixels_gt4=%llu severe_percent_gt4=%.6f severe_pixels_gt8=%llu severe_percent_gt8=%.6f bbox=%d,%d-%d,%d max_channel_difference=%u mean_abs_channel_error=%.6f rms_channel_error=%.6f cache_build_us=%llu old_full_frame_equivalent_bytes=%llu actual_cropped_cache_bytes=%llu cache_memory_bytes=%llu cropped_cache_memory_bytes_estimate=%llu memory_reduction_percent=%.2f full_render_reference_us=%llu cached_dynamic_render_us_avg=%llu cached_dynamic_render_us_max=%llu cached_composite_us_avg=%llu cached_composite_us_max=%llu total_cached_frame_us_avg=%llu total_cached_frame_us_max=%llu unsupported_blend_items=%u result=%s comparison_us=%llu",
			m_static_cache_machine_name.c_str(), m_static_cache_view_name.c_str(),
			m_static_items, m_static_stateful_items, m_static_display_items, m_static_run_count,
			static_cast<unsigned long long>(different_pixels), difference_percent,
			static_cast<unsigned long long>(severe_pixels_gt2), severe_percent_gt2,
			static_cast<unsigned long long>(severe_pixels_gt4), severe_percent_gt4,
			static_cast<unsigned long long>(severe_pixels_gt8), severe_percent_gt8,
			min_x, min_y, max_x, max_y, maximum_channel_difference,
			mean_absolute_channel_error, rms_channel_error,
			static_cast<unsigned long long>(m_static_cache_build_us),
			static_cast<unsigned long long>(m_static_full_frame_equivalent_bytes),
			static_cast<unsigned long long>(m_static_cache_memory_bytes),
			static_cast<unsigned long long>(m_static_cache_memory_bytes),
			static_cast<unsigned long long>(m_static_cropped_cache_memory_bytes_estimate),
			m_static_full_frame_equivalent_bytes != 0 ? 100.0 * (1.0 - double(m_static_cache_memory_bytes) / double(m_static_full_frame_equivalent_bytes)) : 0.0,
			static_cast<unsigned long long>(m_static_full_render_equivalent_us),
			static_cast<unsigned long long>(m_static_dynamic_render_count != 0 ? m_static_dynamic_render_total_us / m_static_dynamic_render_count : 0),
			static_cast<unsigned long long>(m_static_dynamic_render_max_us),
			static_cast<unsigned long long>(m_static_cached_frame_count != 0 ? m_static_composite_total_us / m_static_cached_frame_count : 0),
			static_cast<unsigned long long>(m_static_composite_max_us),
			static_cast<unsigned long long>(m_static_cached_frame_count != 0 ? m_static_cached_frame_total_us / m_static_cached_frame_count : 0),
			static_cast<unsigned long long>(m_static_cached_frame_max_us),
			m_static_unsupported_blend_items,
			accepted ? "ACCEPTED" : "FALLBACK",
			static_cast<unsigned long long>(comparison_us)));
		if (!accepted)
		{
			m_static_cache_valid = false;
			m_static_runs.clear();
			m_static_cache_fallback = true;
			m_static_cache_fallback_reason = util::string_format("validation different_pixels=%llu difference_percent=%.4f severe_percent_gt2=%.6f severe_percent_gt4=%.6f severe_percent_gt8=%.6f max_channel_difference=%u mean_abs_channel_error=%.6f rms_channel_error=%.6f",
				static_cast<unsigned long long>(different_pixels), difference_percent,
				severe_percent_gt2, severe_percent_gt4, severe_percent_gt8,
				maximum_channel_difference, mean_absolute_channel_error, rms_channel_error);
		}
		m_static_cache_validation_done = true;
		return accepted;
	}

	bool render_static_cached_frame(int width, int height, std::uint32_t *pixels, std::uint64_t &dynamic_us, std::uint64_t &composite_us)
	{
		const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
		dynamic_us = 0;
		composite_us = 0;
		std::fill(pixels, pixels + pixel_count, 0xff000000U);
		for (const auto &run : m_static_runs)
		{
			if (run.is_static)
			{
				const auto composite_start = std::chrono::steady_clock::now();
				composite_cached_rgb_region(pixels, width, run);
				composite_us += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - composite_start).count());
				++m_static_cache_hits;
			}
			else
			{
				const auto dynamic_start = std::chrono::steady_clock::now();
				auto &run_list = m_video_target->get_primitives(run.first_item, run.item_count, false);
				embedded_video_renderer::draw_primitives(run_list, pixels, static_cast<u32>(width), static_cast<u32>(height), static_cast<u32>(width));
				dynamic_us += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - dynamic_start).count());
			}
		}
		return true;
	}

	bool render_static_cached(int width, int height, std::uint32_t *pixels)
	{
		if (!static_cache_supported())
			return false;
		const auto &view = m_video_target->current_view();
		const bool cache_context_changed = (m_static_cache_valid || m_static_cache_fallback)
			&& (m_static_cache_machine_name != machine().basename()
				|| m_static_cache_view_name != view.name()
				|| m_static_cache_visibility_mask != m_video_target->visibility_mask()
				|| m_static_cache_orientation != m_video_target->orientation()
				|| m_static_cache_pixel_aspect != m_video_target->pixel_aspect()
				|| m_static_cache_view_aspect != view.effective_aspect()
				|| m_static_cache_width != width
				|| m_static_cache_height != height);
		if (cache_context_changed)
			invalidate_static_cache();
		if (m_static_cache_fallback)
			return false;
		if (!m_static_cache_valid || m_static_cache_width != width || m_static_cache_height != height)
			if (!build_static_cache(width, height))
				return false;

		const auto cached_start = std::chrono::steady_clock::now();
		std::uint64_t dynamic_us = 0;
		std::uint64_t composite_us = 0;
		render_static_cached_frame(width, height, pixels, dynamic_us, composite_us);
		const auto cached_us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - cached_start).count());
		m_static_dynamic_render_total_us += dynamic_us;
		m_static_dynamic_render_max_us = std::max(m_static_dynamic_render_max_us, dynamic_us);
		m_static_composite_total_us += composite_us;
		m_static_composite_max_us = std::max(m_static_composite_max_us, composite_us);
		m_static_cached_frame_total_us += cached_us;
		m_static_cached_frame_max_us = std::max(m_static_cached_frame_max_us, cached_us);
		++m_static_dynamic_render_count;
		++m_static_cached_frame_count;

		if (!m_static_cache_validation_done)
		{
			const auto comparison_start = std::chrono::steady_clock::now();
			const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
			std::vector<std::uint32_t> full_pixels(pixel_count, 0xff000000U);
			auto &full_list = m_video_target->get_primitives();
			embedded_video_renderer::draw_primitives(full_list, full_pixels.data(), static_cast<u32>(width), static_cast<u32>(height), static_cast<u32>(width));
			if (!compare_static_cache_frames(pixels, full_pixels.data(), width, height,
				static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - comparison_start).count())))
			{
				std::copy(full_pixels.begin(), full_pixels.end(), pixels);
				return false;
			}
		}
		return true;
	}

	void input_update(bool relative_reset) override
	{
		poll_input_modules(relative_reset);
	}

	void check_osd_inputs() override {}
	void process_events() override {}
	bool has_focus() const override { return true; }

	osd_options &options() override { return m_options; }

	bool no_sound() override { return false; }
	bool sound_external_per_channel_volume() override { return false; }
	bool sound_split_streams_per_source() override { return false; }
	std::uint32_t sound_get_generation() override { return 1; }

	osd::audio_info sound_get_information() override
	{
		osd::audio_info info;
		info.m_generation = 1;
		info.m_default_sink = 1;
		info.m_default_source = 0;

		osd::audio_info::node_info sink;
		sink.m_name = "ves";
		sink.m_display_name = "VintageEmulatorStudio Embedded Sink";
		sink.m_id = 1;
		sink.m_rate = osd::audio_rate_range{
			static_cast<std::uint32_t>(m_options.sample_rate()),
			1'000,
			1'000'000 };
		sink.m_port_names = { "left", "right" };
		sink.m_port_positions = { osd::channel_position::FL(), osd::channel_position::FR() };
		sink.m_sinks = 2;
		sink.m_sources = 0;
		info.m_nodes.emplace_back(std::move(sink));
		return info;
	}

	std::uint32_t sound_stream_sink_open(std::uint32_t, std::string, std::uint32_t) override
	{
		m_diag.audio_sink_open_count.fetch_add(1, std::memory_order_relaxed);
		return 1;
	}

	std::uint32_t sound_stream_source_open(std::uint32_t, std::string, std::uint32_t) override
	{
		return 0;
	}

	void sound_stream_set_volumes(std::uint32_t, const std::vector<float> &) override {}
	void sound_stream_close(std::uint32_t) override {}

	void sound_stream_sink_update(std::uint32_t, const std::int16_t *buffer, int samples_this_frame) override
	{
		if (m_diag.state_restore_first_timeslice_completed_ms.load(std::memory_order_acquire) != 0)
		{
			const auto callback_ms = steadyMs();
			const auto first = m_diag.state_restore_first_audio_ms.load(std::memory_order_acquire);
			if (first == 0)
			{
				std::uint64_t expected = 0;
				m_diag.state_restore_first_audio_ms.compare_exchange_strong(expected, callback_ms, std::memory_order_release, std::memory_order_relaxed);
			}
			else if (callback_ms > first && callback_ms - first <= 100)
			{
				std::uint64_t expected = 0;
				m_diag.state_restore_second_audio_ms.compare_exchange_strong(expected, callback_ms, std::memory_order_release, std::memory_order_relaxed);
			}
			else if (callback_ms > first)
			{
				// Start a new candidate pair after a long gap.  The guard is released
				// only after two callbacks demonstrate plausible realtime cadence.
				m_diag.state_restore_first_audio_ms.store(callback_ms, std::memory_order_release);
				m_diag.state_restore_second_audio_ms.store(0, std::memory_order_release);
			}
		}
		if (samples_this_frame <= 0)
			return;

		static constexpr float scale = 1.0f / 32768.0f;
		float block_peak = 0.0f;
		const auto requested = static_cast<std::size_t>(samples_this_frame);
		const auto written = m_audio_queue.push_bulk(requested, [&](StereoFrame *dest, std::size_t count, std::size_t source_offset)
		{
			const auto *src = buffer + (source_offset * 2);
			for (std::size_t i = 0; i < count; ++i)
			{
				const StereoFrame frame {
					static_cast<float>(src[i * 2]) * scale,
					static_cast<float>(src[i * 2 + 1]) * scale };

				block_peak = std::max(block_peak, std::max(std::fabs(frame.left), std::fabs(frame.right)));
				dest[i] = frame;
			}
		});

		if (written != 0)
			m_diag.audio_frames_written.fetch_add(written, std::memory_order_relaxed);

		const auto dropped = requested - written;
		if (dropped != 0)
		{
			const auto *src = buffer + (written * 2);
			for (std::size_t i = 0; i < dropped; ++i)
			{
				const auto left = static_cast<float>(src[i * 2]) * scale;
				const auto right = static_cast<float>(src[i * 2 + 1]) * scale;
				block_peak = std::max(block_peak, std::max(std::fabs(left), std::fabs(right)));
			}
		}

		if (block_peak > 0.0f)
		{
			auto current_peak = m_diag.peak_abs.load(std::memory_order_relaxed);
			while (block_peak > current_peak && !m_diag.peak_abs.compare_exchange_weak(current_peak, block_peak, std::memory_order_relaxed))
			{
			}
		}

		if (dropped != 0)
		{
			m_diag.audio_frames_dropped.fetch_add(dropped, std::memory_order_relaxed);
			m_diag.audio_overflows.fetch_add(dropped, std::memory_order_relaxed);
		}

		if (++m_audio_sink_diagnostics_counter >= k_audio_sink_diagnostics_interval || dropped != 0)
		{
			m_audio_sink_diagnostics_counter = 0;
			update_audio_queue_diagnostics(m_diag, m_audio_queue.size());
		}
	}

	void sound_stream_source_update(std::uint32_t, std::int16_t *, int) override {}
	void sound_begin_update() override {}
	void sound_end_update() override {}

	std::unique_ptr<osd::midi_input_port> create_midi_input(std::string_view name) override
	{
		if (name != "ves")
		{
			const std::string name_string(name);
			osd_printf_warning("Embedded TX81Z test expected MIDI input 'ves', got '%s'\n", name_string.c_str());
		}

		m_diag.midi_input_open_count.fetch_add(1, std::memory_order_relaxed);
		return std::make_unique<memory_midi_input_port>(m_midi_queue, m_diag);
	}

	std::unique_ptr<osd::midi_output_port> create_midi_output(std::string_view) override
	{
		m_diag.midi_output_open_count.fetch_add(1, std::memory_order_relaxed);
		return std::make_unique<memory_midi_output_port>();
	}

	std::vector<osd::midi_port_info> list_midi_ports() override
	{
		return {
			osd::midi_port_info { "ves", true, true, true, true }
		};
	}

private:
	static std::string state_error_message(save_error error)
	{
		switch (error)
		{
		case STATERR_NONE: return {};
		case STATERR_INVALID_HEADER: return "MAME rejected the snapshot header";
		case STATERR_READ_ERROR: return "MAME could not read the snapshot stream";
		case STATERR_WRITE_ERROR: return "MAME could not write the snapshot stream";
		default: return "MAME save manager returned error " + std::to_string(static_cast<int>(error));
		}
	}

	void process_state_requests()
	{
		constexpr std::uint64_t state_scheduler_wait_timeout_ms = 2000;
		if (!m_pending_state_request)
		{
			if (!m_state_requests.pop(m_current_state_request))
				return;
			m_pending_state_request = true;
			m_state_scheduler_wait_started_ms = steadyMs();
			m_diag.state_request_received_ms.store(m_state_scheduler_wait_started_ms, std::memory_order_relaxed);
			if (m_current_state_request.operation == StateOperation::Load)
			{
				m_audio_queue.clear();
				m_midi_queue.clear();
			}
		}
		if (machine().phase() != machine_phase::RUNNING || !machine().scheduler().can_save())
		{
			const auto waited_ms = steadyMs() - m_state_scheduler_wait_started_ms;
			if (waited_ms < state_scheduler_wait_timeout_ms)
				return;

			StateOperationResult timeout_result;
			timeout_result.operation = m_current_state_request.operation;
			timeout_result.engine_generation = m_current_state_request.engine_generation;
			timeout_result.request_id = m_current_state_request.request_id;
			timeout_result.scheduler_wait_ms = waited_ms;
			timeout_result.total_duration_ms = waited_ms;
			timeout_result.error_message = "Timed out waiting for a safe scheduler save-state boundary";
			const bool save = timeout_result.operation == StateOperation::Save;
			(save ? m_diag.state_save_failures : m_diag.state_load_failures).fetch_add(1, std::memory_order_relaxed);
			m_state_results.push(timeout_result);
			m_pending_state_request = false;
			return;
		}

		StateOperationResult result;
		result.operation = m_current_state_request.operation;
		result.engine_generation = m_current_state_request.engine_generation;
		result.request_id = m_current_state_request.request_id;
		result.scheduler_wait_ms = steadyMs() - m_state_scheduler_wait_started_ms;
		if (result.operation == StateOperation::Save)
		{
			std::ostringstream stream(std::ios::binary);
			const auto error = machine().save().write_stream(stream);
			if (error == STATERR_NONE)
			{
				m_state_snapshot = stream.str();
				for (std::size_t i = 0; i < m_state_snapshot_held_notes.size(); ++i)
					m_state_snapshot_held_notes[i] = m_active_host_midi_notes[i].load(std::memory_order_acquire);
				result.snapshot_blob.assign(m_state_snapshot.begin(), m_state_snapshot.end());
				result.held_midi_notes = m_state_snapshot_held_notes;
				result.success = true;
				result.snapshot_size = m_state_snapshot.size();
				m_diag.state_snapshot_bytes.store(result.snapshot_size, std::memory_order_relaxed);
			}
			else
				result.error_message = state_error_message(error);
		}
		else if (m_current_state_request.snapshot_blob.empty() && m_state_snapshot.empty())
			result.error_message = "No in-memory state snapshot has been captured";
		else
		{
			if (!m_current_state_request.snapshot_blob.empty())
			{
				m_state_snapshot.assign(m_current_state_request.snapshot_blob.begin(), m_current_state_request.snapshot_blob.end());
				m_state_snapshot_held_notes = m_current_state_request.held_midi_notes;
			}
			result.held_midi_notes = m_state_snapshot_held_notes;
			std::istringstream stream(m_state_snapshot, std::ios::binary);
			m_diag.state_restore_read_completed_ms.store(0, std::memory_order_release);
			m_diag.state_restore_first_timeslice_completed_ms.store(0, std::memory_order_release);
			m_diag.state_restore_first_audio_ms.store(0, std::memory_order_release);
			m_diag.state_restore_second_audio_ms.store(0, std::memory_order_release);
			m_diag.state_restore_first_video_ms.store(0, std::memory_order_release);
			m_diag.state_restore_first_video_generation.store(0, std::memory_order_release);
			m_diag.state_restore_cache_build_start_ms.store(0, std::memory_order_release);
			m_diag.state_restore_cache_build_end_ms.store(0, std::memory_order_release);
			m_diag.state_restore_mouse_queue_cleared.store(0, std::memory_order_release);
			m_diag.state_restore_pointer_reset.store(false, std::memory_order_release);
			m_diag.state_restore_view_rebound.store(false, std::memory_order_release);
			m_diag.state_restore_interactive_items_before.store(0, std::memory_order_release);
			m_diag.state_restore_interactive_items_after.store(0, std::memory_order_release);
			m_diag.state_restore_first_mouse_event_ms.store(0, std::memory_order_release);
			m_diag.state_restore_first_input_hit_ms.store(0, std::memory_order_release);
			const auto stream_started_ms = steadyMs();
			m_diag.state_restore_read_started_ms.store(stream_started_ms, std::memory_order_release);
			const auto error = machine().save().read_stream(stream);
			result.stream_duration_ms = steadyMs() - stream_started_ms;
			if (error == STATERR_NONE)
			{
				m_diag.state_restore_read_completed_ms.store(steadyMs(), std::memory_order_release);
				unsigned midi_transport_resets = 0;
				for (device_t &device : device_enumerator(machine().root_device()))
					if (auto *midi_input = dynamic_cast<midiin_device *>(&device))
					{
						midi_input->reset_transport_after_state_load();
						++midi_transport_resets;
					}
				if (midi_transport_resets != 0)
					append_engine_lifecycle_log("[state restore] MIDI transport reset driver=" + std::string(machine().basename()));
				reset_post_restore_video_and_input();
				m_restore_waiting_for_clean_timeslice = true;
				result.success = true;
				result.snapshot_size = m_state_snapshot.size();
				m_audio_queue.clear();
				m_midi_queue.clear();
			}
			else
			{
				result.error_message = state_error_message(error);
				result.load_failed_requires_restart = true;
			}
		}
		result.total_duration_ms = m_current_state_request.requested_at_ms != 0
			? steadyMs() - m_current_state_request.requested_at_ms
			: result.scheduler_wait_ms + result.stream_duration_ms;

		const bool save = result.operation == StateOperation::Save;
		(save ? (result.success ? m_diag.state_save_successes : m_diag.state_save_failures)
		      : (result.success ? m_diag.state_load_successes : m_diag.state_load_failures)).fetch_add(1, std::memory_order_relaxed);
		m_state_results.push(result);
		m_pending_state_request = false;
	}

	void process_midi_panic_request()
	{
		if (!m_midi_panic_requested.load(std::memory_order_relaxed))
			return;
		if (!m_midi_panic_requested.exchange(false, std::memory_order_acq_rel))
			return;

		m_midi_queue.clear();
		const auto now = steadyMs();
		const auto queue_byte = [this, now](std::uint8_t value)
		{
			if (m_midi_queue.push(queued_midi_byte { value, now }))
				m_diag.midi_bytes_queued.fetch_add(1, std::memory_order_relaxed);
			else
				m_diag.midi_queue_overflow.fetch_add(1, std::memory_order_relaxed);
		};
		for (std::uint8_t channel = 0; channel < 16; ++channel)
		{
			for (std::uint8_t note = 0; note < 128; ++note)
			{
				const auto index = static_cast<std::size_t>(channel) * 2 + (note >> 6);
				if (!BIT(m_midi_panic_held_notes[index].load(std::memory_order_acquire), note & 63))
					continue;
				queue_byte(static_cast<std::uint8_t>(0x80U | channel));
				queue_byte(note);
				queue_byte(0);
			}
		}
		for (std::uint8_t channel = 0; channel < 16; ++channel)
		{
			for (const std::uint8_t controller : { std::uint8_t(64), std::uint8_t(123),
				std::uint8_t(120), std::uint8_t(121) })
			{
				queue_byte(static_cast<std::uint8_t>(0xb0U | channel));
				queue_byte(controller);
				queue_byte(0);
			}
			queue_byte(static_cast<std::uint8_t>(0xe0U | channel));
			queue_byte(0);
			queue_byte(64);
		}
		m_diag.midi_first_queued_ms.store(now, std::memory_order_relaxed);
		m_diag.midi_last_queued_ms.store(now, std::memory_order_relaxed);
	}

	void reset_post_restore_video_and_input()
	{
		invalidate_video_jobs();
		const auto mouse_cleared = m_mouse_queue.clear();
		m_diag.state_restore_mouse_queue_cleared.store(mouse_cleared, std::memory_order_release);
		const bool was_pressed = m_diag.mouse_left_down.exchange(false, std::memory_order_acq_rel);
		m_diag.mouse_release_pending.store(false, std::memory_order_release);
		m_diag.mouse_current_x.store(-1, std::memory_order_relaxed);
		m_diag.mouse_current_y.store(-1, std::memory_order_relaxed);
		m_diag.mouse_pointer_target_index.store(0, std::memory_order_relaxed);
		m_diag.mouse_hit_item.store(false, std::memory_order_relaxed);
		m_diag.mouse_hit_input_tag.store(0, std::memory_order_relaxed);
		m_diag.mouse_hit_input_mask.store(0, std::memory_order_relaxed);
		m_diag.mouse_input_field_active.store(false, std::memory_order_relaxed);

		ui_event event;
		while (machine().ui_input().pop_event(&event)) { }

		if (m_video_target != nullptr)
		{
			const auto before = m_video_target->current_view().interactive_items().size();
			if (was_pressed)
				m_video_target->pointer_aborted(osd::ui_event_handler::pointer::MOUSE, 0, 0, 0, 0, 1, 0);
			const auto view = m_video_target->view();
			m_video_target->set_view(view);
			m_diag.state_restore_interactive_items_before.store(before, std::memory_order_relaxed);
			m_diag.state_restore_interactive_items_after.store(m_video_target->current_view().interactive_items().size(), std::memory_order_relaxed);
			m_diag.state_restore_view_rebound.store(true, std::memory_order_release);
		}
		m_diag.state_restore_pointer_reset.store(true, std::memory_order_release);
		m_video_bridge.invalidatePublishedFrames(m_diag);
		osd_printf_verbose("[VES state restore] mouse queue cleared=%llu pointer reset=yes view rebound=%s interactive_items=%llu->%llu\n",
			static_cast<unsigned long long>(mouse_cleared),
			m_video_target != nullptr ? "yes" : "no",
			static_cast<unsigned long long>(m_diag.state_restore_interactive_items_before.load(std::memory_order_relaxed)),
			static_cast<unsigned long long>(m_diag.state_restore_interactive_items_after.load(std::memory_order_relaxed)));
	}

	void process_floppy_requests()
	{
		FloppyChangeRequest request;
		if (!m_floppy_requests.pop(request))
			return;

		FloppyChangeResult result;
		result.media_type = request.media_type;
		result.engine_generation = request.engine_generation;
		result.request_id = request.request_id;
		device_image_interface *image = nullptr;
		for (device_image_interface &candidate : image_interface_enumerator(machine().root_device()))
			if (candidate.brief_instance_name() == request.brief_instance_name)
			{
				image = &candidate;
				break;
			}
		if (image == nullptr)
		{
			result.error_message = request.media_type == EmbeddedMediaType::CdRom
				? "CD-ROM image device was not found"
				: "Floppy image device was not found";
			m_floppy_results.push(result);
			return;
		}

		if (request.path.empty())
		{
			image->unload();
			result.success = true;
			result.drive_empty = true;
		}
		else
		{
			auto const [error, message] = image->load(request.path);
			result.success = !error;
			result.drive_empty = !result.success;
			result.applied_path = result.success ? request.path : std::string();
			result.error_message = message.empty() ? (error ? error.message() : std::string()) : message;
			result.error_value = error.value();
		}
		m_floppy_results.push(result);
	}

	void process_embedded_ui_pointer_events()
	{
		if (m_video_target == nullptr)
			return;

		ui_event event;
		while (machine().ui_input().pop_event(&event))
		{
			if (event.target != m_video_target)
				continue;

			switch (event.event_type)
			{
			case ui_event::type::POINTER_UPDATE:
				m_video_target->pointer_updated(event.pointer_type, event.pointer_id, event.pointer_device,
					event.pointer_x, event.pointer_y, event.pointer_buttons, event.pointer_pressed,
					event.pointer_released, event.pointer_clicks);
				break;
			case ui_event::type::POINTER_LEAVE:
				m_video_target->pointer_left(event.pointer_type, event.pointer_id, event.pointer_device,
					event.pointer_x, event.pointer_y, event.pointer_released, event.pointer_clicks);
				break;
			case ui_event::type::POINTER_ABORT:
				m_video_target->pointer_aborted(event.pointer_type, event.pointer_id, event.pointer_device,
					event.pointer_x, event.pointer_y, event.pointer_released, event.pointer_clicks);
				break;
			default:
				break;
			}
		}

		m_video_target->update_pointer_fields();
	}

	void consume_mouse_events()
	{
		if (m_diag.stop_requested.load(std::memory_order_acquire))
		{
			m_diag.mouse_forwarding_enabled.store(false, std::memory_order_release);
			m_mouse_queue.clear();
			if (m_diag.mouse_left_down.load(std::memory_order_acquire))
				m_diag.mouse_release_pending.store(true, std::memory_order_release);
		}

		if (m_video_target == nullptr)
			return;

		if (m_diag.mouse_release_pending.exchange(false, std::memory_order_acq_rel))
		{
			m_mouse_queue.clear();
			const auto x = std::max(0, m_diag.mouse_current_x.load(std::memory_order_relaxed));
			const auto y = std::max(0, m_diag.mouse_current_y.load(std::memory_order_relaxed));
			m_video_target->push_pointer_update(
				osd::ui_event_handler::pointer::MOUSE, 0, 0,
				x, y, 0, 0, 1, 0);
			m_diag.mouse_left_down.store(false, std::memory_order_release);
			m_diag.mouse_synthetic_release_count.fetch_add(1, std::memory_order_relaxed);
		}

		EmbeddedMouseEvent event;
		while (m_diag.mouse_forwarding_enabled.load(std::memory_order_acquire) && m_mouse_queue.pop(event))
		{
			if (m_diag.state_restore_read_completed_ms.load(std::memory_order_acquire) != 0)
			{
				std::uint64_t expected = 0;
				if (m_diag.state_restore_first_mouse_event_ms.compare_exchange_strong(expected, steadyMs(), std::memory_order_release, std::memory_order_relaxed))
					osd_printf_verbose("[VES state restore] first mouse event consumed\n");
			}
			const auto left_down = event.type == EmbeddedMouseEventType::LeftDown;
			const auto left_up = event.type == EmbeddedMouseEventType::LeftUp;
			const auto buttons = (left_down || (!left_up && m_diag.mouse_left_down.load(std::memory_order_relaxed))) ? 1U : 0U;
			const auto pressed = left_down ? 1U : 0U;
			const auto released = left_up ? 1U : 0U;
			m_diag.mouse_pointer_target_index.store(static_cast<std::uint64_t>(m_video_target->index()), std::memory_order_relaxed);
			diagnose_pointer_hit(event.x, event.y);
			m_video_target->push_pointer_update(
				osd::ui_event_handler::pointer::MOUSE, 0, 0,
				event.x, event.y, buttons, pressed, released, 0);
			m_diag.mouse_current_x.store(event.x, std::memory_order_relaxed);
			m_diag.mouse_current_y.store(event.y, std::memory_order_relaxed);
			m_diag.mouse_last_event_type.store(static_cast<std::uint64_t>(event.type), std::memory_order_relaxed);
			m_diag.mouse_events_consumed.fetch_add(1, std::memory_order_relaxed);
			if (left_down)
				m_diag.mouse_left_down.store(true, std::memory_order_release);
			else if (left_up)
				m_diag.mouse_left_down.store(false, std::memory_order_release);
		}
	}

	void diagnose_pointer_hit(std::int32_t x, std::int32_t y)
	{
		m_diag.mouse_hit_item.store(false, std::memory_order_relaxed);
		m_diag.mouse_hit_input_tag.store(0, std::memory_order_relaxed);
		m_diag.mouse_hit_input_mask.store(0, std::memory_order_relaxed);
		m_diag.mouse_input_field_active.store(false, std::memory_order_relaxed);

		s32 visible_width = 0;
		s32 visible_height = 0;
		m_video_target->compute_visible_area(static_cast<s32>(m_video_target->width()), static_cast<s32>(m_video_target->height()),
			m_video_target->pixel_aspect(), m_video_target->orientation(), visible_width, visible_height);
		if (visible_width <= 0 || visible_height <= 0)
			return;

		float layout_x = (static_cast<float>(x) - (m_video_target->width() - visible_width) * 0.5f) / visible_width;
		float layout_y = (static_cast<float>(y) - (m_video_target->height() - visible_height) * 0.5f) / visible_height;
		if (m_video_target->orientation() & ORIENTATION_FLIP_X)
			layout_x = 1.0f - layout_x;
		if (m_video_target->orientation() & ORIENTATION_FLIP_Y)
			layout_y = 1.0f - layout_y;
		if (m_video_target->orientation() & ORIENTATION_SWAP_XY)
			std::swap(layout_x, layout_y);

		const auto &items = m_video_target->current_view().interactive_items();
		for (auto item = items.rbegin(); item != items.rend(); ++item)
		{
			if (!item->get().bounds().includes(layout_x, layout_y))
				continue;
			const auto [port, mask] = item->get().input_tag_and_mask();
			if (port == nullptr)
				continue;
			m_diag.mouse_hit_item.store(true, std::memory_order_relaxed);
			m_diag.mouse_hit_input_mask.store(static_cast<std::uint64_t>(mask), std::memory_order_relaxed);
			const auto tag = std::string(port->tag());
			if (tag == "P2") m_diag.mouse_hit_input_tag.store(2, std::memory_order_relaxed);
			else if (tag == "P5") m_diag.mouse_hit_input_tag.store(5, std::memory_order_relaxed);
			else if (tag == "P6") m_diag.mouse_hit_input_tag.store(6, std::memory_order_relaxed);
			if (auto *field = port->field(mask))
			{
				m_diag.mouse_input_field_active.store(field->digital_value(), std::memory_order_relaxed);
				if (m_diag.state_restore_read_completed_ms.load(std::memory_order_acquire) != 0)
				{
					std::uint64_t expected = 0;
					if (m_diag.state_restore_first_input_hit_ms.compare_exchange_strong(expected, steadyMs(), std::memory_order_release, std::memory_order_relaxed))
						osd_printf_verbose("[VES state restore] first input-field hit\n");
				}
			}
			return;
		}
	}

	void release_video_target()
	{
		if (m_video_target != nullptr)
			invalidate_video_jobs();
		invalidate_static_cache();
		if (m_video_target != nullptr)
		{
			machine().render().target_free(m_video_target);
			m_video_target = nullptr;
		}
		m_diag.video_render_target_available.store(false, std::memory_order_relaxed);
	}

	bool ensure_video_target()
	{
		if (m_video_target != nullptr)
			return true;

		constexpr u32 capture_flags = RENDER_CREATE_HIDDEN | RENDER_CREATE_CAPTURE;
		m_video_target = machine().render().target_alloc(nullptr, capture_flags);
		if (m_video_target == nullptr)
		{
			m_diag.video_last_error_code.store(2, std::memory_order_relaxed);
			return false;
		}
		// Dynamic targets are allocated after running_machine::start() has
		// resolved tags for the targets that existed during startup.
		m_video_target->resolve_tags();

		unsigned selected_view = 0;
		bool usable_view = false;
		for (unsigned index = 0; m_video_target->view_name(index) != nullptr; ++index)
		{
			const auto *name = m_video_target->view_name(index);
			if (name != nullptr && std::strcmp(name, "Front Panel") == 0)
			{
				selected_view = index;
				m_video_target->set_view(index);
				usable_view = !m_video_target->current_view().items().empty();
				break;
			}
		}
		if (!usable_view)
		{
			for (unsigned index = 0; m_video_target->view_name(index) != nullptr; ++index)
			{
				m_video_target->set_view(index);
				if (!m_video_target->current_view().items().empty())
				{
					selected_view = index;
					usable_view = true;
					break;
				}
			}
		}
		if (!usable_view)
		{
			m_diag.video_last_error_code.store(4, std::memory_order_relaxed);
			m_diag.video_state.store(static_cast<std::uint64_t>(EmbeddedVideoState::Unavailable), std::memory_order_relaxed);
			return false;
		}
		m_video_target->set_view(selected_view);
		m_video_target->set_keepaspect(true);
		m_video_target->set_max_update_rate(5.0f);
		m_video_target->set_bounds(k_video_width, k_video_height);

		s32 minimum_width = 0;
		s32 minimum_height = 0;
		m_video_target->compute_minimum_size(minimum_width, minimum_height);
		if (minimum_width > 0 && minimum_height > 0)
		{
			const auto height = std::clamp(static_cast<int>(std::llround(static_cast<double>(k_video_width) * minimum_height / minimum_width)), 256, 4096);
			m_video_target->set_bounds(k_video_width, height);
			m_diag.video_frame_width.store(static_cast<std::uint64_t>(k_video_width), std::memory_order_relaxed);
			m_diag.video_frame_height.store(static_cast<std::uint64_t>(height), std::memory_order_relaxed);
			m_diag.video_source_aspect_x1000.store(static_cast<std::uint64_t>((static_cast<double>(k_video_width) / height) * 1000.0), std::memory_order_relaxed);
		}
		else
		{
			m_diag.video_frame_width.store(static_cast<std::uint64_t>(k_video_width), std::memory_order_relaxed);
			m_diag.video_frame_height.store(static_cast<std::uint64_t>(k_video_height), std::memory_order_relaxed);
			m_diag.video_source_aspect_x1000.store(static_cast<std::uint64_t>((static_cast<double>(k_video_width) / k_video_height) * 1000.0), std::memory_order_relaxed);
		}

		m_diag.video_target_flags.store(static_cast<std::uint64_t>(capture_flags), std::memory_order_relaxed);
		m_diag.video_target_generation.fetch_add(1, std::memory_order_relaxed);
		m_diag.video_target_orientation.store(static_cast<std::uint64_t>(m_video_target->orientation()), std::memory_order_relaxed);
		m_diag.video_target_pixel_aspect_x1000.store(static_cast<std::uint64_t>(m_video_target->pixel_aspect() * 1000.0f), std::memory_order_relaxed);
		m_diag.video_render_target_available.store(true, std::memory_order_relaxed);
		m_diag.video_state.store(static_cast<std::uint64_t>(EmbeddedVideoState::WaitingForFirstFrame), std::memory_order_relaxed);
		return true;
	}



	void capture_video_frame(bool skip_redraw)
	{
		if (skip_redraw || m_diag.stop_requested.load(std::memory_order_relaxed)
			|| !m_diag.video_capture_enabled.load(std::memory_order_relaxed))
			return;

		if (!m_diag.video_editor_display_active.load(std::memory_order_relaxed))
		{
			m_diag.video_frames_skipped_inactive.fetch_add(1, std::memory_order_relaxed);
			return;
		}

		const auto now = steadyMs();
		if (m_diag.video_deadline_reset_requests.exchange(0, std::memory_order_acq_rel) != 0)
			m_next_video_deadline_ms = 0;

		const auto interval_ms = std::max<std::uint64_t>(1, m_diag.video_capture_interval_ms.load(std::memory_order_acquire));
		if (m_next_video_deadline_ms == 0)
			m_next_video_deadline_ms = now;
		if (now < m_next_video_deadline_ms)
		{
			m_diag.video_frames_skipped_deadline.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		const auto missed = (now - m_next_video_deadline_ms) / interval_ms;
		m_next_video_deadline_ms += (missed + 1) * interval_ms;
		m_diag.video_capture_requested.fetch_add(1, std::memory_order_relaxed);
		m_diag.video_capture_queue_depth_before.store(m_audio_queue.size(), std::memory_order_relaxed);
		try
		{
			if (!ensure_video_target())
				return;

			if (machine().phase() < machine_phase::RESET)
				return;

			for (screen_device &screen : screen_device_enumerator(machine().root_device()))
			{
				screen.update_partial(screen.visible_area().max_y);
				screen.update_quads();
			}

			const auto width = std::clamp(static_cast<s32>(m_diag.video_requested_width.load(std::memory_order_acquire)), 256, 4096);
			s32 minimum_width = 0;
			s32 minimum_height = 0;
			m_video_target->compute_minimum_size(minimum_width, minimum_height);
			const auto height = (minimum_width > 0 && minimum_height > 0)
				? std::clamp(static_cast<s32>(std::llround(static_cast<double>(width) * minimum_height / minimum_width)), 256, 4096)
				: k_video_height;
			if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
				return;
			if (m_failed_video_width == width)
				return;

			m_video_target->set_bounds(width, height);
			auto job = std::make_unique<immutable_video_job>();
			job->width = width;
			job->height = height;
			job->token = m_video_job_token.load(std::memory_order_acquire);
			u32 item_index = 0;
			u32 static_items = 0;
			u32 total_items = 0;
			for (const auto &item : m_video_target->current_view().visible_items())
			{
				const bool display = item.get().screen() != nullptr;
				const bool unsupported_blend = !display && !cacheable_static_blend(item.get().blend_mode());
				const bool is_static = !display && !unsupported_blend && !item.get().has_dynamic_dependency();
				if (job->runs.empty() || job->runs.back().is_static != is_static)
					job->runs.push_back({ is_static, 0, {} });
				auto &run = job->runs.back();
				auto &primlist = m_video_target->get_primitives(item_index, 1, false);
				for (const render_primitive *primitive = primlist.first(); primitive != nullptr; primitive = primitive->next())
				{
					immutable_primitive snapshot;
					if (!snapshot_primitive(*primitive, snapshot, job->snapshot_bytes))
					{
						m_diag.video_raster_error_code.store(3, std::memory_order_relaxed);
						m_diag.video_last_error_code.store(3, std::memory_order_relaxed);
						return;
					}
					run.primitives.emplace_back(std::move(snapshot));
				}
				if (is_static)
					++static_items;
				++total_items;
				++item_index;
			}
			for (auto &run : job->runs)
				run.signature = snapshot_signature(run);
			const auto static_ratio = total_items != 0 ? double(static_items) / total_items : 0.0;
			const auto static_run_count = static_cast<u32>(std::count_if(job->runs.begin(), job->runs.end(),
				[] (const auto &run) { return run.is_static; }));
			job->use_static_cache = static_items >= 3 && static_run_count != 0
				&& static_run_count <= 32 && static_ratio >= 0.20;
			m_diag.video_capture_started.fetch_add(1, std::memory_order_relaxed);
			m_diag.video_capture_in_progress.store(true, std::memory_order_release);
			const bool submitted = submit_video_job(std::move(job));
			if (!submitted)
				m_diag.video_capture_in_progress.store(false, std::memory_order_release);
			if (submitted && m_diag.video_capture_single_frame.load(std::memory_order_acquire))
				m_diag.video_capture_enabled.store(false, std::memory_order_release);
			if (m_last_video_capture_ms != 0 && now > m_last_video_capture_ms)
				m_diag.video_measured_frame_rate_x1000.store(1'000'000ULL / (now - m_last_video_capture_ms), std::memory_order_relaxed);
			m_last_video_capture_ms = now;
		}
		catch (...)
		{
			m_diag.video_capture_in_progress.store(false, std::memory_order_release);
			m_diag.video_raster_error_code.store(1, std::memory_order_relaxed);
			m_diag.video_raster_error_index.store(0, std::memory_order_relaxed);
			m_diag.video_last_error_code.store(1, std::memory_order_relaxed);
			m_diag.video_state.store(static_cast<std::uint64_t>(EmbeddedVideoState::Error), std::memory_order_relaxed);
		}
	}

	osd_options &m_options;
	midi_byte_queue &m_midi_queue;
	audio_frame_queue &m_audio_queue;
	mouse_event_queue &m_mouse_queue;
	floppy_request_queue &m_floppy_requests;
	floppy_result_queue &m_floppy_results;
	state_request_queue &m_state_requests;
	state_result_queue &m_state_results;
	std::atomic<bool> &m_midi_panic_requested;
	std::array<std::atomic<std::uint64_t>, 32> &m_midi_panic_held_notes;
	std::array<std::atomic<std::uint64_t>, 32> &m_active_host_midi_notes;
	StateOperationRequest m_current_state_request;
	bool m_pending_state_request = false;
	bool m_restore_waiting_for_clean_timeslice = false;
	std::uint64_t m_state_scheduler_wait_started_ms = 0;
	std::string m_state_snapshot;
	HeldMidiNotes m_state_snapshot_held_notes {};
	embedded_video_bridge &m_video_bridge;
	EngineDiagnostics &m_diag;
	mutable std::mutex m_startup_output_mutex;
	std::string m_startup_output;
	render_target *m_video_target = nullptr;
	std::uint64_t m_last_video_capture_ms = 0;
	std::uint64_t m_next_video_deadline_ms = 0;
	std::uint32_t m_audio_sink_diagnostics_counter = 0;
	int m_failed_video_width = 0;
	std::thread m_video_worker;
	std::mutex m_video_worker_mutex;
	std::condition_variable m_video_worker_condition;
	std::unique_ptr<immutable_video_job> m_pending_video_job;
	bool m_video_worker_stop = false;
	std::atomic<std::uint64_t> m_video_job_token { 1 };
	std::uint64_t m_worker_cache_token = 0;
	int m_worker_cache_width = 0;
	int m_worker_cache_height = 0;
	std::vector<worker_static_run> m_worker_static_runs;
	bool m_static_cache_valid = false;
	bool m_static_cache_fallback = false;
	bool m_static_cache_validation_done = false;
	int m_static_cache_width = 0;
	int m_static_cache_height = 0;
	std::string m_static_cache_machine_name;
	std::string m_static_cache_view_name;
	std::string m_static_cache_fallback_reason;
	u32 m_static_cache_visibility_mask = 0;
	int m_static_cache_orientation = 0;
	float m_static_cache_pixel_aspect = 0.0f;
	float m_static_cache_view_aspect = 0.0f;
	std::vector<static_layout_run> m_static_runs;
	std::uint32_t m_static_items = 0;
	std::uint32_t m_static_stateful_items = 0;
	std::uint32_t m_static_display_items = 0;
	std::uint32_t m_static_unsupported_blend_items = 0;
	std::uint32_t m_static_run_count = 0;
	std::uint64_t m_static_cache_build_us = 0;
	std::uint64_t m_static_full_render_equivalent_us = 0;
	std::uint64_t m_static_cache_memory_bytes = 0;
	std::uint64_t m_static_cropped_cache_memory_bytes_estimate = 0;
	std::uint64_t m_static_full_frame_equivalent_bytes = 0;
	std::uint64_t m_static_cache_hits = 0;
	std::uint64_t m_static_dynamic_render_total_us = 0;
	std::uint64_t m_static_dynamic_render_max_us = 0;
	std::uint64_t m_static_dynamic_render_count = 0;
	std::uint64_t m_static_composite_total_us = 0;
	std::uint64_t m_static_composite_max_us = 0;
	std::uint64_t m_static_cached_frame_total_us = 0;
	std::uint64_t m_static_cached_frame_max_us = 0;
	std::uint64_t m_static_cached_frame_count = 0;
	static constexpr int k_video_width = 1024;
	static constexpr int k_video_height = 576;
};

class embedded_plugin_host
{
public:
	explicit embedded_plugin_host(machine_manager &manager)
		: m_manager(manager)
	{
	}

	void initialize()
	{
		if (!m_manager.options().plugins())
			return;

		path_iterator paths(m_manager.options().plugins_path());
		std::string path;
		while (paths.next(path))
			m_plugins.scan_directory(path, true);

		for (auto &plugin : m_plugins.plugins())
			plugin.m_start = false;

		auto *const layout = m_plugins.find("layout");
		if (layout == nullptr)
			throw emu_fatalerror("Layout plugin not found in pluginspath: %s", m_manager.options().plugins_path());
		layout->m_start = true;

		m_lua.initialize(m_manager, m_plugins);

		emu_file boot(m_manager.options().plugins_path(), OPEN_FLAG_READ);
		std::error_condition const open_error = boot.open("boot.lua");
		if (open_error)
			throw emu_fatalerror("Unable to open plugin bootstrap script boot.lua in: %s", m_manager.options().plugins_path());

		auto load_result = m_lua.load_script(boot.fullpath());
		if (!load_result.valid())
		{
			sol::error const error = load_result;
			throw emu_fatalerror("Error loading plugin bootstrap script %s: %s", boot.fullpath(), error.what());
		}

		sol::protected_function boot_function = load_result;
		auto call_result = m_lua.invoke(boot_function);
		if (!call_result.valid())
		{
			sol::error const error = call_result;
			throw emu_fatalerror("Error running plugin bootstrap script %s: %s", boot.fullpath(), error.what());
		}

		m_initialized = true;
	}

	void set_machine(running_machine *machine)
	{
		m_lua.set_machine(machine);
	}

	void attach_notifiers()
	{
		if (m_initialized)
			m_lua.attach_notifiers();
	}

	void before_load_settings()
	{
		if (m_initialized)
			m_lua.on_machine_before_load_settings();
	}

	void layout_script(layout_file &file, const char *script)
	{
		if (!m_initialized)
			return;

		auto &lua = m_lua.sol();
		sol::object callback = lua.registry()["cb_layout"];
		if (!callback.is<sol::protected_function>())
			return;

		auto result = m_lua.invoke(
			callback.as<sol::protected_function>(),
			sol::make_reference(lua, &file),
			sol::make_reference(lua, script));
		if (!result.valid())
		{
			sol::error const error = result;
			osd_printf_error("[LUA ERROR] in Layout Plugin callback: %s\n", error.what());
		}
	}

private:
	machine_manager &m_manager;
	plugin_options m_plugins;
	lua_engine m_lua;
	bool m_initialized = false;
};

class embedded_machine_manager : public machine_manager
{
public:
		embedded_machine_manager(emu_options &options, osd_interface &osd, EngineDiagnostics &diag, std::string driver_name,
				std::string startup_arguments,
				std::function<void(EmbeddedStartupDiagnostic)> publish_diagnostic)
		: machine_manager(options, osd), m_driver_name(std::move(driver_name))
		, m_startup_arguments(std::move(startup_arguments))
		, m_diag(diag), m_plugin_host(*this), m_publish_diagnostic(std::move(publish_diagnostic))
	{
	}

	int execute()
	{
		try
		{
			m_plugin_host.initialize();
		}
		catch (std::exception const &error)
		{
			EmbeddedStartupDiagnostic diagnostic;
			diagnostic.category = EmbeddedStartupError::LuaPlugin;
			diagnostic.summary = "VES could not initialize the embedded MAME layout plugin.";
			diagnostic.recovery = "Please reinstall VES.";
			diagnostic.technical_details = error.what();
			m_publish_diagnostic(std::move(diagnostic));
			osd_printf_error("Unable to initialize embedded Layout Plugin host: %s\n", error.what());
			m_diag.machine_exited.fetch_add(1, std::memory_order_relaxed);
			return EMU_ERR_FATALERROR;
		}
		const auto driver_index = driver_list::find(m_driver_name.c_str());
		const auto &driver = driver_index >= 0 ? driver_list::driver(driver_index) : GAME_NAME(tx81z);
		machine_config config(driver, m_options);
		running_machine machine(config, *this);
		set_machine(&machine);
		m_diag.machine_started_ms.store(steadyMs(), std::memory_order_relaxed);
		m_diag.machine_started.fetch_add(1, std::memory_order_relaxed);
		append_engine_lifecycle_log("[VES startup] event=BEFORE_MACHINE_RUN driver=" + m_driver_name
			+ " machine_started=" + std::to_string(m_diag.machine_started.load(std::memory_order_relaxed))
			+ " arguments=\"" + lifecycle_log_field(m_startup_arguments) + "\"");
		const int result = machine.run(false);
		m_diag.machine_running.store(false, std::memory_order_release);
		const auto exited = m_diag.machine_exited.fetch_add(1, std::memory_order_relaxed) + 1;
		append_engine_lifecycle_log("[VES startup] event=AFTER_MACHINE_RUN driver=" + m_driver_name
			+ " result=" + std::to_string(result)
			+ " machine_exited=" + std::to_string(exited)
			+ " machine_running=" + std::to_string(m_diag.machine_running.load(std::memory_order_relaxed) ? 1 : 0)
			+ " normal_scheduler_iterations=" + std::to_string(m_diag.normal_scheduler_iterations.load(std::memory_order_relaxed))
			+ " audio_sink_open_count=" + std::to_string(m_diag.audio_sink_open_count.load(std::memory_order_relaxed))
			+ " midi_input_open_count=" + std::to_string(m_diag.midi_input_open_count.load(std::memory_order_relaxed))
			+ " video_initialized=" + std::to_string(m_diag.video_initialized.load(std::memory_order_relaxed) ? 1 : 0)
			+ " video_frames_produced=" + std::to_string(m_diag.video_frames_produced.load(std::memory_order_relaxed)));
		m_plugin_host.set_machine(nullptr);
		set_machine(nullptr);
		return result;
	}

	void update_machine() override
	{
		m_plugin_host.set_machine(machine());
		m_plugin_host.attach_notifiers();
	}

	void before_load_settings(running_machine &) override
	{
		m_plugin_host.before_load_settings();
	}

	void layout_script(layout_file &file, const char *script)
	{
		m_plugin_host.layout_script(file, script);
	}

	ui_manager *create_ui(running_machine &machine) override
	{
		m_ui = std::make_unique<ui_manager>(machine);
		return m_ui.get();
	}

private:
	std::string m_driver_name;
	std::string m_startup_arguments;
	EngineDiagnostics &m_diag;
	embedded_plugin_host m_plugin_host;
	std::function<void(EmbeddedStartupDiagnostic)> m_publish_diagnostic;
	std::unique_ptr<ui_manager> m_ui;
};

} // anonymous namespace

void dispatch_embedded_layout_script(layout_file &file, const char *script)
{
	auto *const manager = dynamic_cast<embedded_machine_manager *>(&file.device().machine().manager());
	if (manager != nullptr)
		manager->layout_script(file, script);
}

struct EmbeddedEmulatorEngine::Impl
{
		explicit Impl(EmbeddedEmulatorEngineSettings engine_settings)
			: settings(std::move(engine_settings))
		{
			append_engine_lifecycle_log("[VES lifecycle] event=EMBEDDED_ENGINE_IMPL_CONSTRUCTOR engine_impl="
				+ lifecycle_pointer_string(this)
				+ " driver=" + settings.driver_name
				+ " engine_generation=" + std::to_string(settings.engine_generation)
				+ " sample_rate=" + std::to_string(settings.sample_rate));
			diag.audio_ring_capacity_frames.store(audio_frame_queue::capacity(), std::memory_order_relaxed);
			diag.juce_sample_rate.store(static_cast<std::uint64_t>(std::max(settings.sample_rate, 0)), std::memory_order_relaxed);
			diag.mame_sample_rate.store(static_cast<std::uint64_t>(std::max(settings.sample_rate, 0)), std::memory_order_relaxed);
		diag.effective_mame_sample_rate.store(static_cast<std::uint64_t>(std::max(settings.sample_rate, 0)), std::memory_order_relaxed);
	}

		~Impl()
		{
			append_engine_lifecycle_log("[VES lifecycle] event=EMBEDDED_ENGINE_IMPL_DESTRUCTOR engine_impl="
				+ lifecycle_pointer_string(this)
				+ " driver=" + settings.driver_name
				+ " engine_generation=" + std::to_string(settings.engine_generation)
				+ " sample_rate=" + std::to_string(settings.sample_rate));
			stopAndJoin(std::chrono::seconds(5));
		}

	void start()
	{
		mame_result.store(EMU_ERR_FATALERROR, std::memory_order_relaxed);
		mame_thread = std::thread([this]
		{
#if defined(__APPLE__)
			pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif
			const int result = runMameThread();
			mame_result.store(result, std::memory_order_relaxed);
		});
	}

	bool waitForProviders(std::chrono::milliseconds timeout)
	{
		return waitUntil([this]
		{
			return diag.machine_started.load(std::memory_order_relaxed) != 0
				&& diag.midi_input_open_count.load(std::memory_order_relaxed) != 0
				&& diag.audio_sink_open_count.load(std::memory_order_relaxed) != 0;
		}, timeout);
	}

	bool stopAndJoin(std::chrono::milliseconds timeout)
	{
		diag.stop_requested.store(true, std::memory_order_relaxed);
		const bool exited = waitUntil([this]
		{
			return diag.machine_exited.load(std::memory_order_relaxed) != 0;
		}, timeout);

		if (mame_thread.joinable())
			mame_thread.join();
		floppy_requests.clear();
		floppy_results.clear();
		floppy_change_pending.store(false, std::memory_order_release);
		state_requests.clear();
		state_results.clear();
		state_operation_pending.store(false, std::memory_order_release);
		return exited;
	}

	void sendMidiBytes(const std::uint8_t *data, std::size_t size)
	{
		if (data != nullptr && size >= 3)
		{
			const auto status = data[0] & 0xf0U;
			if (status == 0x80U || status == 0x90U)
			{
				const auto channel = data[0] & 0x0fU;
				const auto note = data[1] & 0x7fU;
				const auto index = static_cast<std::size_t>(channel) * 2 + (note >> 6);
				const auto mask = std::uint64_t(1) << (note & 63);
				if (status == 0x90U && data[2] != 0)
					active_host_midi_notes[index].fetch_or(mask, std::memory_order_release);
				else
					active_host_midi_notes[index].fetch_and(~mask, std::memory_order_release);
			}
		}
		const auto now = steadyMs();
		for (std::size_t i = 0; i < size; ++i)
		{
			if (diag.midi_first_queued_ms.load(std::memory_order_relaxed) == 0)
				diag.midi_first_queued_ms.store(now, std::memory_order_relaxed);
			diag.midi_last_queued_ms.store(now, std::memory_order_relaxed);

			if (midi_queue.push(queued_midi_byte { data[i], now }))
				diag.midi_bytes_queued.fetch_add(1, std::memory_order_relaxed);
			else
				diag.midi_queue_overflow.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void sendMidiPanic(const HeldMidiNotes &held_notes)
	{
		for (std::size_t i = 0; i < held_notes.size(); ++i)
		{
			midi_panic_held_notes[i].store(held_notes[i], std::memory_order_release);
			active_host_midi_notes[i].store(0, std::memory_order_release);
		}
		midi_panic_requested.store(true, std::memory_order_release);
	}

	void resetAudioWindow()
	{
		const auto dropped = audio_queue.clear();
		diag.audio_frames_written.store(0, std::memory_order_relaxed);
		diag.audio_frames_dropped.store(0, std::memory_order_relaxed);
		diag.audio_stale_frames_dropped.fetch_add(dropped, std::memory_order_relaxed);
		diag.audio_overflows.store(0, std::memory_order_relaxed);
		diag.audio_frames_read.store(0, std::memory_order_relaxed);
		diag.peak_abs.store(0.0f, std::memory_order_relaxed);
		update_audio_queue_diagnostics(diag, audio_queue.size());
		resetLatencyDiagnostics();
	}

	void resetLatencyDiagnostics()
	{
		diag.audio_queue_read_low_watermark_frames.store(UINT64_MAX, std::memory_order_relaxed);
		diag.audio_sink_last_callback_us.store(0, std::memory_order_relaxed);
		diag.audio_sink_latest_interarrival_us.store(0, std::memory_order_relaxed);
		diag.audio_sink_max_interarrival_us.store(0, std::memory_order_relaxed);
		diag.audio_sink_latest_block_frames.store(0, std::memory_order_relaxed);
		diag.audio_sink_min_block_frames.store(UINT64_MAX, std::memory_order_relaxed);
		diag.audio_sink_max_block_frames.store(0, std::memory_order_relaxed);
		diag.audio_sink_recent_max_block_frames.store(0, std::memory_order_relaxed);
		diag.audio_sink_blocks_approx_240.store(0, std::memory_order_relaxed);
		diag.audio_sink_blocks_approx_480.store(0, std::memory_order_relaxed);
		diag.audio_sink_blocks_approx_720.store(0, std::memory_order_relaxed);
		diag.audio_sink_blocks_960_plus.store(0, std::memory_order_relaxed);
		diag.audio_sink_blocks_other.store(0, std::memory_order_relaxed);
		diag.audio_sink_statistics_reset_generation.fetch_add(1, std::memory_order_release);
		diag.audio_underrun_callbacks.store(0, std::memory_order_relaxed);
		diag.audio_missing_output_frames.store(0, std::memory_order_relaxed);
		diag.video_capture_queue_depth_before.store(0, std::memory_order_relaxed);
		diag.video_capture_queue_depth_after.store(0, std::memory_order_relaxed);
	}

	void resetMidiTimingWindow()
	{
		diag.midi_first_queued_ms.store(0, std::memory_order_relaxed);
		diag.midi_last_queued_ms.store(0, std::memory_order_relaxed);
		diag.midi_first_read_ms.store(0, std::memory_order_relaxed);
		diag.midi_last_read_ms.store(0, std::memory_order_relaxed);
		diag.midi_max_queue_to_read_ms.store(0, std::memory_order_relaxed);
		diag.midi_first_read_machine_uptime_ms.store(0, std::memory_order_relaxed);
	}

	void drainAudio(AudioStats &stats)
	{
		StereoFrame frame;
		while (audio_queue.pop(frame))
		{
			++stats.frames;
			const float abs_left = std::fabs(frame.left);
			const float abs_right = std::fabs(frame.right);
			stats.peak = std::max(stats.peak, std::max(abs_left, abs_right));
			if (abs_left > 0.0f)
				++stats.non_zero_samples;
			if (abs_right > 0.0f)
				++stats.non_zero_samples;
			stats.sum_squares += static_cast<double>(frame.left) * frame.left;
			stats.sum_squares += static_cast<double>(frame.right) * frame.right;
		}

		if (stats.frames != 0)
			stats.rms = std::sqrt(stats.sum_squares / static_cast<double>(stats.frames * 2));
		diag.audio_frames_read.store(stats.frames, std::memory_order_relaxed);
		update_audio_queue_diagnostics(diag, audio_queue.size());
	}

	std::size_t readAudioFrames(StereoFrame *frames, std::size_t max_frames)
	{
		const auto queued_before_read = audio_queue.size();
		auto low_watermark = diag.audio_queue_read_low_watermark_frames.load(std::memory_order_relaxed);
		while (queued_before_read < low_watermark
			&& !diag.audio_queue_read_low_watermark_frames.compare_exchange_weak(
				low_watermark, queued_before_read, std::memory_order_relaxed))
		{
		}
		const auto count = audio_queue.pop(frames, max_frames);
		diag.audio_frames_read.fetch_add(count, std::memory_order_relaxed);
		if (count < max_frames)
			diag.audio_underruns.fetch_add(1, std::memory_order_relaxed);
		if (++audio_read_diagnostics_counter >= k_audio_read_diagnostics_interval || count < max_frames)
		{
			audio_read_diagnostics_counter = 0;
			update_audio_queue_diagnostics(diag, audio_queue.size());
		}
		return count;
	}

	std::size_t queuedAudioFrames()
	{
		return audio_queue.size();
	}

	std::size_t discardQueuedAudio()
	{
		const auto dropped = audio_queue.clear();
		if (dropped != 0)
			diag.audio_stale_frames_dropped.fetch_add(dropped, std::memory_order_relaxed);
		update_audio_queue_diagnostics(diag, audio_queue.size());
		return dropped;
	}

	std::size_t discardOldestAudioFrames(std::size_t frames)
	{
		const auto dropped = audio_queue.discard_oldest(frames);
		if (dropped != 0)
			diag.audio_stale_frames_dropped.fetch_add(dropped, std::memory_order_relaxed);
		update_audio_queue_diagnostics(diag, audio_queue.size());
		return dropped;
	}

	void noteHostAudioConfiguration(double sample_rate, int block_size)
	{
		const auto safe_rate = static_cast<std::uint64_t>(sample_rate > 0.0 ? std::llround(sample_rate) : settings.sample_rate);
		const auto safe_block = static_cast<std::uint64_t>(std::max(block_size, 0));
		const auto target = std::max<std::uint64_t>(1024, safe_block * 2);
		const auto max_tolerated = std::max<std::uint64_t>(1536, target + safe_block);
		diag.juce_sample_rate.store(safe_rate, std::memory_order_relaxed);
		diag.juce_configured_block_size.store(safe_block, std::memory_order_relaxed);
		diag.juce_block_size.store(safe_block, std::memory_order_relaxed);
		diag.audio_target_queue_frames.store(target, std::memory_order_relaxed);
		diag.audio_max_tolerated_queue_frames.store(max_tolerated, std::memory_order_relaxed);
		resetLatencyDiagnostics();
	}

	std::uint64_t machineUptimeMs() const
	{
		const auto started = diag.machine_started_ms.load(std::memory_order_relaxed);
		if (started == 0)
			return 0;
		const auto now = steadyMs();
		return now >= started ? now - started : 0;
	}

	const EngineDiagnostics &diagnostics() const { return diag; }
	EngineDiagnostics &diagnostics() { return diag; }
	int mameResult() const { return mame_result.load(std::memory_order_relaxed); }
	const std::string &driverName() const { return settings.driver_name; }
	std::uint64_t engineGeneration() const { return settings.engine_generation; }
	EmbeddedStartupDiagnostic startupDiagnostic() const
	{
		std::lock_guard<std::mutex> guard(startup_diagnostic_mutex);
		return startup_diagnostic;
	}

	void publishStartupDiagnostic(EmbeddedStartupDiagnostic diagnostic)
	{
		std::lock_guard<std::mutex> guard(startup_diagnostic_mutex);
		startup_diagnostic = std::move(diagnostic);
	}

	bool copyLatestVideoFrame(VideoFrameSnapshot &snapshot)
	{
		return video_bridge.copy(snapshot, diag);
	}

	void setVideoDisplayActive(bool active)
	{
		if (!active)
			requestMouseRelease();
		const bool previous = diag.video_editor_display_active.exchange(active, std::memory_order_relaxed);
		if (previous != active)
		{
			if (active)
				diag.video_deadline_reset_requests.fetch_add(1, std::memory_order_release);
		}
	}

	void setVideoCaptureEnabled(bool enabled)
	{
		diag.video_capture_enabled.store(enabled, std::memory_order_release);
		if (enabled)
			diag.video_deadline_reset_requests.fetch_add(1, std::memory_order_release);
	}

	void setVideoCaptureSingleFrame(bool single_frame)
	{
		diag.video_capture_single_frame.store(single_frame, std::memory_order_release);
		if (single_frame)
			diag.video_capture_enabled.store(true, std::memory_order_release);
	}

	void setVideoCaptureIntervalMs(std::uint64_t interval_ms)
	{
		const auto clamped = std::clamp<std::uint64_t>(interval_ms, 50, 10'000);
		diag.video_capture_interval_ms.store(clamped, std::memory_order_release);
		diag.video_target_frame_rate.store(1000 / clamped, std::memory_order_relaxed);
		diag.video_deadline_reset_requests.fetch_add(1, std::memory_order_release);
	}

	void requestVideoCaptureWidth(int width)
	{
			diag.video_requested_width.store(static_cast<std::uint64_t>(std::clamp(width, 256, 4096)), std::memory_order_release);
	}

	bool enqueueMouseEvent(EmbeddedMouseEventType type, std::int32_t x, std::int32_t y)
	{
		if (!diag.mouse_forwarding_enabled.load(std::memory_order_acquire) && type != EmbeddedMouseEventType::LeftUp)
			return false;

		EmbeddedMouseEvent event;
		event.type = type;
		event.x = x;
		event.y = y;
		event.sequence = mouse_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
		event.timestamp_ms = steadyMs();
		if (!mouse_queue.push(event))
		{
			if (type == EmbeddedMouseEventType::Move)
				diag.mouse_dropped_move_events.fetch_add(1, std::memory_order_relaxed);
			else
			{
				diag.mouse_critical_event_failures.fetch_add(1, std::memory_order_relaxed);
				diag.mouse_last_error.store(1, std::memory_order_relaxed);
				diag.mouse_forwarding_enabled.store(false, std::memory_order_release);
				diag.mouse_release_pending.store(true, std::memory_order_release);
			}
			return false;
		}

		diag.mouse_events_enqueued.fetch_add(1, std::memory_order_relaxed);
		const auto queued = mouse_queue.size();
		auto high_water = diag.mouse_queue_high_watermark.load(std::memory_order_relaxed);
		while (high_water < queued
			&& !diag.mouse_queue_high_watermark.compare_exchange_weak(high_water, queued, std::memory_order_relaxed)) {}
		return true;
	}

	void requestMouseRelease()
	{
		if (!diag.mouse_forwarding_enabled.load(std::memory_order_acquire)
			&& !diag.mouse_left_down.load(std::memory_order_acquire))
			return;
		const auto x = std::max(0, diag.mouse_current_x.load(std::memory_order_relaxed));
		const auto y = std::max(0, diag.mouse_current_y.load(std::memory_order_relaxed));
		if (!enqueueMouseEvent(EmbeddedMouseEventType::LeftUp, x, y))
			diag.mouse_release_pending.store(true, std::memory_order_release);
	}

	bool requestFloppyChange(const FloppyChangeRequest &request)
	{
		if (diag.stop_requested.load(std::memory_order_acquire)
			|| diag.machine_started.load(std::memory_order_acquire) == 0
			|| diag.machine_exited.load(std::memory_order_acquire) != 0)
			return false;

		bool expected = false;
		if (!floppy_change_pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
			return false;

		if (!floppy_requests.push(request))
		{
			floppy_change_pending.store(false, std::memory_order_release);
			return false;
		}

		return true;
	}

	bool pollFloppyChangeResult(FloppyChangeResult &result)
	{
		if (!floppy_results.pop(result))
			return false;
		floppy_change_pending.store(false, std::memory_order_release);
		return true;
	}

	bool isFloppyChangePending() const
	{
		return floppy_change_pending.load(std::memory_order_acquire);
	}

	bool requestStateOperation(const StateOperationRequest &request)
	{
		if (!settings.state_operations_allowed
			|| diag.stop_requested.load(std::memory_order_acquire)
			|| diag.machine_started.load(std::memory_order_acquire) == 0
			|| diag.machine_exited.load(std::memory_order_acquire) != 0)
			return false;
		bool expected = false;
		if (!state_operation_pending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
			return false;
		if (!state_requests.push(request))
		{
			state_operation_pending.store(false, std::memory_order_release);
			return false;
		}
		if (request.operation == StateOperation::Save)
			diag.state_save_requests.fetch_add(1, std::memory_order_relaxed);
		else
			diag.state_load_requests.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	bool pollStateOperationResult(StateOperationResult &result)
	{
		if (!state_results.pop(result))
			return false;
		state_operation_pending.store(false, std::memory_order_release);
		return true;
	}

	bool isStateOperationPending() const
	{
		return state_operation_pending.load(std::memory_order_acquire);
	}

private:
	int runMameThread()
	{
		osd_options options;
		embedded_osd osd(options, midi_queue, audio_queue, mouse_queue, floppy_requests, floppy_results,
			state_requests, state_results, midi_panic_requested, midi_panic_held_notes,
			active_host_midi_notes, video_bridge, diag);
		osd.register_options();

		std::vector<std::string> args {
			"EmbeddedMameTx81zTest",
			settings.driver_name,
			"-rompath", settings.rom_path,
			"-cfg_directory", settings.cfg_path,
			"-nvram_directory", settings.nvram_path,
			"-video", "none",
			"-sound", "none",
			"-midiprovider", "none",
			"-keyboardprovider", "none",
			"-mouseprovider", "none",
			"-lightgunprovider", "none",
			"-joystickprovider", "none",
			"-plugins",
			"-plugin", "layout",
			"-pluginspath", settings.plugins_path,
			"-noreadconfig",
			"-nowriteconfig",
			"-skip_gameinfo",
			"-samplerate", std::to_string(settings.sample_rate),
			"-throttle",
			"-sleep",
			"-speed", "1.0",
			"-norefreshspeed",
			"-audio_latency", "0",
			"-nonvram_save"
		};
		if (!settings.native_midi_input_option.empty())
		{
			args.emplace_back("-" + settings.native_midi_input_option);
			args.emplace_back("ves");
		}
		if (!settings.native_midi_output_option.empty())
		{
			args.emplace_back("-" + settings.native_midi_output_option);
			args.emplace_back("ves");
		}
		if (!settings.retrofit_midi_input_option.empty())
		{
			args.emplace_back("-" + settings.retrofit_midi_input_option);
			args.emplace_back("ves");
		}
		if (!settings.artwork_path.empty())
		{
			args.emplace_back("-artpath");
			args.emplace_back(settings.artwork_path);
		}
		for (const auto &media : settings.startup_media_options)
		{
			if (!media.option_name.empty() && !media.path.empty())
			{
				args.emplace_back("-" + media.option_name);
				args.emplace_back(media.path);
			}
		}
		std::ostringstream startup_arguments;
		for (const auto &argument : args)
		{
			if (startup_arguments.tellp() > 0)
				startup_arguments << ' ';
			startup_arguments << '"' << argument << '"';
		}
		append_engine_lifecycle_log("[VES startup] event=MAME_ARGUMENTS driver=" + settings.driver_name
			+ " arguments=\"" + lifecycle_log_field(startup_arguments.str()) + "\"");
		const auto selected_driver_index = driver_list::find(settings.driver_name.c_str());
		const auto &option_driver = selected_driver_index >= 0 ? driver_list::driver(selected_driver_index) : GAME_NAME(tx81z);
		// Image options (including S3000XL's -flop) are registered per machine.
		// Set the real system before parsing them; parsing against the old hardcoded
		// TX81Z system raised an uncaught unknown-option exception in the AU thread.
		options.set_system_name(std::string(option_driver.name));
		try
		{
			options.parse_command_line(args, OPTION_PRIORITY_CMDLINE);
		}
		catch (const options_exception &error)
		{
			append_engine_lifecycle_log("[VES startup] event=OPTIONS_EXCEPTION driver=" + settings.driver_name
				+ " text=\"" + lifecycle_log_field(error.what()) + "\"");
			EmbeddedStartupDiagnostic diagnostic;
			diagnostic.category = EmbeddedStartupError::Configuration;
			diagnostic.summary = "The emulator startup options are invalid.";
			diagnostic.technical_details = error.what();
			publishStartupDiagnostic(std::move(diagnostic));
			osd_printf_error("VintageEmulatorStudio startup option error: %s\\n", error.what());
			diag.machine_exited.fetch_add(1, std::memory_order_relaxed);
			return EMU_ERR_INVALID_CONFIG;
		}
		catch (const emu_fatalerror &error)
		{
			append_engine_lifecycle_log("[VES startup] event=CONFIG_FATAL driver=" + settings.driver_name
				+ " exit_code=" + std::to_string(error.exitcode())
				+ " text=\"" + lifecycle_log_field(error.what()) + "\"");
			EmbeddedStartupDiagnostic diagnostic;
			diagnostic.category = EmbeddedStartupError::Configuration;
			diagnostic.summary = "The emulator startup configuration is invalid.";
			diagnostic.technical_details = error.what();
			publishStartupDiagnostic(std::move(diagnostic));
			osd_printf_error("VintageEmulatorStudio startup configuration error: %s\\n", error.what());
			diag.machine_exited.fetch_add(1, std::memory_order_relaxed);
			return error.exitcode();
		}
		options.set_system_name(option_driver.name);
		diag.mame_throttle.store(options.throttle(), std::memory_order_relaxed);
		diag.mame_sleep.store(options.sleep(), std::memory_order_relaxed);
		diag.mame_refresh_speed.store(options.refresh_speed(), std::memory_order_relaxed);
		diag.mame_speed_percent.store(static_cast<std::uint64_t>(std::llround(options.speed() * 100.0f)), std::memory_order_relaxed);
		diag.mame_seconds_to_run.store(static_cast<std::uint64_t>(std::max(options.seconds_to_run(), 0)), std::memory_order_relaxed);
		diag.mame_benchmark_seconds.store(static_cast<std::uint64_t>(std::max(options.bench(), 0)), std::memory_order_relaxed);
		diag.mame_audio_latency_us.store(static_cast<std::uint64_t>(std::max(options.audio_latency(), 0.0f) * 1000.0f), std::memory_order_relaxed);
		diag.effective_mame_sample_rate.store(static_cast<std::uint64_t>(std::max(options.sample_rate(), 0)), std::memory_order_relaxed);

		EmbeddedStartupDiagnostic auditDiagnostic;
		try
		{
			auditDiagnostic = auditDriverRomSet(options, option_driver);
		}
		catch (const std::exception &error)
		{
			auditDiagnostic.category = EmbeddedStartupError::EngineFailure;
			auditDiagnostic.summary = "The emulator failed to start.";
			auditDiagnostic.technical_details = std::string("MAME ROM audit failed: ") + error.what();
		}
		catch (...)
		{
			auditDiagnostic.category = EmbeddedStartupError::Unknown;
			auditDiagnostic.summary = "The emulator failed to start.";
			auditDiagnostic.technical_details = "MAME ROM audit failed with an unknown exception.";
		}

		if (auditDiagnostic.category != EmbeddedStartupError::None)
		{
			const bool romSpecificFailure = auditDiagnostic.category == EmbeddedStartupError::MissingRom
				|| auditDiagnostic.category == EmbeddedStartupError::RomChecksumMismatch
				|| auditDiagnostic.category == EmbeddedStartupError::InvalidRomSet;
			publishStartupDiagnostic(std::move(auditDiagnostic));
			diag.machine_exited.fetch_add(1, std::memory_order_relaxed);
			return romSpecificFailure ? EMU_ERR_MISSING_FILES : EMU_ERR_FATALERROR;
		}

		try
		{
			embedded_machine_manager manager(
					options,
					osd,
					diag,
					settings.driver_name,
					startup_arguments.str(),
					[this] (EmbeddedStartupDiagnostic diagnostic) { publishStartupDiagnostic(std::move(diagnostic)); });
			manager.start_http_server();
			const int result = manager.execute();
			const auto captured_output = osd.startup_output();
			if (result != 0 && looks_like_media_failure (captured_output))
			{
				EmbeddedStartupDiagnostic diagnostic;
				diagnostic.category = EmbeddedStartupError::MediaLoad;
				diagnostic.summary = "Media Failure";
				diagnostic.details = "MAME rejected a configured startup media image.";
				diagnostic.recovery = "Check that the configured media image is valid and accessible.";
				diagnostic.technical_details = captured_output;
				publishStartupDiagnostic (std::move (diagnostic));
			}
			if (result != 0 && startupDiagnostic().category == EmbeddedStartupError::None)
			{
				EmbeddedStartupDiagnostic diagnostic;
				diagnostic.category = EmbeddedStartupError::EngineFailure;
				diagnostic.summary = "The emulator failed to start.";
				diagnostic.technical_details = util::string_format("MAME returned result %d", result);
				publishStartupDiagnostic(std::move(diagnostic));
			}
			const auto returned_diagnostic = startupDiagnostic();
			append_engine_lifecycle_log("[VES startup] event=MAME_EXECUTE_RETURN driver=" + settings.driver_name
				+ " result=" + std::to_string(result)
				+ " diagnostic_summary=\"" + lifecycle_log_field(returned_diagnostic.summary) + "\""
				+ " diagnostic_details=\"" + lifecycle_log_field(returned_diagnostic.details) + "\""
				+ " diagnostic_technical=\"" + lifecycle_log_field(returned_diagnostic.technical_details) + "\""
				+ " mame_output=\"" + lifecycle_log_field(captured_output) + "\"");
			return result;
		}
		catch (const emu_fatalerror &error)
		{
			append_engine_lifecycle_log("[VES startup] event=MAME_FATAL_EXCEPTION driver=" + settings.driver_name
				+ " exit_code=" + std::to_string(error.exitcode())
				+ " text=\"" + lifecycle_log_field(error.what()) + "\"");
			EmbeddedStartupDiagnostic diagnostic;
			diagnostic.category = EmbeddedStartupError::EngineFailure;
			diagnostic.summary = "The emulator failed to start.";
			diagnostic.technical_details = error.what();
			publishStartupDiagnostic(std::move(diagnostic));
			diag.machine_exited.fetch_add(1, std::memory_order_relaxed);
			return error.exitcode();
		}
		catch (const std::exception &error)
		{
			append_engine_lifecycle_log("[VES startup] event=MAME_STD_EXCEPTION driver=" + settings.driver_name
				+ " text=\"" + lifecycle_log_field(error.what()) + "\"");
			EmbeddedStartupDiagnostic diagnostic;
			diagnostic.category = EmbeddedStartupError::EngineFailure;
			diagnostic.summary = "The emulator failed to start.";
			diagnostic.technical_details = error.what();
			publishStartupDiagnostic(std::move(diagnostic));
			diag.machine_exited.fetch_add(1, std::memory_order_relaxed);
			return EMU_ERR_FATALERROR;
		}
		catch (...)
		{
			append_engine_lifecycle_log("[VES startup] event=MAME_UNKNOWN_EXCEPTION driver=" + settings.driver_name);
			EmbeddedStartupDiagnostic diagnostic;
			diagnostic.category = EmbeddedStartupError::Unknown;
			diagnostic.summary = "The emulator failed to start.";
			diagnostic.technical_details = "Unknown exception in embedded MAME startup.";
			publishStartupDiagnostic(std::move(diagnostic));
			diag.machine_exited.fetch_add(1, std::memory_order_relaxed);
			return EMU_ERR_FATALERROR;
		}
	}

	template <typename Predicate>
	bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (predicate())
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		return predicate();
	}

	EmbeddedEmulatorEngineSettings settings;
	midi_byte_queue midi_queue;
	audio_frame_queue audio_queue;
	mouse_event_queue mouse_queue;
	floppy_request_queue floppy_requests;
	floppy_result_queue floppy_results;
	state_request_queue state_requests;
	state_result_queue state_results;
	embedded_video_bridge video_bridge;
	EngineDiagnostics diag;
	mutable std::mutex startup_diagnostic_mutex;
	EmbeddedStartupDiagnostic startup_diagnostic;
	std::uint32_t audio_read_diagnostics_counter = 0;
	std::thread mame_thread;
	std::atomic<std::uint64_t> mouse_sequence { 0 };
	std::atomic<bool> floppy_change_pending { false };
	std::atomic<bool> state_operation_pending { false };
	std::atomic<bool> midi_panic_requested { false };
	std::array<std::atomic<std::uint64_t>, 32> midi_panic_held_notes {};
	std::array<std::atomic<std::uint64_t>, 32> active_host_midi_notes {};
	std::atomic<int> mame_result { EMU_ERR_FATALERROR };
};

EmbeddedEmulatorEngine::EmbeddedEmulatorEngine(EmbeddedEmulatorEngineSettings settings)
	: m_impl(std::make_unique<Impl>(std::move(settings)))
{
	append_engine_lifecycle_log("[VES lifecycle] event=EMBEDDED_ENGINE_CONSTRUCTOR engine="
		+ lifecycle_pointer_string(this)
		+ " engine_impl=" + lifecycle_pointer_string(m_impl.get()));
}

EmbeddedEmulatorEngine::~EmbeddedEmulatorEngine()
{
	append_engine_lifecycle_log("[VES lifecycle] event=EMBEDDED_ENGINE_DESTRUCTOR engine="
		+ lifecycle_pointer_string(this)
		+ " engine_impl=" + lifecycle_pointer_string(m_impl.get()));
}

void EmbeddedEmulatorEngine::start()
{
	m_impl->start();
}

bool EmbeddedEmulatorEngine::waitForProviders(std::chrono::milliseconds timeout)
{
	return m_impl->waitForProviders(timeout);
}

bool EmbeddedEmulatorEngine::stopAndJoin(std::chrono::milliseconds timeout)
{
	return m_impl->stopAndJoin(timeout);
}

void EmbeddedEmulatorEngine::sendMidiBytes(const std::uint8_t *data, std::size_t size)
{
	m_impl->sendMidiBytes(data, size);
}

void EmbeddedEmulatorEngine::sendMidiPanic(const HeldMidiNotes &held_notes)
{
	m_impl->sendMidiPanic(held_notes);
}

void EmbeddedEmulatorEngine::resetAudioWindow()
{
	m_impl->resetAudioWindow();
}

void EmbeddedEmulatorEngine::resetLatencyDiagnostics()
{
	m_impl->resetLatencyDiagnostics();
}

void EmbeddedEmulatorEngine::resetMidiTimingWindow()
{
	m_impl->resetMidiTimingWindow();
}

void EmbeddedEmulatorEngine::drainAudio(AudioStats &stats)
{
	m_impl->drainAudio(stats);
}

std::size_t EmbeddedEmulatorEngine::readAudioFrames(StereoFrame *frames, std::size_t max_frames)
{
	return m_impl->readAudioFrames(frames, max_frames);
}

std::size_t EmbeddedEmulatorEngine::queuedAudioFrames()
{
	return m_impl->queuedAudioFrames();
}

std::size_t EmbeddedEmulatorEngine::discardQueuedAudio()
{
	return m_impl->discardQueuedAudio();
}

std::size_t EmbeddedEmulatorEngine::discardOldestAudioFrames(std::size_t frames)
{
	return m_impl->discardOldestAudioFrames(frames);
}

void EmbeddedEmulatorEngine::noteHostAudioConfiguration(double sample_rate, int block_size)
{
	m_impl->noteHostAudioConfiguration(sample_rate, block_size);
}

bool EmbeddedEmulatorEngine::copyLatestVideoFrame(VideoFrameSnapshot &snapshot)
{
	return m_impl->copyLatestVideoFrame(snapshot);
}

void EmbeddedEmulatorEngine::setVideoDisplayActive(bool active)
{
	m_impl->setVideoDisplayActive(active);
}

void EmbeddedEmulatorEngine::setVideoCaptureEnabled(bool enabled)
{
	m_impl->setVideoCaptureEnabled(enabled);
}

void EmbeddedEmulatorEngine::setVideoCaptureSingleFrame(bool single_frame)
{
	m_impl->setVideoCaptureSingleFrame(single_frame);
}

void EmbeddedEmulatorEngine::setVideoCaptureIntervalMs(std::uint64_t interval_ms)
{
	m_impl->setVideoCaptureIntervalMs(interval_ms);
}

void EmbeddedEmulatorEngine::requestVideoCaptureWidth(int width)
{
	m_impl->requestVideoCaptureWidth(width);
}

bool EmbeddedEmulatorEngine::enqueueMouseEvent(EmbeddedMouseEventType type, std::int32_t x, std::int32_t y)
{
	return m_impl->enqueueMouseEvent(type, x, y);
}

void EmbeddedEmulatorEngine::requestMouseRelease()
{
	m_impl->requestMouseRelease();
}

bool EmbeddedEmulatorEngine::requestFloppyChange(const FloppyChangeRequest &request)
{
	return m_impl->requestFloppyChange(request);
}

bool EmbeddedEmulatorEngine::pollFloppyChangeResult(FloppyChangeResult &result)
{
	return m_impl->pollFloppyChangeResult(result);
}

bool EmbeddedEmulatorEngine::isFloppyChangePending() const
{
	return m_impl->isFloppyChangePending();
}

bool EmbeddedEmulatorEngine::requestMediaChange(const MediaChangeRequest &request)
{
	return m_impl->requestFloppyChange(request);
}

bool EmbeddedEmulatorEngine::pollMediaChangeResult(MediaChangeResult &result)
{
	return m_impl->pollFloppyChangeResult(result);
}

bool EmbeddedEmulatorEngine::isMediaChangePending() const
{
	return m_impl->isFloppyChangePending();
}

bool EmbeddedEmulatorEngine::requestStateOperation(const StateOperationRequest &request)
{
	return m_impl->requestStateOperation(request);
}

bool EmbeddedEmulatorEngine::pollStateOperationResult(StateOperationResult &result)
{
	return m_impl->pollStateOperationResult(result);
}

bool EmbeddedEmulatorEngine::isStateOperationPending() const
{
	return m_impl->isStateOperationPending();
}

std::uint64_t EmbeddedEmulatorEngine::machineUptimeMs() const
{
	return m_impl->machineUptimeMs();
}

const EngineDiagnostics &EmbeddedEmulatorEngine::diagnostics() const
{
	return m_impl->diagnostics();
}

EngineDiagnostics &EmbeddedEmulatorEngine::diagnostics()
{
	return m_impl->diagnostics();
}

int EmbeddedEmulatorEngine::mameResult() const
{
	return m_impl->mameResult();
}

const std::string &EmbeddedEmulatorEngine::driverName() const
{
	return m_impl->driverName();
}

std::uint64_t EmbeddedEmulatorEngine::engineGeneration() const
{
	return m_impl->engineGeneration();
}

EmbeddedStartupDiagnostic EmbeddedEmulatorEngine::startupDiagnostic() const
{
	return m_impl->startupDiagnostic();
}

std::uint64_t steadyMs()
{
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace ves
