#include "destinations/reconnect_policy.hpp"

#include "test_harness.hpp"

using multistream::ReconnectPolicy;

TEST_CASE(defaults_match_libobs)
{
	const ReconnectPolicy policy;
	CHECK_EQ(policy.max_retries(), 20);
	CHECK_EQ(policy.base_delay_seconds(), 2);
	CHECK(policy.enabled());
}

TEST_CASE(zero_retries_disables_reconnect)
{
	const auto policy = ReconnectPolicy::create(0, 2);
	REQUIRE(policy.has_value());
	CHECK(!policy->enabled());
}

TEST_CASE(accepts_the_limits)
{
	CHECK(ReconnectPolicy::create(10000, 1).has_value());
	CHECK(ReconnectPolicy::create(1, 900).has_value());
}

TEST_CASE(rejects_out_of_range_values)
{
	CHECK(!ReconnectPolicy::create(-1, 2).has_value());
	CHECK(!ReconnectPolicy::create(10001, 2).has_value());
	CHECK(!ReconnectPolicy::create(5, 0).has_value());
	CHECK(!ReconnectPolicy::create(5, 901).has_value());
}
