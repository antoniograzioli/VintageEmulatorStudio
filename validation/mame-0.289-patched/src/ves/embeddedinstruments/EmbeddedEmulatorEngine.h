// license:BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
// copyright-holders:Vintage Emulator Studio contributors

#ifndef VES_EMBEDDED_EMULATOR_ENGINE_H
#define VES_EMBEDDED_EMULATOR_ENGINE_H

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ves {

enum class EmbeddedMouseEventType : std::uint8_t
{
	Move,
	LeftDown,
	LeftUp
};

struct EmbeddedMouseEvent
{
	EmbeddedMouseEventType type = EmbeddedMouseEventType::Move;
	std::int32_t x = 0;
	std::int32_t y = 0;
	std::uint64_t sequence = 0;
	std::uint64_t timestamp_ms = 0;
};

struct StereoFrame
{
	float left = 0.0f;
	float right = 0.0f;
};

struct AudioStats
{
	std::uint64_t frames = 0;
	std::uint64_t non_zero_samples = 0;
	double sum_squares = 0.0;
	float peak = 0.0f;
	double rms = 0.0;
};

struct VideoFrameSnapshot
{
	int width = 0;
	int height = 0;
	std::uint64_t generation = 0;
	std::uint64_t timestamp_ms = 0;
	std::vector<std::uint32_t> pixels;
};

enum class EmbeddedMediaType : std::uint8_t
{
	Floppy,
	CdRom
};

struct MediaChangeRequest
{
	EmbeddedMediaType media_type = EmbeddedMediaType::Floppy;
	std::uint64_t engine_generation = 0;
	std::uint64_t request_id = 0;
	std::string brief_instance_name = "flop";
	std::string path;
};

struct MediaChangeResult
{
	EmbeddedMediaType media_type = EmbeddedMediaType::Floppy;
	std::uint64_t engine_generation = 0;
	std::uint64_t request_id = 0;
	bool success = false;
	bool drive_empty = true;
	std::string applied_path;
	std::string error_message;
	int error_value = 0;
};

enum class StateOperation : std::uint8_t
{
	Save,
	Load
};

struct StateOperationRequest
{
	StateOperation operation = StateOperation::Save;
	std::uint64_t engine_generation = 0;
	std::uint64_t request_id = 0;
	std::uint64_t requested_at_ms = 0;
	std::vector<std::uint8_t> snapshot_blob;
};

struct StateOperationResult
{
	StateOperation operation = StateOperation::Save;
	std::uint64_t engine_generation = 0;
	std::uint64_t request_id = 0;
	bool success = false;
	std::uint64_t snapshot_size = 0;
	std::uint64_t scheduler_wait_ms = 0;
	std::uint64_t stream_duration_ms = 0;
	std::uint64_t total_duration_ms = 0;
	std::vector<std::uint8_t> snapshot_blob;
	std::string error_message;
};

using FloppyChangeRequest = MediaChangeRequest;
using FloppyChangeResult = MediaChangeResult;

enum class EmbeddedVideoState : std::uint8_t
{
	Disabled,
	WaitingForMachine,
	WaitingForFirstFrame,
	Running,
	Unavailable,
	Error
};

enum class EmbeddedStartupError : std::uint8_t
{
	None,
	RuntimeResources,
	PrimaryRomMissing,
	MissingRom,
	RomChecksumMismatch,
	InvalidRomSet,
	MediaUnavailable,
	MediaLoad,
	Configuration,
	Nvram,
	LuaPlugin,
	DeviceInitialization,
	AudioInitialization,
	EngineFailure,
	StartupTimeout,
	Unknown
};

struct EmbeddedStartupIssue
{
	std::string name;
	std::string owner;
	std::string expected_crc;
	std::string expected_sha1;
	std::string actual_crc;
	std::string actual_sha1;
	std::uint64_t expected_length = 0;
	std::uint64_t actual_length = 0;
};

struct EmbeddedStartupDiagnostic
{
	EmbeddedStartupError category = EmbeddedStartupError::None;
	std::string summary;
	std::string details;
	std::string recovery;
	std::string technical_details;
	std::vector<EmbeddedStartupIssue> issues;
};

struct EngineDiagnostics
{
	std::atomic<std::uint64_t> audio_ring_capacity_frames { 0 };
	std::atomic<std::uint64_t> audio_current_queued_frames { 0 };
	std::atomic<std::uint64_t> audio_max_queued_frames { 0 };
	std::atomic<std::uint64_t> audio_min_queued_frames { 0 };
	std::atomic<std::uint64_t> audio_queued_observations { 0 };
	std::atomic<std::uint64_t> audio_queued_accumulator_frames { 0 };
	std::atomic<std::uint64_t> midi_bytes_queued { 0 };
	std::atomic<std::uint64_t> midi_bytes_consumed { 0 };
	std::atomic<std::uint64_t> midi_queue_overflow { 0 };
	std::atomic<std::uint64_t> midi_first_queued_ms { 0 };
	std::atomic<std::uint64_t> midi_last_queued_ms { 0 };
	std::atomic<std::uint64_t> midi_first_read_ms { 0 };
	std::atomic<std::uint64_t> midi_last_read_ms { 0 };
	std::atomic<std::uint64_t> midi_max_queue_to_read_ms { 0 };
	std::atomic<std::uint64_t> midi_first_read_machine_uptime_ms { 0 };
	std::atomic<std::uint64_t> midi_first_audio_onset_ms { 0 };
	std::atomic<std::uint64_t> midi_to_audio_onset_ms { 0 };
	std::atomic<std::uint64_t> midi_input_open_count { 0 };
	std::atomic<std::uint64_t> midi_output_open_count { 0 };
	std::atomic<std::uint64_t> audio_sink_open_count { 0 };
	std::atomic<std::uint64_t> audio_frames_written { 0 };
	std::atomic<std::uint64_t> audio_frames_dropped { 0 };
	std::atomic<std::uint64_t> audio_stale_frames_dropped { 0 };
	std::atomic<std::uint64_t> audio_frames_read { 0 };
	std::atomic<std::int64_t> audio_producer_consumer_frame_difference { 0 };
	std::atomic<std::uint64_t> audio_queued_latency_us { 0 };
	std::atomic<std::uint64_t> audio_max_queued_latency_us { 0 };
	std::atomic<std::uint64_t> audio_target_queue_frames { 512 };
	std::atomic<std::uint64_t> audio_max_tolerated_queue_frames { 768 };
	std::atomic<std::uint64_t> audio_trimmed_frames { 0 };
	std::atomic<std::uint64_t> audio_underruns { 0 };
	std::atomic<std::uint64_t> audio_underrun_callbacks { 0 };
	std::atomic<std::uint64_t> audio_missing_output_frames { 0 };
	std::atomic<std::uint64_t> audio_overflows { 0 };
	std::atomic<std::uint64_t> audio_queue_read_low_watermark_frames { UINT64_MAX };
	std::atomic<std::uint64_t> audio_sink_last_callback_us { 0 };
	std::atomic<std::uint64_t> audio_sink_latest_interarrival_us { 0 };
	std::atomic<std::uint64_t> audio_sink_max_interarrival_us { 0 };
	std::atomic<std::uint64_t> audio_sink_latest_block_frames { 0 };
	std::atomic<std::uint64_t> audio_sink_min_block_frames { UINT64_MAX };
	std::atomic<std::uint64_t> audio_sink_max_block_frames { 0 };
	std::atomic<std::uint64_t> audio_sink_recent_max_block_frames { 0 };
	std::atomic<std::uint64_t> audio_sink_blocks_approx_240 { 0 };
	std::atomic<std::uint64_t> audio_sink_blocks_approx_480 { 0 };
	std::atomic<std::uint64_t> audio_sink_blocks_approx_720 { 0 };
	std::atomic<std::uint64_t> audio_sink_blocks_960_plus { 0 };
	std::atomic<std::uint64_t> audio_sink_blocks_other { 0 };
	std::atomic<std::uint64_t> audio_sink_statistics_reset_generation { 0 };
	std::atomic<std::uint64_t> audio_rate_producer_frames { 0 };
	std::atomic<std::uint64_t> audio_rate_host_requested_frames { 0 };
	std::atomic<std::uint64_t> audio_rate_host_callback_count { 0 };
	std::atomic<std::uint64_t> audio_rate_host_latest_block { 0 };
	std::atomic<std::uint64_t> audio_rate_host_window_min_block { UINT64_MAX };
	std::atomic<std::uint64_t> audio_rate_host_window_max_block { 0 };
	std::atomic<std::uint64_t> audio_rate_sink_gap_total_us { 0 };
	std::atomic<std::uint64_t> audio_rate_sink_gap_count { 0 };
	std::atomic<std::uint64_t> audio_rate_sink_gap_gt_7500 { 0 };
	std::atomic<std::uint64_t> audio_rate_sink_gap_gt_10000 { 0 };
	std::atomic<std::uint64_t> audio_rate_sink_gap_gt_15000 { 0 };
	std::atomic<std::uint64_t> audio_rate_sink_window_max_gap_us { 0 };
	std::atomic<std::uint64_t> audio_rate_scheduler_update_count { 0 };
	std::atomic<std::uint64_t> audio_rate_scheduler_gap_total_us { 0 };
	std::atomic<std::uint64_t> audio_rate_scheduler_gap_count { 0 };
	std::atomic<std::uint64_t> audio_rate_scheduler_window_max_gap_us { 0 };
	std::atomic<std::uint64_t> audio_rate_scheduler_gap_gt_7500 { 0 };
	std::atomic<std::uint64_t> audio_rate_scheduler_gap_gt_10000 { 0 };
	std::atomic<std::uint64_t> audio_rate_scheduler_gap_gt_15000 { 0 };
	std::atomic<std::uint64_t> audio_rate_sink_window_min_block { UINT64_MAX };
	std::atomic<std::uint64_t> audio_rate_sink_window_max_block { 0 };
	std::atomic<std::uint64_t> audio_rate_sink_hist_le_256 { 0 };
	std::atomic<std::uint64_t> audio_rate_sink_hist_257_512 { 0 };
	std::atomic<std::uint64_t> audio_rate_sink_hist_513_768 { 0 };
	std::atomic<std::uint64_t> audio_rate_sink_hist_769_1024 { 0 };
	std::atomic<std::uint64_t> audio_rate_sink_hist_gt_1024 { 0 };
	std::atomic<std::uint64_t> audio_rate_queue_depth_total { 0 };
	std::atomic<std::uint64_t> audio_rate_queue_depth_count { 0 };
	std::atomic<std::uint64_t> audio_rate_queue_window_min { UINT64_MAX };
	std::atomic<std::uint64_t> audio_rate_queue_window_max { 0 };
	std::atomic<std::uint64_t> audio_rate_queue_below_host_block { 0 };
	std::atomic<std::uint64_t> audio_jitter_state { 0 };
	std::atomic<std::uint64_t> audio_jitter_prime_level_frames { 0 };
	std::atomic<std::uint64_t> audio_jitter_reprime_count { 0 };
	std::atomic<std::uint64_t> audio_jitter_consecutive_underrun_callbacks { 0 };
	std::atomic<std::uint64_t> audio_jitter_longest_underrun_run { 0 };
	std::atomic<std::uint64_t> audio_jitter_reprime_silenced_callbacks { 0 };
	std::atomic<std::uint64_t> video_capture_queue_depth_before { 0 };
	std::atomic<std::uint64_t> video_capture_queue_depth_after { 0 };
	std::atomic<std::uint64_t> mame_audio_callback_block_size { 0 };
	std::atomic<std::uint64_t> mame_audio_callback_count { 0 };
	std::atomic<std::uint64_t> juce_process_block_count { 0 };
	std::atomic<std::uint64_t> juce_sample_rate { 0 };
	std::atomic<std::uint64_t> mame_sample_rate { 0 };
	std::atomic<std::uint64_t> effective_mame_sample_rate { 0 };
	std::atomic<std::uint64_t> juce_configured_block_size { 0 };
	std::atomic<std::uint64_t> juce_block_size { 0 };
	std::atomic<std::uint64_t> boot_ready_queued_frames { 0 };
	std::atomic<std::uint64_t> boot_ready_queued_latency_us { 0 };
	std::atomic<std::uint64_t> boot_ready_stale_frames { 0 };
	std::atomic<std::uint64_t> boot_ready_flush_count { 0 };
	std::atomic<bool> mame_throttle { true };
	std::atomic<bool> mame_sleep { true };
	std::atomic<bool> mame_refresh_speed { false };
	std::atomic<std::uint64_t> mame_speed_percent { 100 };
	std::atomic<std::uint64_t> mame_seconds_to_run { 0 };
	std::atomic<std::uint64_t> mame_benchmark_seconds { 0 };
	std::atomic<std::uint64_t> mame_audio_latency_us { 0 };
	std::atomic<std::uint64_t> osd_updates { 0 };
	std::atomic<std::uint64_t> machine_started { 0 };
	std::atomic<std::uint64_t> machine_exited { 0 };
	std::atomic<std::uint64_t> machine_started_ms { 0 };
	std::atomic<bool> machine_running { false };
	std::atomic<std::uint64_t> normal_scheduler_iterations { 0 };
	std::atomic<std::uint64_t> stream_updates { 0 };
	std::atomic<float> peak_abs { 0.0f };
	std::atomic<bool> stop_requested { false };
	std::atomic<bool> native_audio_opened { false };
	std::atomic<bool> native_midi_opened { false };
	std::atomic<bool> video_initialized { false };
	std::atomic<bool> video_render_target_available { false };
	std::atomic<bool> video_capture_enabled { false };
	std::atomic<bool> video_capture_single_frame { false };
	std::atomic<bool> video_editor_display_active { false };
	std::atomic<std::uint64_t> video_state { static_cast<std::uint64_t>(EmbeddedVideoState::Disabled) };
	std::atomic<std::uint64_t> video_capture_requested { 0 };
	std::atomic<std::uint64_t> video_capture_started { 0 };
	std::atomic<std::uint64_t> video_capture_completed { 0 };
	std::atomic<bool> video_capture_in_progress { false };
	std::atomic<std::uint64_t> video_rasterization_duration_us { 0 };
	std::atomic<std::uint64_t> video_rasterization_total_us { 0 };
	std::atomic<std::uint64_t> video_rasterization_max_us { 0 };
	std::atomic<std::uint64_t> video_screen_update_partial_total_us { 0 };
	std::atomic<std::uint64_t> video_screen_update_partial_max_us { 0 };
	std::atomic<std::uint64_t> video_screen_update_partial_count { 0 };
	std::atomic<std::uint64_t> video_screen_update_quads_total_us { 0 };
	std::atomic<std::uint64_t> video_screen_update_quads_max_us { 0 };
	std::atomic<std::uint64_t> video_screen_update_quads_count { 0 };
	std::atomic<std::uint64_t> video_primitive_build_total_us { 0 };
	std::atomic<std::uint64_t> video_primitive_build_max_us { 0 };
	std::atomic<std::uint64_t> video_primitive_build_count { 0 };
	std::atomic<std::uint64_t> video_capture_total_us { 0 };
	std::atomic<std::uint64_t> video_capture_max_us { 0 };
	std::atomic<std::uint64_t> video_capture_timing_count { 0 };
	std::atomic<std::uint64_t> video_prepare_total_us { 0 };
	std::atomic<std::uint64_t> video_prepare_max_us { 0 };
	std::atomic<std::uint64_t> video_snapshot_total_us { 0 };
	std::atomic<std::uint64_t> video_snapshot_max_us { 0 };
	std::atomic<std::uint64_t> video_snapshot_bytes { 0 };
	std::atomic<std::uint64_t> video_jobs_submitted { 0 };
	std::atomic<std::uint64_t> video_jobs_replaced { 0 };
	std::atomic<std::uint64_t> video_jobs_completed { 0 };
	std::atomic<std::uint64_t> video_worker_raster_us { 0 };
	std::atomic<std::uint64_t> video_worker_static_composite_us { 0 };
	std::atomic<std::uint64_t> video_worker_total_us { 0 };
	std::atomic<std::uint64_t> video_worker_max_us { 0 };
	std::atomic<std::uint64_t> video_worker_pending_depth { 0 };
	std::atomic<std::uint64_t> video_raster_error_code { 0 };
	std::atomic<std::uint64_t> video_raster_error_index { 0 };
	std::atomic<std::uint64_t> video_deadline_reset_requests { 0 };
	std::atomic<bool> mouse_forwarding_enabled { false };
	std::atomic<std::uint64_t> mouse_events_enqueued { 0 };
	std::atomic<std::uint64_t> mouse_events_consumed { 0 };
	std::atomic<std::uint64_t> mouse_dropped_move_events { 0 };
	std::atomic<std::uint64_t> mouse_critical_event_failures { 0 };
	std::atomic<std::uint64_t> mouse_last_error { 0 };
	std::atomic<std::uint64_t> mouse_queue_high_watermark { 0 };
	std::atomic<std::uint64_t> mouse_last_event_type { 0 };
	std::atomic<std::int32_t> mouse_current_x { -1 };
	std::atomic<std::int32_t> mouse_current_y { -1 };
	std::atomic<bool> mouse_left_down { false };
	std::atomic<std::uint64_t> mouse_synthetic_release_count { 0 };
	std::atomic<bool> mouse_release_pending { false };
	std::atomic<std::uint64_t> mouse_pointer_target_index { 0 };
	std::atomic<bool> mouse_hit_item { false };
	std::atomic<std::uint64_t> mouse_hit_input_tag { 0 };
	std::atomic<std::uint64_t> mouse_hit_input_mask { 0 };
	std::atomic<bool> mouse_input_field_active { false };
	std::atomic<std::uint64_t> state_restore_mouse_queue_cleared { 0 };
	std::atomic<bool> state_restore_pointer_reset { false };
	std::atomic<bool> state_restore_view_rebound { false };
	std::atomic<std::uint64_t> state_restore_interactive_items_before { 0 };
	std::atomic<std::uint64_t> state_restore_interactive_items_after { 0 };
	std::atomic<std::uint64_t> state_restore_first_mouse_event_ms { 0 };
	std::atomic<std::uint64_t> state_restore_first_input_hit_ms { 0 };
	std::atomic<std::uint64_t> video_frame_width { 0 };
	std::atomic<std::uint64_t> video_frame_height { 0 };
	std::atomic<std::uint64_t> video_requested_width { 1024 };
	std::atomic<std::uint64_t> video_source_aspect_x1000 { 0 };
	std::atomic<std::uint64_t> video_frames_produced { 0 };
	std::atomic<std::uint64_t> video_frames_displayed { 0 };
	std::atomic<std::uint64_t> video_frames_dropped { 0 };
	std::atomic<std::uint64_t> video_frames_replaced { 0 };
	std::atomic<std::uint64_t> video_frames_skipped_deadline { 0 };
	std::atomic<std::uint64_t> video_frames_skipped_inactive { 0 };
	std::atomic<std::uint64_t> video_frames_skipped_paused { 0 };
	std::atomic<std::uint64_t> video_frame_generation { 0 };
	std::atomic<std::uint64_t> video_frame_reset_generation { 0 };
	std::atomic<std::uint64_t> video_capture_interval_ms { 200 };
	std::atomic<std::uint64_t> video_target_frame_rate { 5 };
	std::atomic<std::uint64_t> video_measured_frame_rate_x1000 { 0 };
	std::atomic<std::uint64_t> video_last_frame_timestamp_ms { 0 };
	std::atomic<std::uint64_t> video_last_error_code { 0 };
	std::atomic<std::uint64_t> video_target_flags { 0 };
	std::atomic<std::uint64_t> video_target_generation { 0 };
	std::atomic<std::uint64_t> video_target_orientation { 0 };
	std::atomic<std::uint64_t> video_target_pixel_aspect_x1000 { 0 };
	std::atomic<std::uint64_t> state_save_requests { 0 };
	std::atomic<std::uint64_t> state_load_requests { 0 };
	std::atomic<std::uint64_t> state_save_successes { 0 };
	std::atomic<std::uint64_t> state_save_failures { 0 };
	std::atomic<std::uint64_t> state_load_successes { 0 };
	std::atomic<std::uint64_t> state_load_failures { 0 };
	std::atomic<std::uint64_t> state_snapshot_bytes { 0 };
	std::atomic<std::uint64_t> state_request_received_ms { 0 };
	std::atomic<std::uint64_t> state_timeslice_exit_ms { 0 };
	std::atomic<std::uint64_t> state_restore_read_started_ms { 0 };
	std::atomic<std::uint64_t> state_restore_read_completed_ms { 0 };
	std::atomic<std::uint64_t> state_restore_first_timeslice_completed_ms { 0 };
	std::atomic<std::uint64_t> state_restore_first_audio_ms { 0 };
	std::atomic<std::uint64_t> state_restore_second_audio_ms { 0 };
	std::atomic<std::uint64_t> state_restore_first_video_ms { 0 };
	std::atomic<std::uint64_t> state_restore_first_video_generation { 0 };
	std::atomic<std::uint64_t> state_restore_cache_build_start_ms { 0 };
	std::atomic<std::uint64_t> state_restore_cache_build_end_ms { 0 };
};

struct EmbeddedEmulatorEngineSettings
{
	std::string rom_path;
	std::string cfg_path;
	std::string nvram_path;
	std::string plugins_path;
	std::string artwork_path;
	std::string driver_name = "tx81z";
	int sample_rate = 48000;
	std::string native_midi_input_option;
	std::string native_midi_output_option;
	std::string retrofit_midi_input_option;
	struct StartupMediaOption
	{
		std::string option_name;
		std::string path;
	};
	std::vector<StartupMediaOption> startup_media_options;
	std::uint64_t engine_generation = 0;
};

class EmbeddedEmulatorEngine
{
public:
	explicit EmbeddedEmulatorEngine(EmbeddedEmulatorEngineSettings settings);
	~EmbeddedEmulatorEngine();

	EmbeddedEmulatorEngine(const EmbeddedEmulatorEngine &) = delete;
	EmbeddedEmulatorEngine &operator=(const EmbeddedEmulatorEngine &) = delete;

	void start();
	bool waitForProviders(std::chrono::milliseconds timeout);
	bool stopAndJoin(std::chrono::milliseconds timeout);

	void sendMidiBytes(const std::uint8_t *data, std::size_t size);
	void resetAudioWindow();
	void resetLatencyDiagnostics();
	void resetMidiTimingWindow();
	void drainAudio(AudioStats &stats);
	std::size_t readAudioFrames(StereoFrame *frames, std::size_t max_frames);
	std::size_t queuedAudioFrames();
	std::size_t discardQueuedAudio();
	std::size_t discardOldestAudioFrames(std::size_t frames);
	void noteHostAudioConfiguration(double sample_rate, int block_size);
	bool copyLatestVideoFrame(VideoFrameSnapshot &snapshot);
	void setVideoDisplayActive(bool active);
	void setVideoCaptureEnabled(bool enabled);
	void setVideoCaptureSingleFrame(bool single_frame);
	void setVideoCaptureIntervalMs(std::uint64_t interval_ms);
	void requestVideoCaptureWidth(int width);
	bool enqueueMouseEvent(EmbeddedMouseEventType type, std::int32_t x, std::int32_t y);
	void requestMouseRelease();
	bool requestFloppyChange(const FloppyChangeRequest &request);
	bool pollFloppyChangeResult(FloppyChangeResult &result);
	bool isFloppyChangePending() const;
	bool requestMediaChange(const MediaChangeRequest &request);
	bool pollMediaChangeResult(MediaChangeResult &result);
	bool isMediaChangePending() const;
	bool requestStateOperation(const StateOperationRequest &request);
	bool pollStateOperationResult(StateOperationResult &result);
	bool isStateOperationPending() const;

	std::uint64_t machineUptimeMs() const;
	const EngineDiagnostics &diagnostics() const;
	EngineDiagnostics &diagnostics();
	int mameResult() const;
	const std::string &driverName() const;
	std::uint64_t engineGeneration() const;
	EmbeddedStartupDiagnostic startupDiagnostic() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

std::uint64_t steadyMs();

} // namespace ves

#endif // VES_EMBEDDED_EMULATOR_ENGINE_H
