#include "vertical_canvas/vertical_canvas.hpp"

#include "destinations/runtime_requirements.hpp"
#include "persistence/vertical_settings_file.hpp"
#include "streaming/localized_text.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <graphics/matrix4.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

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

// Fills the whole canvas area in solid black, in canvas coordinates. The display's own
// background (the margins around the canvas) is a lighter gray, so this is what makes the
// canvas rectangle visible, the same way the main OBS preview separates the two.
void fill_canvas_backdrop(int width, int height)
{
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	vec4 color;
	vec4_set(&color, 0.0f, 0.0f, 0.0f, 1.0f);
	gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &color);
	gs_technique_t *technique = gs_effect_get_technique(effect, "Solid");
	gs_technique_begin(technique);
	gs_technique_begin_pass(technique, 0);
	gs_render_start(true);
	gs_vertex2f(0.0f, 0.0f);
	gs_vertex2f(static_cast<float>(width), 0.0f);
	gs_vertex2f(0.0f, static_cast<float>(height));
	gs_vertex2f(static_cast<float>(width), static_cast<float>(height));
	gs_render_stop(GS_TRISTRIP);
	gs_technique_end_pass(technique);
	gs_technique_end(technique);
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
	OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
	view.linked = obs_data_has_user_value(settings, "multistream_link");
	view.manual = view.linked && obs_data_get_bool(settings, "multistream_manual");
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

void VerticalPreviewTarget::render(std::uint32_t window_width, std::uint32_t window_height, bool with_overlay)
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

	fill_canvas_backdrop(width_, height_);
	obs_canvas_render(canvas_);

	gs_projection_pop();
	gs_viewport_pop();

	if (with_overlay) {
		draw_overlay(window_width, window_height);
	}
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
	if (!overlay_.box && !has_guides && overlay_.safe_zones.empty()) {
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
	if (!overlay_.safe_zones.empty()) {
		// Translucent amber, under the selection marks.
		gs_blend_state_push();
		gs_enable_blending(true);
		gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
		vec4 amber;
		vec4_set(&amber, 1.0f, 0.71f, 0.33f, 0.22f);
		gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &amber);
		gs_technique_t *zones = gs_effect_get_technique(effect, "Solid");
		gs_technique_begin(zones);
		gs_technique_begin_pass(zones, 0);
		gs_render_start(true);
		for (const Rect &zone : overlay_.safe_zones) {
			add_triangle_pair(mapping.to_window({zone.x, zone.y}), mapping.to_window({zone.right(), zone.y}),
					  mapping.to_window({zone.x, zone.bottom()}),
					  mapping.to_window({zone.right(), zone.bottom()}));
		}
		gs_render_stop(GS_TRIS);
		gs_technique_end_pass(zones);
		gs_technique_end(zones);
		gs_blend_state_pop();
	}
	if (!overlay_.box && !has_guides) {
		gs_matrix_pop();
		gs_projection_pop();
		gs_viewport_pop();
		return;
	}

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
	  settings_(load_vertical_settings(settings_path_)),
	  signal_context_(std::make_unique<QObject>())
{
	signal_handler_connect(obs_get_signal_handler(), "video_reset", on_video_reset, this);
}

VerticalCanvas::~VerticalCanvas()
{
	// Disconnecting waits for a signal in flight, so nothing queues work after this.
	signal_handler_disconnect(obs_get_signal_handler(), "video_reset", on_video_reset, this);
	unwatch();
	signal_context_.reset();
}

void VerticalCanvas::on_video_reset(void *data, calldata_t *)
{
	auto *self = static_cast<VerticalCanvas *>(data);
	// Emitted from inside obs_reset_video: reset the canvas after that call has returned.
	QMetaObject::invokeMethod(self->signal_context_.get(), [self] { self->refresh_video(); }, Qt::QueuedConnection);
}

void VerticalCanvas::on_external_change(void *data, calldata_t *)
{
	auto *self = static_cast<VerticalCanvas *>(data);
	if (self->change_queued_.exchange(true)) {
		return;
	}
	QMetaObject::invokeMethod(
		self->signal_context_.get(),
		[self] {
			self->change_queued_ = false;
			self->watch_active_scene();
			self->notify();
		},
		Qt::QueuedConnection);
}

