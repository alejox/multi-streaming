#include "destinations/runtime_requirements.hpp"

namespace multistream {

bool is_runtime_supported(std::uint32_t obs_version)
{
	return obs_version >= make_obs_version(33, 0, 0);
}

} // namespace multistream
