#include "destinations/reconnect_history.hpp"

#include "test_harness.hpp"

using multistream::ReconnectHistory;

TEST_CASE(attempts_count_retries_until_connected)
{
	ReconnectHistory history;
	CHECK_EQ(history.attempt(), 0);
	history.retry_scheduled(1.0);
	CHECK_EQ(history.attempt(), 1);
	history.retry_scheduled(3.0);
	CHECK_EQ(history.attempt(), 2);
	history.connected();
	CHECK_EQ(history.attempt(), 0);
}

TEST_CASE(retries_of_one_outage_are_one_drop)
{
	ReconnectHistory history;
	for (int retry = 0; retry < 10; ++retry) {
		history.retry_scheduled(retry * 2.0);
	}
	CHECK(!history.flapping(20.0));
}

TEST_CASE(repeated_drops_in_the_window_are_flapping)
{
	ReconnectHistory history;
	for (int drop = 0; drop < 2; ++drop) {
		history.retry_scheduled(drop * 8.0);
		history.connected();
	}
	CHECK(!history.flapping(16.0));
	history.retry_scheduled(16.0);
	history.connected();
	CHECK(history.flapping(16.0));
}

TEST_CASE(the_evidence_pattern_of_a_drop_every_8_seconds_is_flapping)
{
	ReconnectHistory history;
	for (int drop = 0; drop < 42; ++drop) {
		history.retry_scheduled(drop * 8.0);
		history.connected();
	}
	CHECK(history.flapping(42 * 8.0));
}

TEST_CASE(drops_spread_wider_than_the_window_are_not_flapping)
{
	ReconnectHistory history;
	for (int drop = 0; drop < 10; ++drop) {
		history.retry_scheduled(drop * 61.0);
		history.connected();
	}
	CHECK(!history.flapping(9 * 61.0));
}

TEST_CASE(flapping_clears_once_the_drops_age_out)
{
	ReconnectHistory history;
	for (int drop = 0; drop < 3; ++drop) {
		history.retry_scheduled(drop * 8.0);
		history.connected();
	}
	CHECK(history.flapping(30.0));
	CHECK(!history.flapping(16.0 + ReconnectHistory::kFlappingWindowSeconds + 1.0));
}

TEST_CASE(reset_forgets_everything)
{
	ReconnectHistory history;
	for (int drop = 0; drop < 3; ++drop) {
		history.retry_scheduled(drop * 8.0);
		history.connected();
	}
	history.retry_scheduled(30.0);
	history.reset();
	CHECK_EQ(history.attempt(), 0);
	CHECK(!history.flapping(31.0));
}

TEST_CASE(recent_drops_counts_every_drop_in_the_window)
{
	ReconnectHistory history;
	CHECK_EQ(history.recent_drops(0.0), 0);
	for (int drop = 0; drop < 42; ++drop) {
		history.retry_scheduled(drop * 8.0);
		history.connected();
	}
	// Drops at 216 s to 328 s are within two minutes of 336 s.
	CHECK_EQ(history.recent_drops(42 * 8.0), 15);
}

TEST_CASE(recent_drops_forget_drops_older_than_the_window)
{
	ReconnectHistory history;
	for (int drop = 0; drop < 3; ++drop) {
		history.retry_scheduled(drop * 10.0);
		history.connected();
	}
	CHECK_EQ(history.recent_drops(20.0), 3);
	CHECK_EQ(history.recent_drops(135.0), 1);
	CHECK(!history.flapping(135.0));
	CHECK_EQ(history.recent_drops(500.0), 0);
}

TEST_CASE(retries_of_one_outage_count_as_one_recent_drop)
{
	ReconnectHistory history;
	for (int retry = 0; retry < 5; ++retry) {
		history.retry_scheduled(retry * 2.0);
	}
	CHECK_EQ(history.recent_drops(10.0), 1);
	history.reset();
	CHECK_EQ(history.recent_drops(10.0), 0);
}
