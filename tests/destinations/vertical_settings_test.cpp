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

TEST_CASE(scene_order_round_trips)
{
	multistream::VerticalSettings settings;
	settings.scene_order = {"B", "A"};
	const auto decoded = multistream::decode_vertical_settings(multistream::encode_vertical_settings(settings));
	REQUIRE(decoded.has_value());
	REQUIRE(decoded->scene_order.size() == 2);
	CHECK_EQ(decoded->scene_order[0], "B");
}

TEST_CASE(settings_without_a_scene_order_still_load)
{
	const auto decoded = multistream::decode_vertical_settings(R"({"width":720,"height":1280})");
	REQUIRE(decoded.has_value());
	CHECK(decoded->scene_order.empty());
}

TEST_CASE(a_scene_order_of_the_wrong_type_is_ignored)
{
	const auto decoded = multistream::decode_vertical_settings(R"({"width":720,"height":1280,"scene_order":3})");
	REQUIRE(decoded.has_value());
	CHECK(decoded->scene_order.empty());
}

TEST_CASE(ordering_scenes_keeps_known_names_in_order_and_new_ones_after)
{
	const auto ordered = multistream::order_scenes({"A", "B", "C", "D"}, {"C", "gone", "A"});
	REQUIRE(ordered.size() == 4);
	CHECK_EQ(ordered[0], "C");
	CHECK_EQ(ordered[1], "A");
	CHECK_EQ(ordered[2], "B");
	CHECK_EQ(ordered[3], "D");
}

TEST_CASE(moving_a_scene_swaps_it_with_its_neighbor)
{
	const std::vector<std::string> scenes = {"A", "B", "C"};
	const auto up = multistream::move_scene(scenes, "C", true);
	CHECK_EQ(up[1], "C");
	CHECK_EQ(up[2], "B");
	const auto down = multistream::move_scene(scenes, "A", false);
	CHECK_EQ(down[0], "B");
	CHECK_EQ(down[1], "A");
}

TEST_CASE(moving_a_scene_past_the_ends_or_unknown_changes_nothing)
{
	const std::vector<std::string> scenes = {"A", "B"};
	CHECK(multistream::move_scene(scenes, "A", true) == scenes);
	CHECK(multistream::move_scene(scenes, "B", false) == scenes);
	CHECK(multistream::move_scene(scenes, "Z", true) == scenes);
}

TEST_CASE(legacy_scene_fields_migrate_into_the_first_collection_loaded)
{
	const auto legacy = decode_vertical_settings(
		R"({"width":720,"height":1280,"active_scene":"Game","scene_order":["Game","Chat"]})");
	REQUIRE(legacy.has_value());

	const VerticalSettings selected = select_collection(*legacy, "Main");
	CHECK_EQ(selected.width, 720);
	CHECK_EQ(selected.active_scene, std::string("Game"));
	CHECK(selected.scene_order == (std::vector<std::string>{"Game", "Chat"}));
	REQUIRE(selected.collections.count("Main") == 1);
	CHECK_EQ(selected.collections.at("Main").active_scene, std::string("Game"));
}

TEST_CASE(a_collection_never_seen_starts_without_scene_state_once_others_are_known)
{
	VerticalSettings stored = select_collection(VerticalSettings{}, "Main");
	stored.active_scene = "Game";
	stored.scene_order = {"Game", "Chat"};
	remember_collection(stored, "Main");

	const VerticalSettings other = select_collection(stored, "Other");
	CHECK(other.active_scene.empty());
	CHECK(other.scene_order.empty());
	CHECK_EQ(other.collections.at("Main").active_scene, std::string("Game"));
}

TEST_CASE(each_collection_keeps_its_own_scene_state_and_the_resolution_is_shared)
{
	VerticalSettings settings = select_collection(VerticalSettings{}, "Main");
	settings.active_scene = "Game";
	settings.scene_order = {"Game", "Chat"};
	remember_collection(settings, "Main");

	settings = select_collection(settings, "Other");
	settings.active_scene = "Talk";
	settings.width = 720;
	settings.height = 1280;
	remember_collection(settings, "Other");

	const auto decoded = decode_vertical_settings(encode_vertical_settings(settings));
	REQUIRE(decoded.has_value());

	const VerticalSettings main = select_collection(*decoded, "Main");
	CHECK_EQ(main.active_scene, std::string("Game"));
	CHECK(main.scene_order == (std::vector<std::string>{"Game", "Chat"}));
	CHECK_EQ(main.width, 720);

	const VerticalSettings other = select_collection(*decoded, "Other");
	CHECK_EQ(other.active_scene, std::string("Talk"));
	CHECK(other.scene_order.empty());
}

TEST_CASE(an_unnamed_collection_is_not_remembered)
{
	VerticalSettings settings;
	settings.active_scene = "Game";
	remember_collection(settings, "");
	CHECK(settings.collections.empty());
}

TEST_CASE(malformed_collection_entries_are_skipped)
{
	const auto decoded = decode_vertical_settings(
		R"({"collections":{"Main":{"active_scene":"Game","scene_order":["Game",4]},"Bad":3}})");
	REQUIRE(decoded.has_value());
	CHECK(decoded->collections.count("Bad") == 0);
	REQUIRE(decoded->collections.count("Main") == 1);
	CHECK(decoded->collections.at("Main").scene_order == (std::vector<std::string>{"Game"}));
}
