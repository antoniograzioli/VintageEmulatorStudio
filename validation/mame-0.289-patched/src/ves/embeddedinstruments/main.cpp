// license:BSD-3-Clause
// copyright-holders:OpenAI

#include "EmbeddedEmulatorEngine.h"

#include "emu.h"
#include "main.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using ves::AudioStats;
using ves::EmbeddedEmulatorEngine;
using ves::EmbeddedEmulatorEngineSettings;
using ves::EngineDiagnostics;
using ves::steadyMs;

namespace {

struct test_settings
{
	std::string rom_path;
	int repeat = 1;
	bool clean_state = false;
	std::uint64_t run_id = steadyMs();
};

struct cycle_summary
{
	bool passed = false;
	bool providers_ready = false;
	bool clean_shutdown = false;
	bool audio_detected = false;
	int mame_result = EMU_ERR_FATALERROR;
	int boot_delay_seconds = 0;
	int midi_channel = 0;
	float peak = 0.0f;
	double rms = 0.0;
	std::uint64_t non_zero_samples = 0;
	std::uint64_t midi_queued = 0;
	std::uint64_t midi_consumed = 0;
};

struct attempt_result
{
	AudioStats audio;
	std::uint64_t queued_delta = 0;
	std::uint64_t consumed_delta = 0;
	std::uint64_t max_queue_to_read_ms = 0;
	std::uint64_t first_read_uptime_ms = 0;
};

std::string cfg_path(const test_settings &settings, int cycle_index)
{
	if (settings.clean_state)
		return "/private/tmp/ves-embeddedinstruments-clean-cfg-" + std::to_string(settings.run_id) + "-" + std::to_string(cycle_index);
	return "/private/tmp/ves-embeddedinstruments-persistent-cfg";
}

std::string nvram_path(const test_settings &settings, int cycle_index)
{
	if (settings.clean_state)
		return "/private/tmp/ves-embeddedinstruments-clean-nvram-" + std::to_string(settings.run_id) + "-" + std::to_string(cycle_index);
	return "VintageEmulatorStudio-Standalone/Support/nvram";
}

void prepare_clean_state(const test_settings &settings, int cycle_index)
{
	const std::filesystem::path source = "Support/nvram/tx81z/nvram";
	const std::filesystem::path destination_dir = std::filesystem::path(nvram_path(settings, cycle_index)) / "tx81z";
	const std::filesystem::path destination = destination_dir / "nvram";
	std::error_code ec;
	std::filesystem::create_directories(destination_dir, ec);
	if (!ec && std::filesystem::exists(source, ec))
	{
		std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
		std::cout << "Clean-state NVRAM seed: " << source.string()
			<< (ec ? " copy failed" : " copied") << "\n";
	}
	else
	{
		std::cout << "Clean-state NVRAM seed: missing, running empty cold state\n";
	}
}

void wait_for_machine_uptime(EmbeddedEmulatorEngine &engine, int seconds)
{
	const auto target_ms = static_cast<std::uint64_t>(seconds) * 1000;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds + 5);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (engine.machineUptimeMs() >= target_ms)
			return;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
}

void send_midi_bytes(EmbeddedEmulatorEngine &engine, const std::uint8_t *data, std::size_t size)
{
	engine.sendMidiBytes(data, size);
}

void send_all_notes_off_and_reset(EmbeddedEmulatorEngine &engine)
{
	std::cout << "Sending All Notes Off and Reset All Controllers on all channels at uptime "
		<< engine.machineUptimeMs() << "ms\n";
	for (int channel = 1; channel <= 16; ++channel)
	{
		const std::uint8_t all_notes_off[] = {
			static_cast<std::uint8_t>(0xb0 | ((channel - 1) & 0x0f)),
			0x7b,
			0x00
		};
		const std::uint8_t reset_controllers[] = {
			static_cast<std::uint8_t>(0xb0 | ((channel - 1) & 0x0f)),
			0x79,
			0x00
		};
		send_midi_bytes(engine, all_notes_off, sizeof(all_notes_off));
		send_midi_bytes(engine, reset_controllers, sizeof(reset_controllers));
	}
}

std::string hex_bytes(std::initializer_list<std::uint8_t> bytes)
{
	std::ostringstream stream;
	stream << std::hex << std::uppercase << std::setfill('0');
	bool first = true;
	for (auto byte : bytes)
	{
		if (!first)
			stream << ' ';
		first = false;
		stream << std::setw(2) << static_cast<int>(byte);
	}
	return stream.str();
}

