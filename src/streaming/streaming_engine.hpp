// Runs one obs_output_t per destination next to OBS's own stream.
//
// Threading: every public method runs on the UI thread, which alone decides what happens
// to outputs, services, and encoders. libobs signals arrive on other threads; they only
// update state under a mutex and hand the rest to the Dispatcher. A signal callback never
// touches an output, because releasing one there could join the thread running it.
//
// Stopping and destroying an output can join the threads of its RTMP stream, which can
// block for a long time and, while its connect thread runs, forever (see ConnectGate). So
// the UI thread only stops an output when the gate is clear, and never destroys one: a
// retired output goes to its own thread, which waits for the gate first.
//
// Reading a stream key can block for as long as a Keychain prompt stays open, so each read
// runs on its own thread, and its start finishes on the UI thread unless a stop or
// teardown voided its ticket meanwhile.
//
// Lifetime: a retired output no longer reaches its slot, and its signals are disconnected
// before it is released, without holding a mutex, since signal_handler_disconnect waits
// for a running callback. The owner shuts the engine down before destroying the
// Dispatcher's target, so that no worker can reach that target afterwards.
#pragma once

#include "destinations/deferred_work.hpp"
#include "destinations/destination.hpp"
#include "destinations/encoder_leases.hpp"
#include "destinations/encoder_plan.hpp"
#include "destinations/secret_store.hpp"
#include "destinations/session.hpp"
#include "destinations/start_tickets.hpp"
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
	// The retry in progress, from 1. 0 while connected.
	int reconnect_attempt = 0;
	// Seconds until libobs tries that retry; 0 while it is being tried or none is scheduled.
	int retry_in_seconds = 0;
	bool flapping = false;
	// Drops in the flapping window, for showing how unstable the connection is.
	int recent_drops = 0;
};

class StreamingEngine {
public:
	// Runs a function later on the UI thread, in order, and is callable from any thread.
	using Dispatcher = std::function<void(std::function<void()>)>;
	using StatusListener = std::function<void(const DestinationStatus &)>;

	// The store is shared because a read blocked on a prompt can outlive the engine.
	StreamingEngine(std::shared_ptr<SecretStore> secrets, VideoSources &video_sources, Dispatcher dispatcher);
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

	void main_stream_started();
	void main_stream_stopping();

	void sample_stats();

	// Force-stops and releases everything, for a profile change, which resets video and
	// needs all outputs gone. Waits for that, though only for a few seconds: an output
	// that is still connecting cannot be stopped until it stops connecting.
	void stop_everything_now();

	// Force-stops and releases the outputs on one source, for when that source goes away.
	void stop_source_now(VideoSource source);

	// True while anything may still use that source's video: its destinations, or for
	// the vertical canvas any output still being retired.
	bool video_in_use(VideoSource source);

	// Like stop_everything_now, and refuses later starts. Safe to call repeatedly.
	void shutdown();

private:
	struct Slot;
	struct OutputLink;
	class WorkerChannel;

	static void on_output_started(void *data, calldata_t *params);
	static void on_output_stopped(void *data, calldata_t *params);
	static void on_output_reconnecting(void *data, calldata_t *params);
	static void on_output_reconnected(void *data, calldata_t *params);

	std::shared_ptr<Slot> find_slot(const std::string &id) const;
	std::map<std::string, SessionState> session_states() const;

	std::optional<std::string> start_blocker(const Destination &destination);
	void read_key_then_start(const EncoderAssignment &assignment, std::shared_ptr<const MainStreamSnapshot> main_stream);
	void finish_start(std::uint64_t ticket, const EncoderAssignment &assignment,
			  const MainStreamSnapshot &main_stream, const SecretLookup &key);
	std::optional<std::string> start_output(Slot &slot, const EncoderAssignment &assignment,
						const MainStreamSnapshot &main_stream, const std::string &key);
	obs_encoder_t *lease_video_encoder(const EncoderAssignment &assignment, const MainStreamSnapshot &main_stream,
					   std::string &failure);
	obs_encoder_t *lease_audio_encoder(const EncoderAssignment &assignment, const MainStreamSnapshot &main_stream,
					   std::string &failure);
	void return_lease(std::string &key);

	void fail_start(Slot &slot, const std::string &reason);
	void wait_for_retired_outputs();
	void issue_requested_stop(Slot &slot);
	void retire_output(Slot &slot, bool force_stop);
	void force_stop_and_release(Slot &slot);
	void release_if_ended(Slot &slot);

	// Callable from any thread.
	static void publish_later(StreamingEngine &engine, const std::string &id);
	void publish(Slot &slot);
	static DestinationStatus status_of(const Slot &slot);

	std::shared_ptr<SecretStore> secrets_;
	VideoSources &video_sources_;
	std::shared_ptr<WorkerChannel> channel_;
	StatusListener listener_;

	std::vector<Destination> destinations_;
	std::map<std::string, std::shared_ptr<Slot>> slots_;

	StartTickets key_reads_;
	EncoderLeases leases_;
	std::map<std::string, OBSEncoder> encoders_;
	DeferredWork retired_outputs_;
	DeferredWork stop_waits_;
	bool shut_down_ = false;
};

} // namespace multistream
