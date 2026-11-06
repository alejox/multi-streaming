#include "vertical_canvas/vertical_canvas.hpp"

#include "destinations/runtime_requirements.hpp"
#include "persistence/vertical_settings_file.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
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
	++rendered_frames_;
}

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
		persist();
	}
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

std::vector<std::string> VerticalCanvas::main_sources() const
{
	std::vector<std::string> names;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			static_cast<std::vector<std::string> *>(param)->push_back(obs_source_get_name(source));
			return true;
		},
		&names);
	return names;
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
			VerticalItem view;
			view.id = obs_sceneitem_get_id(item);
			view.name = obs_source_get_name(obs_sceneitem_get_source(item));
			view.visible = obs_sceneitem_visible(item);
			static_cast<std::vector<VerticalItem> *>(param)->push_back(std::move(view));
			return true;
		},
		&result);
	std::reverse(result.begin(), result.end());
	return result;
}

bool VerticalCanvas::add_source(const std::string &name)
{
	if (!canvas_) {
		return false;
	}
	OBSSourceAutoRelease channel = obs_canvas_get_channel(canvas_, 0);
	obs_scene_t *scene = obs_scene_from_source(channel);
	OBSSourceAutoRelease source = obs_get_source_by_name(name.c_str());
	if (scene == nullptr || !source) {
		return false;
	}

	obs_sceneitem_t *item = obs_scene_add(scene, source);
	if (item == nullptr) {
		return false;
	}
	place_item(item, OBS_BOUNDS_SCALE_INNER);
	notify();
	return true;
}

OBSSceneItem VerticalCanvas::find_item(std::int64_t id) const
{
	if (!canvas_) {
		return nullptr;
	}
	OBSSourceAutoRelease channel = obs_canvas_get_channel(canvas_, 0);
	obs_scene_t *scene = obs_scene_from_source(channel);
	return scene != nullptr ? obs_scene_find_sceneitem_by_id(scene, id) : nullptr;
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
	obs_sceneitem_set_order(item, move == Move::Up ? OBS_ORDER_MOVE_UP : OBS_ORDER_MOVE_DOWN);
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
