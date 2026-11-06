#include "destinations/reconnect_policy.hpp"

namespace multistream {
namespace {

constexpr int kMaxRetriesLimit = 10000;
constexpr int kBaseDelayLimitSeconds = 900;

} // namespace

std::optional<ReconnectPolicy> ReconnectPolicy::create(int max_retries, int base_delay_seconds)
{
	const bool valid = max_retries >= 0 && max_retries <= kMaxRetriesLimit && base_delay_seconds >= 1 &&
			   base_delay_seconds <= kBaseDelayLimitSeconds;
	if (!valid) {
		return std::nullopt;
	}

	ReconnectPolicy policy;
	policy.max_retries_ = max_retries;
	policy.base_delay_seconds_ = base_delay_seconds;
	return policy;
}

} // namespace multistream
