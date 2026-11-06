// Per-destination reconnect settings. Defaults and growth mirror libobs
// (obs-output.c): 20 retries, 2 s base delay, factor 1.5, 15-minute cap.
#pragma once

#include <optional>

namespace multistream {

class ReconnectPolicy {
public:
	ReconnectPolicy() = default;

	// Returns nothing unless 0 <= max_retries <= 10000 and
	// 1 <= base_delay_seconds <= max_delay_seconds <= 900.
	static std::optional<ReconnectPolicy> create(int max_retries, int base_delay_seconds, int max_delay_seconds);

	int max_retries() const { return max_retries_; }
	int base_delay_seconds() const { return base_delay_seconds_; }
	int max_delay_seconds() const { return max_delay_seconds_; }
	bool enabled() const { return max_retries_ > 0; }

	// Delay before the given 1-based attempt; attempts below 1 count as the first.
	int delay_seconds_for_attempt(int attempt) const;

	bool operator==(const ReconnectPolicy &other) const
	{
		return max_retries_ == other.max_retries_ && base_delay_seconds_ == other.base_delay_seconds_ &&
		       max_delay_seconds_ == other.max_delay_seconds_;
	}

private:
	int max_retries_ = 20;
	int base_delay_seconds_ = 2;
	int max_delay_seconds_ = 900;
};

} // namespace multistream
