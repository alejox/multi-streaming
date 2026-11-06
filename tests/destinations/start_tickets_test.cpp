#include "destinations/start_tickets.hpp"

#include "test_harness.hpp"

using multistream::StartTickets;

TEST_CASE(a_current_ticket_is_redeemed_once)
{
	StartTickets tickets;
	const auto ticket = tickets.issue("a");
	CHECK(tickets.pending("a"));
	CHECK(tickets.redeem("a", ticket));
	CHECK(!tickets.pending("a"));
	CHECK(!tickets.redeem("a", ticket));
}

TEST_CASE(a_stop_during_the_read_voids_the_ticket)
{
	StartTickets tickets;
	const auto ticket = tickets.issue("a");
	tickets.cancel("a");
	CHECK(!tickets.pending("a"));
	CHECK(!tickets.redeem("a", ticket));
}

TEST_CASE(a_read_from_before_a_stop_and_restart_does_not_count)
{
	StartTickets tickets;
	const auto first = tickets.issue("a");
	tickets.cancel("a");
	const auto second = tickets.issue("a");
	CHECK(first != second);
	CHECK(!tickets.redeem("a", first));
	CHECK(tickets.pending("a"));
	CHECK(tickets.redeem("a", second));
}

TEST_CASE(destinations_do_not_affect_each_other)
{
	StartTickets tickets;
	const auto a = tickets.issue("a");
	const auto b = tickets.issue("b");
	tickets.cancel("a");
	CHECK(!tickets.redeem("a", a));
	CHECK(tickets.redeem("b", b));
}

TEST_CASE(teardown_voids_every_ticket)
{
	StartTickets tickets;
	const auto a = tickets.issue("a");
	const auto b = tickets.issue("b");
	tickets.cancel_all();
	CHECK(!tickets.redeem("a", a));
	CHECK(!tickets.redeem("b", b));
}

TEST_CASE(an_unknown_destination_has_no_valid_ticket)
{
	StartTickets tickets;
	CHECK(!tickets.pending("a"));
	CHECK(!tickets.redeem("a", 1));
}
