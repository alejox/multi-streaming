// What the plugin itself remembers about the vertical canvas. The canvas and its scenes
// are saved by OBS in the scene collection; the resolution and the program scene are not.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace multistream {

struct VerticalSettings {
	int width = 1080;
	int height = 1920;
	std::string active_scene;
};

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

struct Viewport {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

// The largest centered rectangle of the base's aspect that fits the window.
Viewport fit_viewport(int base_width, int base_height, int window_width, int window_height);

} // namespace multistream
