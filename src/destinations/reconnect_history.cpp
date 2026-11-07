#include "destinations/reconnect_history.hpp"

#include <algorithm>

namespace multistream {

void ReconnectHistory::retry_scheduled(double now_seconds)
{
	if (attempt_ == 0) {
		drops_.push_back(now_seconds);
		while (now_seconds - drops_.front() > kFlappingWindowSeconds) {
			drops_.pop_front();
		}
	}
	++attempt_;
}

void ReconnectHistory::connected()
{
	attempt_ = 0;
}

void ReconnectHistory::reset()
{
	drops_.clear();
	attempt_ = 0;
}

int ReconnectHistory::recent_drops(double now_seconds) const
{
	return static_cast<int>(std::count_if(drops_.begin(), drops_.end(), [now_seconds](double drop) {
		return now_seconds - drop <= kFlappingWindowSeconds;
	}));
}

bool ReconnectHistory::flapping(double now_seconds) const
{
	return recent_drops(now_seconds) >= kFlappingDrops;
}

} // namespace multistream
