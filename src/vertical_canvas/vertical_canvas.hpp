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
#include "vertical_canvas/auto_layout.hpp"
#include "vertical_canvas/preview_geometry.hpp"
#include "vertical_canvas/source_catalog.hpp"

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

class QObject;

namespace multistream {

// The editing marks over the canvas, as the UI thread last saw them. The graphics thread
// only draws this copy: it never touches a scene item, which the UI thread can remove.
struct PreviewOverlay {
	std::optional<ItemBox> box;
	bool handles = false;
	SnapGuides guides;
	// Shaded strips that phone apps cover with their own buttons, in canvas pixels.
	std::vector<Rect> safe_zones;
	double pixel_ratio = 1.0;
};

// What a preview draws. Its draw callback runs on the graphics thread and can outlast the
// canvas, in which case it draws nothing.
class VerticalPreviewTarget {
public:
	void set(obs_canvas_t *canvas, int width, int height);
	void clear();
	void set_overlay(const PreviewOverlay &overlay);

	// Graphics thread, in a display's draw callback.
	// with_overlay false draws only the canvas, as a monitor of what goes out.
	void render(std::uint32_t window_width, std::uint32_t window_height, bool with_overlay = true);

	std::uint64_t rendered_frames() const { return rendered_frames_; }

#if defined(MULTI_STREAM_E2E_SECRETS)
	// Draws the marks into a texture and reports their red pixels, since no other test can
	// see a native surface. Enters the graphics context itself. Returns "count x0,y0-x1,y1".
	std::string probe_overlay(std::uint32_t window_width, std::uint32_t window_height);
#endif

private:
	void draw_overlay(std::uint32_t window_width, std::uint32_t window_height) const;

	std::mutex mutex_;
	OBSCanvas canvas_;
	int width_ = 0;
	int height_ = 0;
	PreviewOverlay overlay_;
	std::atomic<std::uint64_t> rendered_frames_{0};
};

struct VerticalItem {
	std::int64_t id = 0;
	std::string name;
	std::string type_id;
	bool visible = true;
	bool locked = false;
	// Audio-only sources have no place in the picture.
	bool has_video = true;
	// Placed by the linked scene's automatic layout, and whether the user moved it since.
	bool linked = false;
	bool manual = false;
};

// A main (horizontal) scene, and the vertical scene linked to it if there is one.
struct MainSceneChoice {
	std::string uuid;
	std::string name;
	std::optional<std::string> vertical_scene;
};

// The link of the vertical scene on the canvas to its main scene.
struct ActiveLink {
	std::string main_name;
	TwinLayout layout = TwinLayout::CameraTop;
	double focus = 0.5;
	// Imported items placed by the automatic layout; none when the scene only follows.
	bool automatic = false;
};

class VerticalCanvas final : public VideoSources {
public:
	explicit VerticalCanvas(std::filesystem::path settings_path);
	~VerticalCanvas() override;
	VerticalCanvas(const VerticalCanvas &) = delete;
	VerticalCanvas &operator=(const VerticalCanvas &) = delete;

	video_t *video_for(VideoSource source) override;
	std::shared_ptr<void> hold(VideoSource source) override;

	// Finds or creates the canvas and its first scene. Returns whether it is available.
	bool attach();
	void detach();
	bool available() const { return static_cast<bool>(canvas_); }
	// The canvas uuid OBS stores as the Enhanced Broadcasting additional canvas; empty without one.
	std::string canvas_uuid() const;

	// After a profile change or any main video reset: main's frame rate and format may be
	// different now. Main video resets are also followed on their own, see video_reset.
	void refresh_video();

	// Whether the frontend removed this canvas, which then must be detached.
	bool removed() const;
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
	bool move_scene(const std::string &name, bool up);

	// The main canvas's video sources, which can be added to a scene.
	std::vector<std::string> main_sources() const;
	std::vector<InputType> creatable_input_types() const;

	// Top of the stack first, in the active scene.
	std::vector<VerticalItem> items() const;
	// These return the new item in the active scene.
	std::optional<std::int64_t> add_source(const std::string &name);
	std::optional<std::int64_t> create_source(const std::string &type_id, const std::string &name);
	bool source_name_taken(const std::string &name) const;
	bool open_item_properties(std::int64_t id);
	bool remove_item(std::int64_t id);
	enum class Move { Up, Down, Top, Bottom };
	bool move_item(std::int64_t id, Move move);
	bool set_item_visible(std::int64_t id, bool visible);
	bool fit_item(std::int64_t id);
	bool fill_item(std::int64_t id);
	bool set_item_locked(std::int64_t id, bool locked);
	bool center_item(std::int64_t id);
	bool reset_item_transform(std::int64_t id);

