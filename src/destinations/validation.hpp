#pragma once

#include "destinations/destination.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace multistream {

enum class DestinationField { Id, Name, ServerUrl, VideoBitrate, VideoEncoder, AudioTrack };

struct ValidationIssue {
	DestinationField field;
	std::string message;
};

// Returns why the URL is unusable, or nothing when it is valid. The scheme must be
// rtmp or rtmps. Userinfo, query, and fragment are rejected because the URL is
// stored in plain JSON and must never carry a credential; the key has its own field.
std::optional<std::string> validate_server_url(std::string_view url);

std::vector<ValidationIssue> validate_destination(const Destination &destination);

} // namespace multistream