void VerticalCanvas::watch_canvas()
{
	canvas_signals_.clear();
	canvas_signals_.reserve(5);
	signal_handler_t *canvas_signals = obs_canvas_get_signal_handler(canvas_);
	for (const char *signal : {"source_add", "source_remove", "source_rename", "channel_change"}) {
		canvas_signals_.emplace_back(canvas_signals, signal, on_external_change, this);
	}
	// Items mostly show the main canvas's sources, whose renames only the core reports.
	canvas_signals_.emplace_back(obs_get_signal_handler(), "source_rename", on_external_change, this);
	watch_active_scene();
}

void VerticalCanvas::watch_active_scene()
{
	OBSSourceAutoRelease scene = canvas_ ? obs_canvas_get_channel(canvas_, 0) : nullptr;
	if (scene && watched_scene_ && obs_weak_source_references_source(watched_scene_, scene)) {
		return;
	}
	scene_signals_.clear();
	watched_scene_ = nullptr;
	if (!scene || !obs_source_is_scene(scene)) {
		return;
	}

	OBSWeakSourceAutoRelease weak = obs_source_get_weak_source(scene);
	watched_scene_ = weak.Get();
	signal_handler_t *scene_signals = obs_source_get_signal_handler(scene);
	scene_signals_.reserve(6);
	for (const char *signal : {"item_add", "item_remove", "reorder", "refresh", "item_visible", "item_locked"}) {
		scene_signals_.emplace_back(scene_signals, signal, on_external_change, this);
	}
}

void VerticalCanvas::unwatch()
{
	main_signals_.clear();
	relayout_queue_.clear();
	scene_signals_.clear();
	canvas_signals_.clear();
	watched_scene_ = nullptr;
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

	// The scenes, and so what is remembered about them, belong to the collection loaded now.
	char *collection = obs_frontend_get_current_scene_collection();
	collection_ = collection ? collection : "";
	bfree(collection);
	settings_ = select_collection(load_vertical_settings(settings_path_), collection_);

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
	watch_canvas();
	refresh_links();
	follow_program();
	preview_->set(canvas_, settings_.width, settings_.height);
	obs_log(LOG_INFO, "vertical canvas ready at %dx%d with %zu scenes", settings_.width, settings_.height,
		names.size());
	notify();
	return true;
}

