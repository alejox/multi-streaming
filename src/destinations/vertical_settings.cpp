#include "destinations/vertical_settings.hpp"

#include <json.hpp>

#include <algorithm>

namespace multistream {
namespace {

using nlohmann::json;

constexpr int kMaximumSide = 8192;

int resolution_field(const json &document, const char *name, int fallback)
{
	const auto found = document.find(name);
	return found != document.end() && found->is_number_integer() ? found->get<int>() : fallback;
}

std::vector<std::string> string_list(const json &document, const char *name)
{
	std::vector<std::string> result;
	const auto list = document.find(name);
	if (list != document.end() && list->is_array()) {
		for (const json &item : *list) {
			if (item.is_string()) {
				result.push_back(item.get<std::string>());
			}
		}
	}
	return result;
}

std::string string_field(const json &document, const char *name)
{
	const auto found = document.find(name);
	return found != document.end() && found->is_string() ? found->get<std::string>() : std::string();
}

} // namespace

bool is_valid_vertical_resolution(int width, int height)
{
	const auto valid_side = [](int side) {
		return side > 0 && side <= kMaximumSide && side % 2 == 0;
	};
	return valid_side(width) && valid_side(height);
}

ResolutionChange decide_resolution_change(const VerticalSettings &current, int width, int height, bool vertical_live)
{
	if (!is_valid_vertical_resolution(width, height)) {
		return ResolutionChange::Invalid;
	}
	if (width == current.width && height == current.height) {
		return ResolutionChange::Unchanged;
	}
	return vertical_live ? ResolutionChange::RefusedWhileLive : ResolutionChange::Apply;
}

std::string encode_vertical_settings(const VerticalSettings &settings)
{
	json collections = json::object();
	for (const auto &[name, state] : settings.collections) {
		collections[name] = {{"active_scene", state.active_scene}, {"scene_order", state.scene_order}};
	}
	// The top-level scene fields keep the file readable by versions without collections.
	const json document = {
		{"width", settings.width},
		{"height", settings.height},
		{"active_scene", settings.active_scene},
		{"scene_order", settings.scene_order},
		{"collections", collections},
	};
	return document.dump(2);
}

std::optional<VerticalSettings> decode_vertical_settings(const std::string &text)
{
	const json document = json::parse(text, nullptr, false);
	if (!document.is_object()) {
		return std::nullopt;
	}

	VerticalSettings settings;
	const int width = resolution_field(document, "width", settings.width);
	const int height = resolution_field(document, "height", settings.height);
	if (is_valid_vertical_resolution(width, height)) {
		settings.width = width;
		settings.height = height;
	}

	settings.active_scene = string_field(document, "active_scene");
	settings.scene_order = string_list(document, "scene_order");

	const auto collections = document.find("collections");
	if (collections != document.end() && collections->is_object()) {
		for (const auto &[name, entry] : collections->items()) {
			if (entry.is_object()) {
				settings.collections[name] = {string_field(entry, "active_scene"),
							      string_list(entry, "scene_order")};
			}
		}
	}
	return settings;
}

VerticalSettings select_collection(VerticalSettings settings, const std::string &collection)
{
	const auto known = settings.collections.find(collection);
	if (known != settings.collections.end()) {
		settings.active_scene = known->second.active_scene;
		settings.scene_order = known->second.scene_order;
	} else if (!settings.collections.empty()) {
		settings.active_scene.clear();
		settings.scene_order.clear();
	}
	remember_collection(settings, collection);
	return settings;
}

void remember_collection(VerticalSettings &settings, const std::string &collection)
{
	if (!collection.empty()) {
		settings.collections[collection] = {settings.active_scene, settings.scene_order};
	}
}

std::optional<std::string> pick_active_scene(const std::vector<std::string> &scenes, const std::string &wanted)
{
	if (scenes.empty()) {
		return std::nullopt;
	}
	return std::find(scenes.begin(), scenes.end(), wanted) != scenes.end() ? wanted : scenes.front();
}

bool can_remove_scene(std::size_t scene_count)
{
	return scene_count > 1;
}

std::string unique_scene_name(const std::string &base, const std::vector<std::string> &existing)
{
	const auto taken = [&](const std::string &name) {
		return std::find(existing.begin(), existing.end(), name) != existing.end();
	};
	if (!taken(base)) {
		return base;
	}
	for (int number = 2;; ++number) {
		std::string name = base + " " + std::to_string(number);
		if (!taken(name)) {
			return name;
		}
	}
}

std::vector<std::string> order_scenes(const std::vector<std::string> &scenes, const std::vector<std::string> &order)
{
	std::vector<std::string> result;
	for (const std::string &name : order) {
		if (std::find(scenes.begin(), scenes.end(), name) != scenes.end() &&
		    std::find(result.begin(), result.end(), name) == result.end()) {
			result.push_back(name);
		}
	}
	for (const std::string &name : scenes) {
		if (std::find(result.begin(), result.end(), name) == result.end()) {
			result.push_back(name);
		}
	}
	return result;
}

std::vector<std::string> move_scene(const std::vector<std::string> &ordered, const std::string &name, bool up)
{
	std::vector<std::string> result = ordered;
	const auto found = std::find(result.begin(), result.end(), name);
	if (found == result.end()) {
		return result;
	}
	const auto index = static_cast<std::size_t>(found - result.begin());
	if (up && index > 0) {
		std::swap(result[index], result[index - 1]);
	} else if (!up && index + 1 < result.size()) {
		std::swap(result[index], result[index + 1]);
	}
	return result;
}

Viewport fit_viewport(int base_width, int base_height, int window_width, int window_height)
{
	if (base_width <= 0 || base_height <= 0 || window_width <= 0 || window_height <= 0) {
		return {};
	}

	const double base_aspect = static_cast<double>(base_width) / base_height;
	const double window_aspect = static_cast<double>(window_width) / window_height;

	Viewport viewport;
	if (window_aspect > base_aspect) {
		viewport.height = window_height;
		viewport.width = static_cast<int>(window_height * base_aspect);
	} else {
		viewport.width = window_width;
		viewport.height = static_cast<int>(window_width / base_aspect);
	}
	viewport.x = (window_width - viewport.width) / 2;
	viewport.y = (window_height - viewport.height) / 2;
	return viewport;
}

} // namespace multistream
