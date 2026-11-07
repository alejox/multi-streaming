#pragma once

#include <cstdint>
#include <string>

namespace multistream {

// A reading of an output's cumulative counters. total_frames includes dropped frames.
struct OutputCounters {
	std::uint64_t total_bytes = 0;
	std::uint64_t total_frames = 0;
	std::uint64_t dropped_frames = 0;
	std::uint64_t timestamp_ns = 0;
};

struct StreamStats {
	double bitrate_kbps = 0.0;
	double interval_drop_percent = 0.0;
	double total_drop_percent = 0.0;
};

// Values that cannot be derived from the interval are reported as zero: a counter
// that went backwards (the output restarted), or a zero or backward time interval.
StreamStats compute_stream_stats(const OutputCounters &previous, const OutputCounters &current);

// How worrying a drop percentage is: above 1 % viewers start to notice, above 5 % the
// stream is visibly broken.
enum class DropLevel { Normal, Warning, Severe };
DropLevel drop_level(double drop_percent);

// "hh:mm:ss"; the hours keep growing past 99.
std::string format_elapsed(std::uint64_t seconds);

} // namespace multistream
