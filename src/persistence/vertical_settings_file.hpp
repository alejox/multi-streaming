// The vertical canvas settings file, replaced atomically on save with a backup of the
// previous version, which loading falls back to.
#pragma once

#include "destinations/vertical_settings.hpp"

#include <filesystem>

namespace multistream {

// The backup when the file is missing or unusable, else defaults: nothing here is worth
// stopping for.
VerticalSettings load_vertical_settings(const std::filesystem::path &path);

// Returns a description of the problem, if any.
std::optional<std::string> save_vertical_settings(const std::filesystem::path &path, const VerticalSettings &settings);

} // namespace multistream
