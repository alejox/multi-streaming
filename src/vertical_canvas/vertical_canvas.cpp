#include "vertical_canvas/vertical_canvas.hpp"

#include "destinations/runtime_requirements.hpp"
#include "persistence/vertical_settings_file.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <graphics/matrix4.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace multistream {
namespace {

constexpr const char *kCanvasName = "obs-multi-stream-vertical";

std::uint32_t canvas_flags()
{
#if defined(MULTI_STREAM_E2E_SECRETS)
	// Lets the audio check compare against PROGRAM.
	if (const char *flags = std::getenv("MULTI_STREAM_E2E_CANVAS_FLAGS")) {
		return static_cast<std::uint32_t>(std::strtoul(flags, nullptr, 10));
	}
#endif
	return ACTIVATE | SCENE_REF;
}

OBSCanvas find_canvas()
{
	OBSCanvas found;
	obs_frontend_canvas_list list = {};
	obs_frontend_get_canvases(&list);
	for (size_t i = 0; i < list.canvases.num; ++i) {
		if (std::string(obs_canvas_get_name(list.canvases.array[i])) == kCanvasName) {
			found = list.canvases.array[i];
			break;
		}
	}
	obs_frontend_canvas_list_free(&list);
	return found;
}

constexpr float kOutlineLogicalPixels = 2.0f;
constexpr float kHandleLogicalPixels = 8.0f;

void add_triangle_pair(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
	for (const Vec2 &corner : {a, b, c, b, d, c}) {
		gs_vertex2f(static_cast<float>(corner.x), static_cast<float>(corner.y));
	}
}

void add_line(Vec2 from, Vec2 to, double thickness)
{
	const double length = std::hypot(to.x - from.x, to.y - from.y);
	if (length <= 0.0) {
		return;
	}
	const Vec2 side{-(to.y - from.y) / length * thickness / 2.0, (to.x - from.x) / length * thickness / 2.0};
	add_triangle_pair({from.x + side.x, from.y + side.y}, {to.x + side.x, to.y + side.y},
			  {from.x - side.x, from.y - side.y}, {to.x - side.x, to.y - side.y});
}

void add_square(Vec2 center, double side)
{
	const double half = side / 2.0;
	add_triangle_pair({center.x - half, center.y - half}, {center.x + half, center.y - half},
			  {center.x - half, center.y + half}, {center.x + half, center.y + half});
}

std::uint32_t top_left()
{
	return OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
}

std::optional<Vec2> cropped_source_size(obs_sceneitem_t *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	obs_sceneitem_crop crop = {};
	obs_sceneitem_get_crop(item, &crop);
	const double width = static_cast<double>(obs_source_get_width(source)) - crop.left - crop.right;
	const double height = static_cast<double>(obs_source_get_height(source)) - crop.top - crop.bottom;
	if (width <= 0.0 || height <= 0.0) {
		return std::nullopt;
	}
	return Vec2{width, height};
}

// libobs recomputes the draw transform on the next video tick, so read the placement itself
// when it says everything, which a plain item's does: a burst of nudges adds up correctly.
std::optional<ItemBox> box_of(obs_sceneitem_t *item)
{
	const std::optional<Vec2> size = cropped_source_size(item);
	if (!size) {
		return std::nullopt;
	}

	const double rotation = obs_sceneitem_get_rot(item);
	if (obs_sceneitem_get_bounds_type(item) == OBS_BOUNDS_NONE && obs_sceneitem_get_alignment(item) == top_left()) {
		vec2 position;
		vec2 scale;
		obs_sceneitem_get_pos(item, &position);
		obs_sceneitem_get_scale(item, &scale);
		ItemBox box;
		box.origin = {position.x, position.y};
		box.width = scale.x * size->x;
		box.height = scale.y * size->y;
		box.rotation_degrees = rotation;
		return box;
	}

	matrix4 transform;
	obs_sceneitem_get_draw_transform(item, &transform);
	return box_from_draw_transform({transform.t.x, transform.t.y}, {transform.x.x, transform.x.y},
				       {transform.y.x, transform.y.y}, rotation, size->x, size->y);
}

bool has_video(obs_sceneitem_t *item)
{
	return (obs_source_get_output_flags(obs_sceneitem_get_source(item)) & OBS_SOURCE_VIDEO) != 0;
}

VerticalItem view_of(obs_sceneitem_t *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	VerticalItem view;
	view.id = obs_sceneitem_get_id(item);
	view.name = obs_source_get_name(source);
	const char *type = obs_source_get_unversioned_id(source);
	view.type_id = type != nullptr ? type : "";
	view.visible = obs_sceneitem_visible(item);
	view.locked = obs_sceneitem_locked(item);
	view.has_video = has_video(item);
	return view;
}

} // namespace