void VerticalCanvas::detach()
{
	preview_->clear();
	unwatch();
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

bool VerticalCanvas::removed() const
{
	return canvas_ && obs_canvas_removed(canvas_);
}

void VerticalCanvas::check_removed()
{
	if (removed()) {
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
		// Linked items were skipped above: their layout depends on the new size.
		relayout_all_linked();
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
	watch_active_scene();
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
	mark_manual(item);
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
	mark_manual(item);
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
	mark_manual(item);
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
	// Pinned only when it really moved: a drag back to where it was keeps it automatic.
	if (const std::optional<ItemBox> current = box_of(item);
	    !current || std::fabs(current->origin.x - box.origin.x) > 0.5 ||
	    std::fabs(current->origin.y - box.origin.y) > 0.5 || std::fabs(current->width - box.width) > 0.5 ||
	    std::fabs(current->height - box.height) > 0.5) {
		mark_manual(item);
	}
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
	remember_collection(settings_, collection_);
	if (const auto problem = save_vertical_settings(settings_path_, settings_)) {
		obs_log(LOG_WARNING, "%s", localize(*problem).c_str());
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
					OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
					if (obs_data_has_user_value(settings, "multistream_link") &&
					    !obs_data_get_bool(settings, "multistream_manual")) {
						return true;
					}
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

// ---------------------------------------------------------------------------------------
// Linked scenes
// ---------------------------------------------------------------------------------------

namespace {

// Kept in private settings, which OBS saves with the scene collection.
constexpr const char *kLinkOf = "multistream_link_of";   // scene: uuid of its main scene
constexpr const char *kLayout = "multistream_layout";     // scene: TwinLayout id
constexpr const char *kFocus = "multistream_focus";       // scene: 0..1
constexpr const char *kLink = "multistream_link";         // item: id of its main item
constexpr const char *kManual = "multistream_manual";     // item: moved by hand
constexpr const char *kVisibilityLink = "multistream_visibility_link"; // item: follows main
constexpr const char *kOutside = "multistream_outside";   // item: outside the focus window

constexpr int kRelayoutDelayMs = 100;

std::string uuid_of(obs_source_t *source)
{
	const char *uuid = source != nullptr ? obs_source_get_uuid(source) : nullptr;
	return uuid != nullptr ? uuid : "";
}

std::string link_of(obs_source_t *scene_source)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene_source);
	return obs_data_get_string(settings, kLinkOf);
}

CanvasSize main_canvas_size()
{
	obs_video_info video = {};
	if (!obs_get_video_info(&video)) {
		return {1920.0, 1080.0};
	}
	return {static_cast<double>(video.base_width), static_cast<double>(video.base_height)};
}

SourceFacts facts_of(obs_sceneitem_t *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	const char *type = obs_source_get_unversioned_id(source);
	const char *name = obs_source_get_name(source);
	return facts_for_source(type != nullptr ? type : "", name != nullptr ? name : "", obs_source_is_scene(source));
}

std::optional<Rect> rect_of(obs_sceneitem_t *item)
{
	const std::optional<ItemBox> box = box_of(item);
	if (!box) {
		return std::nullopt;
	}
	const std::array<Vec2, 4> corners = box_corners(*box);
	double left = corners[0].x, top = corners[0].y, right = corners[0].x, bottom = corners[0].y;
	for (const Vec2 &corner : corners) {
		left = std::min(left, corner.x);
		top = std::min(top, corner.y);
		right = std::max(right, corner.x);
		bottom = std::max(bottom, corner.y);
	}
	return Rect{left, top, right - left, bottom - top};
}

MainItemFacts main_facts(obs_sceneitem_t *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	MainItemFacts facts;
	facts.box = rect_of(item).value_or(Rect{});
	facts.source_size = {static_cast<double>(obs_source_get_width(source)),
			     static_cast<double>(obs_source_get_height(source))};
	obs_sceneitem_crop crop = {};
	obs_sceneitem_get_crop(item, &crop);
	facts.crop = {crop.left, crop.top, crop.right, crop.bottom};
	vec2 scale;
	obs_sceneitem_get_scale(item, &scale);
	facts.scale = {scale.x, scale.y};
	facts.rotation_degrees = obs_sceneitem_get_rot(item);
	facts.has_bounds = obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE;
	vec2 bounds;
	obs_sceneitem_get_bounds(item, &bounds);
	facts.bounds = {bounds.x, bounds.y};
	return facts;
}

bool importable(obs_sceneitem_t *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	// A group belongs to one scene only, and audio comes from the main mix anyway.
	return !obs_source_is_group(source) && (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) != 0;
}

bool scene_has_camera(obs_scene_t *scene)
{
	bool found = false;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			if (obs_sceneitem_visible(item) && facts_of(item).camera) {
				*static_cast<bool *>(param) = true;
				return false;
			}
			return true;
		},
		&found);
	return found;
}

std::vector<obs_sceneitem_t *> items_linked_to(obs_scene_t *scene, std::int64_t main_id)
{
	struct Search {
		std::int64_t id;
		std::vector<obs_sceneitem_t *> found;
	} search{main_id, {}};
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *current = static_cast<Search *>(param);
			OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
			if (obs_data_has_user_value(settings, kLink) && obs_data_get_int(settings, kLink) == current->id) {
				current->found.push_back(item);
			}
			return true;
		},
		&search);
	return search.found;
}

struct LinkSettings {
	std::string main_uuid;
	TwinLayout layout = TwinLayout::CameraTop;
	double focus = 0.5;
};

LinkSettings link_settings(obs_source_t *scene_source)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(scene_source);
	obs_data_set_default_double(settings, kFocus, 0.5);
	LinkSettings link;
	link.main_uuid = obs_data_get_string(settings, kLinkOf);
	link.layout = twin_layout_from_id(obs_data_get_string(settings, kLayout)).value_or(TwinLayout::CameraTop);
	link.focus = std::clamp(obs_data_get_double(settings, kFocus), 0.0, 1.0);
	return link;
}

