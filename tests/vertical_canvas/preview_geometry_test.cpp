#include "vertical_canvas/preview_geometry.hpp"

#include "test_harness.hpp"

#include <cmath>

using namespace multistream;

namespace {

constexpr CanvasSize kCanvas{1080.0, 1920.0};
constexpr double kTolerance = 0.001;

ItemBox box_at(double x, double y, double width, double height, double rotation = 0.0)
{
	ItemBox box;
	box.origin = {x, y};
	box.width = width;
	box.height = height;
	box.rotation_degrees = rotation;
	return box;
}

PreviewItem item(std::int64_t id, ItemBox box, bool visible = true, bool locked = false)
{
	PreviewItem result;
	result.id = id;
	result.box = box;
	result.visible = visible;
	result.locked = locked;
	return result;
}

#define CHECK_POINT(actual, expected_x, expected_y)        \
	do {                                               \
		const Vec2 check_point = (actual);         \
		CHECK_NEAR(check_point.x, (expected_x), kTolerance); \
		CHECK_NEAR(check_point.y, (expected_y), kTolerance); \
	} while (false)

PreviewInteraction::Tolerances tolerances()
{
	PreviewInteraction::Tolerances result;
	result.handle_radius = 10.0;
	result.snap_distance = 20.0;
	result.drag_threshold = 4.0;
	return result;
}

} // namespace

TEST_CASE(mapping_goes_through_the_letterboxed_viewport)
{
	// A 9:16 canvas in a 400x200 window: viewport 112x200 at x 144.
	const PreviewMapping mapping(1080, 1920, 400, 200);
	REQUIRE(mapping.valid());
	CHECK_POINT(mapping.to_canvas({144.0, 0.0}), 0.0, 0.0);
	CHECK_POINT(mapping.to_canvas({256.0, 200.0}), 1080.0, 1920.0);
	CHECK_POINT(mapping.to_window({1080.0, 1920.0}), 256.0, 200.0);
	CHECK_NEAR(mapping.canvas_per_pixel(), 9.6, kTolerance);
}

TEST_CASE(mapping_does_not_clamp_points_in_the_margin)
{
	const PreviewMapping mapping(1080, 1920, 400, 200);
	CHECK(mapping.to_canvas({0.0, 0.0}).x < 0.0);
}

TEST_CASE(mapping_of_an_empty_window_is_invalid)
{
	CHECK(!PreviewMapping(1080, 1920, 0, 0).valid());
}

TEST_CASE(box_contains_follows_rotation)
{
	const ItemBox box = box_at(100.0, 100.0, 200.0, 100.0, 90.0);
	// Rotated 90 degrees clockwise about its origin: it now spreads down and to the left.
	CHECK(box_contains(box, {60.0, 200.0}));
	CHECK(!box_contains(box, {200.0, 150.0}));
}

TEST_CASE(box_contains_handles_a_flipped_box)
{
	const ItemBox box = box_at(300.0, 100.0, -200.0, 100.0);
	CHECK(box_contains(box, {200.0, 150.0}));
	CHECK(!box_contains(box, {350.0, 150.0}));
}

TEST_CASE(pick_item_returns_the_topmost)
{
	const std::vector<PreviewItem> items = {item(2, box_at(0, 0, 100, 100)), item(1, box_at(0, 0, 500, 500))};
	CHECK_EQ(pick_item(items, {50.0, 50.0}).value_or(0), 2);
	CHECK_EQ(pick_item(items, {200.0, 200.0}).value_or(0), 1);
	CHECK(!pick_item(items, {900.0, 900.0}));
}

TEST_CASE(pick_item_skips_hidden_and_locked)
{
	const std::vector<PreviewItem> items = {item(3, box_at(0, 0, 100, 100), false, false),
						item(2, box_at(0, 0, 100, 100), true, true),
						item(1, box_at(0, 0, 100, 100))};
	CHECK_EQ(pick_item(items, {50.0, 50.0}).value_or(0), 1);
}