void VerticalPreviewTarget::set(obs_canvas_t *canvas, int width, int height)
{
	const std::lock_guard<std::mutex> lock(mutex_);
	canvas_ = canvas;
	width_ = width;
	height_ = height;
}

void VerticalPreviewTarget::clear()
{
	set(nullptr, 0, 0);
}

void VerticalPreviewTarget::render(std::uint32_t window_width, std::uint32_t window_height)
{
	const std::lock_guard<std::mutex> lock(mutex_);
	if (!canvas_) {
		return;
	}

	const Viewport viewport = fit_viewport(width_, height_, static_cast<int>(window_width), static_cast<int>(window_height));
	if (viewport.width <= 0 || viewport.height <= 0) {
		return;
	}

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, static_cast<float>(width_), 0.0f, static_cast<float>(height_), -100.0f, 100.0f);
	gs_set_viewport(viewport.x, viewport.y, viewport.width, viewport.height);
	obs_canvas_render(canvas_);
	gs_projection_pop();
	gs_viewport_pop();

	draw_overlay(window_width, window_height);
	++rendered_frames_;
}

void VerticalPreviewTarget::set_overlay(const PreviewOverlay &overlay)
{
	const std::lock_guard<std::mutex> lock(mutex_);
	overlay_ = overlay;
}

// In window pixels and over the whole window, so the marks stay the same size whatever the
// preview's scale, and handles remain visible in the margins beside the canvas.
void VerticalPreviewTarget::draw_overlay(std::uint32_t window_width, std::uint32_t window_height) const
{
	const bool has_guides = overlay_.guides.vertical_x || overlay_.guides.horizontal_y;
	if (!overlay_.box && !has_guides) {
		return;
	}

	const PreviewMapping mapping(width_, height_, static_cast<int>(window_width), static_cast<int>(window_height));
	const double ratio = overlay_.pixel_ratio;
	const double thickness = kOutlineLogicalPixels * ratio;

	gs_viewport_push();
	gs_projection_push();
	gs_matrix_push();
	gs_matrix_identity();
	gs_set_viewport(0, 0, static_cast<int>(window_width), static_cast<int>(window_height));
	gs_ortho(0.0f, static_cast<float>(window_width), 0.0f, static_cast<float>(window_height), -100.0f, 100.0f);

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	vec4 red;
	vec4_set(&red, 1.0f, 0.0f, 0.0f, 1.0f);
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &red);
	gs_technique_t *technique = gs_effect_get_technique(effect, "Solid");
	gs_technique_begin(technique);
	gs_technique_begin_pass(technique, 0);

	gs_render_start(true);
	if (overlay_.guides.vertical_x) {
		add_line(mapping.to_window({*overlay_.guides.vertical_x, 0.0}),
			 mapping.to_window({*overlay_.guides.vertical_x, static_cast<double>(height_)}), thickness / 2.0);
	}
	if (overlay_.guides.horizontal_y) {
		add_line(mapping.to_window({0.0, *overlay_.guides.horizontal_y}),
			 mapping.to_window({static_cast<double>(width_), *overlay_.guides.horizontal_y}), thickness / 2.0);
	}
	if (overlay_.box) {
		const std::array<Vec2, 4> corners = box_corners(*overlay_.box);
		for (std::size_t i = 0; i < corners.size(); ++i) {
			add_line(mapping.to_window(corners[i]), mapping.to_window(corners[(i + 1) % corners.size()]),
				 thickness);
		}
		if (overlay_.handles) {
			for (const Handle handle : kAllHandles) {
				add_square(mapping.to_window(handle_position(*overlay_.box, handle)),
					   kHandleLogicalPixels * ratio);
			}
		}
	}
	gs_render_stop(GS_TRIS);

	gs_technique_end_pass(technique);
	gs_technique_end(technique);

	gs_matrix_pop();
	gs_projection_pop();
	gs_viewport_pop();
}

