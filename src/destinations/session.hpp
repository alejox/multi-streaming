#pragma once

#include <string>

namespace multistream {

enum class SessionState { Idle, Starting, Live, Reconnecting, Stopping, Failed };

const char *to_string(SessionState state);

// True while an output should be running: a stop still has work to do.
bool is_running(SessionState state);

// Lifecycle of one destination's output. Each event returns whether it was valid
// for the current state; an invalid event changes nothing.
class Session {
public:
	SessionState state() const { return state_; }
	const std::string &failure_reason() const { return failure_reason_; }

	bool request_start();
	bool output_started();
	bool connection_lost();
	bool reconnected();
	bool request_stop();
	bool output_stopped();
	bool fail(std::string reason);

private:
	SessionState state_ = SessionState::Idle;
	std::string failure_reason_;
};

// Applies an output's stop signal. A clean stop ends the session; any other stop is a
// failure with the given reason, even if a stop was requested, so it is never hidden.
// libobs only signals a stop after its own reconnect attempts are over.
bool apply_output_stop(Session &session, bool clean, std::string reason);

} // namespace multistream
