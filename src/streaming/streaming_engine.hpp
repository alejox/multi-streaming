// Runs one obs_output_t per destination next to OBS's own stream.
//
// Threading: every public method runs on the UI thread, which alone owns outputs,
// services, and encoders. libobs signals arrive on other threads; they only update a
// session under its mutex and hand the rest to the Dispatcher. A signal callback never
// touches an output, because releasing one there could join the thread running it.
//
// Lifetime: signals are disconnected before an output is released, without holding a
// session mutex, since signal_handler_disconnect waits for a running callback. After
// that, no callback can reach the slot. The owner destroys the Dispatcher's target
// before the engine, so that no queued work outlives it.
#pragma once

#include "destinations/destination.hpp"
#include "destinations/encoder_leases.hpp"
#include "destinations/encoder_plan.hpp"
#include "destinations/secret_store.hpp"
#include "destinations/session.hpp"
#include "destinations/stream_stats.hpp"
#include "streaming/main_stream_encoders.hpp"
#include "streaming/video_sources.hpp"

#include <obs.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace multistream {

struct DestinationStatus {
	std::string id;
	SessionState state = SessionState::Idle;
	std::string failure_reason;
	StreamStats stats;
	std::uint64_t dropped_frames = 0;
};

class StreamingEngine {
public:
	// Runs a function later on the UI thread, in order, and is callable from any thread.
	using Dispatcher = std::function<void(std::function<void()>)>;
	using StatusListener = std::function<void(const DestinationStatus &)>;

	StreamingEngine(SecretStore &secrets, VideoSources &video_sources, Dispatcher dispatcher);
	~StreamingEngine();

	StreamingEngine(const StreamingEngine &) = delete;
	StreamingEngine &operator=(const StreamingEngine &) = delete;

	// Called on the UI thread for each status change and stats sample.
	void set_status_listener(StatusListener listener);

	// A running destination keeps its settings until its next start. One that is no
	// longer listed is stopped at once.
	void set_destinations(std::vector<Destination> destinations);
	std::vector<DestinationStatus> statuses() const;

	void start(const std::vector<std::string> &ids);
	void stop(const std::vector<std::string> &ids);
	void start_all();
	void stop_all();

	void main_stream_starting();
	void main_stream_stopping();

	void sample_stats();

	// Force-stops and releases everything before returning, for a profile change, which
	// resets video and needs all outputs gone.
	void stop_everything_now();

	// Like stop_everything_now, and refuses later starts. Safe to call repeatedly.
	void shutdown();

private:
	struct Slot;

	static void on_output_started(void *data, calldata_t *params);
	static void on_output_stopped(void *data, calldata_t *params);
	static void on_output_reconnecting(void *data, calldata_t *params);
	static void on_output_reconnected(void *data, calldata_t *params);

	std::shared_ptr<Slot> find_slot(const std::string &id) const;
	std::map<std::string, SessionState> session_states() const;

	std::optional<std::string> start_blocker(const Destination &destination);
	std::optional<std::string> start_output(Slot &slot, const EncoderAssignment &assignment,
						const MainStreamSnapshot &main_stream);
	obs_encoder_t *lease_video_encoder(const EncoderAssignment &assignment, const MainStreamSnapshot &main_stream,
					   std::string &failure);
	obs_encoder_t *lease_audio_encoder(const EncoderAssignment &assignment, const MainStreamSnapshot &main_stream,
					   std::string &failure);
	void return_lease(std::string &key);

	void fail_start(Slot &slot, const std::string &reason);
	void connect_signals(Slot &slot);
	void disconnect_signals(Slot &slot);
	void release_resources(Slot &slot);
	void force_stop_and_release(Slot &slot);
	void release_if_ended(Slot &slot);

	// Callable from any thread.
	void publish_later(const std::string &id);
	void publish(Slot &slot);
	static DestinationStatus status_of(const Slot &slot);

	SecretStore &secrets_;
	VideoSources &video_sources_;
	Dispatcher dispatcher_;
	StatusListener listener_;

	std::vector<Destination> destinations_;
	std::map<std::string, std::shared_ptr<Slot>> slots_;

	EncoderLeases leases_;
	std::map<std::string, OBSEncoder> encoders_;
	bool shut_down_ = false;
};

} // namespace multistream
