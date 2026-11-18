#include "vertical_canvas/auto_layout.hpp"

#include "test_harness.hpp"

using namespace multistream;

namespace {

constexpr CanvasSize kMain{1920.0, 1080.0};
constexpr CanvasSize kVertical{1080.0, 1920.0};
constexpr double kTolerance = 0.01;

MainItemFacts plain_item(Rect box, Vec2 source_size)
{
	MainItemFacts item;
	item.box = box;
	item.source_size = source_size;
	item.scale = {box.width / source_size.x, box.height / source_size.y};
	return item;
}

bool inside(const Rect &inner, const Rect &outer)
{
	return inner.x >= outer.x - kTolerance && inner.y >= outer.y - kTolerance &&
	       inner.right() <= outer.right() + kTolerance && inner.bottom() <= outer.bottom() + kTolerance;
}

} // namespace

TEST_CASE(layout_ids_round_trip)
{
	for (const TwinLayout layout : kAllTwinLayouts) {
		CHECK(twin_layout_from_id(twin_layout_id(layout)) == layout);
	}
	CHECK(!twin_layout_from_id("something_else"));
}

TEST_CASE(cameras_are_recognized_by_type_or_name)
{
	CHECK(facts_for_source("macos-avcapture", "Logitech", false).camera);
	CHECK(facts_for_source("ndi_source", "Facecam NDI", false).camera);
	CHECK(facts_for_source("image_source", "Cámara frontal", false).camera);
	CHECK(!facts_for_source("image_source", "Logo", false).camera);
	CHECK(facts_for_source("screen_capture", "Pantalla", false).capture);
	CHECK(facts_for_source("scene", "Juego", true).capture);
	CHECK(!facts_for_source("browser_source", "Alertas", false).capture);
}

TEST_CASE(classification_follows_type_and_coverage)
{
	const SourceFacts camera{true, false};
	const SourceFacts capture{false, true};
	const SourceFacts other{};
	CHECK_EQ(classify_linked_item(camera, {1500, 700, 400, 300}, kMain), LinkedRole::Camera);
	CHECK_EQ(classify_linked_item(capture, {0, 0, 1920, 1080}, kMain), LinkedRole::Content);
	CHECK_EQ(classify_linked_item(capture, {0, 0, 400, 300}, kMain), LinkedRole::Overlay);
	CHECK_EQ(classify_linked_item(other, {-10, -10, 1940, 1100}, kMain), LinkedRole::Background);
	CHECK_EQ(classify_linked_item(other, {0, 0, 1400, 900}, kMain), LinkedRole::Content);
	CHECK_EQ(classify_linked_item(other, {40, 40, 300, 120}, kMain), LinkedRole::Overlay);
}

TEST_CASE(regions_split_the_canvas_by_layout)
{
	const TwinRegions top = twin_regions(TwinLayout::CameraTop, 0.5, true, kMain, kVertical);
	CHECK_NEAR(top.camera.height, 768.0, kTolerance);
	CHECK_NEAR(top.content.y, 768.0, kTolerance);
	CHECK_NEAR(top.content.height, 1152.0, kTolerance);

	const TwinRegions no_camera = twin_regions(TwinLayout::CameraTop, 0.5, false, kMain, kVertical);
	CHECK(no_camera.camera.empty());
	CHECK_NEAR(no_camera.content.height, 1920.0, kTolerance);

	const TwinRegions full = twin_regions(TwinLayout::FullScreen, 0.5, true, kMain, kVertical);
	CHECK(full.camera.empty());
}

TEST_CASE(focus_window_keeps_the_content_aspect_and_slides)
{
	const TwinRegions left = twin_regions(TwinLayout::FullScreen, 0.0, false, kMain, kVertical);
	const TwinRegions right = twin_regions(TwinLayout::FullScreen, 1.0, false, kMain, kVertical);
	CHECK_NEAR(left.focus_window.height, 1080.0, kTolerance);
	CHECK_NEAR(left.focus_window.width, 1080.0 * 1080.0 / 1920.0, kTolerance);
	CHECK_NEAR(left.focus_window.x, 0.0, kTolerance);
	CHECK_NEAR(right.focus_window.right(), 1920.0, kTolerance);

	// A wide content region (camera band) needs a wide window.
	const TwinRegions banded = twin_regions(TwinLayout::CameraBig, 0.5, true, kMain, kVertical);
	CHECK_NEAR(banded.focus_window.width / banded.focus_window.height, banded.content.width / banded.content.height,
		   kTolerance);
	CHECK(inside(banded.focus_window, {0, 0, 1920, 1080}));
}

TEST_CASE(full_screen_capture_is_cropped_to_the_window)
{
	const TwinRegions regions = twin_regions(TwinLayout::FullScreen, 0.5, false, kMain, kVertical);
	const MainItemFacts screen = plain_item({0, 0, 1920, 1080}, {3840, 2160});
	const LinkedPlacement placed = place_linked_item(LinkedRole::Content, screen, regions, kMain, kVertical);

	CHECK_EQ(placed.kind, PlacementKind::Plain);
	CHECK_NEAR(placed.position.x, 0.0, kTolerance);
	CHECK_NEAR(placed.position.y, 0.0, kTolerance);
	// The window is 607.5 wide out of 1920, centered: 656.25 cropped on each side, in
	// main pixels, which are 2 source pixels each.
	CHECK_EQ(placed.crop.left, 1313);
	CHECK_EQ(placed.crop.right, 1313);
	CHECK_EQ(placed.crop.top, 0);
	const double shown_width = (3840 - placed.crop.left - placed.crop.right) * placed.scale.x;
	CHECK_NEAR(shown_width, 1080.0, 1.5);
	CHECK_NEAR((2160 - placed.crop.top - placed.crop.bottom) * placed.scale.y, 1920.0, 1.5);
}