TEST_CASE(handle_at_finds_corners_and_edges)
{
	const ItemBox box = box_at(100.0, 100.0, 200.0, 100.0);
	CHECK(handle_at(box, {100.0, 100.0}, 10.0) == Handle::TopLeft);
	CHECK(handle_at(box, {305.0, 205.0}, 10.0) == Handle::BottomRight);
	CHECK(handle_at(box, {200.0, 100.0}, 10.0) == Handle::Top);
	CHECK(handle_at(box, {300.0, 150.0}, 10.0) == Handle::Right);
	CHECK(handle_at(box, {200.0, 150.0}, 10.0) == Handle::None);
}

TEST_CASE(handle_at_scales_with_the_radius_it_is_given)
{
	const ItemBox box = box_at(100.0, 100.0, 200.0, 100.0);
	CHECK(handle_at(box, {120.0, 120.0}, 5.0) == Handle::None);
	CHECK(handle_at(box, {120.0, 120.0}, 40.0) == Handle::TopLeft);
}

TEST_CASE(handle_at_follows_rotation)
{
	const ItemBox box = box_at(100.0, 100.0, 200.0, 100.0, 90.0);
	// The local bottom-right corner lands at x 0, y 300.
	CHECK(handle_at(box, {0.0, 300.0}, 5.0) == Handle::BottomRight);
}

TEST_CASE(cursors_match_the_handle_direction)
{
	const ItemBox box = box_at(100.0, 100.0, 200.0, 200.0);
	CHECK(cursor_for_handle(box, Handle::Left) == PreviewCursor::SizeHorizontal);
	CHECK(cursor_for_handle(box, Handle::Top) == PreviewCursor::SizeVertical);
	CHECK(cursor_for_handle(box, Handle::TopLeft) == PreviewCursor::SizeDiagonalDown);
	CHECK(cursor_for_handle(box, Handle::BottomLeft) == PreviewCursor::SizeDiagonalUp);
}

TEST_CASE(cursors_follow_rotation_and_flip)
{
	const ItemBox rotated = box_at(100.0, 100.0, 200.0, 200.0, 90.0);
	CHECK(cursor_for_handle(rotated, Handle::Left) == PreviewCursor::SizeVertical);
	const ItemBox flipped = box_at(100.0, 100.0, -200.0, 200.0);
	CHECK(cursor_for_handle(flipped, Handle::TopLeft) == PreviewCursor::SizeDiagonalUp);
}

TEST_CASE(a_move_far_from_edges_is_the_rounded_delta)
{
	const BoxChange change = plan_move(box_at(100.0, 100.0, 200.0, 100.0), {50.4, 30.6}, kCanvas, {}, 10.0);
	CHECK_POINT(change.box.origin, 150.0, 131.0);
	CHECK(!change.guides.vertical_x);
	CHECK(!change.guides.horizontal_y);
}

TEST_CASE(a_move_snaps_to_the_near_edges)
{
	const BoxChange change = plan_move(box_at(100.0, 100.0, 200.0, 100.0), {-95.0, 1715.0}, kCanvas, {}, 10.0);
	CHECK_POINT(change.box.origin, 0.0, 1820.0);
	CHECK_NEAR(change.guides.vertical_x.value_or(-1.0), 0.0, kTolerance);
	CHECK_NEAR(change.guides.horizontal_y.value_or(-1.0), 1920.0, kTolerance);
}

TEST_CASE(a_move_snaps_to_the_right_edge_and_center)
{
	const BoxChange change = plan_move(box_at(0.0, 0.0, 200.0, 100.0), {873.0, 906.0}, kCanvas, {}, 10.0);
	CHECK_POINT(change.box.origin, 880.0, 910.0);
	CHECK_NEAR(change.guides.vertical_x.value_or(-1.0), 1080.0, kTolerance);
	CHECK_NEAR(change.guides.horizontal_y.value_or(-1.0), 960.0, kTolerance);
}

