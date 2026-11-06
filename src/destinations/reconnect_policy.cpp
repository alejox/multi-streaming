#include "destinations/reconnect_policy.hpp"

namespace multistream {
namespace {

constexpr int kMaxRetriesLimit = 10000;
constexpr int kMaxDelayLimitSeconds = 900;
constexpr double kGrowthFactor = 1.5;

} // namespace

std::optional<ReconnectPolicy> ReconnectPolicy::create(int max_retries, int base_delay_seconds, int max_delay_seconds)
{
	const bool valid = max_retries >= 0 && max_retries <= kMaxRetriesLimit && base_delay_seconds >= 1 &&
			   base_delay_seconds <= max_delay_seconds && max_delay_seconds <= kMaxDelayLimitSeconds;
	if (!valid) {
		return std::nullopt;
	}

	ReconnectPolicy policy;
	policy.max_retries_ = max_retries;
	policy.base_delay_seconds_ = base_delay_seconds;
	policy.max_delay_seconds_ = max_delay_seconds;
	return policy;
}

int ReconnectPolicy::delay_seconds_for_attempt(int attempt) const
{
	double delay = base_delay_seconds_;
	for (int current = 1; current < attempt && delay < max_delay_seconds_; ++current) {
		delay *= kGrowthFactor;
	}
	return delay < max_delay_seconds_ ? static_cast<int>(delay) : max_delay_seconds_;
}

} // namespace multistream
