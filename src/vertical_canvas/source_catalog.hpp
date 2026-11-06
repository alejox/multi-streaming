// What the Add source button offers. Audio-only types have no place in a video composition,
// because audio always comes from the main mix.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace multistream {

struct InputType {
	std::string id;
	std::string unversioned_id;
	std::string display_name;
	bool has_video = true;
	bool disabled = false;
	bool deprecated = false;
};

// Sorted by display name, ignoring case.
std::vector<InputType> creatable_video_types(std::vector<InputType> types);

// The names containing the query, ignoring case and surrounding spaces, sorted the same way.
std::vector<std::string> filter_names(const std::vector<std::string> &names, std::string_view query);

} // namespace multistream