TEST_CASE(a_snap_distance_of_zero_turns_snapping_off)
{
	const BoxChange change = plan_move(box_at(100.0, 100.0, 200.0, 100.0), {-95.0, 0.0}, kCanvas, {}, 0.0);
	CHECK_POINT(change.box.origin, 5.0, 100.0);
}

TEST_CASE(a_rotated_move_snaps_its_bounding_rectangle)
{
	// Rotated 90 degrees, its bounding rectangle's left side is at origin.x - height.
	const BoxChange change = plan_move(box_at(300.0, 100.0, 200.0, 100.0, 90.0), {-195.0, 0.0}, kCanvas, {}, 10.0);
	CHECK_NEAR(change.box.origin.x, 100.0, kTolerance);
}

TEST_CASE(a_full_canvas_box_stays_put_at_the_edges)
{
	const BoxChange change = plan_move(box_at(0.0, 0.0, 1080.0, 1920.0), {6.0, -7.0}, kCanvas, {}, 10.0);
	CHECK_POINT(change.box.origin, 0.0, 0.0);
}

TEST_CASE(a_move_snaps_to_another_items_edge)
{
	const std::vector<ItemBox> others = {box_at(400.0, 500.0, 200.0, 200.0)};
	const BoxChange change = plan_move(box_at(100.0, 100.0, 200.0, 100.0), {95.0, 0.0}, kCanvas, others, 10.0);
	CHECK_POINT(change.box.origin, 200.0, 100.0);
	CHECK_NEAR(change.guides.vertical_x.value_or(-1.0), 400.0, kTolerance);
	CHECK(!change.guides.horizontal_y);
}

TEST_CASE(a_move_snaps_to_another_items_center)
{
	const std::vector<ItemBox> others = {box_at(700.0, 500.0, 200.0, 200.0)};
	const BoxChange change = plan_move(box_at(100.0, 100.0, 100.0, 100.0), {645.0, 0.0}, kCanvas, others, 10.0);
	CHECK_POINT(change.box.origin, 750.0, 100.0);
	CHECK_NEAR(change.guides.vertical_x.value_or(-1.0), 800.0, kTolerance);
	CHECK(!change.guides.horizontal_y);
}

TEST_CASE(a_stretch_snaps_the_moved_edge_to_another_item)
{
	const ItemBox start = box_at(100.0, 100.0, 200.0, 100.0);
	const std::vector<ItemBox> others = {box_at(400.0, 500.0, 200.0, 200.0)};
	StretchOptions options;
	options.free_aspect = true;
	options.snap_distance = 10.0;
	const BoxChange change = plan_stretch(start, Handle::Right, {395.0, 150.0}, kCanvas, others, options);
	CHECK_NEAR(change.box.width, 300.0, kTolerance);
	CHECK_NEAR(change.guides.vertical_x.value_or(-1.0), 400.0, kTolerance);
}

TEST_CASE(a_locked_corner_stretch_keeps_aspect_and_the_opposite_corner)
{
	const ItemBox start = box_at(100.0, 100.0, 200.0, 100.0);
	const BoxChange change = plan_stretch(start, Handle::BottomRight, {500.0, 200.0}, kCanvas, {}, {});
	CHECK_NEAR(change.box.width, 400.0, kTolerance);
	CHECK_NEAR(change.box.height, 200.0, kTolerance);
	CHECK_POINT(change.box.origin, 100.0, 100.0);
}

TEST_CASE(a_locked_top_left_stretch_keeps_the_bottom_right_corner)
{
	const ItemBox start = box_at(100.0, 100.0, 200.0, 100.0);
	const BoxChange change = plan_stretch(start, Handle::TopLeft, {200.0, 150.0}, kCanvas, {}, {});
	CHECK_NEAR(change.box.width, 100.0, kTolerance);
	CHECK_NEAR(change.box.height, 50.0, kTolerance);
	CHECK_POINT(box_point(change.box, 1.0, 1.0), 300.0, 200.0);
}

