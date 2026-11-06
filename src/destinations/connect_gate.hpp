// Tells when an rtmp_output may be stopped or destroyed without racing its connect thread.
//
// rtmp_stream_stop and rtmp_stream_destroy join the connect thread whenever it has not
// finished, and that thread detaches itself on its way out (rtmp-stream.c:1533-1536 at
// OBS 33.0.1). On macOS a join that is already waiting when the detach happens never
// returns: pthread_detach clears the joiner, so thread exit wakes nobody. The join is
// reachable from a "stop" signal, which fires on the connect thread just before it
// detaches.
//
// libobs does not expose whether that thread is alive, so this tracks the only evidence
// there is: the output's signals, and when the next reconnect attempt can begin.
#pragma once

#include <cstdint>

namespace multistream {

class ConnectGate {
public:
	// A signal fired on the connect thread is followed by a few instructions and one log
	// line. This is far longer than that and short enough for nobody to notice.
	static constexpr std::int64_t kSettleMilliseconds = 1000;

	// The next attempt is not trusted to begin exactly when libobs computed.
	static constexpr std::int64_t kRetryMarginMilliseconds = 500;

	// Call before obs_output_start.
	void connect_started();

	// obs_output_start refused: it refuses before creating the connect thread.
	void connect_refused();

	// The "start" or "reconnect_success" signal.
	void connected(std::int64_t now_ms);

	// The "reconnect" signal: libobs now waits, and then starts another connect thread.
	void retry_scheduled(std::int64_t now_ms, int base_delay_seconds);

	// The "stop" signal, which libobs only fires when no more attempts will follow.
	void ended(std::int64_t now_ms);

	bool has_ended() const { return phase_ == Phase::Ended; }

	// True when no connect thread can be running now or start within the margin.
	bool clear(std::int64_t now_ms) const;

private:
	enum class Phase { NotStarted, Connecting, Connected, WaitingToRetry, Ended };

	Phase phase_ = Phase::NotStarted;
	std::int64_t settled_at_ms_ = 0;
	std::int64_t next_attempt_ms_ = 0;
	int retries_ = 0;
};

// The shortest delay libobs can wait before the given 1-based retry: its base delay grows
// by at least 1.5 per retry, up to its 15-minute cap (obs-output.c, output_reconnect).
std::int64_t minimum_retry_delay_ms(int base_delay_seconds, int retry);

} // namespace multistream
