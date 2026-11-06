// Runtime OBS version requirements. The Canvas API this plugin depends on is
// officially unstable, so the build is pinned to 33.0.1 and runtimes older than
// 33.0 are refused.
#pragma once

#include <cstdint>

namespace multistream {

// Packs a version like libobs' MAKE_SEMANTIC_VERSION, as returned by obs_get_version().
constexpr std::uint32_t make_obs_version(std::uint32_t major, std::uint32_t minor, std::uint32_t patch)
{
	return (major << 24) | (minor << 16) | patch;
}

bool is_runtime_supported(std::uint32_t obs_version);

} // namespace multistream