#if defined(MULTI_STREAM_E2E_SECRETS)
std::string VerticalPreviewTarget::probe_overlay(std::uint32_t window_width, std::uint32_t window_height)
{
	const std::lock_guard<std::mutex> lock(mutex_);
	long count = 0;
	std::uint32_t left = window_width;
	std::uint32_t top = window_height;
	std::uint32_t right = 0;
	std::uint32_t bottom = 0;

	obs_enter_graphics();
	gs_texrender_t *render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *stage = gs_stagesurface_create(window_width, window_height, GS_RGBA);
	if (gs_texrender_begin(render, window_width, window_height)) {
		vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		draw_overlay(window_width, window_height);
		gs_texrender_end(render);
		gs_stage_texture(stage, gs_texrender_get_texture(render));

		std::uint8_t *data = nullptr;
		std::uint32_t line_size = 0;
		if (gs_stagesurface_map(stage, &data, &line_size)) {
			for (std::uint32_t y = 0; y < window_height; ++y) {
				for (std::uint32_t x = 0; x < window_width; ++x) {
					const std::uint8_t *pixel = data + y * line_size + x * 4;
					if (pixel[0] > 200 && pixel[1] < 50 && pixel[2] < 50 && pixel[3] > 200) {
						++count;
						left = std::min(left, x);
						right = std::max(right, x);
						top = std::min(top, y);
						bottom = std::max(bottom, y);
					}
				}
			}
			gs_stagesurface_unmap(stage);
		}
	}
	gs_stagesurface_destroy(stage);
	gs_texrender_destroy(render);
	obs_leave_graphics();

	return std::to_string(count) + " " + std::to_string(left) + "," + std::to_string(top) + "-" +
	       std::to_string(right) + "," + std::to_string(bottom);
}
#endif

VerticalCanvas::VerticalCanvas(std::filesystem::path settings_path)
	: settings_path_(std::move(settings_path)),
	  settings_(load_vertical_settings(settings_path_))
{
}

video_t *VerticalCanvas::video_for(VideoSource source)
{
	if (source == VideoSource::Main) {
		return obs_get_video();
	}
	return canvas_ ? obs_canvas_get_video(canvas_) : nullptr;
}

std::shared_ptr<void> VerticalCanvas::hold(VideoSource source)
{
	if (source == VideoSource::Main || !canvas_) {
		return nullptr;
	}
	return std::make_shared<OBSCanvas>(canvas_);
}

bool VerticalCanvas::attach()
{
	if (canvas_) {
		return true;
	}

	if (!is_canvas_api_tested(obs_get_version())) {
		obs_log(LOG_WARNING, "the Canvas API was not tested on OBS %s: the vertical canvas is disabled",
			obs_get_version_string());
		notify();
		return false;
	}

	OBSCanvas canvas = find_canvas();
	if (!canvas) {
		OBSCanvasAutoRelease created = obs_frontend_add_canvas(kCanvasName, nullptr, static_cast<int>(canvas_flags()));
		canvas = created.Get();
	}
	if (!canvas) {
		obs_log(LOG_ERROR, "could not create the vertical canvas: it is disabled");
		notify();
		return false;
	}
	canvas_ = canvas;

	if (!reset_video()) {
		obs_log(LOG_ERROR, "could not set up video for the vertical canvas: it is disabled");
		canvas_ = nullptr;
		notify();
		return false;
	}

	std::vector<std::string> names = scenes();
	if (names.empty()) {
		if (!add_scene(obs_module_text("Vertical.DefaultScene"))) {
			obs_log(LOG_ERROR, "could not create a vertical scene: the vertical canvas is disabled");
			canvas_ = nullptr;
			notify();
			return false;
		}
		names = scenes();
	}

	switch_scene(pick_active_scene(names, settings_.active_scene).value_or(std::string()));
	preview_->set(canvas_, settings_.width, settings_.height);
	obs_log(LOG_INFO, "vertical canvas ready at %dx%d with %zu scenes", settings_.width, settings_.height,
		names.size());
	notify();
	return true;
}

