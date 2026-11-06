// Per-destination reconnect settings, passed to obs_output_set_reconnect_settings.
// That is everything libobs lets a caller set: its growth factor of about 1.5 and
// its 15-minute delay cap are hard-coded in obs-output.c, so neither is modeled here.
#pragma once

#include <optional>

namespace multistream {

class ReconnectPolicy {
public:
	ReconnectPolicy() = default;

	// Returns nothing unless 0 <= max_retries <= 10000 and 1 <= base_delay_seconds <= 900.
	static std::optional<ReconnectPolicy> create(int max_retries, int base_delay_seconds);

	int max_retries() const { return max_retries_; }
	int base_delay_seconds() const { return base_delay_seconds_; }
	bool enabled() const { return max_retries_ > 0; }

	bool operator==(const ReconnectPolicy &other) const
	{
		return max_retries_ == other.max_retries_ && base_delay_seconds_ == other.base_delay_seconds_;
	}

private:
	int max_retries_ = 20;
	int base_delay_seconds_ = 2;
};

} // namespace multistream
