#include "destinations/connect_gate.hpp"

#include "test_harness.hpp"

using multistream::ConnectGate;
using multistream::minimum_retry_delay_ms;

namespace {
constexpr std::int64_t kSettle = ConnectGate::kSettleMilliseconds;
constexpr std::int64_t kMargin = ConnectGate::kRetryMarginMilliseconds;
} // namespace

TEST_CASE(an_output_that_never_started_is_clear)
{
	CHECK(ConnectGate().clear(0));
}

TEST_CASE(connecting_is_never_clear)
{
	ConnectGate gate;
	gate.connect_started();
	CHECK(!gate.clear(0));
	CHECK(!gate.clear(1000000));
}

TEST_CASE(a_refused_start_is_clear_at_once_because_no_thread_was_created)
{
	ConnectGate gate;
	gate.connect_started();
	gate.connect_refused();
	CHECK(gate.clear(0));
}

TEST_CASE(connected_is_clear_only_after_the_connect_thread_had_time_to_finish)
{
	ConnectGate gate;
	gate.connect_started();
	gate.connected(5000);
	CHECK(!gate.clear(5000));
	CHECK(!gate.clear(5000 + kSettle - 1));
	CHECK(gate.clear(5000 + kSettle));
	CHECK(gate.clear(5000 + kSettle * 100));
}

TEST_CASE(the_stop_signal_is_not_clear_at_once_because_it_fires_on_the_connect_thread)
{
	ConnectGate gate;
	gate.connect_started();
	gate.ended(5000);
	CHECK(gate.has_ended());
	CHECK(!gate.clear(5000));
	CHECK(gate.clear(5000 + kSettle));
}

TEST_CASE(waiting_to_retry_is_clear_only_inside_the_window_before_the_next_attempt)
{
	ConnectGate gate;
	gate.connect_started();
	gate.connected(0);
	gate.retry_scheduled(10000, 4);

	CHECK(!gate.clear(10000));
	CHECK(gate.clear(10000 + kSettle));
	CHECK(gate.clear(10000 + 4000 - kMargin - 1));
	CHECK(!gate.clear(10000 + 4000 - kMargin));
	CHECK(!gate.clear(10000 + 4000));
}

TEST_CASE(a_later_retry_waits_longer_so_its_window_is_wider)
{
	ConnectGate gate;
	gate.connect_started();
	gate.connected(0);
	gate.retry_scheduled(1000, 2);
	gate.retry_scheduled(10000, 2);

	// Second retry: 3 s rather than 2 s.
	CHECK(gate.clear(10000 + 2400));
	CHECK(!gate.clear(10000 + 2500));
}

TEST_CASE(reconnecting_starts_the_retry_count_over)
{
	ConnectGate gate;
	gate.connect_started();
	gate.connected(0);
	gate.retry_scheduled(1000, 2);
	gate.retry_scheduled(5000, 2);
	gate.connected(9000);
	gate.retry_scheduled(20000, 2);

	CHECK(!gate.clear(20000 + 2000 - kMargin));
}

TEST_CASE(a_one_second_retry_has_no_window_at_all)
{
	ConnectGate gate;
	gate.connect_started();
	gate.connected(0);
	gate.retry_scheduled(1000, 1);

	for (std::int64_t now = 1000; now < 3000; now += 50) {
		CHECK(!gate.clear(now));
	}
}

TEST_CASE(a_signal_after_the_end_does_not_reopen_the_gate)
{
	ConnectGate gate;
	gate.connect_started();
	gate.ended(0);
	gate.retry_scheduled(10, 2);
	gate.connected(20);
	CHECK(gate.has_ended());
	CHECK(gate.clear(kSettle));
}

TEST_CASE(retry_delays_grow_by_one_and_a_half_up_to_the_libobs_cap)
{
	CHECK_EQ(minimum_retry_delay_ms(2, 1), 2000);
	CHECK_EQ(minimum_retry_delay_ms(2, 2), 3000);
	CHECK_EQ(minimum_retry_delay_ms(2, 3), 4500);
	CHECK_EQ(minimum_retry_delay_ms(900, 1), 900000);
	CHECK_EQ(minimum_retry_delay_ms(2, 100), 900000);
}
