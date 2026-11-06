#include "destinations/encoder_leases.hpp"

#include "test_harness.hpp"

using multistream::EncoderLeases;

TEST_CASE(the_first_acquire_creates_and_the_last_release_frees)
{
	EncoderLeases leases;
	CHECK(leases.acquire("video"));
	CHECK(!leases.acquire("video"));
	CHECK_EQ(leases.count("video"), 2);
	CHECK(!leases.release("video"));
	CHECK_EQ(leases.count("video"), 1);
	CHECK(leases.release("video"));
	CHECK_EQ(leases.count("video"), 0);
}

TEST_CASE(keys_are_independent)
{
	EncoderLeases leases;
	CHECK(leases.acquire("a"));
	CHECK(leases.acquire("b"));
	CHECK(leases.release("a"));
	CHECK_EQ(leases.count("b"), 1);
}

TEST_CASE(a_key_can_be_acquired_again_after_it_was_freed)
{
	EncoderLeases leases;
	CHECK(leases.acquire("a"));
	CHECK(leases.release("a"));
	CHECK(leases.acquire("a"));
}

TEST_CASE(releasing_an_unknown_key_does_nothing)
{
	EncoderLeases leases;
	CHECK(!leases.release("a"));
	CHECK_EQ(leases.count("a"), 0);
}