	// Linked scenes. A linked vertical scene shows the same sources as its main scene, placed
	// by the automatic layout, and follows it: items added, removed, shown, hidden or moved
	// there are mirrored here, except items the user moved here by hand. When the program
	// scene changes, the canvas switches to its linked vertical scene, if it has one. The
	// link lives in the scene's private settings, so it is saved with the collection.
	std::vector<MainSceneChoice> main_scene_choices() const;
	// Creates the linked vertical scene, fills it, and shows it. Returns its name.
	std::optional<std::string> import_main_scene(const std::string &main_uuid, TwinLayout layout);
	std::optional<ActiveLink> active_link() const;
	bool set_active_link_layout(TwinLayout layout, double focus);
	// Places the automatic items again; with include_manual, also the ones moved by hand.
	bool relayout_active(bool include_manual);
	// The scene and its items stay as they are, they just stop following the main scene.
	bool unlink_active();
	// Makes the active vertical scene follow a main scene without importing anything: it
	// comes on the canvas when that scene goes on air. A main scene has one linked vertical
	// scene, so another one that followed it stops following.
	bool link_active_to(const std::string &main_uuid);
	bool reset_item_to_auto(std::int64_t id);
	bool item_visibility_linked(std::int64_t id) const;
	bool set_item_visibility_linked(std::int64_t id, bool linked);
	// After the program scene changed.
	void follow_program();
	// After the main scene list changed: watches the main scenes that have a linked scene.
	void refresh_links();

	// Asks OBS to save the collection, which holds the scenes and their items.
	void save_collection() const;

	// For direct editing in the preview. The boxes show what is actually drawn: bounds and
	// alignment are already resolved, so they are the same whatever set them.
	CanvasSize canvas_size() const;
	std::vector<PreviewItem> preview_items() const;
	std::optional<PreviewItem> preview_item(std::int64_t id) const;
	bool contains_item(std::int64_t id) const { return static_cast<bool>(find_item(id)); }

	// Gives the item a plain top-left placement that shows the box, dropping any bounds:
	// the picture stays where it is, and from then on it behaves like any plain item.
	enum class BoxResult {
		Applied,
		// Nothing changed, for now: the item is locked or its source has no size yet.
		Skipped,
		Gone,
	};
	BoxResult set_item_box(std::int64_t id, const ItemBox &box);

private:
	// libobs restores extra canvases with their old video info when main video is reset
	// (Settings > Video), then emits the core "video_reset" signal.
	static void on_video_reset(void *data, calldata_t *);
	// Scenes and sources also change through OBS's own docks and dialogs, on any thread.
	// Bursts (a scene collection load, a group of items) become one queued notification.
	static void on_external_change(void *data, calldata_t *);

	void watch_canvas();
	// Follows the scene on the canvas's channel; reconnects only when it changed.
	void watch_active_scene();
	void unwatch();

	bool reset_video();
	void set_channel(obs_source_t *scene_source);
	void persist();
	void notify();
	OBSSceneItem find_item(std::int64_t id) const;
	void place_item(obs_sceneitem_t *item, obs_bounds_type bounds) const;
	void replace_bounds_of_all_items();

	// Linked scenes, see auto_layout.hpp.
	obs_scene_t *linked_scene_for(const std::string &main_uuid) const;
	obs_scene_t *active_scene_object() const;
	void place_linked(obs_sceneitem_t *item, obs_scene_t *main_scene, TwinLayout layout, double focus,
			  bool has_camera) const;
	void relayout_scene(obs_scene_t *scene, bool include_manual);
	void relayout_all_linked();
	void unlink_scene(obs_scene_t *scene);
	void mark_manual(obs_sceneitem_t *item) const;
	static void on_main_item_add(void *data, calldata_t *params);
	static void on_main_item_remove(void *data, calldata_t *params);
	static void on_main_item_visible(void *data, calldata_t *params);
	static void on_main_item_transform(void *data, calldata_t *params);
	void main_item_added(const std::string &main_uuid, std::int64_t id);
	void main_item_removed(const std::string &main_uuid, std::int64_t id);
	void main_item_visible(const std::string &main_uuid, std::int64_t id, bool visible);
	void queue_relayout(const std::string &main_uuid);
	std::optional<std::int64_t> add_to_active_scene(obs_source_t *source);

	std::filesystem::path settings_path_;
	VerticalSettings settings_;
	// The scene collection the scene state in settings_ belongs to; empty before attach().
	std::string collection_;
	OBSCanvas canvas_;
	std::shared_ptr<VerticalPreviewTarget> preview_ = std::make_shared<VerticalPreviewTarget>();
	std::function<void()> listener_;
	// The canvas's scene list and renames of the sources its items show.
	std::vector<OBSSignal> canvas_signals_;
	// The items of the scene on the channel.
	std::vector<OBSSignal> scene_signals_;
	// The main scenes that have a linked vertical scene.
	std::vector<OBSSignal> main_signals_;
	std::vector<std::string> relayout_queue_;
	OBSWeakSource watched_scene_;
	std::atomic<bool> change_queued_{false};
	// Receives queued work from libobs signals; destroying it drops what is still queued.
	std::unique_ptr<QObject> signal_context_;
};

} // namespace multistream
