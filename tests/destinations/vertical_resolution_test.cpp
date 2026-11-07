#include "destinations/vertical_resolution.hpp"

#include "test_harness.hpp"

using namespace multistream;

TEST_CASE(the_presets_are_the_common_vertical_sizes_in_order)
{
	const auto &presets = vertical_resolution_presets();
	REQUIRE(presets.size() == 3);
	CHECK_EQ(presets[0].width, 1080);
	CHECK_EQ(presets[0].height, 1920);
	CHECK_EQ(presets[1].width, 720);
	CHECK_EQ(presets[1].height, 1280);
	CHECK_EQ(presets[2].width, 1440);
	CHECK_EQ(presets[2].height, 2560);
}

TEST_CASE(a_size_finds_its_preset_or_none_for_custom)
{
	CHECK(preset_index_for(1080, 1920) == std::optional<std::size_t>(0));
	CHECK(preset_index_for(1440, 2560) == std::optional<std::size_t>(2));
	CHECK(!preset_index_for(1920, 1080));
	CHECK(!preset_index_for(1000, 1920));
}

TEST_CASE(an_odd_side_is_the_problem_before_the_range)
{
	CHECK(check_vertical_resolution(1080, 1920) == ResolutionProblem::None);
	CHECK(check_vertical_resolution(1081, 1920) == ResolutionProblem::NotEven);
	CHECK(check_vertical_resolution(1080, 1921) == ResolutionProblem::NotEven);
	CHECK(check_vertical_resolution(0, 1920) == ResolutionProblem::OutOfRange);
	CHECK(check_vertical_resolution(8194, 1920) == ResolutionProblem::OutOfRange);
	CHECK(check_vertical_resolution(8193, 1920) == ResolutionProblem::NotEven);
}

TEST_CASE(the_check_agrees_with_the_settings_rule)
{
	for (int width : {-2, 0, 1, 2, 719, 720, 8192, 8193, 8194}) {
		for (int height : {1, 2, 1280, 8192, 9000}) {
			CHECK_EQ(check_vertical_resolution(width, height) == ResolutionProblem::None,
				 is_valid_vertical_resolution(width, height));
		}
	}
}

TEST_CASE(the_aspect_is_reduced_when_it_is_simple)
{
	CHECK_EQ(aspect_ratio_text(1080, 1920), std::string("9:16"));
	CHECK_EQ(aspect_ratio_text(720, 1280), std::string("9:16"));
	CHECK_EQ(aspect_ratio_text(1080, 1350), std::string("4:5"));
	CHECK_EQ(aspect_ratio_text(1080, 1080), std::string("1:1"));
}

TEST_CASE(an_awkward_aspect_is_shown_as_a_decimal_ratio)
{
	CHECK_EQ(aspect_ratio_text(1078, 1920), std::string("0.56:1"));
	CHECK_EQ(aspect_ratio_text(0, 1920), std::string());
	CHECK_EQ(aspect_ratio_text(1080, 0), std::string());
}