bool link_item(obs_scene_t *vertical_scene, obs_sceneitem_t *main_item)
{
	obs_sceneitem_t *item = obs_scene_add(vertical_scene, obs_sceneitem_get_source(main_item));
	if (item == nullptr) {
		return false;
	}
	OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
	obs_data_set_int(settings, kLink, obs_sceneitem_get_id(main_item));
	obs_data_set_bool(settings, kManual, false);
	obs_data_set_bool(settings, kVisibilityLink, true);
	obs_sceneitem_set_visible(item, obs_sceneitem_visible(main_item));
	return true;
}

} // namespace

obs_scene_t *VerticalCanvas::linked_scene_for(const std::string &main_uuid) const
{
	if (!canvas_ || main_uuid.empty()) {
		return nullptr;
	}
	struct Search {
		const std::string &uuid;
		obs_scene_t *found;
	} search{main_uuid, nullptr};
	obs_canvas_enum_scenes(
		canvas_,
		[](void *param, obs_source_t *source) {
			auto *current = static_cast<Search *>(param);
			if (link_of(source) == current->uuid) {
				current->found = obs_scene_from_source(source);
				return false;
			}
			return true;
		},
		&search);
	return search.found;
}

obs_scene_t *VerticalCanvas::active_scene_object() const
{
	if (!canvas_) {
		return nullptr;
	}
	// The canvas holds the scene (SCENE_REF), so it outlives this reference.
	OBSSourceAutoRelease channel = obs_canvas_get_channel(canvas_, 0);
	return obs_scene_from_source(channel);
}

void VerticalCanvas::place_linked(obs_sceneitem_t *item, obs_scene_t *main_scene, TwinLayout layout, double focus,
				  bool has_camera) const
{
	OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
	if (!obs_data_has_user_value(settings, kLink)) {
		return;
	}
	obs_sceneitem_t *main_item = obs_scene_find_sceneitem_by_id(main_scene, obs_data_get_int(settings, kLink));
	if (main_item == nullptr) {
		return;
	}

	const CanvasSize main = main_canvas_size();
	const MainItemFacts facts = main_facts(main_item);
	const LinkedRole role = classify_linked_item(facts_of(main_item), facts.box, main);
	const TwinRegions regions = twin_regions(layout, focus, has_camera, main, canvas_size());
	const LinkedPlacement placed = place_linked_item(role, facts, regions, main, canvas_size());

	const bool was_outside = obs_data_get_bool(settings, kOutside);
	obs_data_set_bool(settings, kOutside, placed.kind == PlacementKind::Hidden);
	if (placed.kind == PlacementKind::Hidden) {
		obs_sceneitem_set_visible(item, false);
		return;
	}

	obs_transform_info info = {};
	obs_sceneitem_get_info2(main_item, &info);
	const obs_bounds_type main_bounds = info.bounds_type;
	const std::uint32_t main_bounds_alignment = info.bounds_alignment;
	const bool main_crop_to_bounds = info.crop_to_bounds;
	const float rotation = info.rot;

	info.alignment = placed.centered ? OBS_ALIGN_CENTER : top_left();
	vec2_set(&info.pos, static_cast<float>(placed.position.x), static_cast<float>(placed.position.y));
	vec2_set(&info.scale, static_cast<float>(placed.scale.x), static_cast<float>(placed.scale.y));
	info.rot = placed.centered ? rotation : 0.0f;
	info.bounds_type = OBS_BOUNDS_NONE;
	switch (placed.kind) {
	case PlacementKind::Cover:
		vec2_set(&info.pos, static_cast<float>(placed.cover.x), static_cast<float>(placed.cover.y));
		vec2_set(&info.scale, 1.0f, 1.0f);
		info.bounds_type = OBS_BOUNDS_SCALE_OUTER;
		info.bounds_alignment = OBS_ALIGN_CENTER;
		vec2_set(&info.bounds, static_cast<float>(placed.cover.width), static_cast<float>(placed.cover.height));
		info.crop_to_bounds = true;
		break;
	case PlacementKind::ScaledBounds:
		vec2_set(&info.scale, 1.0f, 1.0f);
		info.bounds_type = main_bounds;
		info.bounds_alignment = main_bounds_alignment;
		vec2_set(&info.bounds, static_cast<float>(placed.bounds.x), static_cast<float>(placed.bounds.y));
		info.crop_to_bounds = main_crop_to_bounds;
		break;
	case PlacementKind::Plain:
	case PlacementKind::Hidden:
		break;
	}
	const obs_sceneitem_crop crop = {placed.crop.left, placed.crop.top, placed.crop.right, placed.crop.bottom};

	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_set_info2(item, &info);
	obs_sceneitem_set_crop(item, &crop);
	obs_sceneitem_defer_update_end(item);

	obs_data_set_default_bool(settings, kVisibilityLink, true);
	if (was_outside || obs_data_get_bool(settings, kVisibilityLink)) {
		obs_sceneitem_set_visible(item, obs_sceneitem_visible(main_item));
	}
}

