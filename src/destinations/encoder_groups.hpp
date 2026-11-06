#pragma once

#include "destinations/destination.hpp"

#include <string>
#include <vector>

namespace multistream {

// Destinations that can use one video encoder. Share-mode groups carry a normalized
// encoder with no custom fields. The audio track does not split a group: audio
// encoders are per track and independent of video encoder sharing.
struct EncoderGroup {
	VideoSource video_source;
	EncoderPreference encoder;
	std::vector<std::string> destination_ids;
};

// Groups the given destinations in order of first appearance. The caller decides
// which destinations to pass, for example only the ones being started.
std::vector<EncoderGroup> group_by_encoder(const std::vector<Destination> &destinations);

} // namespace multistream
