#include "streaming/streaming_engine.hpp"

#include "destinations/session_selection.hpp"
#include "destinations/validation.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <chrono>
#include <mutex>

namespace multistream {
namespace {

constexpr const char *kOutputId = "rtmp_output";
constexpr const char *kServiceId = "rtmp_custom";

std::string object_name(const std::string &kind, const std::string &id)
{
	return "obs-multi-stream-" + kind + "-" + id;
}

std::uint64_t now_ns()
{
	const auto now = std::chrono::steady_clock::now().time_since_epoch();
	return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

const char *describe_stop_code(long long code)
{
	switch (code) {
	case OBS_OUTPUT_BAD_PATH:
		return obs_module_text("Failure.BadPath");
	case OBS_OUTPUT_CONNECT_FAILED:
		return obs_module_text("Failure.ConnectFailed");
	case OBS_OUTPUT_INVALID_STREAM:
		return obs_module_text("Failure.InvalidStream");
	case OBS_OUTPUT_DISCONNECTED:
		return obs_module_text("Failure.Disconnected");
	case OBS_OUTPUT_UNSUPPORTED:
		return obs_module_text("Failure.Unsupported");
	case OBS_OUTPUT_ENCODE_ERROR:
		return obs_module_text("Failure.EncodeError");
	default:
		return obs_module_text("Failure.OutputError");
	}
}

} // namespace

struct StreamingEngine::Slot {
	Slot(StreamingEngine &owner, Destination initial) : engine(owner), destination(std::move(initial)) {}

	StreamingEngine &engine;

	// Read by signal callbacks, so only changed while its signals are disconnected.
	Destination destination;

	mutable std::mutex session_mutex;
	Session session;

	// UI thread only.
	OBSOutput output;
	OBSService service;
	std::string video_key;
	std::string audio_key;
	OutputCounters counters;
	StreamStats stats;
	std::optional<SessionState> last_logged_state;

