#include "vertical_canvas/preview_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace multistream {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMinimumSide = 1.0;
constexpr double kNoRotationTolerance = 0.001;

struct Rotation {
	double cosine;
	double sine;

	explicit Rotation(double degrees) : cosine(std::cos(degrees * kPi / 180.0)), sine(std::sin(degrees * kPi / 180.0))
	{
	}

	Vec2 apply(Vec2 local) const { return {local.x * cosine - local.y * sine, local.x * sine + local.y * cosine}; }
	Vec2 undo(Vec2 world) const { return {world.x * cosine + world.y * sine, -world.x * sine + world.y * cosine}; }
};

Vec2 to_local(const ItemBox &box, Vec2 point)
{
	return Rotation(box.rotation_degrees).undo({point.x - box.origin.x, point.y - box.origin.y});
}

struct HandleSides {
	bool left = false;
	bool right = false;
	bool top = false;
	bool bottom = false;

	bool corner() const { return (left || right) && (top || bottom); }
};

HandleSides sides_of(Handle handle)
{
	HandleSides sides;
	sides.left = handle == Handle::TopLeft || handle == Handle::Left || handle == Handle::BottomLeft;
	sides.right = handle == Handle::TopRight || handle == Handle::Right || handle == Handle::BottomRight;
	sides.top = handle == Handle::TopLeft || handle == Handle::Top || handle == Handle::TopRight;
	sides.bottom = handle == Handle::BottomLeft || handle == Handle::Bottom || handle == Handle::BottomRight;
	return sides;
}

bool is_unrotated(const ItemBox &box)
{
	return std::fabs(std::remainder(box.rotation_degrees, 360.0)) < kNoRotationTolerance;
}

struct Bounds {
	double left;
	double top;
	double right;
	double bottom;
};

Bounds bounding_rectangle(const ItemBox &box)
{
	const std::array<Vec2, 4> corners = box_corners(box);
	Bounds bounds{corners[0].x, corners[0].y, corners[0].x, corners[0].y};
	for (const Vec2 &corner : corners) {
		bounds.left = std::min(bounds.left, corner.x);
		bounds.right = std::max(bounds.right, corner.x);
		bounds.top = std::min(bounds.top, corner.y);
		bounds.bottom = std::max(bounds.bottom, corner.y);
	}
	return bounds;
}

struct Snap {
	double offset = 0.0;
	std::optional<double> line;

	void consider(double position, double target, double distance)
	{
		const double candidate = target - position;
		const double best = line ? std::fabs(offset) : distance;
		if (std::fabs(candidate) < best) {
			offset = candidate;
			line = target;
		}
	}
};

// Whichever of the box's sides and center lands closest to the canvas's sides and center.
Snap snap_span(double low, double high, double canvas_side, double distance)
{
	Snap snap;
	if (distance <= 0.0) {
		return snap;
	}
	snap.consider(low, 0.0, distance);
	snap.consider(high, canvas_side, distance);
	snap.consider((low + high) / 2.0, canvas_side / 2.0, distance);
	return snap;
}

// One moved edge, against the canvas's sides and center.
Snap snap_edge(double position, double canvas_side, double distance)
{
	Snap snap;
	if (distance <= 0.0) {
		return snap;
	}
	snap.consider(position, 0.0, distance);
	snap.consider(position, canvas_side, distance);
	snap.consider(position, canvas_side / 2.0, distance);
	return snap;
}

double keep_minimum(double side, double fallback_sign)
{
	if (std::fabs(side) >= kMinimumSide) {
		return side;
	}
	return std::copysign(kMinimumSide, side != 0.0 ? side : fallback_sign);
}

} // namespace

Vec2 box_point(const ItemBox &box, double along_width, double along_height)
{
	const Vec2 offset = Rotation(box.rotation_degrees).apply({box.width * along_width, box.height * along_height});
	return {box.origin.x + offset.x, box.origin.y + offset.y};
}

std::array<Vec2, 4> box_corners(const ItemBox &box)
{
	return {box_point(box, 0.0, 0.0), box_point(box, 1.0, 0.0), box_point(box, 1.0, 1.0), box_point(box, 0.0, 1.0)};
}