TEST_CASE(a_locked_edge_stretch_grows_the_other_side_around_its_center)
{
	const ItemBox start = box_at(100.0, 100.0, 200.0, 100.0);
	const BoxChange change = plan_stretch(start, Handle::Bottom, {200.0, 300.0}, kCanvas, {}, {});
	CHECK_NEAR(change.box.height, 200.0, kTolerance);
	CHECK_NEAR(change.box.width, 400.0, kTolerance);
	CHECK_POINT(change.box.origin, 0.0, 100.0);
}

TEST_CASE(a_free_stretch_moves_only_what_the_handle_moves)
{
	const ItemBox start = box_at(100.0, 100.0, 200.0, 100.0);
	StretchOptions options;
	options.free_aspect = true;
	const BoxChange corner = plan_stretch(start, Handle::BottomRight, {500.0, 150.0}, kCanvas, {}, options);
	CHECK_NEAR(corner.box.width, 400.0, kTolerance);
	CHECK_NEAR(corner.box.height, 50.0, kTolerance);
	const BoxChange edge = plan_stretch(start, Handle::Right, {400.0, 999.0}, kCanvas, {}, options);
	CHECK_NEAR(edge.box.width, 300.0, kTolerance);
	CHECK_NEAR(edge.box.height, 100.0, kTolerance);
}

TEST_CASE(a_stretch_at_the_start_point_does_not_jump)
{
	// A 9:16 fill box whose aspect differs from a 16:9 source must stay as it is.
	const ItemBox start = box_at(0.0, 0.0, 1080.0, 1920.0);
	const BoxChange change = plan_stretch(start, Handle::BottomRight, {1080.0, 1920.0}, kCanvas, {}, {});
	CHECK_NEAR(change.box.width, 1080.0, kTolerance);
	CHECK_NEAR(change.box.height, 1920.0, kTolerance);
	CHECK_POINT(change.box.origin, 0.0, 0.0);
}

TEST_CASE(a_stretch_snaps_the_moved_edge)
{
	const ItemBox start = box_at(100.0, 100.0, 200.0, 100.0);
	StretchOptions options;
	options.free_aspect = true;
	options.snap_distance = 10.0;
	const BoxChange change = plan_stretch(start, Handle::Right, {1074.0, 150.0}, kCanvas, {}, options);
	CHECK_NEAR(change.box.width, 980.0, kTolerance);
	CHECK_NEAR(change.guides.vertical_x.value_or(-1.0), 1080.0, kTolerance);
}

TEST_CASE(a_stretch_leaves_the_unmoved_edge_out_of_snapping)
{
	// The left edge sits 3 pixels from the canvas edge; the right edge moves far from any.
	const ItemBox start = box_at(3.0, 100.0, 200.0, 100.0);
	StretchOptions options;
	options.free_aspect = true;
	options.snap_distance = 10.0;
	const BoxChange change = plan_stretch(start, Handle::Right, {400.0, 150.0}, kCanvas, {}, options);
	CHECK_NEAR(change.box.width, 397.0, kTolerance);
	CHECK_NEAR(change.box.origin.x, 3.0, kTolerance);
	CHECK(!change.guides.vertical_x);
}

TEST_CASE(a_stretch_past_the_opposite_side_flips)
{
	const ItemBox start = box_at(100.0, 100.0, 200.0, 100.0);
	StretchOptions options;
	options.free_aspect = true;
	const BoxChange change = plan_stretch(start, Handle::Right, {50.0, 150.0}, kCanvas, {}, options);
	CHECK_NEAR(change.box.width, -50.0, kTolerance);
	CHECK_POINT(change.box.origin, 100.0, 100.0);
}

TEST_CASE(a_stretch_keeps_at_least_one_pixel)
{
	const ItemBox start = box_at(100.0, 100.0, 200.0, 100.0);
	StretchOptions options;
	options.free_aspect = true;
	const BoxChange change = plan_stretch(start, Handle::Right, {100.0, 150.0}, kCanvas, {}, options);
	CHECK_NEAR(change.box.width, 1.0, kTolerance);
}

