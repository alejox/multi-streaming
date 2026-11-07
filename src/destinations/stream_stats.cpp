#include "destinations/stream_stats.hpp"

#include <algorithm>
#include <cstdio>

namespace multistream {
namespace {

double drop_percent(std::uint64_t dropped, std::uint64_t total)
{
	if (total == 0) {
		return 0.0;
	}
	return std::min(100.0, static_cast<double>(dropped) * 100.0 / static_cast<double>(total));
}

} // namespace

StreamStats compute_stream_stats(const OutputCounters &previous, const OutputCounters &current)
{
	StreamStats stats;

	if (current.timestamp_ns > previous.timestamp_ns && current.total_bytes >= previous.total_bytes) {
		const auto bits = static_cast<double>(current.total_bytes - previous.total_bytes) * 8.0;
		const double seconds = static_cast<double>(current.timestamp_ns - previous.timestamp_ns) / 1e9;
		stats.bitrate_kbps = bits / seconds / 1000.0;
	}

	if (current.total_frames >= previous.total_frames && current.dropped_frames >= previous.dropped_frames) {
		stats.interval_drop_percent = drop_percent(current.dropped_frames - previous.dropped_frames,
							   current.total_frames - previous.total_frames);
	}

	stats.total_drop_percent = drop_percent(current.dropped_frames, current.total_frames);
	return stats;
}

DropLevel drop_level(double drop_percent)
{
	if (drop_percent > 5.0) {
		return DropLevel::Severe;
	}
	if (drop_percent > 1.0) {
		return DropLevel::Warning;
	}
	return DropLevel::Normal;
}

std::string format_elapsed(std::uint64_t seconds)
{
	char text[32];
	std::snprintf(text, sizeof(text), "%02llu:%02u:%02u", static_cast<unsigned long long>(seconds / 3600),
		      static_cast<unsigned>(seconds / 60 % 60), static_cast<unsigned>(seconds % 60));
	return text;
}

} // namespace multistream
