// What the plugin remembers about updates, kept in its own small file.
#pragma once

#include <optional>
#include <string>

namespace multistream {

struct UpdateSettings {
	// Download new versions in the background and install them when OBS closes.
	bool auto_install = true;
};

std::string encode_update_settings(const UpdateSettings &settings);

// nullopt when the text is not a settings object; missing fields keep their defaults.
std::optional<UpdateSettings> decode_update_settings(const std::string &text);

} // namespace multistream
