// A destination's output resolution, chosen by its short side: 1080 means 1920x1080 on a
// 16:9 canvas and 1080x1920 on a 9:16 one. Services cap what they take in (Twitch and Kick
// at 1080p, YouTube up to 4K), so one canvas can feed each at the size it accepts.
#pragma once

#include <optional>
#include <vector>

namespace multistream {

// 0 means "the canvas size, unscaled".
inline constexpr int kCanvasOutput = 0;
inline constexpr int kMinOutputShortSide = 144;
inline constexpr int kMaxOutputShortSide = 4320;

struct OutputSize {
	int width = 0;
	int height = 0;

	bool operator==(const OutputSize &other) const { return width == other.width && height == other.height; }
};

// The short sides the editor offers, largest first.
const std::vector<int> &output_short_side_presets();

bool is_valid_output_short_side(int short_side);

// The size to scale a canvas of canvas_width x canvas_height to, keeping its aspect, with
// even sides as encoders need. Nothing when no scaling is asked for or it would not change
// the size.
std::optional<OutputSize> scaled_output_size(int canvas_width, int canvas_height, int short_side);

} // namespace multistream
