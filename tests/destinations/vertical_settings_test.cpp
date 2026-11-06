#include "destinations/vertical_settings.hpp"

#include "test_harness.hpp"

using namespace multistream;

TEST_CASE(the_default_resolution_is_1080_by_1920_and_valid)
{
	const VerticalSettings settings;
	CHECK_EQ(settings.width, 1080);
	CHECK_EQ(settings.height, 1920);
	CHECK(is_valid_vertical_resolution(settings.width, settings.height));
}

TEST_CASE(a_resolution_must_be_even_positive_and_within_the_limit)
{
	CHECK(is_valid_vertical_resolution(2, 2));
	CHECK(is_valid_vertical_resolution(8192, 8192));
	CHECK(!is_valid_vertical_resolution(0, 1920));
	CHECK(!is_valid_vertical_resolution(1080, -2));
	CHECK(!is_valid_vertical_resolution(1081, 1920));
	CHECK(!is_valid_vertical_resolution(1080, 1921));
	CHECK(!is_valid_vertical_resolution(8194, 1920));
}

TEST_CASE(a_changed_resolution_is_refused_while_a_vertical_destination_is_live)
{
	const VerticalSettings current;
	CHECK(decide_resolution_change(current, 720, 1280, false) == ResolutionChange::Apply);
	CHECK(decide_resolution_change(current, 720, 1280, true) == ResolutionChange::RefusedWhileLive);
}

TEST_CASE(the_same_resolution_is_not_a_change_even_while_live)
{
	const VerticalSettings current;
	CHECK(decide_resolution_change(current, 1080, 1920, true) == ResolutionChange::Unchanged);
	CHECK(decide_resolution_change(current, 1080, 1920, false) == ResolutionChange::Unchanged);
}

TEST_CASE(an_invalid_resolution_is_invalid_before_anything_else)
{
	const VerticalSettings current;
	CHECK(decide_resolution_change(current, 721, 1280, true) == ResolutionChange::Invalid);
}

TEST_CASE(settings_round_trip)
{
	VerticalSettings settings;
	settings.width = 720;
	settings.height = 1280;
	settings.active_scene = "Game";

	const auto decoded = decode_vertical_settings(encode_vertical_settings(settings));
	REQUIRE(decoded.has_value());
	CHECK_EQ(decoded->width, 720);
	CHECK_EQ(decoded->height, 1280);
	CHECK_EQ(decoded->active_scene, std::string("Game"));
}

TEST_CASE(unusable_text_is_refused)
{
	CHECK(!decode_vertical_settings("not json").has_value());
	CHECK(!decode_vertical_settings("[1]").has_value());
}

TEST_CASE(invalid_fields_fall_back_to_defaults)
{
	const auto odd = decode_vertical_settings(R"({"width":1081,"height":1920,"active_scene":"S"})");
	REQUIRE(odd.has_value());
	CHECK_EQ(odd->width, 1080);
	CHECK_EQ(odd->height, 1920);
	CHECK_EQ(odd->active_scene, std::string("S"));

	const auto mistyped = decode_vertical_settings(R"({"width":"wide","active_scene":3})");
	REQUIRE(mistyped.has_value());
	CHECK_EQ(mistyped->width, 1080);
	CHECK(mistyped->active_scene.empty());
}

TEST_CASE(the_active_scene_is_the_wanted_one_when_it_still_exists)
{
	CHECK_EQ(*pick_active_scene({"A", "B"}, "B"), std::string("B"));
	CHECK_EQ(*pick_active_scene({"A", "B"}, "gone"), std::string("A"));
	CHECK(!pick_active_scene({}, "B").has_value());
}

TEST_CASE(the_last_scene_cannot_be_removed)
{
	CHECK(!can_remove_scene(0));
	CHECK(!can_remove_scene(1));
	CHECK(can_remove_scene(2));
}

TEST_CASE(scene_names_get_the_lowest_free_number)
{
	CHECK_EQ(unique_scene_name("Scene", {}), std::string("Scene"));
	CHECK_EQ(unique_scene_name("Scene", {"Scene"}), std::string("Scene 2"));
	CHECK_EQ(unique_scene_name("Scene", {"Scene", "Scene 2", "Scene 3"}), std::string("Scene 4"));
	CHECK_EQ(unique_scene_name("Scene", {"Other", "Scene 2"}), std::string("Scene"));
}

TEST_CASE(a_vertical_base_in_a_wide_window_is_pillarboxed)
{
	const Viewport viewport = fit_viewport(1080, 1920, 1000, 960);
	CHECK_EQ(viewport.width, 540);
	CHECK_EQ(viewport.height, 960);
	CHECK_EQ(viewport.x, 230);
	CHECK_EQ(viewport.y, 0);
}

TEST_CASE(a_vertical_base_in_a_tall_window_is_letterboxed)
{
	const Viewport viewport = fit_viewport(1080, 1920, 540, 1000);
	CHECK_EQ(viewport.width, 540);
	CHECK_EQ(viewport.height, 960);
	CHECK_EQ(viewport.x, 0);
	CHECK_EQ(viewport.y, 20);
}

TEST_CASE(a_degenerate_window_or_base_gives_an_empty_viewport)
{
	CHECK_EQ(fit_viewport(1080, 1920, 0, 100).width, 0);
	CHECK_EQ(fit_viewport(0, 0, 100, 100).width, 0);
}