TEST_CASE(content_outside_the_window_is_hidden)
{
	const TwinRegions regions = twin_regions(TwinLayout::FullScreen, 0.0, false, kMain, kVertical);
	const MainItemFacts far_right = plain_item({1500, 0, 420, 1080}, {420, 1080});
	CHECK_EQ(place_linked_item(LinkedRole::Content, far_right, regions, kMain, kVertical).kind,
		 PlacementKind::Hidden);
}

TEST_CASE(content_without_size_or_rotated_fills_its_region)
{
	const TwinRegions regions = twin_regions(TwinLayout::CameraTop, 0.5, true, kMain, kVertical);
	MainItemFacts not_ready = plain_item({0, 0, 1920, 1080}, {1920, 1080});
	not_ready.source_size = {};
	const LinkedPlacement a = place_linked_item(LinkedRole::Content, not_ready, regions, kMain, kVertical);
	CHECK_EQ(a.kind, PlacementKind::Cover);
	CHECK_NEAR(a.cover.y, regions.content.y, kTolerance);

	MainItemFacts rotated = plain_item({0, 0, 1920, 1080}, {1920, 1080});
	rotated.rotation_degrees = 90.0;
	CHECK_EQ(place_linked_item(LinkedRole::Content, rotated, regions, kMain, kVertical).kind, PlacementKind::Cover);
}

TEST_CASE(camera_fills_the_band_or_becomes_an_overlay)
{
	const MainItemFacts cam = plain_item({1500, 700, 400, 300}, {1920, 1080});
	const TwinRegions banded = twin_regions(TwinLayout::CameraTop, 0.5, true, kMain, kVertical);
	const LinkedPlacement in_band = place_linked_item(LinkedRole::Camera, cam, banded, kMain, kVertical);
	CHECK_EQ(in_band.kind, PlacementKind::Cover);
	CHECK_NEAR(in_band.cover.height, banded.camera.height, kTolerance);

	const TwinRegions full = twin_regions(TwinLayout::FullScreen, 0.5, true, kMain, kVertical);
	const LinkedPlacement overlay = place_linked_item(LinkedRole::Camera, cam, full, kMain, kVertical);
	CHECK_EQ(overlay.kind, PlacementKind::Plain);
}

TEST_CASE(overlays_keep_their_corner_inside_the_safe_area)
{
	const TwinRegions regions = twin_regions(TwinLayout::CameraTop, 0.5, true, kMain, kVertical);
	struct Case {
		Rect box;
		bool left, top;
	} cases[] = {
		{{20, 20, 300, 100}, true, true},
		{{1600, 20, 300, 100}, false, true},
		{{20, 960, 300, 100}, true, false},
		{{1600, 960, 300, 100}, false, false},
	};
	for (const Case &c : cases) {
		const MainItemFacts item = plain_item(c.box, {c.box.width, c.box.height});
		const LinkedPlacement placed = place_linked_item(LinkedRole::Overlay, item, regions, kMain, kVertical);
		CHECK_EQ(placed.kind, PlacementKind::Plain);
		const Rect shown{placed.position.x, placed.position.y, c.box.width * placed.scale.x,
				 c.box.height * placed.scale.y};
		CHECK(inside(shown, regions.safe));
		CHECK((shown.x + shown.width / 2.0 < 540.0) == c.left);
		CHECK((shown.y + shown.height / 2.0 < 960.0) == c.top);
		// Larger than a plain 1080/1920 downscale.
		CHECK(placed.scale.x > 1080.0 / 1920.0);
	}
}

TEST_CASE(wide_overlays_shrink_to_fit_and_bounds_scale)
{
	const TwinRegions regions = twin_regions(TwinLayout::FullScreen, 0.5, false, kMain, kVertical);
	MainItemFacts banner = plain_item({0, 900, 1920, 180}, {1920, 180});
	banner.has_bounds = true;
	banner.bounds = {1920, 180};
	const LinkedPlacement placed = place_linked_item(LinkedRole::Overlay, banner, regions, kMain, kVertical);
	CHECK_EQ(placed.kind, PlacementKind::ScaledBounds);
	CHECK(placed.bounds.x <= regions.safe.width * 0.9 + kTolerance);
	CHECK_NEAR(placed.bounds.x / placed.bounds.y, 1920.0 / 180.0, kTolerance);
}

TEST_CASE(safe_zones_scale_with_the_canvas)
{
	const std::vector<Rect> zones = app_safe_zones({720.0, 1280.0});
	CHECK_EQ(zones.size(), std::size_t{3});
	for (const Rect &zone : zones) {
		CHECK(inside(zone, {0, 0, 720, 1280}));
	}
	CHECK_NEAR(zones[1].bottom(), 1280.0, kTolerance);
}