void VerticalCanvas::relayout_scene(obs_scene_t *scene, bool include_manual)
{
	if (scene == nullptr) {
		return;
	}
	const LinkSettings link = link_settings(obs_scene_get_source(scene));
	OBSSourceAutoRelease main_source = obs_get_source_by_uuid(link.main_uuid.c_str());
	obs_scene_t *main_scene = obs_scene_from_source(main_source);
	if (main_scene == nullptr) {
		return;
	}

	struct Pass {
		const VerticalCanvas *canvas;
		obs_scene_t *main_scene;
		const LinkSettings &link;
		bool has_camera;
		bool include_manual;
	} pass{this, main_scene, link, scene_has_camera(main_scene), include_manual};
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *current = static_cast<Pass *>(param);
			OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
			if (current->include_manual) {
				obs_data_set_bool(settings, kManual, false);
			} else if (obs_data_get_bool(settings, kManual)) {
				return true;
			}
			current->canvas->place_linked(item, current->main_scene, current->link.layout, current->link.focus,
						      current->has_camera);
			return true;
		},
		&pass);
}

void VerticalCanvas::relayout_all_linked()
{
	if (!canvas_) {
		return;
	}
	std::vector<obs_scene_t *> linked;
	obs_canvas_enum_scenes(
		canvas_,
		[](void *param, obs_source_t *source) {
			if (!link_of(source).empty()) {
				static_cast<std::vector<obs_scene_t *> *>(param)->push_back(obs_scene_from_source(source));
			}
			return true;
		},
		&linked);
	for (obs_scene_t *scene : linked) {
		relayout_scene(scene, false);
	}
}

void VerticalCanvas::mark_manual(obs_sceneitem_t *item) const
{
	OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
	if (obs_data_has_user_value(settings, kLink)) {
		obs_data_set_bool(settings, kManual, true);
	}
}

std::vector<MainSceneChoice> VerticalCanvas::main_scene_choices() const
{
	std::vector<MainSceneChoice> choices;
	obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; ++i) {
		obs_source_t *source = list.sources.array[i];
		MainSceneChoice choice;
		choice.uuid = uuid_of(source);
		choice.name = obs_source_get_name(source);
		if (obs_scene_t *linked = linked_scene_for(choice.uuid)) {
			choice.vertical_scene = obs_source_get_name(obs_scene_get_source(linked));
		}
		choices.push_back(std::move(choice));
	}
	obs_frontend_source_list_free(&list);
	return choices;
}

std::optional<std::string> VerticalCanvas::import_main_scene(const std::string &main_uuid, TwinLayout layout)
{
	if (!canvas_) {
		return std::nullopt;
	}
	if (obs_scene_t *existing = linked_scene_for(main_uuid)) {
		const std::string name = obs_source_get_name(obs_scene_get_source(existing));
		switch_scene(name);
		return name;
	}
	OBSSourceAutoRelease main_source = obs_get_source_by_uuid(main_uuid.c_str());
	obs_scene_t *main_scene = obs_scene_from_source(main_source);
	if (main_scene == nullptr) {
		return std::nullopt;
	}

	const QString wanted =
		QString::fromUtf8(obs_module_text("Vertical.LinkedSceneName")).arg(obs_source_get_name(main_source));
	const std::string name = unique_scene_name(wanted.toStdString(), scenes());
	OBSSceneAutoRelease scene = obs_canvas_scene_create(canvas_, name.c_str());
	if (!scene) {
		return std::nullopt;
	}
	{
		OBSDataAutoRelease settings = obs_source_get_private_settings(obs_scene_get_source(scene));
		obs_data_set_string(settings, kLinkOf, main_uuid.c_str());
		obs_data_set_string(settings, kLayout, std::string(twin_layout_id(layout)).c_str());
		obs_data_set_double(settings, kFocus, 0.5);
	}

	// Bottom to top, so the layers keep their order.
	std::vector<OBSSceneItem> main_items;
	obs_scene_enum_items(
		main_scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			if (importable(item)) {
				static_cast<std::vector<OBSSceneItem> *>(param)->emplace_back(item);
			}
			return true;
		},
		&main_items);
	for (const OBSSceneItem &item : main_items) {
		link_item(scene, item);
	}
	relayout_scene(scene, false);

	switch_scene(name);
	refresh_links();
	obs_log(LOG_INFO, "linked vertical scene created for a main scene with %zu items", main_items.size());
	notify();
	return name;
}