void VerticalCanvas::detach()
{
	preview_->clear();
	if (canvas_) {
		// Scene deactivation and the release belong here, not to whoever goes last.
		obs_canvas_set_channel(canvas_, 0, nullptr);
	}
	canvas_ = nullptr;
	notify();
}

void VerticalCanvas::refresh_video()
{
	if (canvas_ && !reset_video()) {
		obs_log(LOG_ERROR, "could not reset video for the vertical canvas: it is disabled");
		detach();
	}
}

void VerticalCanvas::check_removed()
{
	if (canvas_ && obs_canvas_removed(canvas_)) {
		detach();
	}
}

void VerticalCanvas::set_changed_listener(std::function<void()> listener)
{
	listener_ = std::move(listener);
}

bool VerticalCanvas::apply_resolution(int width, int height)
{
	const VerticalSettings previous = settings_;
	settings_.width = width;
	settings_.height = height;

	if (canvas_ && !reset_video()) {
		settings_ = previous;
		reset_video();
		notify();
		return false;
	}

	if (canvas_) {
		preview_->set(canvas_, settings_.width, settings_.height);
		replace_bounds_of_all_items();
	}
	persist();
	notify();
	return true;
}

std::vector<std::string> VerticalCanvas::scenes() const
{
	std::vector<std::string> names;
	if (canvas_) {
		obs_canvas_enum_scenes(
			canvas_,
			[](void *param, obs_source_t *source) {
				static_cast<std::vector<std::string> *>(param)->push_back(obs_source_get_name(source));
				return true;
			},
			&names);
	}
	return names;
}

std::optional<std::string> VerticalCanvas::active_scene() const
{
	if (!canvas_) {
		return std::nullopt;
	}
	OBSSourceAutoRelease source = obs_canvas_get_channel(canvas_, 0);
	return source ? std::optional<std::string>(obs_source_get_name(source)) : std::nullopt;
}

std::optional<std::string> VerticalCanvas::add_scene(const std::string &wanted_name)
{
	if (!canvas_ || wanted_name.empty()) {
		return std::nullopt;
	}
	const std::string name = unique_scene_name(wanted_name, scenes());
	OBSSceneAutoRelease scene = obs_canvas_scene_create(canvas_, name.c_str());
	if (!scene) {
		return std::nullopt;
	}
	notify();
	return name;
}

bool VerticalCanvas::rename_scene(const std::string &name, const std::string &new_name)
{
	if (!canvas_ || new_name.empty()) {
		return false;
	}
	OBSSourceAutoRelease taken = obs_canvas_get_source_by_name(canvas_, new_name.c_str());
	OBSSourceAutoRelease source = obs_canvas_get_source_by_name(canvas_, name.c_str());
	if (taken || !source) {
		return false;
	}

	obs_source_set_name(source, new_name.c_str());
	if (settings_.active_scene == name) {
		settings_.active_scene = new_name;
	}
	std::replace(settings_.scene_order.begin(), settings_.scene_order.end(), name, new_name);
	persist();
	notify();
	return true;
}

bool VerticalCanvas::remove_scene(const std::string &name)
{
	const std::vector<std::string> names = scenes();
	if (!canvas_ || !can_remove_scene(names.size())) {
		return false;
	}
	OBSSourceAutoRelease source = obs_canvas_get_source_by_name(canvas_, name.c_str());
	if (!source) {
		return false;
	}

	if (active_scene() == name) {
		std::vector<std::string> others = names;
		others.erase(std::remove(others.begin(), others.end(), name), others.end());
		switch_scene(others.front());
	}
	obs_source_remove(source);
	notify();
	return true;
}

