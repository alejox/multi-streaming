#include "destinations/runtime_requirements.hpp"

#include "test_harness.hpp"

using multistream::is_runtime_supported;
using multistream::make_obs_version;

TEST_CASE(rejects_versions_below_obs_33)
{
	CHECK(!is_runtime_supported(make_obs_version(32, 1, 2)));
	CHECK(!is_runtime_supported(make_obs_version(28, 0, 0)));
	CHECK(!is_runtime_supported(0));
}

TEST_CASE(accepts_obs_33_and_later)
{
	CHECK(is_runtime_supported(make_obs_version(33, 0, 0)));
	CHECK(is_runtime_supported(make_obs_version(33, 0, 1)));
	CHECK(is_runtime_supported(make_obs_version(34, 2, 0)));
}

TEST_CASE(decodes_libobs_semantic_version)
{
	// libobs packs the version as major << 24 | minor << 16 | patch.
	CHECK_EQ(make_obs_version(33, 0, 1), (33u << 24) | 1u);
}

TEST_CASE(the_canvas_api_is_only_trusted_on_the_major_version_it_was_tested_on)
{
	using multistream::is_canvas_api_tested;
	using multistream::make_obs_version;
	CHECK(is_canvas_api_tested(make_obs_version(33, 0, 1)));
	CHECK(is_canvas_api_tested(make_obs_version(33, 1, 0)));
	CHECK(!is_canvas_api_tested(make_obs_version(32, 9, 9)));
	CHECK(!is_canvas_api_tested(make_obs_version(34, 0, 0)));
}