std::optional<ActiveLink> VerticalCanvas::active_link() const
{
	obs_scene_t *scene = active_scene_object();
	if (scene == nullptr) {
		return std::nullopt;
	}
	const LinkSettings link = link_settings(obs_scene_get_source(scene));
	if (link.main_uuid.empty()) {
		return std::nullopt;
	}
	ActiveLink active;
	OBSSourceAutoRelease main_source = obs_get_source_by_uuid(link.main_uuid.c_str());
	active.main_name = main_source ? obs_source_get_name(main_source) : "";
	active.layout = link.layout;
	active.focus = link.focus;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
			if (obs_data_has_user_value(settings, kLink)) {
				*static_cast<bool *>(param) = true;
				return false;
			}
			return true;
		},
		&active.automatic);
	return active;
}

bool VerticalCanvas::set_active_link_layout(TwinLayout layout, double focus)
{
	obs_scene_t *scene = active_scene_object();
	if (scene == nullptr || link_of(obs_scene_get_source(scene)).empty()) {
		return false;
	}
	{
		OBSDataAutoRelease settings = obs_source_get_private_settings(obs_scene_get_source(scene));
		obs_data_set_string(settings, kLayout, std::string(twin_layout_id(layout)).c_str());
		obs_data_set_double(settings, kFocus, std::clamp(focus, 0.0, 1.0));
	}
	relayout_scene(scene, false);
	notify();
	return true;
}

bool VerticalCanvas::relayout_active(bool include_manual)
{
	obs_scene_t *scene = active_scene_object();
	if (scene == nullptr || link_of(obs_scene_get_source(scene)).empty()) {
		return false;
	}
	relayout_scene(scene, include_manual);
	notify();
	return true;
}

bool VerticalCanvas::unlink_active()
{
	obs_scene_t *scene = active_scene_object();
	if (scene == nullptr) {
		return false;
	}
	unlink_scene(scene);
	refresh_links();
	notify();
	return true;
}

bool VerticalCanvas::link_active_to(const std::string &main_uuid)
{
	obs_scene_t *scene = active_scene_object();
	OBSSourceAutoRelease main_source = obs_get_source_by_uuid(main_uuid.c_str());
	if (scene == nullptr || !obs_scene_from_source(main_source)) {
		return false;
	}
	obs_scene_t *previous = linked_scene_for(main_uuid);
	if (previous == scene) {
		return true;
	}
	if (previous != nullptr) {
		unlink_scene(previous);
	}
	// A scene follows one main scene: following another drops what was linked to the old one.
	if (!link_of(obs_scene_get_source(scene)).empty()) {
		unlink_scene(scene);
	}
	{
		OBSDataAutoRelease settings = obs_source_get_private_settings(obs_scene_get_source(scene));
		obs_data_set_string(settings, kLinkOf, main_uuid.c_str());
		obs_data_set_default_string(settings, kLayout, std::string(twin_layout_id(TwinLayout::CameraTop)).c_str());
	}
	refresh_links();
	follow_program();
	notify();
	return true;
}

void VerticalCanvas::unlink_scene(obs_scene_t *scene)
{
	{
		OBSDataAutoRelease settings = obs_source_get_private_settings(obs_scene_get_source(scene));
		obs_data_erase(settings, kLinkOf);
	}
	// Its items become plain items: nothing places them automatically any more.
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *) {
			OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
			for (const char *key : {kLink, kManual, kVisibilityLink, kOutside}) {
				obs_data_erase(settings, key);
			}
			return true;
		},
		nullptr);
}