bool box_contains(const ItemBox &box, Vec2 point)
{
	if (box.width == 0.0 || box.height == 0.0) {
		return false;
	}
	const Vec2 local = to_local(box, point);
	const double along_width = local.x / box.width;
	const double along_height = local.y / box.height;
	return along_width >= 0.0 && along_width <= 1.0 && along_height >= 0.0 && along_height <= 1.0;
}

std::optional<ItemBox> box_from_draw_transform(Vec2 translation, Vec2 x_axis, Vec2 y_axis, double rotation_degrees,
					       double source_width, double source_height)
{
	const bool finite = std::isfinite(translation.x) && std::isfinite(translation.y) && std::isfinite(x_axis.x) &&
			    std::isfinite(x_axis.y) && std::isfinite(y_axis.x) && std::isfinite(y_axis.y);
	if (!finite || source_width <= 0.0 || source_height <= 0.0) {
		return std::nullopt;
	}

	const Rotation rotation(rotation_degrees);
	const Vec2 along_x = rotation.apply({1.0, 0.0});
	const Vec2 along_y = rotation.apply({0.0, 1.0});

	ItemBox box;
	box.origin = translation;
	box.width = (x_axis.x * along_x.x + x_axis.y * along_x.y) * source_width;
	box.height = (y_axis.x * along_y.x + y_axis.y * along_y.y) * source_height;
	box.rotation_degrees = rotation_degrees;
	if (box.width == 0.0 || box.height == 0.0) {
		return std::nullopt;
	}
	return box;
}

std::optional<ItemPlacement> placement_for_box(const ItemBox &box, double source_width, double source_height)
{
	if (source_width <= 0.0 || source_height <= 0.0) {
		return std::nullopt;
	}
	return ItemPlacement{box.origin, {box.width / source_width, box.height / source_height}};
}

PreviewMapping::PreviewMapping(int canvas_width, int canvas_height, int window_width, int window_height)
	: canvas_width_(canvas_width),
	  canvas_height_(canvas_height),
	  viewport_(fit_viewport(canvas_width, canvas_height, window_width, window_height))
{
}

Vec2 PreviewMapping::to_canvas(Vec2 window_pixels) const
{
	if (!valid()) {
		return {};
	}
	return {(window_pixels.x - viewport_.x) * canvas_width_ / viewport_.width,
		(window_pixels.y - viewport_.y) * canvas_height_ / viewport_.height};
}

Vec2 PreviewMapping::to_window(Vec2 canvas_point) const
{
	if (!valid()) {
		return {};
	}
	return {viewport_.x + canvas_point.x * viewport_.width / canvas_width_,
		viewport_.y + canvas_point.y * viewport_.height / canvas_height_};
}

double PreviewMapping::canvas_per_pixel() const
{
	return valid() ? static_cast<double>(canvas_height_) / viewport_.height : 1.0;
}

Vec2 handle_position(const ItemBox &box, Handle handle)
{
	const HandleSides sides = sides_of(handle);
	const double along_width = sides.left ? 0.0 : sides.right ? 1.0 : 0.5;
	const double along_height = sides.top ? 0.0 : sides.bottom ? 1.0 : 0.5;
	return box_point(box, along_width, along_height);
}

Handle handle_at(const ItemBox &box, Vec2 point, double radius)
{
	Handle found = Handle::None;
	double best = radius;
	for (const Handle handle : kAllHandles) {
		const Vec2 position = handle_position(box, handle);
		const double distance = std::hypot(point.x - position.x, point.y - position.y);
		if (distance <= best) {
			best = distance;
			found = handle;
		}
	}
	return found;
}

PreviewCursor cursor_for_handle(const ItemBox &box, Handle handle)
{
	if (handle == Handle::None) {
		return PreviewCursor::Arrow;
	}

	// The direction the handle pulls in, as if the box were square, so a long thin box
	// still gets diagonal cursors on its corners.
	const HandleSides sides = sides_of(handle);
	const double local_x = (sides.right ? 1.0 : sides.left ? -1.0 : 0.0) * (box.width < 0.0 ? -1.0 : 1.0);
	const double local_y = (sides.bottom ? 1.0 : sides.top ? -1.0 : 0.0) * (box.height < 0.0 ? -1.0 : 1.0);
	const Vec2 pull = Rotation(box.rotation_degrees).apply({local_x, local_y});

	double angle = std::atan2(pull.y, pull.x) * 180.0 / kPi;
	if (angle < 0.0) {
		angle += 180.0;
	}
	const int octant = static_cast<int>(std::lround(angle / 45.0)) % 4;
	switch (octant) {
	case 0:
		return PreviewCursor::SizeHorizontal;
	case 1:
		return PreviewCursor::SizeDiagonalDown;
	case 2:
		return PreviewCursor::SizeVertical;
	default:
		return PreviewCursor::SizeDiagonalUp;
	}
}