bool VerticalCanvas::switch_scene(const std::string &name)
{
	if (!canvas_) {
		return false;
	}
	OBSSourceAutoRelease source = obs_canvas_get_source_by_name(canvas_, name.c_str());
	if (!source || !obs_source_is_scene(source)) {
		return false;
	}
	obs_canvas_set_channel(canvas_, 0, source);
	settings_.active_scene = name;
	persist();
	notify();
	return true;
}

bool VerticalCanvas::move_scene(const std::string &name, bool up)
{
	settings_.scene_order = multistream::move_scene(scenes(), name, up);
	persist();
	notify();
	return true;
}

std::vector<std::string> VerticalCanvas::main_sources() const
{
	std::vector<std::string> names;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			if ((obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) != 0) {
				static_cast<std::vector<std::string> *>(param)->push_back(obs_source_get_name(source));
			}
			return true;
		},
		&names);
	return names;
}

std::vector<InputType> VerticalCanvas::creatable_input_types() const
{
	std::vector<InputType> types;
	const char *id = nullptr;
	const char *unversioned = nullptr;
	for (size_t index = 0; obs_enum_input_types2(index, &id, &unversioned); ++index) {
		const std::uint32_t flags = obs_get_source_output_flags(id);
		const char *name = obs_source_get_display_name(id);
		InputType type;
		type.id = id;
		type.unversioned_id = unversioned != nullptr ? unversioned : id;
		type.display_name = name != nullptr ? name : id;
		type.has_video = (flags & OBS_SOURCE_VIDEO) != 0;
		type.disabled = (flags & OBS_SOURCE_CAP_DISABLED) != 0;
		type.deprecated = (flags & OBS_SOURCE_DEPRECATED) != 0;
		types.push_back(std::move(type));
	}
	return creatable_video_types(std::move(types));
}

std::vector<VerticalItem> VerticalCanvas::items() const
{
	std::vector<VerticalItem> result;
	if (!canvas_) {
		return result;
	}
	OBSSourceAutoRelease source = obs_canvas_get_channel(canvas_, 0);
	obs_scene_t *scene = obs_scene_from_source(source);
	if (scene == nullptr) {
		return result;
	}
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			static_cast<std::vector<VerticalItem> *>(param)->push_back(view_of(item));
			return true;
		},
		&result);
	std::reverse(result.begin(), result.end());
	return result;
}

std::optional<std::int64_t> VerticalCanvas::add_source(const std::string &name)
{
	OBSSourceAutoRelease source = obs_get_source_by_name(name.c_str());
	return source ? add_to_active_scene(source) : std::nullopt;
}

std::optional<std::int64_t> VerticalCanvas::create_source(const std::string &type_id, const std::string &name)
{
	if (!canvas_ || name.empty() || source_name_taken(name)) {
		return std::nullopt;
	}
	OBSSourceAutoRelease source = obs_source_create(type_id.c_str(), name.c_str(), nullptr, nullptr);
	return source ? add_to_active_scene(source) : std::nullopt;
}

bool VerticalCanvas::source_name_taken(const std::string &name) const
{
	OBSSourceAutoRelease source = obs_get_source_by_name(name.c_str());
	return static_cast<bool>(source);
}

std::optional<std::int64_t> VerticalCanvas::add_to_active_scene(obs_source_t *source)
{
	if (!canvas_ || (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) == 0) {
		return std::nullopt;
	}
	OBSSourceAutoRelease channel = obs_canvas_get_channel(canvas_, 0);
	obs_scene_t *scene = obs_scene_from_source(channel);
	if (scene == nullptr) {
		return std::nullopt;
	}

	obs_sceneitem_t *item = obs_scene_add(scene, source);
	if (item == nullptr) {
		return std::nullopt;
	}
	place_item(item, OBS_BOUNDS_SCALE_INNER);
	const std::int64_t id = obs_sceneitem_get_id(item);
	notify();
	return id;
}

bool VerticalCanvas::open_item_properties(std::int64_t id)
{
	OBSSceneItem item = find_item(id);
	if (!item) {
		return false;
	}
	obs_frontend_open_source_properties(obs_sceneitem_get_source(item));
	return true;
}