bool VerticalCanvas::reset_item_to_auto(std::int64_t id)
{
	OBSSceneItem item = find_item(id);
	obs_scene_t *scene = active_scene_object();
	if (!item || scene == nullptr) {
		return false;
	}
	const LinkSettings link = link_settings(obs_scene_get_source(scene));
	OBSSourceAutoRelease main_source = obs_get_source_by_uuid(link.main_uuid.c_str());
	obs_scene_t *main_scene = obs_scene_from_source(main_source);
	if (main_scene == nullptr) {
		return false;
	}
	{
		OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
		obs_data_set_bool(settings, kManual, false);
	}
	place_linked(item, main_scene, link.layout, link.focus, scene_has_camera(main_scene));
	notify();
	return true;
}

bool VerticalCanvas::item_visibility_linked(std::int64_t id) const
{
	OBSSceneItem item = find_item(id);
	if (!item) {
		return false;
	}
	OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
	obs_data_set_default_bool(settings, kVisibilityLink, true);
	return obs_data_has_user_value(settings, kLink) && obs_data_get_bool(settings, kVisibilityLink);
}

bool VerticalCanvas::set_item_visibility_linked(std::int64_t id, bool linked)
{
	OBSSceneItem item = find_item(id);
	if (!item) {
		return false;
	}
	OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
	obs_data_set_bool(settings, kVisibilityLink, linked);
	notify();
	return true;
}

void VerticalCanvas::follow_program()
{
	if (!canvas_) {
		return;
	}
	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	obs_scene_t *linked = linked_scene_for(uuid_of(program));
	if (linked == nullptr || linked == active_scene_object()) {
		return;
	}
	switch_scene(obs_source_get_name(obs_scene_get_source(linked)));
}

void VerticalCanvas::refresh_links()
{
	main_signals_.clear();
	if (!canvas_) {
		return;
	}
	std::set<std::string> main_uuids;
	obs_canvas_enum_scenes(
		canvas_,
		[](void *param, obs_source_t *source) {
			const std::string uuid = link_of(source);
			if (!uuid.empty()) {
				static_cast<std::set<std::string> *>(param)->insert(uuid);
			}
			return true;
		},
		&main_uuids);

	for (const std::string &uuid : main_uuids) {
		OBSSourceAutoRelease main_source = obs_get_source_by_uuid(uuid.c_str());
		if (!main_source) {
			continue;
		}
		signal_handler_t *handler = obs_source_get_signal_handler(main_source);
		main_signals_.emplace_back(handler, "item_add", on_main_item_add, this);
		main_signals_.emplace_back(handler, "item_remove", on_main_item_remove, this);
		main_signals_.emplace_back(handler, "item_visible", on_main_item_visible, this);
		main_signals_.emplace_back(handler, "item_transform", on_main_item_transform, this);
	}
}

namespace {

// The main scene of the item a scene signal is about, and the item's id.
bool signal_item(calldata_t *params, std::string &main_uuid, std::int64_t &id)
{
	auto *item = static_cast<obs_sceneitem_t *>(calldata_ptr(params, "item"));
	obs_scene_t *scene = item != nullptr ? obs_sceneitem_get_scene(item) : nullptr;
	if (scene == nullptr) {
		return false;
	}
	main_uuid = uuid_of(obs_scene_get_source(scene));
	id = obs_sceneitem_get_id(item);
	return true;
}

} // namespace

// These come from any thread; the work happens on the UI thread.
void VerticalCanvas::on_main_item_add(void *data, calldata_t *params)
{
	std::string uuid;
	std::int64_t id = 0;
	if (signal_item(params, uuid, id)) {
		auto *self = static_cast<VerticalCanvas *>(data);
		QMetaObject::invokeMethod(
			self->signal_context_.get(), [self, uuid, id] { self->main_item_added(uuid, id); },
			Qt::QueuedConnection);
	}
}

void VerticalCanvas::on_main_item_remove(void *data, calldata_t *params)
{
	std::string uuid;
	std::int64_t id = 0;
	if (signal_item(params, uuid, id)) {
		auto *self = static_cast<VerticalCanvas *>(data);
		QMetaObject::invokeMethod(
			self->signal_context_.get(), [self, uuid, id] { self->main_item_removed(uuid, id); },
			Qt::QueuedConnection);
	}
}

