#include "destinations/stream_stats.hpp"

#include "test_harness.hpp"

using multistream::compute_stream_stats;
using multistream::OutputCounters;

namespace {

constexpr std::uint64_t kSecondNs = 1000000000;

} // namespace

TEST_CASE(computes_bitrate_from_byte_delta)
{
	const OutputCounters previous{1000, 0, 0, kSecondNs};
	const OutputCounters current{1000 + 750000, 0, 0, 3 * kSecondNs};
	// 750000 bytes * 8 bits over 2 seconds is 3000 kbps.
	CHECK_NEAR(compute_stream_stats(previous, current).bitrate_kbps, 3000.0, 0.001);
}

TEST_CASE(zero_interval_yields_zero_bitrate)
{
	const OutputCounters previous{1000, 0, 0, kSecondNs};
	const OutputCounters current{2000, 0, 0, kSecondNs};
	CHECK_NEAR(compute_stream_stats(previous, current).bitrate_kbps, 0.0, 0.001);
}

TEST_CASE(backward_clock_yields_zero_bitrate)
{
	const OutputCounters previous{1000, 0, 0, 2 * kSecondNs};
	const OutputCounters current{2000, 0, 0, kSecondNs};
	CHECK_NEAR(compute_stream_stats(previous, current).bitrate_kbps, 0.0, 0.001);
}

TEST_CASE(byte_counter_reset_yields_zero_bitrate)
{
	const OutputCounters previous{5000000, 0, 0, kSecondNs};
	const OutputCounters current{100, 0, 0, 2 * kSecondNs};
	CHECK_NEAR(compute_stream_stats(previous, current).bitrate_kbps, 0.0, 0.001);
}

TEST_CASE(computes_interval_and_total_drop_percentages)
{
	const OutputCounters previous{0, 100, 0, kSecondNs};
	const OutputCounters current{0, 200, 25, 2 * kSecondNs};
	const auto stats = compute_stream_stats(previous, current);
	CHECK_NEAR(stats.interval_drop_percent, 25.0, 0.001);
	CHECK_NEAR(stats.total_drop_percent, 12.5, 0.001);
}

TEST_CASE(no_frames_yields_zero_drop_percentages)
{
	const auto stats = compute_stream_stats(OutputCounters{}, OutputCounters{});
	CHECK_NEAR(stats.interval_drop_percent, 0.0, 0.001);
	CHECK_NEAR(stats.total_drop_percent, 0.0, 0.001);
}

TEST_CASE(frame_counter_reset_keeps_only_the_total_percentage)
{
	const OutputCounters previous{0, 1000, 50, kSecondNs};
	const OutputCounters current{0, 10, 1, 2 * kSecondNs};
	const auto stats = compute_stream_stats(previous, current);
	CHECK_NEAR(stats.interval_drop_percent, 0.0, 0.001);
	CHECK_NEAR(stats.total_drop_percent, 10.0, 0.001);
}

TEST_CASE(dropped_counter_reset_alone_yields_zero_interval_percentage)
{
	const OutputCounters previous{0, 100, 50, kSecondNs};
	const OutputCounters current{0, 200, 1, 2 * kSecondNs};
	CHECK_NEAR(compute_stream_stats(previous, current).interval_drop_percent, 0.0, 0.001);
}

TEST_CASE(drop_percentages_never_exceed_one_hundred)
{
	const OutputCounters previous{0, 100, 0, kSecondNs};
	const OutputCounters current{0, 110, 500, 2 * kSecondNs};
	const auto stats = compute_stream_stats(previous, current);
	CHECK_NEAR(stats.interval_drop_percent, 100.0, 0.001);
	CHECK_NEAR(stats.total_drop_percent, 100.0, 0.001);
}
