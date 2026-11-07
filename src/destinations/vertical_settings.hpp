// What the plugin itself remembers about the vertical canvas. The canvas and its scenes
// are saved by OBS in the scene collection; the resolution and the program scene are not.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace multistream {

// The scenes belong to a scene collection, so what the plugin remembers about them does too.
struct VerticalSceneState {
	std::string active_scene;
	// libobs has no order for a canvas's scenes, so this list is the only record of it.
	std::vector<std::string> scene_order;
};

struct VerticalSettings {
	// Shared by every scene collection.
	int width = 1080;
	int height = 1920;
	// The scene state of the collection in use.
	std::string active_scene;
	std::vector<std::string> scene_order;
	// The scene state of every collection seen, by collection name.
	std::map<std::string, VerticalSceneState> collections;
};

// The settings with the scene state of the named collection in use, recorded under it. A
// file from before collections were tracked has its scene state taken by the first
// collection; once any collection is known, an unknown one starts empty.
VerticalSettings select_collection(VerticalSettings settings, const std::string &collection);

// Records the scene state in use under the named collection. An empty name records nothing.
void remember_collection(VerticalSettings &settings, const std::string &collection);

// Even sizes are what the 4:2:0 encoders need; 8192 is a practical upper limit.
bool is_valid_vertical_resolution(int width, int height);

enum class ResolutionChange { Apply, Unchanged, Invalid, RefusedWhileLive };

ResolutionChange decide_resolution_change(const VerticalSettings &current, int width, int height, bool vertical_live);

std::string encode_vertical_settings(const VerticalSettings &settings);

// Nothing means the text cannot be used. Invalid fields fall back to their defaults.
std::optional<VerticalSettings> decode_vertical_settings(const std::string &text);

// The wanted scene if it exists, else the first, else nothing.
std::optional<std::string> pick_active_scene(const std::vector<std::string> &scenes, const std::string &wanted);

// At least one scene always exists.
bool can_remove_scene(std::size_t scene_count);

// The base name if it is free, else the base name with the lowest free number from 2.
std::string unique_scene_name(const std::string &base, const std::vector<std::string> &existing);

// The scenes in the wanted order. Names not in it follow, and names gone from it are dropped.
std::vector<std::string> order_scenes(const std::vector<std::string> &scenes, const std::vector<std::string> &order);

// Swaps the scene with its neighbor. Nothing changes at either end or for an unknown name.
std::vector<std::string> move_scene(const std::vector<std::string> &ordered, const std::string &name, bool up);

struct Viewport {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

// The largest centered rectangle of the base's aspect that fits the window.
Viewport fit_viewport(int base_width, int base_height, int window_width, int window_height);

} // namespace multistream
