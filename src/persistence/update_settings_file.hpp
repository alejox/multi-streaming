// The update settings file, replaced atomically on save with a backup of the previous
// version, which loading falls back to.
#pragma once

#include "destinations/message.hpp"
#include "update/update_settings.hpp"

#include <filesystem>
#include <optional>

namespace multistream {

// The backup when the file is missing or unusable, else defaults (updates on).
UpdateSettings load_update_settings(const std::filesystem::path &path);

// Returns what went wrong, if anything.
std::optional<Message> save_update_settings(const std::filesystem::path &path, const UpdateSettings &settings);

} // namespace multistream
