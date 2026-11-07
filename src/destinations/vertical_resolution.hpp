// What the resolution dialog offers and checks while the user types: the common vertical
// sizes, why a size cannot be used, and its aspect ratio.
#pragma once

#include "destinations/vertical_settings.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace multistream {

struct ResolutionPreset {
	int width = 0;
	int height = 0;
};

// 1080 x 1920 first: the default, and what Shorts, Reels and TikTok expect.
const std::vector<ResolutionPreset> &vertical_resolution_presets();

// The preset with exactly this size, or nothing for a custom size.
std::optional<std::size_t> preset_index_for(int width, int height);

enum class ResolutionProblem { None, NotEven, OutOfRange };

// Agrees with is_valid_vertical_resolution. An odd side is reported first, since that is
// the mistake a typed size usually has.
ResolutionProblem check_vertical_resolution(int width, int height);

// "9:16" when the reduced ratio is simple, else "0.56:1". Empty for a size without area.
std::string aspect_ratio_text(int width, int height);

} // namespace multistream