	SessionState state() const
	{
		const std::lock_guard<std::mutex> lock(session_mutex);
		return session.state();
	}
};

StreamingEngine::StreamingEngine(SecretStore &secrets, VideoSources &video_sources, Dispatcher dispatcher)
	: secrets_(secrets),
	  video_sources_(video_sources),
	  dispatcher_(std::move(dispatcher))
{
}

StreamingEngine::~StreamingEngine()
{
	shutdown();
}

void StreamingEngine::set_status_listener(StatusListener listener)
{
	listener_ = std::move(listener);
}

void StreamingEngine::set_destinations(std::vector<Destination> destinations)
{
	std::map<std::string, std::shared_ptr<Slot>> slots;
	for (const Destination &destination : destinations) {
		std::shared_ptr<Slot> slot = find_slot(destination.id);
		if (!slot) {
			slot = std::make_shared<Slot>(*this, destination);
		} else if (!slot->output) {
			slot->destination = destination;
		}
		slots.emplace(destination.id, std::move(slot));
	}

	for (auto &[id, slot] : slots_) {
		if (slots.find(id) == slots.end()) {
			force_stop_and_release(*slot);
		}
	}

	slots_ = std::move(slots);
	destinations_ = std::move(destinations);
}

std::vector<DestinationStatus> StreamingEngine::statuses() const
{
	std::vector<DestinationStatus> result;
	for (const Destination &destination : destinations_) {
		const std::shared_ptr<Slot> slot = find_slot(destination.id);
		if (!slot) {
			continue;
		}

		result.push_back(status_of(*slot));
	}
	return result;
}

void StreamingEngine::start(const std::vector<std::string> &ids)
{
	if (shut_down_) {
		return;
	}

	std::vector<Destination> startable;
	for (const std::string &id : ids) {
		const std::shared_ptr<Slot> slot = find_slot(id);
		if (!slot) {
			continue;
		}
		release_if_ended(*slot);
		if (slot->output) {
			continue;
		}

		slot->destination = *std::find_if(destinations_.begin(), destinations_.end(),
						  [&](const Destination &candidate) { return candidate.id == id; });
		{
			const std::lock_guard<std::mutex> lock(slot->session_mutex);
			if (!slot->session.request_start()) {
				continue;
			}
		}
		slot->counters = {};
		slot->counters.timestamp_ns = now_ns();
		slot->stats = {};
		publish(*slot);

		if (const auto blocker = start_blocker(slot->destination)) {
			fail_start(*slot, *blocker);
			continue;
		}
		startable.push_back(slot->destination);
	}

	if (startable.empty()) {
		return;
	}

	// Only the destinations being started are grouped. An encoder already running under
	// the same key is joined through its lease.
	const MainStreamSnapshot main_stream = read_main_stream();
	for (const EncoderAssignment &assignment : plan_encoders(startable, main_stream.encoders)) {
		const std::shared_ptr<Slot> slot = find_slot(assignment.destination_id);
		if (const auto failure = start_output(*slot, assignment, main_stream)) {
			fail_start(*slot, *failure);
		}
	}
}

void StreamingEngine::stop(const std::vector<std::string> &ids)
{
	for (const std::string &id : ids) {
		const std::shared_ptr<Slot> slot = find_slot(id);
		if (!slot) {
			continue;
		}
		{
			const std::lock_guard<std::mutex> lock(slot->session_mutex);
			if (!slot->session.request_stop()) {
				continue;
			}
		}
		publish(*slot);

		// While connecting this does nothing, because libobs only stops an active output.
		// on_output_started then issues the stop that was asked for here. A force stop
		// would work now, but it joins the connect thread and would freeze the UI.
		obs_output_stop(slot->output);
	}
}

void StreamingEngine::start_all()
{
	start(select_for_start_all(destinations_, session_states()));
}

void StreamingEngine::stop_all()
{
	stop(select_for_stop_all(destinations_, session_states()));
}

void StreamingEngine::main_stream_starting()
{
	start(select_for_main_start(destinations_, session_states()));
}

void StreamingEngine::main_stream_stopping()
{
	stop(select_for_main_stop(destinations_, session_states()));
}

void StreamingEngine::sample_stats()
{
	const std::uint64_t now = now_ns();
	for (auto &[id, slot] : slots_) {
		const SessionState state = slot->state();
		if (!slot->output || (state != SessionState::Live && state != SessionState::Reconnecting)) {
			continue;
		}

		OutputCounters current;
		current.total_bytes = obs_output_get_total_bytes(slot->output);
		current.total_frames = static_cast<std::uint64_t>(obs_output_get_total_frames(slot->output));
		current.dropped_frames = static_cast<std::uint64_t>(obs_output_get_frames_dropped(slot->output));
		current.timestamp_ns = now;

		slot->stats = compute_stream_stats(slot->counters, current);
		slot->counters = current;
		publish(*slot);
	}
}

void StreamingEngine::stop_everything_now()
{
	for (auto &[id, slot] : slots_) {
		force_stop_and_release(*slot);
	}
}

void StreamingEngine::shutdown()
{
	shut_down_ = true;
	stop_everything_now();
}

void StreamingEngine::on_output_started(void *data, calldata_t *)
{
	Slot *slot = static_cast<Slot *>(data);
	bool stop_requested;
	{
		const std::lock_guard<std::mutex> lock(slot->session_mutex);
		slot->session.output_started();
		stop_requested = slot->session.state() == SessionState::Stopping;
	}
	StreamingEngine &engine = slot->engine;
	const std::string &id = slot->destination.id;
	if (stop_requested) {
		engine.dispatcher_([&engine, id] {
			const std::shared_ptr<Slot> current = engine.find_slot(id);
			if (current && current->output && current->state() == SessionState::Stopping) {
				obs_output_stop(current->output);
			}
		});
	}
	engine.publish_later(id);
}

void StreamingEngine::on_output_stopped(void *data, calldata_t *params)
{
	Slot *slot = static_cast<Slot *>(data);
	const long long code = calldata_int(params, "code");
	const char *last_error = calldata_string(params, "last_error");
	const std::string reason = last_error != nullptr && *last_error != '\0' ? last_error : describe_stop_code(code);
	{
		const std::lock_guard<std::mutex> lock(slot->session_mutex);
		apply_output_stop(slot->session, code == OBS_OUTPUT_SUCCESS, reason);
	}

	StreamingEngine &engine = slot->engine;
	const std::string id = slot->destination.id;
	engine.dispatcher_([&engine, id] {
		const std::shared_ptr<Slot> current = engine.find_slot(id);
		if (current) {
			engine.release_if_ended(*current);
			engine.publish(*current);
		}
	});
}

void StreamingEngine::on_output_reconnecting(void *data, calldata_t *)
{
	Slot *slot = static_cast<Slot *>(data);
	{
		const std::lock_guard<std::mutex> lock(slot->session_mutex);
		slot->session.connection_lost();
	}
	slot->engine.publish_later(slot->destination.id);
}

void StreamingEngine::on_output_reconnected(void *data, calldata_t *)
{
	Slot *slot = static_cast<Slot *>(data);
	{
		const std::lock_guard<std::mutex> lock(slot->session_mutex);
		slot->session.reconnected();
	}
	slot->engine.publish_later(slot->destination.id);
}

std::shared_ptr<StreamingEngine::Slot> StreamingEngine::find_slot(const std::string &id) const
{
	const auto found = slots_.find(id);
	return found == slots_.end() ? nullptr : found->second;
}

std::map<std::string, SessionState> StreamingEngine::session_states() const
{
	std::map<std::string, SessionState> states;
	for (const auto &[id, slot] : slots_) {
		states.emplace(id, slot->state());
	}
	return states;
}

std::optional<std::string> StreamingEngine::start_blocker(const Destination &destination)
{
	const std::vector<ValidationIssue> issues = validate_destination(destination);
	if (!issues.empty()) {
		return issues.front().message;
	}
	if (video_sources_.video_for(destination.video_source) == nullptr) {
		return std::string(obs_module_text(destination.video_source == VideoSource::Vertical
							   ? "Failure.VerticalUnavailable"
							   : "Failure.VideoUnavailable"));
	}
	return std::nullopt;
}

std::optional<std::string> StreamingEngine::start_output(Slot &slot, const EncoderAssignment &assignment,
							 const MainStreamSnapshot &main_stream)
{
	const Destination &destination = slot.destination;

	SecretLookup key = secrets_.get(destination.id);
	if (key.error) {
		return key.error->message;
	}
	if (!key.secret) {
		return std::string(obs_module_text("Failure.NoStreamKey"));
	}

	std::string failure;
	obs_encoder_t *video_encoder = lease_video_encoder(assignment, main_stream, failure);
	if (video_encoder == nullptr) {
		return failure;
	}
	slot.video_key = assignment.video_key;

	obs_encoder_t *audio_encoder = lease_audio_encoder(assignment, main_stream, failure);
	if (audio_encoder == nullptr) {
		return failure;
	}
	slot.audio_key = assignment.audio_key;

	OBSDataAutoRelease service_settings = obs_data_create();
	obs_data_set_string(service_settings, "server", destination.server_url.c_str());
	obs_data_set_string(service_settings, "key", key.secret->c_str());
	OBSServiceAutoRelease service = obs_service_create(kServiceId, object_name("service", destination.id).c_str(),
							   service_settings, nullptr);
	if (!service) {
		return std::string(obs_module_text("Failure.ServiceCreate"));
	}

	OBSOutputAutoRelease output =
		obs_output_create(kOutputId, object_name("output", destination.id).c_str(), nullptr, nullptr);
	if (!output) {
		return std::string(obs_module_text("Failure.OutputCreate"));
	}

	slot.service = service.Get();
	slot.output = output.Get();

	obs_output_set_service(output, service);
	obs_output_set_video_encoder(output, video_encoder);
	obs_output_set_audio_encoder(output, audio_encoder, 0);
	obs_output_set_reconnect_settings(output, destination.reconnect.max_retries(),
					  destination.reconnect.base_delay_seconds());

	connect_signals(slot);

	if (!obs_output_start(output)) {
		const char *last_error = obs_output_get_last_error(output);
		return std::string(last_error != nullptr && *last_error != '\0'
					   ? last_error
					   : obs_module_text("Failure.StartRefused"));
	}
	return std::nullopt;
}

obs_encoder_t *StreamingEngine::lease_video_encoder(const EncoderAssignment &assignment,
						    const MainStreamSnapshot &main_stream, std::string &failure)
{
	const std::string &key = assignment.video_key;
	if (leases_.count(key) > 0) {
		leases_.acquire(key);
		return encoders_.at(key);
	}

	OBSEncoder encoder;
	if (assignment.reuse_main_video) {
		encoder = main_stream.video;
	} else {
		const auto spec = video_encoder_spec(assignment.video_encoder, main_stream.video);
		if (!spec) {
			failure = obs_module_text("Failure.VideoEncoderUnavailable");
			return nullptr;
		}
		OBSEncoderAutoRelease created = obs_video_encoder_create(
			spec->id.c_str(), object_name("encoder", key).c_str(), spec->settings, nullptr);
		if (!created) {
			failure = obs_module_text("Failure.VideoEncoderCreate");
			return nullptr;
		}
		obs_encoder_set_video(created, video_sources_.video_for(assignment.video_source));
		encoder = created.Get();
	}

	leases_.acquire(key);
	return encoders_.insert_or_assign(key, encoder).first->second;
}

obs_encoder_t *StreamingEngine::lease_audio_encoder(const EncoderAssignment &assignment,
						    const MainStreamSnapshot &main_stream, std::string &failure)
{
	const std::string &key = assignment.audio_key;
	if (leases_.count(key) > 0) {
		leases_.acquire(key);
		return encoders_.at(key);
	}

	OBSEncoder encoder;
	if (assignment.reuse_main_audio) {
		encoder = main_stream.audio;
	} else {
		const auto spec = audio_encoder_spec();
		if (!spec) {
			failure = obs_module_text("Failure.AudioEncoderUnavailable");
			return nullptr;
		}
		const auto mixer = static_cast<size_t>(assignment.audio_track - 1);
		OBSEncoderAutoRelease created = obs_audio_encoder_create(
			spec->id.c_str(), object_name("encoder", key).c_str(), spec->settings, mixer, nullptr);
		if (!created) {
			failure = obs_module_text("Failure.AudioEncoderCreate");
			return nullptr;
		}
		obs_encoder_set_audio(created, obs_get_audio());
		encoder = created.Get();
	}

	leases_.acquire(key);
	return encoders_.insert_or_assign(key, encoder).first->second;
}

void StreamingEngine::return_lease(std::string &key)
{
	if (!key.empty() && leases_.release(key)) {
		encoders_.erase(key);
	}
	key.clear();
}

void StreamingEngine::fail_start(Slot &slot, const std::string &reason)
{
	release_resources(slot);
	{
		const std::lock_guard<std::mutex> lock(slot.session_mutex);
		slot.session.fail(reason);
	}
	publish(slot);
}

void StreamingEngine::connect_signals(Slot &slot)
{
	signal_handler_t *signals = obs_output_get_signal_handler(slot.output);
	signal_handler_connect(signals, "start", on_output_started, &slot);
	signal_handler_connect(signals, "stop", on_output_stopped, &slot);
	signal_handler_connect(signals, "reconnect", on_output_reconnecting, &slot);
	signal_handler_connect(signals, "reconnect_success", on_output_reconnected, &slot);
}

void StreamingEngine::disconnect_signals(Slot &slot)
{
	signal_handler_t *signals = obs_output_get_signal_handler(slot.output);
	signal_handler_disconnect(signals, "start", on_output_started, &slot);
	signal_handler_disconnect(signals, "stop", on_output_stopped, &slot);
	signal_handler_disconnect(signals, "reconnect", on_output_reconnecting, &slot);
	signal_handler_disconnect(signals, "reconnect_success", on_output_reconnected, &slot);
}

void StreamingEngine::release_resources(Slot &slot)
{
	if (slot.output) {
		disconnect_signals(slot);
	}

	// The output goes first: destroying it waits for it to finish and clears the
	// service's back-pointer. The output also holds its own encoder references.
	slot.output = nullptr;
	slot.service = nullptr;
	return_lease(slot.video_key);
	return_lease(slot.audio_key);
}

void StreamingEngine::force_stop_and_release(Slot &slot)
{
	if (slot.output) {
		disconnect_signals(slot);
		const SessionState state = slot.state();
		if (is_running(state) || state == SessionState::Stopping) {
			obs_output_force_stop(slot.output);
		}
	}
	release_resources(slot);

	{
		const std::lock_guard<std::mutex> lock(slot.session_mutex);
		slot.session.request_stop();
		slot.session.output_stopped();
	}
	publish(slot);
}

void StreamingEngine::release_if_ended(Slot &slot)
{
	const SessionState state = slot.state();
	if (slot.output && (state == SessionState::Idle || state == SessionState::Failed)) {
		release_resources(slot);
	}
}

void StreamingEngine::publish_later(const std::string &id)
{
	dispatcher_([this, id] {
		const std::shared_ptr<Slot> slot = find_slot(id);
		if (slot) {
			publish(*slot);
		}
	});
}

DestinationStatus StreamingEngine::status_of(const Slot &slot)
{
	DestinationStatus status;
	status.id = slot.destination.id;
	{
		const std::lock_guard<std::mutex> lock(slot.session_mutex);
		status.state = slot.session.state();
		status.failure_reason = slot.session.failure_reason();
	}
	if (status.state == SessionState::Live || status.state == SessionState::Reconnecting) {
		status.stats = slot.stats;
	}
	status.dropped_frames = slot.counters.dropped_frames;
	return status;
}

void StreamingEngine::publish(Slot &slot)
{
	const DestinationStatus status = status_of(slot);

	// Names and states only: neither the server URL nor the key belongs in the log.
	if (status.state != slot.last_logged_state) {
		slot.last_logged_state = status.state;
		if (status.state == SessionState::Failed) {
			obs_log(LOG_WARNING, "destination '%s' failed: %s", slot.destination.name.c_str(),
				status.failure_reason.c_str());
		} else {
			obs_log(LOG_INFO, "destination '%s': %s", slot.destination.name.c_str(),
				to_string(status.state));
		}
	}

	if (listener_) {
		listener_(status);
	}
}

} // namespace multistream