OBSSceneItem VerticalCanvas::find_item(std::int64_t id) const
{
	if (!canvas_) {
		return nullptr;
	}
	OBSSourceAutoRelease channel = obs_canvas_get_channel(canvas_, 0);
	obs_scene_t *scene = obs_scene_from_source(channel);
	if (scene == nullptr) {
		return nullptr;
	}

	// Enumeration holds each item while its callback runs, so the reference taken here
	// cannot race a removal, as one taken after obs_scene_find_sceneitem_by_id could.
	struct Search {
		std::int64_t id;
		OBSSceneItem found;
	} search{id, nullptr};
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *current = static_cast<Search *>(param);
			if (obs_sceneitem_get_id(item) != current->id) {
				return true;
			}
			current->found = item;
			return false;
		},
		&search);
	return search.found;
}

bool VerticalCanvas::remove_item(std::int64_t id)
{
	OBSSceneItem item = find_item(id);
	if (!item) {
		return false;
	}
	obs_sceneitem_remove(item);
	notify();
	return true;
}

bool VerticalCanvas::move_item(std::int64_t id, Move move)
{
	OBSSceneItem item = find_item(id);
	if (!item) {
		return false;
	}
	switch (move) {
	case Move::Up:
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_UP);
		break;
	case Move::Down:
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_DOWN);
		break;
	case Move::Top:
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
		break;
	case Move::Bottom:
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);
		break;
	}
	notify();
	return true;
}

bool VerticalCanvas::set_item_visible(std::int64_t id, bool visible)
{
	OBSSceneItem item = find_item(id);
	if (!item) {
		return false;
	}
	obs_sceneitem_set_visible(item, visible);
	notify();
	return true;
}

bool VerticalCanvas::fit_item(std::int64_t id)
{
	OBSSceneItem item = find_item(id);
	if (!item) {
		return false;
	}
	place_item(item, OBS_BOUNDS_SCALE_INNER);
	notify();
	return true;
}

bool VerticalCanvas::fill_item(std::int64_t id)
{
	OBSSceneItem item = find_item(id);
	if (!item) {
		return false;
	}
	place_item(item, OBS_BOUNDS_SCALE_OUTER);
	notify();
	return true;
}

bool VerticalCanvas::set_item_locked(std::int64_t id, bool locked)
{
	OBSSceneItem item = find_item(id);
	if (!item) {
		return false;
	}
	obs_sceneitem_set_locked(item, locked);
	notify();
	return true;
}

bool VerticalCanvas::center_item(std::int64_t id)
{
	OBSSceneItem item = find_item(id);
	const std::optional<ItemBox> box = item ? box_of(item) : std::nullopt;
	if (!box) {
		return false;
	}
	return set_item_box(id, centered_box(*box, canvas_size())) == BoxResult::Applied;
}

bool VerticalCanvas::reset_item_transform(std::int64_t id)
{
	OBSSceneItem item = find_item(id);
	if (!item) {
		return false;
	}
	obs_transform_info info = {};
	obs_sceneitem_get_info2(item, &info);
	vec2_set(&info.pos, 0.0f, 0.0f);
	vec2_set(&info.scale, 1.0f, 1.0f);
	info.rot = 0.0f;
	info.alignment = top_left();
	info.bounds_type = OBS_BOUNDS_NONE;
	const obs_sceneitem_crop no_crop = {};

	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_set_info2(item, &info);
	obs_sceneitem_set_crop(item, &no_crop);
	obs_sceneitem_defer_update_end(item);
	return true;
}

void VerticalCanvas::save_collection() const
{
	obs_frontend_save();
}

CanvasSize VerticalCanvas::canvas_size() const
{
	return {static_cast<double>(settings_.width), static_cast<double>(settings_.height)};
}

