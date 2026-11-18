#include "destinations/output_resolution.hpp"

#include <algorithm>
#include <cmath>

namespace multistream {

const std::vector<int> &output_short_side_presets()
{
	static const std::vector<int> presets = {2160, 1440, 1080, 720};
	return presets;
}

bool is_valid_output_short_side(int short_side)
{
	return short_side == kCanvasOutput ||
	       (short_side >= kMinOutputShortSide && short_side <= kMaxOutputShortSide && short_side % 2 == 0);
}

std::optional<OutputSize> scaled_output_size(int canvas_width, int canvas_height, int short_side)
{
	if (short_side == kCanvasOutput || canvas_width <= 0 || canvas_height <= 0 ||
	    !is_valid_output_short_side(short_side)) {
		return std::nullopt;
	}
	const bool landscape = canvas_width >= canvas_height;
	const int canvas_short = landscape ? canvas_height : canvas_width;
	const int canvas_long = landscape ? canvas_width : canvas_height;
	if (short_side == canvas_short) {
		return std::nullopt;
	}
	const double exact_long = static_cast<double>(canvas_long) * short_side / canvas_short;
	const int long_side = std::max(2, static_cast<int>(std::lround(exact_long / 2.0)) * 2);
	return landscape ? OutputSize{long_side, short_side} : OutputSize{short_side, long_side};
}

} // namespace multistream