void VerticalCanvas::on_main_item_visible(void *data, calldata_t *params)
{
	std::string uuid;
	std::int64_t id = 0;
	if (signal_item(params, uuid, id)) {
		const bool visible = calldata_bool(params, "visible");
		auto *self = static_cast<VerticalCanvas *>(data);
		QMetaObject::invokeMethod(
			self->signal_context_.get(), [self, uuid, id, visible] { self->main_item_visible(uuid, id, visible); },
			Qt::QueuedConnection);
	}
}

void VerticalCanvas::on_main_item_transform(void *data, calldata_t *params)
{
	std::string uuid;
	std::int64_t id = 0;
	if (signal_item(params, uuid, id)) {
		auto *self = static_cast<VerticalCanvas *>(data);
		QMetaObject::invokeMethod(
			self->signal_context_.get(), [self, uuid] { self->queue_relayout(uuid); }, Qt::QueuedConnection);
	}
}

void VerticalCanvas::main_item_added(const std::string &main_uuid, std::int64_t id)
{
	obs_scene_t *scene = linked_scene_for(main_uuid);
	OBSSourceAutoRelease main_source = obs_get_source_by_uuid(main_uuid.c_str());
	obs_scene_t *main_scene = obs_scene_from_source(main_source);
	obs_sceneitem_t *main_item = main_scene != nullptr ? obs_scene_find_sceneitem_by_id(main_scene, id) : nullptr;
	if (scene == nullptr || main_item == nullptr || !importable(main_item) || !items_linked_to(scene, id).empty()) {
		return;
	}
	if (!link_item(scene, main_item)) {
		return;
	}
	const std::vector<obs_sceneitem_t *> added = items_linked_to(scene, id);
	if (!added.empty()) {
		const LinkSettings link = link_settings(obs_scene_get_source(scene));
		place_linked(added.front(), main_scene, link.layout, link.focus, scene_has_camera(main_scene));
		obs_sceneitem_set_order_position(added.front(), obs_sceneitem_get_order_position(main_item));
	}
	notify();
}

void VerticalCanvas::main_item_removed(const std::string &main_uuid, std::int64_t id)
{
	obs_scene_t *scene = linked_scene_for(main_uuid);
	if (scene == nullptr) {
		return;
	}
	for (obs_sceneitem_t *item : items_linked_to(scene, id)) {
		obs_sceneitem_remove(item);
	}
	notify();
}

void VerticalCanvas::main_item_visible(const std::string &main_uuid, std::int64_t id, bool visible)
{
	obs_scene_t *scene = linked_scene_for(main_uuid);
	if (scene == nullptr) {
		return;
	}
	for (obs_sceneitem_t *item : items_linked_to(scene, id)) {
		OBSDataAutoRelease settings = obs_sceneitem_get_private_settings(item);
		obs_data_set_default_bool(settings, kVisibilityLink, true);
		if (obs_data_get_bool(settings, kVisibilityLink) && !obs_data_get_bool(settings, kOutside)) {
			obs_sceneitem_set_visible(item, visible);
		}
	}
	notify();
}

// A drag in the main preview sends a transform per frame: lay out once it settles.
void VerticalCanvas::queue_relayout(const std::string &main_uuid)
{
	if (linked_scene_for(main_uuid) == nullptr) {
		return;
	}
	const bool idle = relayout_queue_.empty();
	if (std::find(relayout_queue_.begin(), relayout_queue_.end(), main_uuid) == relayout_queue_.end()) {
		relayout_queue_.push_back(main_uuid);
	}
	if (!idle) {
		return;
	}
	QTimer::singleShot(kRelayoutDelayMs, signal_context_.get(), [this] {
		const std::vector<std::string> queued = std::move(relayout_queue_);
		relayout_queue_.clear();
		for (const std::string &uuid : queued) {
			relayout_scene(linked_scene_for(uuid), false);
		}
		notify();
	});
}

std::string VerticalCanvas::canvas_uuid() const
{
	const char *uuid = canvas_ ? obs_canvas_get_uuid(canvas_) : nullptr;
	return uuid != nullptr ? uuid : "";
}

} // namespace multistream
