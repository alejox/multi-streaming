// The math behind editing items in the vertical preview: hit testing, handles, dragging,
// aspect lock, snapping, nudging. No OBS or Qt, so it is tested on its own. Everything is
// in canvas pixels unless a name says otherwise, and y grows downwards.
#pragma once

#include "destinations/vertical_settings.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace multistream {

struct Vec2 {
	double x = 0.0;
	double y = 0.0;
};

// An item's visible rectangle. The origin is where the source's first pixel lands, and the
// size is signed: a flipped item extends left or up from its origin.
struct ItemBox {
	Vec2 origin;
	double width = 0.0;
	double height = 0.0;
	double rotation_degrees = 0.0;
};

Vec2 box_point(const ItemBox &box, double along_width, double along_height);
std::array<Vec2, 4> box_corners(const ItemBox &box);
bool box_contains(const ItemBox &box, Vec2 point);

// What a libobs draw transform means for the box. The axes are its x and y rows, which map
// one source pixel; the size is the source's size after crop. Nothing if it has no size.
std::optional<ItemBox> box_from_draw_transform(Vec2 translation, Vec2 x_axis, Vec2 y_axis, double rotation_degrees,
					       double source_width, double source_height);

// Top-left aligned, unbounded placement that shows the box.
struct ItemPlacement {
	Vec2 position;
	Vec2 scale{1.0, 1.0};
};
std::optional<ItemPlacement> placement_for_box(const ItemBox &box, double source_width, double source_height);

// Converts between device pixels in the preview's window and the canvas, through the same
// letterboxed viewport that the canvas is drawn in.
class PreviewMapping {
public:
	PreviewMapping(int canvas_width, int canvas_height, int window_width, int window_height);

	bool valid() const { return viewport_.width > 0 && viewport_.height > 0; }
	Vec2 to_canvas(Vec2 window_pixels) const;
	Vec2 to_window(Vec2 canvas_point) const;
	// Canvas pixels per window pixel.
	double canvas_per_pixel() const;

private:
	int canvas_width_;
	int canvas_height_;
	Viewport viewport_;
};

enum class Handle { None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };

inline constexpr std::array<Handle, 8> kAllHandles = {Handle::TopLeft,     Handle::Top,    Handle::TopRight,
						      Handle::Right,       Handle::BottomRight, Handle::Bottom,
						      Handle::BottomLeft,  Handle::Left};

Vec2 handle_position(const ItemBox &box, Handle handle);

// The nearest handle within the radius.
Handle handle_at(const ItemBox &box, Vec2 point, double radius);

enum class PreviewCursor { Arrow, Move, SizeHorizontal, SizeVertical, SizeDiagonalDown, SizeDiagonalUp };

// The resize cursor for a handle, following the item's rotation and flips on screen.
PreviewCursor cursor_for_handle(const ItemBox &box, Handle handle);

struct PreviewItem {
	std::int64_t id = 0;
	ItemBox box;
	bool visible = true;
	bool locked = false;
};

// The first item that is visible, unlocked, and under the point. Items are top first.
std::optional<std::int64_t> pick_item(const std::vector<PreviewItem> &items, Vec2 point);

struct CanvasSize {
	double width = 0.0;
	double height = 0.0;
};

// A snap in effect, to draw as a line across the canvas.
struct SnapGuides {
	std::optional<double> vertical_x;
	std::optional<double> horizontal_y;
};

struct BoxChange {
	ItemBox box;
	SnapGuides guides;
};

// Moves the box by the delta and pulls it onto a canvas edge or center, or another item's
// edge or center, within the distance. A distance of 0 turns snapping off.
BoxChange plan_move(const ItemBox &start, Vec2 delta, CanvasSize canvas, const std::vector<ItemBox> &others,
		    double snap_distance);

struct StretchOptions {
	bool free_aspect = false;
	double snap_distance = 0.0;
};

// Puts the handle under the cursor, keeping the opposite side in place. The start box's
// aspect holds unless it is freed, so a resize never jumps to another shape. Crossing the
// opposite side flips the item. Snapping applies only to moved edges of an unrotated item.
BoxChange plan_stretch(const ItemBox &start, Handle handle, Vec2 cursor, CanvasSize canvas,
		       const std::vector<ItemBox> &others, const StretchOptions &options);

// The same size and rotation, with its bounding rectangle centered.
ItemBox centered_box(const ItemBox &box, CanvasSize canvas);

enum class NudgeDirection { Up, Down, Left, Right };

// 1 pixel, or 10 with Shift, matching the main preview.
ItemBox plan_nudge(const ItemBox &box, NudgeDirection direction, bool shift);

// The state of one mouse gesture in the preview, and which item is selected.
class PreviewInteraction {
public:
	struct Tolerances {
		double handle_radius = 0.0;
		double snap_distance = 0.0;
		double drag_threshold = 0.0;
	};

	struct Modifiers {
		bool shift = false;
		bool disable_snap = false;
	};

	std::optional<std::int64_t> selection() const { return selected_; }
	void select(std::optional<std::int64_t> id);

	bool gesture_active() const { return mode_ != Mode::Idle; }
	bool dragging() const { return dragging_; }

	// Returns whether the selection changed. Picks a handle of the selected item first,
	// then the selected item itself, then the topmost item.
	bool press(Vec2 point, const std::vector<PreviewItem> &items, const Tolerances &tolerances);

	// The change to apply, if the gesture has passed the drag threshold. `others` are the
	// other items' boxes to snap against.
	std::optional<BoxChange> move(Vec2 point, CanvasSize canvas, const std::vector<ItemBox> &others,
				      const Modifiers &modifiers);

	// Returns whether the selection changed: a click without a drag on an already selected
	// item selects the topmost item there.
	bool release(Vec2 point, const std::vector<PreviewItem> &items);

	// Ends the gesture at once, for example because the item is gone.
	void cancel();

	PreviewCursor cursor_at(Vec2 point, const std::vector<PreviewItem> &items, double handle_radius) const;

private:
	enum class Mode { Idle, Moving, Stretching };

	std::optional<std::int64_t> selected_;
	Mode mode_ = Mode::Idle;
	bool dragging_ = false;
	Handle handle_ = Handle::None;
	ItemBox start_box_;
	Vec2 start_point_;
	Tolerances tolerances_;
};

} // namespace multistream
