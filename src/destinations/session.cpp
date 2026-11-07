#include "destinations/session.hpp"

namespace multistream {

const char *to_string(SessionState state)
{
	switch (state) {
	case SessionState::Idle:
		return "idle";
	case SessionState::Starting:
		return "starting";
	case SessionState::Live:
		return "live";
	case SessionState::Reconnecting:
		return "reconnecting";
	case SessionState::Stopping:
		return "stopping";
	case SessionState::Failed:
		return "failed";
	}
	return "unknown";
}

bool is_running(SessionState state)
{
	return state == SessionState::Starting || state == SessionState::Live || state == SessionState::Reconnecting;
}

bool Session::request_start()
{
	if (state_ != SessionState::Idle && state_ != SessionState::Failed) {
		return false;
	}
	state_ = SessionState::Starting;
	failure_reason_.clear();
	return true;
}

bool Session::output_started()
{
	if (state_ != SessionState::Starting) {
		return false;
	}
	state_ = SessionState::Live;
	return true;
}

bool Session::connection_lost()
{
	if (state_ != SessionState::Live) {
		return false;
	}
	state_ = SessionState::Reconnecting;
	return true;
}

bool Session::reconnected()
{
	if (state_ != SessionState::Reconnecting) {
		return false;
	}
	state_ = SessionState::Live;
	return true;
}

bool Session::request_stop()
{
	if (state_ != SessionState::Starting && state_ != SessionState::Live && state_ != SessionState::Reconnecting) {
		return false;
	}
	state_ = SessionState::Stopping;
	return true;
}

bool Session::output_stopped()
{
	if (state_ == SessionState::Idle || state_ == SessionState::Failed) {
		return false;
	}
	state_ = SessionState::Idle;
	return true;
}

bool Session::fail(std::string reason)
{
	if (state_ == SessionState::Idle || state_ == SessionState::Failed) {
		return false;
	}
	state_ = SessionState::Failed;
	failure_reason_ = std::move(reason);
	return true;
}

bool apply_output_stop(Session &session, bool clean, std::string reason)
{
	return clean ? session.output_stopped() : session.fail(std::move(reason));
}

} // namespace multistream