void print_attempt(EmbeddedEmulatorEngine &engine, int delay, int channel, const attempt_result &result)
{
	const std::uint8_t on_status = static_cast<std::uint8_t>(0x90 | ((channel - 1) & 0x0f));
	const std::uint8_t off_status = static_cast<std::uint8_t>(0x80 | ((channel - 1) & 0x0f));
	std::cout << "Attempt bootDelay=" << delay << "s"
		<< " channel=" << channel
		<< " uptime=" << engine.machineUptimeMs() << "ms"
		<< " NoteOn=" << hex_bytes({ on_status, 0x3c, 0x64 })
		<< " NoteOff=" << hex_bytes({ off_status, 0x3c, 0x00 })
		<< " queued=" << result.queued_delta
		<< " osdRead=" << result.consumed_delta
		<< " maxQueueToReadMs=" << result.max_queue_to_read_ms
		<< " firstReadUptimeMs=" << result.first_read_uptime_ms
		<< " frames=" << result.audio.frames
		<< " peak=" << result.audio.peak
		<< " rms=" << result.audio.rms
		<< " nonZero=" << result.audio.non_zero_samples
		<< "\n";
}

attempt_result run_channel_attempt(EmbeddedEmulatorEngine &engine, int channel)
{
	EngineDiagnostics &diag = engine.diagnostics();
	engine.resetAudioWindow();
	engine.resetMidiTimingWindow();

	const std::uint8_t note_on[] = {
		static_cast<std::uint8_t>(0x90 | ((channel - 1) & 0x0f)),
		0x3c,
		0x64
	};
	const std::uint8_t note_off[] = {
		static_cast<std::uint8_t>(0x80 | ((channel - 1) & 0x0f)),
		0x3c,
		0x00
	};

	attempt_result result;
	const auto queued_before = diag.midi_bytes_queued.load(std::memory_order_relaxed);
	const auto consumed_before = diag.midi_bytes_consumed.load(std::memory_order_relaxed);
	send_midi_bytes(engine, note_on, sizeof(note_on));
	std::this_thread::sleep_for(std::chrono::milliseconds(1500));
	send_midi_bytes(engine, note_off, sizeof(note_off));
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	engine.drainAudio(result.audio);
	result.queued_delta = diag.midi_bytes_queued.load(std::memory_order_relaxed) - queued_before;
	result.consumed_delta = diag.midi_bytes_consumed.load(std::memory_order_relaxed) - consumed_before;
	result.max_queue_to_read_ms = diag.midi_max_queue_to_read_ms.load(std::memory_order_relaxed);
	result.first_read_uptime_ms = diag.midi_first_read_machine_uptime_ms.load(std::memory_order_relaxed);
	return result;
}

void run_delay_channel_scan(EmbeddedEmulatorEngine &engine, cycle_summary &summary)
{
	static constexpr int boot_delays[] = { 10, 15, 20, 25 };
	for (int delay : boot_delays)
	{
		wait_for_machine_uptime(engine, delay);
		std::cout << "State: MidiTestStarted bootDelay=" << delay << "s uptime="
			<< engine.machineUptimeMs() << "ms\n";
		send_all_notes_off_and_reset(engine);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		for (int channel = 1; channel <= 16; ++channel)
		{
			attempt_result result = run_channel_attempt(engine, channel);
			print_attempt(engine, delay, channel, result);
			if (result.audio.non_zero_samples != 0)
			{
				std::cout << "State: AudioDetected\n";
				summary.audio_detected = true;
				summary.boot_delay_seconds = delay;
				summary.midi_channel = channel;
				summary.peak = result.audio.peak;
				summary.rms = result.audio.rms;
				summary.non_zero_samples = result.audio.non_zero_samples;
				return;
			}
		}
	}

	std::cout << "State: TestFailed: no audio detected at tested boot delays/channels\n";
}

cycle_summary run_cycle(const test_settings &settings, int cycle_index)
{
	cycle_summary summary;
	const auto cfg = cfg_path(settings, cycle_index);
	const auto nvram = nvram_path(settings, cycle_index);

	std::cout << "\n=== Cycle " << cycle_index << " ===\n";
	std::cout << "MAME version: 0.288\n";
	std::cout << "Selected machine: tx81z\n";
	std::cout << "ROM path: " << settings.rom_path << "\n";
	std::cout << "State policy: " << (settings.clean_state ? "clean isolated" : "persistent") << "\n";
	std::cout << "CFG path: " << cfg << "\n";
	std::cout << "NVRAM path: " << nvram << "\n";
	if (settings.clean_state)
		prepare_clean_state(settings, cycle_index);
	std::cout << "Selected sound provider: in-memory ves OSD sink\n";
	std::cout << "Selected MIDI provider: in-memory ves OSD port\n";
	std::cout << "State: MachineRunning requested\n";

	EmbeddedEmulatorEngine engine(EmbeddedEmulatorEngineSettings {
		settings.rom_path,
		cfg,
		nvram
	});
	engine.start();

	summary.providers_ready = engine.waitForProviders(std::chrono::seconds(10));
	std::cout << "State: " << (summary.providers_ready ? "ProvidersReady" : "TestFailed: provider wait timed out") << "\n";

	if (summary.providers_ready)
		run_delay_channel_scan(engine, summary);

	summary.clean_shutdown = engine.stopAndJoin(std::chrono::seconds(5));
	summary.mame_result = engine.mameResult();

	const EngineDiagnostics &diag = engine.diagnostics();
	summary.midi_queued = diag.midi_bytes_queued.load(std::memory_order_relaxed);
	summary.midi_consumed = diag.midi_bytes_consumed.load(std::memory_order_relaxed);
	summary.passed = summary.providers_ready
		&& summary.clean_shutdown
		&& summary.mame_result == EMU_ERR_NONE
		&& summary.audio_detected
		&& summary.midi_consumed >= summary.midi_queued
		&& !diag.native_audio_opened.load(std::memory_order_relaxed)
		&& !diag.native_midi_opened.load(std::memory_order_relaxed);

	std::cout << "MIDI bytes queued total: " << summary.midi_queued << "\n";
	std::cout << "MIDI bytes consumed by OSD port total: " << summary.midi_consumed << "\n";
	std::cout << "Native audio opened: " << (diag.native_audio_opened.load(std::memory_order_relaxed) ? "yes" : "no") << "\n";
	std::cout << "Native MIDI opened: " << (diag.native_midi_opened.load(std::memory_order_relaxed) ? "yes" : "no") << "\n";
	std::cout << "Clean shutdown: " << (summary.clean_shutdown ? "yes" : "no") << "\n";
	std::cout << "MAME result: " << summary.mame_result << "\n";
	std::cout << "Cycle result: " << (summary.passed ? "PASS" : "FAIL") << "\n";
	return summary;
}

