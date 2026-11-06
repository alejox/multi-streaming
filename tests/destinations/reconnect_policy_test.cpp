#include "destinations/reconnect_policy.hpp"

#include "test_harness.hpp"

using multistream::ReconnectPolicy;

TEST_CASE(defaults_match_libobs)
{
	const ReconnectPolicy policy;
	CHECK_EQ(policy.max_retries(), 20);
	CHECK_EQ(policy.base_delay_seconds(), 2);
	CHECK_EQ(policy.max_delay_seconds(), 900);
	CHECK(policy.enabled());
}

TEST_CASE(zero_retries_disables_reconnect)
{
	const auto policy = ReconnectPolicy::create(0, 2, 900);
	REQUIRE(policy.has_value());
	CHECK(!policy->enabled());
}

TEST_CASE(rejects_out_of_range_values)
{
	CHECK(!ReconnectPolicy::create(-1, 2, 900).has_value());
	CHECK(!ReconnectPolicy::create(10001, 2, 900).has_value());
	CHECK(!ReconnectPolicy::create(5, 0, 900).has_value());
	CHECK(!ReconnectPolicy::create(5, 10, 5).has_value());
	CHECK(!ReconnectPolicy::create(5, 2, 901).has_value());
}

TEST_CASE(delay_grows_geometrically_up_to_the_cap)
{
	const auto policy = ReconnectPolicy::create(50, 2, 10);
	REQUIRE(policy.has_value());
	CHECK_EQ(policy->delay_seconds_for_attempt(1), 2);
	CHECK_EQ(policy->delay_seconds_for_attempt(2), 3);
	CHECK_EQ(policy->delay_seconds_for_attempt(3), 4);
	CHECK_EQ(policy->delay_seconds_for_attempt(5), 10);
	CHECK_EQ(policy->delay_seconds_for_attempt(5000), 10);
}

TEST_CASE(delay_treats_attempts_below_one_as_the_first)
{
	const ReconnectPolicy policy;
	CHECK_EQ(policy.delay_seconds_for_attempt(0), 2);
	CHECK_EQ(policy.delay_seconds_for_attempt(-3), 2);
}
