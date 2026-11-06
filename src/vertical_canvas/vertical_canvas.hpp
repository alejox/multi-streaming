// The vertical canvas: finds or creates it in the frontend, keeps its video set up, and
// offers its scenes and composition. UI thread only, except VerticalPreviewTarget.
//
// OBS saves the canvas and its scenes in the scene collection, so they come and go with
// it: attach after a collection loaded, detach before one is unloaded. The Canvas API is
// officially unstable, so any failure leaves the feature disabled, with a log line.
//
// The canvas is ACTIVATE | SCENE_REF, not PROGRAM. PROGRAM adds MIX_AUDIO, which makes the
// channel's scene a root of the global audio mix (obs-audio.c:594-596 at OBS 33.0.1), so
// audio of a source only in the vertical canvas would reach the main mix and every track.
#pragma once

#include "destinations/vertical_settings.hpp"
#include "streaming/video_sources.hpp"

#include <obs.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace multistream {

// What a preview draws. Its draw callback runs on the graphics thread and can outlast the
// canvas, in which case it draws nothing.
class VerticalPreviewTarget {
public:
	void set(obs_canvas_t *canvas, int width, int height);
	void clear();

	// Graphics thread, in a display's draw callback.
	void render(std::uint32_t window_width, std::uint32_t window_height);

	std::uint64_t rendered_frames() const { return rendered_frames_; }

private:
	std::mutex mutex_;
	OBSCanvas canvas_;
	int width_ = 0;
	int height_ = 0;
	std::atomic<std::uint64_t> rendered_frames_{0};
};

struct VerticalItem {
	std::int64_t id = 0;
	std::string name;
	bool visible = true;
};

class VerticalCanvas final : public VideoSources {
public:
	explicit VerticalCanvas(std::filesystem::path settings_path);

	video_t *video_for(VideoSource source) override;
	std::shared_ptr<void> hold(VideoSource source) override;

	// Finds or creates the canvas and its first scene. Returns whether it is available.
	bool attach();
	void detach();
	bool available() const { return static_cast<bool>(canvas_); }

	// After a profile change: main's frame rate may be different now.
	void refresh_video();

	// When the frontend removed a canvas: detaches if it was this one.
	void check_removed();

	void set_changed_listener(std::function<void()> listener);
	std::shared_ptr<VerticalPreviewTarget> preview_target() const { return preview_; }

	const VerticalSettings &settings() const { return settings_; }

	// Applied only after decide_resolution_change allowed it, which only the caller can
	// do since it knows about destinations. Returns whether the video was reset.
	bool apply_resolution(int width, int height);

	std::vector<std::string> scenes() const;
	std::optional<std::string> active_scene() const;
	std::optional<std::string> add_scene(const std::string &wanted_name);
	bool rename_scene(const std::string &name, const std::string &new_name);
	bool remove_scene(const std::string &name);
	bool switch_scene(const std::string &name);

	// The main canvas's sources, which can be added to a scene.
	std::vector<std::string> main_sources() const;

	// Top of the stack first, in the active scene.
	std::vector<VerticalItem> items() const;
	bool add_source(const std::string &name);
	bool remove_item(std::int64_t id);
	enum class Move { Up, Down };
	bool move_item(std::int64_t id, Move move);
	bool set_item_visible(std::int64_t id, bool visible);
	bool fit_item(std::int64_t id);
	bool fill_item(std::int64_t id);

private:
	bool reset_video();
	void set_channel(obs_source_t *scene_source);
	void persist();
	void notify();
	OBSSceneItem find_item(std::int64_t id) const;
	void place_item(obs_sceneitem_t *item, obs_bounds_type bounds) const;
	void replace_bounds_of_all_items();

	std::filesystem::path settings_path_;
	VerticalSettings settings_;
	OBSCanvas canvas_;
	std::shared_ptr<VerticalPreviewTarget> preview_ = std::make_shared<VerticalPreviewTarget>();
	std::function<void()> listener_;
};

} // namespace multistream
