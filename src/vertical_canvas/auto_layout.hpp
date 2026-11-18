// Linked vertical scenes: how an item of a main (horizontal) scene is placed in the vertical
// scene that mirrors it. No OBS or Qt, so it is tested on its own. Canvas pixels, y down.
//
// Each item gets a role from its source type and how much of the main canvas it covers, and
// the role decides the placement:
//   Camera      fills the camera band at the top (cropped to cover, no bars)
//   Content     screen/game/window captures and nested scenes: what falls inside the focus
//               window of the main canvas is reframed into the content region, cropped
//   Background  images, colors, media that cover the canvas: fill the whole vertical canvas
//   Overlay     alerts, logos, text, widgets: anchored to the same corner or edge, a bit
//               larger than a plain downscale so they stay readable on a phone, and kept out
//               of the strips that phone apps cover with their own buttons and captions
#pragma once

#include "vertical_canvas/preview_geometry.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace multistream {

struct Rect {
	double x = 0.0;
	double y = 0.0;
	double width = 0.0;
	double height = 0.0;

	double right() const { return x + width; }
	double bottom() const { return y + height; }
	bool empty() const { return width <= 0.0 || height <= 0.0; }
};

Rect intersect(const Rect &a, const Rect &b);

struct Crop {
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;
};

enum class TwinLayout { CameraTop, FullScreen, CameraBig };

inline constexpr TwinLayout kAllTwinLayouts[] = {TwinLayout::CameraTop, TwinLayout::FullScreen, TwinLayout::CameraBig};

std::string_view twin_layout_id(TwinLayout layout);
std::optional<TwinLayout> twin_layout_from_id(std::string_view id);
// Share of the canvas height the camera band takes, 0 when there is none.
double camera_band_fraction(TwinLayout layout);

enum class LinkedRole { Camera, Content, Background, Overlay };

std::string_view linked_role_id(LinkedRole role);

struct SourceFacts {
	bool camera = false;
	bool capture = false; // screen, window, game, NDI/Syphon, nested scene
};

// From the unversioned source type and its name: a source named "Cam", "Webcam" or "Cámara"
// counts as a camera whatever its type, since cameras also come through NDI or capture cards.
SourceFacts facts_for_source(std::string_view unversioned_type_id, std::string_view name, bool is_scene);

LinkedRole classify_linked_item(const SourceFacts &facts, const Rect &box_in_main, CanvasSize main);

struct TwinRegions {
	Rect camera;       // empty when the layout or the scene has no camera band
	Rect content;      // where reframed content goes
	Rect safe;         // where overlays may go
	Rect focus_window; // the part of the main canvas that content shows, same aspect as content
};

// focus: 0 = the left edge of the main canvas, 1 = the right edge.
TwinRegions twin_regions(TwinLayout layout, double focus, bool has_camera, CanvasSize main, CanvasSize vertical);

// Strips that TikTok, Shorts and Reels cover with their own interface: the button column on
// the right and the caption at the bottom. Scaled to the canvas.
std::vector<Rect> app_safe_zones(CanvasSize vertical);

// What the main item looks like, read from libobs.
struct MainItemFacts {
	Rect box;         // axis-aligned box on the main canvas
	Vec2 source_size; // before crop; zero while the source has no size yet
	Crop crop;
	Vec2 scale{1.0, 1.0};
	double rotation_degrees = 0.0;
	bool has_bounds = false;
	Vec2 bounds; // bounding box size when has_bounds
};

enum class PlacementKind {
	// Top-left aligned, unbounded: position, scale and crop.
	Plain,
	// Covers `rect`, cropping what does not fit; keeps the main item's crop.
	Cover,
	// Keeps the main item's bounds type, with `bounds` as the new size; centered on
	// `position` when `centered`, else top-left at `position`.
	ScaledBounds,
	// Outside the focus window: nothing of it would show.
	Hidden,
};

struct LinkedPlacement {
	PlacementKind kind = PlacementKind::Plain;
	Vec2 position;
	Vec2 scale{1.0, 1.0};
	Crop crop;
	Rect cover;
	Vec2 bounds;
	bool centered = false;
};

LinkedPlacement place_linked_item(LinkedRole role, const MainItemFacts &item, const TwinRegions &regions,
				  CanvasSize main, CanvasSize vertical);

} // namespace multistream
