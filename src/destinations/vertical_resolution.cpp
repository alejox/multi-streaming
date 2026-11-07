#include "destinations/vertical_resolution.hpp"

#include <cstdio>
#include <numeric>

namespace multistream {
namespace {

// Larger reduced terms are not a ratio anyone recognizes.
constexpr int kLargestSimpleTerm = 32;

} // namespace

const std::vector<ResolutionPreset> &vertical_resolution_presets()
{
	static const std::vector<ResolutionPreset> presets = {{1080, 1920}, {720, 1280}, {1440, 2560}};
	return presets;
}

std::optional<std::size_t> preset_index_for(int width, int height)
{
	const auto &presets = vertical_resolution_presets();
	for (std::size_t index = 0; index < presets.size(); ++index) {
		if (presets[index].width == width && presets[index].height == height) {
			return index;
		}
	}
	return std::nullopt;
}

ResolutionProblem check_vertical_resolution(int width, int height)
{
	if (width % 2 != 0 || height % 2 != 0) {
		return ResolutionProblem::NotEven;
	}
	if (!is_valid_vertical_resolution(width, height)) {
		return ResolutionProblem::OutOfRange;
	}
	return ResolutionProblem::None;
}

std::string aspect_ratio_text(int width, int height)
{
	if (width <= 0 || height <= 0) {
		return {};
	}
	const int divisor = std::gcd(width, height);
	const int across = width / divisor;
	const int down = height / divisor;
	if (across <= kLargestSimpleTerm && down <= kLargestSimpleTerm) {
		return std::to_string(across) + ":" + std::to_string(down);
	}
	char decimal[32];
	std::snprintf(decimal, sizeof(decimal), "%.2f:1", static_cast<double>(width) / height);
	return decimal;
}

} // namespace multistream