std::vector<PreviewItem> VerticalCanvas::preview_items() const
{
	std::vector<PreviewItem> result;
	if (!canvas_) {
		return result;
	}
	OBSSourceAutoRelease channel = obs_canvas_get_channel(canvas_, 0);
	obs_scene_t *scene = obs_scene_from_source(channel);
	if (scene == nullptr) {
		return result;
	}
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			if (!has_video(item)) {
				return true;
			}
			const std::optional<ItemBox> box = box_of(item);
			if (box) {
				PreviewItem preview;
				preview.id = obs_sceneitem_get_id(item);
				preview.box = *box;
				preview.visible = obs_sceneitem_visible(item);
				preview.locked = obs_sceneitem_locked(item);
				static_cast<std::vector<PreviewItem> *>(param)->push_back(preview);
			}
			return true;
		},
		&result);
	std::reverse(result.begin(), result.end());
	return result;
}

std::optional<PreviewItem> VerticalCanvas::preview_item(std::int64_t id) const
{
	OBSSceneItem item = find_item(id);
	const std::optional<ItemBox> box = item ? box_of(item) : std::nullopt;
	if (!box) {
		return std::nullopt;
	}
	PreviewItem preview;
	preview.id = id;
	preview.box = *box;
	preview.visible = obs_sceneitem_visible(item);
	preview.locked = obs_sceneitem_locked(item);
	return preview;
}

VerticalCanvas::BoxResult VerticalCanvas::set_item_box(std::int64_t id, const ItemBox &box)
{
	OBSSceneItem item = find_item(id);
	if (!item) {
		return BoxResult::Gone;
	}
	if (obs_sceneitem_locked(item)) {
		return BoxResult::Skipped;
	}

	// The source's size is read now, not when the gesture began: it can change meanwhile.
	const std::optional<Vec2> size = cropped_source_size(item);
	const std::optional<ItemPlacement> placement =
		size ? placement_for_box(box, size->x, size->y) : std::nullopt;
	if (!placement) {
		return BoxResult::Skipped;
	}

	const vec2 position = {static_cast<float>(placement->position.x), static_cast<float>(placement->position.y)};
	const vec2 scale = {static_cast<float>(placement->scale.x), static_cast<float>(placement->scale.y)};
	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	obs_sceneitem_set_alignment(item, top_left());
	obs_sceneitem_set_scale(item, &scale);
	obs_sceneitem_set_pos(item, &position);
	obs_sceneitem_defer_update_end(item);
	return BoxResult::Applied;
}

bool VerticalCanvas::reset_video()
{
	obs_video_info video = {};
	if (!obs_get_video_info(&video)) {
		return false;
	}
	video.base_width = static_cast<std::uint32_t>(settings_.width);
	video.base_height = static_cast<std::uint32_t>(settings_.height);
	video.output_width = video.base_width;
	video.output_height = video.base_height;
	return obs_canvas_reset_video(canvas_, &video);
}

void VerticalCanvas::persist()
{
	if (const auto problem = save_vertical_settings(settings_path_, settings_)) {
		obs_log(LOG_WARNING, "%s", problem->c_str());
	}
}

void VerticalCanvas::notify()
{
	if (listener_) {
		listener_();
	}
}

// Bounds let libobs keep the placement right whatever size the source has now or later.
void VerticalCanvas::place_item(obs_sceneitem_t *item, obs_bounds_type bounds) const
{
	const vec2 size = {static_cast<float>(settings_.width), static_cast<float>(settings_.height)};
	const vec2 center = {size.x / 2.0f, size.y / 2.0f};
	obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds_type(item, bounds);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds_crop(item, false);
	obs_sceneitem_set_bounds(item, &size);
	obs_sceneitem_set_pos(item, &center);
}

void VerticalCanvas::replace_bounds_of_all_items()
{
	struct Context {
		const VerticalCanvas *canvas;
	} context{this};

	obs_canvas_enum_scenes(
		canvas_,
		[](void *param, obs_source_t *source) {
			obs_scene_enum_items(
				obs_scene_from_source(source),
				[](obs_scene_t *, obs_sceneitem_t *item, void *inner) {
					const obs_bounds_type bounds = obs_sceneitem_get_bounds_type(item);
					if (bounds != OBS_BOUNDS_NONE) {
						static_cast<Context *>(inner)->canvas->place_item(item, bounds);
					}
					return true;
				},
				param);
			return true;
		},
		&context);
}

} // namespace multistream
