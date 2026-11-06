#pragma once

#include <cstdint>

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

} // namespace multistream
