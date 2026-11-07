#include "destinations/session.hpp"

#include "test_harness.hpp"

#include <cstdint>

using multistream::is_on_air;
using multistream::seconds_until;
using multistream::Session;
using multistream::SessionState;

namespace {

Session live_session()
{
	Session session;
	REQUIRE(session.request_start());
	REQUIRE(session.output_started());
	return session;
}

} // namespace

TEST_CASE(starts_idle)
{
	const Session session;
	CHECK(session.state() == SessionState::Idle);
	CHECK(session.failure_reason().empty());
}

TEST_CASE(follows_the_happy_path)
{
	Session session;
	CHECK(session.request_start());
	CHECK(session.state() == SessionState::Starting);
	CHECK(session.output_started());
	CHECK(session.state() == SessionState::Live);
	CHECK(session.request_stop());
	CHECK(session.state() == SessionState::Stopping);
	CHECK(session.output_stopped());
	CHECK(session.state() == SessionState::Idle);
}

TEST_CASE(reconnects_and_recovers)
{
	Session session = live_session();
	CHECK(session.connection_lost());
	CHECK(session.state() == SessionState::Reconnecting);
	CHECK(session.reconnected());
	CHECK(session.state() == SessionState::Live);
}

TEST_CASE(stops_while_starting_or_reconnecting)
{
	Session starting;
	REQUIRE(starting.request_start());
	CHECK(starting.request_stop());
	CHECK(starting.state() == SessionState::Stopping);

	Session reconnecting = live_session();
	REQUIRE(reconnecting.connection_lost());
	CHECK(reconnecting.request_stop());
	CHECK(reconnecting.state() == SessionState::Stopping);
}

TEST_CASE(unrequested_clean_stop_returns_to_idle)
{
	Session session = live_session();
	CHECK(session.output_stopped());
	CHECK(session.state() == SessionState::Idle);
}

TEST_CASE(failure_keeps_its_reason_until_the_next_start)
{
	Session session = live_session();
	REQUIRE(session.connection_lost());
	CHECK(session.fail("retries exhausted"));
	CHECK(session.state() == SessionState::Failed);
	CHECK_EQ(session.failure_reason(), "retries exhausted");

	CHECK(!session.output_stopped());
	CHECK(session.state() == SessionState::Failed);
	CHECK_EQ(session.failure_reason(), "retries exhausted");

	CHECK(session.request_start());
	CHECK(session.state() == SessionState::Starting);
	CHECK(session.failure_reason().empty());
}

TEST_CASE(failure_without_a_reason_still_fails_and_leaves_the_text_to_the_ui)
{
	Session session;
	REQUIRE(session.request_start());
	CHECK(session.fail(""));
	CHECK(session.state() == SessionState::Failed);
	CHECK(session.failure_reason().empty());
}

TEST_CASE(failure_during_stop_is_not_swallowed)
{
	Session session = live_session();
	REQUIRE(session.request_stop());
	CHECK(session.fail("encoder error"));
	CHECK(session.state() == SessionState::Failed);
}

TEST_CASE(rejects_invalid_transitions_without_changing_state)
{
	Session idle;
	CHECK(!idle.output_started());
	CHECK(!idle.connection_lost());
	CHECK(!idle.reconnected());
	CHECK(!idle.request_stop());
	CHECK(!idle.output_stopped());
	CHECK(!idle.fail("nope"));
	CHECK(idle.state() == SessionState::Idle);
	CHECK(idle.failure_reason().empty());

	Session live = live_session();
	CHECK(!live.request_start());
	CHECK(!live.output_started());
	CHECK(!live.reconnected());
	CHECK(live.state() == SessionState::Live);

	Session stopping = live_session();
	REQUIRE(stopping.request_stop());
	CHECK(!stopping.request_start());
	CHECK(!stopping.request_stop());
	CHECK(!stopping.connection_lost());
	CHECK(stopping.state() == SessionState::Stopping);
}

TEST_CASE(names_every_state)
{
	CHECK_EQ(std::string(to_string(SessionState::Idle)), "idle");
	CHECK_EQ(std::string(to_string(SessionState::Starting)), "starting");
	CHECK_EQ(std::string(to_string(SessionState::Live)), "live");
	CHECK_EQ(std::string(to_string(SessionState::Reconnecting)), "reconnecting");
	CHECK_EQ(std::string(to_string(SessionState::Stopping)), "stopping");
	CHECK_EQ(std::string(to_string(SessionState::Failed)), "failed");
}

TEST_CASE(a_clean_output_stop_ends_the_session)
{
	Session session = live_session();
	CHECK(apply_output_stop(session, true, ""));
	CHECK(session.state() == SessionState::Idle);
}

TEST_CASE(an_unclean_output_stop_fails_with_its_reason)
{
	Session session = live_session();
	CHECK(apply_output_stop(session, false, "Connection reset"));
	CHECK(session.state() == SessionState::Failed);
	CHECK_EQ(session.failure_reason(), "Connection reset");
}

TEST_CASE(a_connection_failure_after_a_requested_stop_is_still_reported)
{
	Session session;
	REQUIRE(session.request_start());
	REQUIRE(session.request_stop());
	CHECK(apply_output_stop(session, false, "Could not connect"));
	CHECK(session.state() == SessionState::Failed);
}

TEST_CASE(is_running_covers_the_states_a_stop_applies_to)
{
	CHECK(is_running(SessionState::Starting));
	CHECK(is_running(SessionState::Live));
	CHECK(is_running(SessionState::Reconnecting));
	CHECK(!is_running(SessionState::Idle));
	CHECK(!is_running(SessionState::Stopping));
	CHECK(!is_running(SessionState::Failed));
}

TEST_CASE(on_air_covers_live_and_reconnecting_only)
{
	CHECK(is_on_air(SessionState::Live));
	CHECK(is_on_air(SessionState::Reconnecting));
	CHECK(!is_on_air(SessionState::Idle));
	CHECK(!is_on_air(SessionState::Starting));
	CHECK(!is_on_air(SessionState::Stopping));
	CHECK(!is_on_air(SessionState::Failed));
}

TEST_CASE(retry_countdown_rounds_up_to_whole_seconds)
{
	constexpr std::uint64_t second = 1000000000;
	CHECK_EQ(seconds_until(10 * second, 10 * second - 1), 1);
	CHECK_EQ(seconds_until(10 * second, 7 * second), 3);
	CHECK_EQ(seconds_until(10 * second, 7 * second + 1), 3);
}

TEST_CASE(retry_countdown_is_zero_once_the_retry_is_due)
{
	constexpr std::uint64_t second = 1000000000;
	CHECK_EQ(seconds_until(10 * second, 10 * second), 0);
	CHECK_EQ(seconds_until(10 * second, 12 * second), 0);
	CHECK_EQ(seconds_until(0, 5 * second), 0);
}
