#include "vertical_canvas/auto_layout.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace multistream {
namespace {

// Measured on a 1080x1920 canvas, scaled to whatever the canvas is.
constexpr double kRefWidth = 1080.0;
constexpr double kRefHeight = 1920.0;
constexpr double kButtonColumn = 150.0; // right edge, likes/comments/share
constexpr double kButtonColumnTop = 700.0;
constexpr double kCaption = 380.0; // bottom, caption and music line
constexpr double kTopBar = 120.0;  // top, LIVE badge and viewer count
constexpr double kSideMargin = 48.0;

// Overlays are drawn this much larger than a plain downscale so they stay readable.
constexpr double kOverlayBoost = 1.3;
constexpr double kOverlayMaxWidth = 0.9; // of the safe area
// How much of the original gap to an edge an anchored overlay keeps.
constexpr double kEdgeGapShare = 0.5;

std::string lower(std::string_view text)
{
	std::string result(text);
	std::transform(result.begin(), result.end(), result.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

bool contains(const std::string &haystack, std::string_view needle)
{
	return haystack.find(needle) != std::string::npos;
}

double clamp_between(double value, double low, double high)
{
	return high < low ? low : std::clamp(value, low, high);
}

} // namespace

Rect intersect(const Rect &a, const Rect &b)
{
	const double x = std::max(a.x, b.x);
	const double y = std::max(a.y, b.y);
	const double right = std::min(a.right(), b.right());
	const double bottom = std::min(a.bottom(), b.bottom());
	if (right <= x || bottom <= y) {
		return {};
	}
	return {x, y, right - x, bottom - y};
}

std::string_view twin_layout_id(TwinLayout layout)
{
	switch (layout) {
	case TwinLayout::FullScreen:
		return "full_screen";
	case TwinLayout::CameraBig:
		return "camera_big";
	case TwinLayout::CameraTop:
		break;
	}
	return "camera_top";
}

std::optional<TwinLayout> twin_layout_from_id(std::string_view id)
{
	for (const TwinLayout layout : kAllTwinLayouts) {
		if (twin_layout_id(layout) == id) {
			return layout;
		}
	}
	return std::nullopt;
}

double camera_band_fraction(TwinLayout layout)
{
	switch (layout) {
	case TwinLayout::CameraTop:
		return 0.40;
	case TwinLayout::CameraBig:
		return 0.55;
	case TwinLayout::FullScreen:
		break;
	}
	return 0.0;
}

std::string_view linked_role_id(LinkedRole role)
{
	switch (role) {
	case LinkedRole::Camera:
		return "camera";
	case LinkedRole::Content:
		return "content";
	case LinkedRole::Background:
		return "background";
	case LinkedRole::Overlay:
		break;
	}
	return "overlay";
}

SourceFacts facts_for_source(std::string_view unversioned_type_id, std::string_view name, bool is_scene)
{
	static constexpr std::string_view kCameraTypes[] = {"av_capture_input", "macos-avcapture",
							    "macos-avcapture-fast", "dshow_input", "v4l2_input"};
	static constexpr std::string_view kCaptureTypes[] = {"screen_capture",
							     "display_capture",
							     "window_capture",
							     "game_capture",
							     "monitor_capture",
							     "xshm_input",
							     "xcomposite_input",
							     "syphon-input",
							     "ndi_source",
							     "decklink-input",
							     "pipewire-screen-capture-source",
							     "pipewire-desktop-capture-source",
							     "pipewire-window-capture-source"};

	SourceFacts facts;
	for (const std::string_view type : kCameraTypes) {
		facts.camera = facts.camera || unversioned_type_id == type;
	}
	const std::string lowered = lower(name);
	for (const std::string_view word : {"cam", "cámara", "camara", "webcam", "facecam"}) {
		facts.camera = facts.camera || contains(lowered, word);
	}
	facts.capture = is_scene;
	for (const std::string_view type : kCaptureTypes) {
		facts.capture = facts.capture || unversioned_type_id == type;
	}
	return facts;
}

LinkedRole classify_linked_item(const SourceFacts &facts, const Rect &box_in_main, CanvasSize main)
{
	if (facts.camera) {
		return LinkedRole::Camera;
	}
	const Rect visible = intersect(box_in_main, {0.0, 0.0, main.width, main.height});
	const double area = main.width * main.height;
	const double coverage = area > 0.0 ? visible.width * visible.height / area : 0.0;
	if (facts.capture) {
		return coverage >= 0.25 ? LinkedRole::Content : LinkedRole::Overlay;
	}
	if (coverage >= 0.9) {
		return LinkedRole::Background;
	}
	if (coverage >= 0.5) {
		return LinkedRole::Content;
	}
	return LinkedRole::Overlay;
}

TwinRegions twin_regions(TwinLayout layout, double focus, bool has_camera, CanvasSize main, CanvasSize vertical)
{
	TwinRegions regions;
	const double band = has_camera ? std::round(vertical.height * camera_band_fraction(layout)) : 0.0;
	if (band > 0.0) {
		regions.camera = {0.0, 0.0, vertical.width, band};
	}
	regions.content = {0.0, band, vertical.width, vertical.height - band};

	const double sx = vertical.width / kRefWidth;
	const double sy = vertical.height / kRefHeight;
	regions.safe = {kSideMargin * sx, kTopBar * sy, vertical.width - (kSideMargin + kButtonColumn) * sx,
			vertical.height - (kTopBar + kCaption) * sy};

	// As tall as the main canvas allows, at the content region's aspect.
	const double aspect = regions.content.height > 0.0 ? regions.content.width / regions.content.height : 1.0;
	double width = main.height * aspect;
	double height = main.height;
	if (width > main.width) {
		width = main.width;
		height = main.width / aspect;
	}
	const double clamped = std::clamp(focus, 0.0, 1.0);
	regions.focus_window = {clamped * (main.width - width), (main.height - height) / 2.0, width, height};
	return regions;
}

std::vector<Rect> app_safe_zones(CanvasSize vertical)
{
	const double sx = vertical.width / kRefWidth;
	const double sy = vertical.height / kRefHeight;
	const double caption_top = vertical.height - kCaption * sy;
	return {
		{vertical.width - kButtonColumn * sx, kButtonColumnTop * sy, kButtonColumn * sx,
		 caption_top - kButtonColumnTop * sy},
		{0.0, caption_top, vertical.width, kCaption * sy},
		{0.0, 0.0, vertical.width, kTopBar * sy},
	};
}

namespace {

LinkedPlacement cover(const Rect &rect, const Crop &crop)
{
	LinkedPlacement placement;
	placement.kind = PlacementKind::Cover;
	placement.cover = rect;
	placement.crop = crop;
	return placement;
}

// What of the item falls in the focus window, moved and scaled into the content region.
LinkedPlacement reframe(const MainItemFacts &item, const TwinRegions &regions)
{
	const double visible_width = item.source_size.x - item.crop.left - item.crop.right;
	const double visible_height = item.source_size.y - item.crop.top - item.crop.bottom;
	const bool plain = !item.has_bounds && std::fabs(item.rotation_degrees) < 0.01 && item.scale.x > 0.0 &&
			   item.scale.y > 0.0;
	if (!plain || visible_width <= 0.0 || visible_height <= 0.0 || item.box.empty()) {
		// Anything this cannot reason about still fills its region.
		return cover(regions.content, item.crop);
	}

	const Rect &window = regions.focus_window;
	const Rect inside = intersect(item.box, window);
	if (inside.empty()) {
		LinkedPlacement hidden;
		hidden.kind = PlacementKind::Hidden;
		return hidden;
	}

	const double per_pixel_x = item.box.width / visible_width;
	const double per_pixel_y = item.box.height / visible_height;
	const double k = regions.content.width / window.width;

	LinkedPlacement placement;
	placement.kind = PlacementKind::Plain;
	placement.crop.left = item.crop.left + static_cast<int>(std::lround((inside.x - item.box.x) / per_pixel_x));
	placement.crop.top = item.crop.top + static_cast<int>(std::lround((inside.y - item.box.y) / per_pixel_y));
	placement.crop.right =
		item.crop.right + static_cast<int>(std::lround((item.box.right() - inside.right()) / per_pixel_x));
	placement.crop.bottom =
		item.crop.bottom + static_cast<int>(std::lround((item.box.bottom() - inside.bottom()) / per_pixel_y));
	placement.position = {regions.content.x + (inside.x - window.x) * k,
			      regions.content.y + (inside.y - window.y) * k};
	placement.scale = {per_pixel_x * k, per_pixel_y * k};
	return placement;
}

LinkedPlacement anchor(const MainItemFacts &item, const TwinRegions &regions, CanvasSize main, CanvasSize vertical)
{
	const Rect &safe = regions.safe;
	const Rect &box = item.box;
	LinkedPlacement placement;
	placement.crop = item.crop;
	if (box.empty() || main.width <= 0.0 || main.height <= 0.0) {
		placement.position = {safe.x, safe.y};
		return placement;
	}

	double s = std::min(vertical.width / main.width * kOverlayBoost, 1.0);
	if (box.width * s > safe.width * kOverlayMaxWidth) {
		s = safe.width * kOverlayMaxWidth / box.width;
	}
	const double width = box.width * s;
	const double height = box.height * s;
	const double center_x = (box.x + box.width / 2.0) / main.width;
	const double center_y = (box.y + box.height / 2.0) / main.height;
	const double gap_x = vertical.width / main.width * kEdgeGapShare;
	const double gap_y = vertical.height / main.height * kEdgeGapShare;

	double x = safe.x + (safe.width - width) / 2.0;
	if (center_x < 1.0 / 3.0) {
		x = safe.x + box.x * gap_x;
	} else if (center_x > 2.0 / 3.0) {
		x = safe.right() - width - (main.width - box.right()) * gap_x;
	}
	double y = safe.y + (safe.height - height) / 2.0;
	if (center_y < 1.0 / 3.0) {
		y = safe.y + box.y * gap_y;
	} else if (center_y > 2.0 / 3.0) {
		y = safe.bottom() - height - (main.height - box.bottom()) * gap_y;
	}
	x = clamp_between(x, safe.x, safe.right() - width);
	y = clamp_between(y, safe.y, safe.bottom() - height);

	const bool rotated = std::fabs(item.rotation_degrees) > 0.01;
	if (item.has_bounds) {
		placement.kind = PlacementKind::ScaledBounds;
		placement.bounds = {item.bounds.x * s, item.bounds.y * s};
	} else {
		placement.kind = PlacementKind::Plain;
		placement.scale = {item.scale.x * s, item.scale.y * s};
	}
	// A rotated item turns around its center, so it is placed by its center too.
	placement.centered = rotated;
	placement.position = rotated ? Vec2{x + width / 2.0, y + height / 2.0} : Vec2{x, y};
	return placement;
}

} // namespace

LinkedPlacement place_linked_item(LinkedRole role, const MainItemFacts &item, const TwinRegions &regions,
				  CanvasSize main, CanvasSize vertical)
{
	switch (role) {
	case LinkedRole::Background:
		return cover({0.0, 0.0, vertical.width, vertical.height}, item.crop);
	case LinkedRole::Camera:
		if (!regions.camera.empty()) {
			return cover(regions.camera, item.crop);
		}
		return anchor(item, regions, main, vertical);
	case LinkedRole::Content:
		return reframe(item, regions);
	case LinkedRole::Overlay:
		break;
	}
	return anchor(item, regions, main, vertical);
}

} // namespace multistream