std::optional<std::int64_t> pick_item(const std::vector<PreviewItem> &items, Vec2 point)
{
	for (const PreviewItem &item : items) {
		if (item.visible && !item.locked && box_contains(item.box, point)) {
			return item.id;
		}
	}
	return std::nullopt;
}

BoxChange plan_move(const ItemBox &start, Vec2 delta, CanvasSize canvas, double snap_distance)
{
	BoxChange change;
	change.box = start;
	change.box.origin = {std::round(start.origin.x + delta.x), std::round(start.origin.y + delta.y)};

	const Bounds bounds = bounding_rectangle(change.box);
	const Snap horizontal = snap_span(bounds.left, bounds.right, canvas.width, snap_distance);
	const Snap vertical = snap_span(bounds.top, bounds.bottom, canvas.height, snap_distance);
	change.box.origin.x += horizontal.offset;
	change.box.origin.y += vertical.offset;
	change.guides.vertical_x = horizontal.line;
	change.guides.horizontal_y = vertical.line;
	return change;
}

BoxChange plan_stretch(const ItemBox &start, Handle handle, Vec2 cursor, CanvasSize canvas,
		       const StretchOptions &options)
{
	const HandleSides sides = sides_of(handle);
	const Vec2 local = to_local(start, cursor);
	const bool unrotated = is_unrotated(start);

	BoxChange change;
	double left = 0.0;
	double top = 0.0;
	double right = start.width;
	double bottom = start.height;

	if (sides.left || sides.right) {
		double &edge = sides.left ? left : right;
		edge = local.x;
		if (unrotated) {
			const Snap snap = snap_edge(start.origin.x + edge, canvas.width, options.snap_distance);
			edge += snap.offset;
			change.guides.vertical_x = snap.line;
		}
	}
	if (sides.top || sides.bottom) {
		double &edge = sides.top ? top : bottom;
		edge = local.y;
		if (unrotated) {
			const Snap snap = snap_edge(start.origin.y + edge, canvas.height, options.snap_distance);
			edge += snap.offset;
			change.guides.horizontal_y = snap.line;
		}
	}

	double width = right - left;
	double height = bottom - top;

	if (!options.free_aspect && start.width != 0.0 && start.height != 0.0) {
		const double aspect = std::fabs(start.width / start.height);
		bool width_follows;
		if (sides.corner()) {
			width_follows = height != 0.0 && std::fabs(width / height) < aspect;
		} else {
			width_follows = sides.top || sides.bottom;
		}
		if (width_follows) {
			const double sign = width != 0.0 ? width : start.width;
			width = std::copysign(std::fabs(height) * aspect, sign);
		} else {
			const double sign = height != 0.0 ? height : start.height;
			height = std::copysign(std::fabs(width) / aspect, sign);
		}
	}

	width = keep_minimum(width, start.width);
	height = keep_minimum(height, start.height);

	// The opposite side holds. An edge handle's other axis grows around its center.
	if (sides.left) {
		left = right - width;
	} else if (sides.right) {
		right = left + width;
	} else {
		const double center = (left + right) / 2.0;
		left = center - width / 2.0;
		right = center + width / 2.0;
	}
	if (sides.top) {
		top = bottom - height;
	} else if (sides.bottom) {
		bottom = top + height;
	} else {
		const double center = (top + bottom) / 2.0;
		top = center - height / 2.0;
		bottom = center + height / 2.0;
	}

	// Whole pixels for an edge that moved, without shifting the side that holds.
	if (unrotated && left != 0.0) {
		left = std::round(start.origin.x + left) - start.origin.x;
	}
	if (unrotated && top != 0.0) {
		top = std::round(start.origin.y + top) - start.origin.y;
	}

	const Vec2 offset = Rotation(start.rotation_degrees).apply({left, top});
	change.box = start;
	change.box.origin = {start.origin.x + offset.x, start.origin.y + offset.y};
	change.box.width = right - left;
	change.box.height = bottom - top;
	return change;
}

