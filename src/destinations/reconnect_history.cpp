#include "destinations/reconnect_history.hpp"

#include <algorithm>
#include <cstddef>

namespace multistream {

void ReconnectHistory::retry_scheduled(double now_seconds)
{
	if (attempt_ == 0) {
		drops_.push_back(now_seconds);
		while (drops_.size() > static_cast<std::size_t>(kFlappingDrops)) {
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

bool ReconnectHistory::flapping(double now_seconds) const
{
	const auto recent = std::count_if(drops_.begin(), drops_.end(), [now_seconds](double drop) {
		return now_seconds - drop <= kFlappingWindowSeconds;
	});
	return recent >= kFlappingDrops;
}

} // namespace multistream
