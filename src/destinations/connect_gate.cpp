#include "destinations/connect_gate.hpp"

#include <algorithm>
#include <cmath>

namespace multistream {

void ConnectGate::connect_started()
{
	phase_ = Phase::Connecting;
	retries_ = 0;
}

void ConnectGate::connect_refused()
{
	phase_ = Phase::NotStarted;
}

void ConnectGate::connected(std::int64_t now_ms)
{
	if (phase_ == Phase::Ended) {
		return;
	}
	phase_ = Phase::Connected;
	settled_at_ms_ = now_ms;
	retries_ = 0;
}

void ConnectGate::retry_scheduled(std::int64_t now_ms, int base_delay_seconds)
{
	if (phase_ == Phase::Ended) {
		return;
	}
	phase_ = Phase::WaitingToRetry;
	settled_at_ms_ = now_ms;
	++retries_;
	next_attempt_ms_ = now_ms + minimum_retry_delay_ms(base_delay_seconds, retries_);
}

void ConnectGate::ended(std::int64_t now_ms)
{
	phase_ = Phase::Ended;
	settled_at_ms_ = now_ms;
}

bool ConnectGate::clear(std::int64_t now_ms) const
{
	switch (phase_) {
	case Phase::NotStarted:
		return true;
	case Phase::Connecting:
		return false;
	case Phase::Connected:
	case Phase::Ended:
		return now_ms >= settled_at_ms_ + kSettleMilliseconds;
	case Phase::WaitingToRetry:
		return now_ms >= settled_at_ms_ + kSettleMilliseconds && now_ms < next_attempt_ms_ - kRetryMarginMilliseconds;
	}
	return false;
}

std::int64_t minimum_retry_delay_ms(int base_delay_seconds, int retry)
{
	constexpr double kGrowth = 1.5;
	constexpr double kCapMilliseconds = 15.0 * 60.0 * 1000.0;

	const double delay = base_delay_seconds * 1000.0 * std::pow(kGrowth, std::max(retry - 1, 0));
	return static_cast<std::int64_t>(std::min(delay, kCapMilliseconds));
}

} // namespace multistream
