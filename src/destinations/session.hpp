#pragma once

#include <string>

namespace multistream {

enum class SessionState { Idle, Starting, Live, Reconnecting, Stopping, Failed };

const char *to_string(SessionState state);

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

} // namespace multistream