ItemBox centered_box(const ItemBox &box, CanvasSize canvas)
{
	const Bounds bounds = bounding_rectangle(box);
	ItemBox centered = box;
	centered.origin.x += canvas.width / 2.0 - (bounds.left + bounds.right) / 2.0;
	centered.origin.y += canvas.height / 2.0 - (bounds.top + bounds.bottom) / 2.0;
	return centered;
}

ItemBox plan_nudge(const ItemBox &box, NudgeDirection direction, bool shift)
{
	const double step = shift ? 10.0 : 1.0;
	ItemBox moved = box;
	switch (direction) {
	case NudgeDirection::Up:
		moved.origin.y -= step;
		break;
	case NudgeDirection::Down:
		moved.origin.y += step;
		break;
	case NudgeDirection::Left:
		moved.origin.x -= step;
		break;
	case NudgeDirection::Right:
		moved.origin.x += step;
		break;
	}
	return moved;
}

void PreviewInteraction::select(std::optional<std::int64_t> id)
{
	if (id != selected_) {
		cancel();
	}
	selected_ = id;
}

bool PreviewInteraction::press(Vec2 point, const std::vector<PreviewItem> &items, const Tolerances &tolerances)
{
	cancel();
	tolerances_ = tolerances;
	start_point_ = point;

	const auto selected = std::find_if(items.begin(), items.end(), [this](const PreviewItem &item) {
		return selected_ && item.id == *selected_ && item.visible && !item.locked;
	});
	if (selected != items.end()) {
		handle_ = handle_at(selected->box, point, tolerances.handle_radius);
		if (handle_ != Handle::None) {
			mode_ = Mode::Stretching;
			start_box_ = selected->box;
			return false;
		}
		if (box_contains(selected->box, point)) {
			mode_ = Mode::Moving;
			start_box_ = selected->box;
			return false;
		}
	}

	const std::optional<std::int64_t> picked = pick_item(items, point);
	const bool changed = picked != selected_;
	selected_ = picked;
	if (picked) {
		const auto found = std::find_if(items.begin(), items.end(),
						[&](const PreviewItem &item) { return item.id == *picked; });
		mode_ = Mode::Moving;
		start_box_ = found->box;
	}
	return changed;
}

std::optional<BoxChange> PreviewInteraction::move(Vec2 point, CanvasSize canvas, const Modifiers &modifiers)
{
	if (mode_ == Mode::Idle) {
		return std::nullopt;
	}

	const Vec2 delta{point.x - start_point_.x, point.y - start_point_.y};
	if (!dragging_ && std::hypot(delta.x, delta.y) < tolerances_.drag_threshold) {
		return std::nullopt;
	}
	dragging_ = true;

	const double snap_distance = modifiers.disable_snap ? 0.0 : tolerances_.snap_distance;
	if (mode_ == Mode::Moving) {
		return plan_move(start_box_, delta, canvas, snap_distance);
	}

	StretchOptions options;
	options.free_aspect = modifiers.shift;
	options.snap_distance = snap_distance;
	return plan_stretch(start_box_, handle_, point, canvas, options);
}

bool PreviewInteraction::release(Vec2 point, const std::vector<PreviewItem> &items)
{
	const bool clicked = mode_ == Mode::Moving && !dragging_;
	cancel();
	if (!clicked) {
		return false;
	}
	const std::optional<std::int64_t> picked = pick_item(items, point);
	const bool changed = picked != selected_;
	selected_ = picked;
	return changed;
}

void PreviewInteraction::cancel()
{
	mode_ = Mode::Idle;
	dragging_ = false;
	handle_ = Handle::None;
}

PreviewCursor PreviewInteraction::cursor_at(Vec2 point, const std::vector<PreviewItem> &items, double handle_radius) const
{
	for (const PreviewItem &item : items) {
		if (selected_ && item.id == *selected_ && item.visible && !item.locked) {
			const Handle handle = handle_at(item.box, point, handle_radius);
			if (handle != Handle::None) {
				return cursor_for_handle(item.box, handle);
			}
			if (box_contains(item.box, point)) {
				return PreviewCursor::Move;
			}
		}
	}
	return PreviewCursor::Arrow;
}

} // namespace multistream