TEST_CASE(a_rotated_stretch_keeps_the_opposite_corner_on_screen)
{
	const ItemBox start = box_at(300.0, 100.0, 200.0, 100.0, 90.0);
	const Vec2 opposite = box_point(start, 0.0, 0.0);
	StretchOptions options;
	options.free_aspect = true;
	const Vec2 cursor = box_point(start, 2.0, 2.0);
	const BoxChange change = plan_stretch(start, Handle::BottomRight, cursor, kCanvas, {}, options);
	CHECK_NEAR(change.box.width, 400.0, kTolerance);
	CHECK_NEAR(change.box.height, 200.0, kTolerance);
	CHECK_POINT(box_point(change.box, 0.0, 0.0), opposite.x, opposite.y);
	CHECK_NEAR(change.box.rotation_degrees, 90.0, kTolerance);
}

TEST_CASE(a_draw_transform_gives_the_visible_box)
{
	// A 1920x1080 source drawn at 0.5625 scale, centered vertically in a 1080x1920 canvas.
	const auto box = box_from_draw_transform({0.0, 656.25}, {0.5625, 0.0}, {0.0, 0.5625}, 0.0, 1920.0, 1080.0);
	REQUIRE(box.has_value());
	CHECK_POINT(box->origin, 0.0, 656.25);
	CHECK_NEAR(box->width, 1080.0, kTolerance);
	CHECK_NEAR(box->height, 607.5, kTolerance);
}

TEST_CASE(a_draw_transform_keeps_flips_and_rotation)
{
	// Rotated 90 degrees, x axis points down. A flip on x makes it point up.
	const auto box = box_from_draw_transform({100.0, 100.0}, {0.0, -2.0}, {-3.0, 0.0}, 90.0, 10.0, 10.0);
	REQUIRE(box.has_value());
	CHECK_NEAR(box->width, -20.0, kTolerance);
	CHECK_NEAR(box->height, 30.0, kTolerance);
	CHECK_NEAR(box->rotation_degrees, 90.0, kTolerance);
}