void print_usage(const char *argv0)
{
	std::cerr << "Usage: " << argv0 << " <rom-directory> [--repeat N] [--clean-state|--persistent-state]\n";
}

} // anonymous namespace

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		print_usage(argv[0]);
		return 2;
	}

	test_settings settings;
	settings.rom_path = argv[1];

	for (int i = 2; i < argc; ++i)
	{
		const std::string arg(argv[i]);
		if (arg == "--repeat" && i + 1 < argc)
		{
			settings.repeat = std::max(1, std::atoi(argv[++i]));
		}
		else if (arg == "--clean-state")
		{
			settings.clean_state = true;
		}
		else if (arg == "--persistent-state")
		{
			settings.clean_state = false;
		}
		else
		{
			print_usage(argv[0]);
			return 2;
		}
	}

	std::vector<cycle_summary> summaries;
	summaries.reserve(settings.repeat);
	for (int cycle = 1; cycle <= settings.repeat; ++cycle)
		summaries.emplace_back(run_cycle(settings, cycle));

	int passes = 0;
	int shutdown_failures = 0;
	int silent_runs = 0;
	int min_boot = 0;
	int max_boot = 0;
	double boot_sum = 0.0;
	for (const auto &summary : summaries)
	{
		if (summary.passed)
			++passes;
		if (!summary.clean_shutdown)
			++shutdown_failures;
		if (!summary.audio_detected)
			++silent_runs;
		if (summary.audio_detected)
		{
			if (min_boot == 0 || summary.boot_delay_seconds < min_boot)
				min_boot = summary.boot_delay_seconds;
			if (summary.boot_delay_seconds > max_boot)
				max_boot = summary.boot_delay_seconds;
			boot_sum += summary.boot_delay_seconds;
		}
	}

	const int audio_runs = settings.repeat - silent_runs;
	std::cout << "\n=== Repeat Summary ===\n";
	std::cout << "Passes: " << passes << "\n";
	std::cout << "Failures: " << (settings.repeat - passes) << "\n";
	std::cout << "Silent runs: " << silent_runs << "\n";
	std::cout << "Shutdown failures: " << shutdown_failures << "\n";
	std::cout << "Average boot-to-audio delay: " << (audio_runs != 0 ? boot_sum / audio_runs : 0.0) << "s\n";
	std::cout << "Minimum boot-to-audio delay: " << min_boot << "s\n";
	std::cout << "Maximum boot-to-audio delay: " << max_boot << "s\n";
	return passes == settings.repeat ? 0 : 1;
}

const char *emulator_info::get_appname() { return "EmbeddedMameTx81zTest"; }
const char *emulator_info::get_appname_lower() { return "embeddedmametx81ztest"; }
const char *emulator_info::get_configname() { return "embeddedmametx81ztest"; }
const char *emulator_info::get_copyright() { return ""; }
const char *emulator_info::get_copyright_info() { return ""; }
const char *emulator_info::get_bare_build_version() { return "0.288"; }
const char *emulator_info::get_build_version() { return "0.288 embedded TX81Z test"; }
void emulator_info::display_ui_chooser(running_machine &) {}
int emulator_info::start_frontend(emu_options &, osd_interface &, std::vector<std::string> &) { return 0; }
int emulator_info::start_frontend(emu_options &, osd_interface &, int, char *[]) { return 0; }
void emulator_info::sound_hook(const std::map<std::string, std::vector<std::pair<const float *, int>>> &) {}
bool emulator_info::draw_user_interface(running_machine &) { return false; }
void emulator_info::periodic_check() {}
bool emulator_info::frame_hook() { return false; }
void emulator_info::layout_script_cb(layout_file &, const char *) {}
bool emulator_info::standalone() { return true; }