TEST_CASE(a_draw_transform_without_size_or_finite_values_gives_nothing)
{
	CHECK(!box_from_draw_transform({0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, 0.0, 0.0, 10.0));
	CHECK(!box_from_draw_transform({0.0, 0.0}, {std::nan(""), 0.0}, {0.0, 1.0}, 0.0, 10.0, 10.0));
}

TEST_CASE(a_placement_shows_the_box_and_needs_a_source_size)
{
	const auto placement = placement_for_box(box_at(10.0, 20.0, -100.0, 50.0), 200.0, 100.0);
	REQUIRE(placement.has_value());
	CHECK_POINT(placement->position, 10.0, 20.0);
	CHECK_POINT(placement->scale, -0.5, 0.5);
	CHECK(!placement_for_box(box_at(0.0, 0.0, 10.0, 10.0), 0.0, 100.0));
}

TEST_CASE(centering_uses_the_bounding_rectangle)
{
	CHECK_POINT(centered_box(box_at(0.0, 0.0, 200.0, 100.0), kCanvas).origin, 440.0, 910.0);
	const ItemBox rotated = centered_box(box_at(0.0, 0.0, 200.0, 100.0, 90.0), kCanvas);
	const Vec2 center = box_point(rotated, 0.5, 0.5);
	CHECK_POINT(center, 540.0, 960.0);
}

TEST_CASE(a_nudge_moves_by_one_or_ten)
{
	const ItemBox box = box_at(10.0, 10.0, 5.0, 5.0);
	CHECK_POINT(plan_nudge(box, NudgeDirection::Left, false).origin, 9.0, 10.0);
	CHECK_POINT(plan_nudge(box, NudgeDirection::Down, true).origin, 10.0, 20.0);
	CHECK_POINT(plan_nudge(box, NudgeDirection::Up, false).origin, 10.0, 9.0);
	CHECK_POINT(plan_nudge(box, NudgeDirection::Right, true).origin, 20.0, 10.0);
}

TEST_CASE(a_click_selects_the_topmost_and_an_empty_click_deselects)
{
	const std::vector<PreviewItem> items = {item(2, box_at(0, 0, 100, 100)), item(1, box_at(0, 0, 500, 500))};
	PreviewInteraction interaction;
	CHECK(interaction.press({50.0, 50.0}, items, tolerances()));
	CHECK_EQ(interaction.selection().value_or(0), 2);
	interaction.release({50.0, 50.0}, items);
	CHECK(interaction.press({900.0, 900.0}, items, tolerances()));
	CHECK(!interaction.selection());
	CHECK(!interaction.gesture_active());
}

TEST_CASE(a_drag_moves_the_selected_item_after_the_threshold)
{
	const std::vector<PreviewItem> items = {item(1, box_at(100, 100, 200, 100))};
	PreviewInteraction interaction;
	interaction.press({150.0, 150.0}, items, tolerances());
	CHECK(!interaction.move({152.0, 150.0}, kCanvas, {}, {}));
	const auto change = interaction.move({250.0, 170.0}, kCanvas, {}, {});
	REQUIRE(change.has_value());
	CHECK_POINT(change->box.origin, 200.0, 120.0);
	CHECK(interaction.dragging());
	// Below the threshold again, the drag goes on.
	const auto back = interaction.move({151.0, 150.0}, kCanvas, {}, {});
	REQUIRE(back.has_value());
	CHECK_POINT(back->box.origin, 101.0, 100.0);
}

TEST_CASE(a_modifier_disables_snapping_during_a_drag)
{
	const std::vector<PreviewItem> items = {item(1, box_at(100, 100, 200, 100))};
	PreviewInteraction interaction;
	interaction.press({150.0, 150.0}, items, tolerances());
	PreviewInteraction::Modifiers modifiers;
	const auto snapped = interaction.move({55.0, 150.0}, kCanvas, {}, modifiers);
	REQUIRE(snapped.has_value());
	CHECK_NEAR(snapped->box.origin.x, 0.0, kTolerance);
	modifiers.disable_snap = true;
	const auto free = interaction.move({55.0, 150.0}, kCanvas, {}, modifiers);
	REQUIRE(free.has_value());
	CHECK_NEAR(free->box.origin.x, 5.0, kTolerance);
}

TEST_CASE(a_handle_of_the_selected_item_starts_a_stretch)
{
	const std::vector<PreviewItem> items = {item(1, box_at(100, 100, 200, 100))};
	PreviewInteraction interaction;
	interaction.select(1);
	CHECK(!interaction.press({300.0, 200.0}, items, tolerances()));
	const auto change = interaction.move({500.0, 300.0}, kCanvas, {}, {});
	REQUIRE(change.has_value());
	CHECK_NEAR(change->box.width, 400.0, kTolerance);
	CHECK_NEAR(change->box.height, 200.0, kTolerance);
}

TEST_CASE(shift_frees_the_aspect)
{
	const std::vector<PreviewItem> items = {item(1, box_at(100, 100, 200, 100))};
	PreviewInteraction interaction;
	interaction.select(1);
	interaction.press({300.0, 200.0}, items, tolerances());
	PreviewInteraction::Modifiers modifiers;
	modifiers.shift = true;
	const auto change = interaction.move({500.0, 250.0}, kCanvas, {}, modifiers);
	REQUIRE(change.has_value());
	CHECK_NEAR(change->box.width, 400.0, kTolerance);
	CHECK_NEAR(change->box.height, 150.0, kTolerance);
}

TEST_CASE(handles_of_an_unselected_item_do_nothing)
{
	const std::vector<PreviewItem> items = {item(1, box_at(100, 100, 200, 100))};
	PreviewInteraction interaction;
	CHECK(interaction.press({299.0, 199.0}, items, tolerances()));
	const auto change = interaction.move({350.0, 250.0}, kCanvas, {}, {});
	REQUIRE(change.has_value());
	CHECK_NEAR(change->box.width, 200.0, kTolerance);
}

TEST_CASE(a_locked_selected_item_cannot_move_or_stretch)
{
	const std::vector<PreviewItem> items = {item(1, box_at(100, 100, 200, 100), true, true)};
	PreviewInteraction interaction;
	interaction.select(1);
	interaction.press({300.0, 200.0}, items, tolerances());
	CHECK(!interaction.move({500.0, 300.0}, kCanvas, {}, {}));
	CHECK(handle_at(items[0].box, {300.0, 200.0}, 10.0) == Handle::BottomRight);
}

TEST_CASE(a_hidden_selected_item_loses_its_handles)
{
	const std::vector<PreviewItem> items = {item(1, box_at(100, 100, 200, 100), false, false)};
	PreviewInteraction interaction;
	interaction.select(1);
	interaction.press({300.0, 200.0}, items, tolerances());
	CHECK(!interaction.selection());
	CHECK(!interaction.move({500.0, 300.0}, kCanvas, {}, {}));
}

TEST_CASE(a_click_without_drag_on_a_selected_item_below_selects_the_topmost)
{
	const std::vector<PreviewItem> items = {item(2, box_at(0, 0, 100, 100)), item(1, box_at(0, 0, 500, 500))};
	PreviewInteraction interaction;
	interaction.select(1);
	CHECK(!interaction.press({50.0, 50.0}, items, tolerances()));
	CHECK_EQ(interaction.selection().value_or(0), 1);
	CHECK(interaction.release({50.0, 50.0}, items));
	CHECK_EQ(interaction.selection().value_or(0), 2);
}

TEST_CASE(a_drag_of_a_selected_item_below_keeps_it_selected)
{
	const std::vector<PreviewItem> items = {item(2, box_at(0, 0, 100, 100)), item(1, box_at(0, 0, 500, 500))};
	PreviewInteraction interaction;
	interaction.select(1);
	interaction.press({50.0, 50.0}, items, tolerances());
	CHECK(interaction.move({90.0, 90.0}, kCanvas, {}, {}).has_value());
	CHECK(!interaction.release({90.0, 90.0}, items));
	CHECK_EQ(interaction.selection().value_or(0), 1);
}

TEST_CASE(a_cancelled_gesture_stops_changing_the_box)
{
	const std::vector<PreviewItem> items = {item(1, box_at(100, 100, 200, 100))};
	PreviewInteraction interaction;
	interaction.press({150.0, 150.0}, items, tolerances());
	interaction.cancel();
	CHECK(!interaction.gesture_active());
	CHECK(!interaction.move({300.0, 300.0}, kCanvas, {}, {}));
}

TEST_CASE(a_selection_change_ends_the_gesture)
{
	const std::vector<PreviewItem> items = {item(1, box_at(100, 100, 200, 100))};
	PreviewInteraction interaction;
	interaction.press({150.0, 150.0}, items, tolerances());
	interaction.select(std::nullopt);
	CHECK(!interaction.move({300.0, 300.0}, kCanvas, {}, {}));
}

TEST_CASE(the_cursor_follows_what_is_under_it)
{
	const std::vector<PreviewItem> items = {item(1, box_at(100, 100, 200, 100)),
						item(2, box_at(600, 600, 100, 100), true, true)};
	PreviewInteraction interaction;
	CHECK(interaction.cursor_at({150.0, 150.0}, items, 10.0) == PreviewCursor::Arrow);
	interaction.select(1);
	CHECK(interaction.cursor_at({150.0, 150.0}, items, 10.0) == PreviewCursor::Move);
	CHECK(interaction.cursor_at({300.0, 150.0}, items, 10.0) == PreviewCursor::SizeHorizontal);
	CHECK(interaction.cursor_at({900.0, 900.0}, items, 10.0) == PreviewCursor::Arrow);
	interaction.select(2);
	CHECK(interaction.cursor_at({650.0, 650.0}, items, 10.0) == PreviewCursor::Arrow);
	CHECK(interaction.cursor_at({600.0, 600.0}, items, 10.0) == PreviewCursor::Arrow);
}
