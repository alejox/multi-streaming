#include "destinations/encoder_groups.hpp"

#include <algorithm>

namespace multistream {

std::vector<EncoderGroup> group_by_encoder(const std::vector<Destination> &destinations)
{
	std::vector<EncoderGroup> groups;

	for (const Destination &destination : destinations) {
		const EncoderPreference encoder = destination.encoder.mode == EncoderMode::Share ? EncoderPreference{}
												 : destination.encoder;

		const auto existing = std::find_if(groups.begin(), groups.end(), [&](const EncoderGroup &group) {
			return group.video_source == destination.video_source && group.encoder == encoder;
		});

		if (existing != groups.end()) {
			existing->destination_ids.push_back(destination.id);
		} else {
			groups.push_back({destination.video_source, encoder, {destination.id}});
		}
	}

	return groups;
}

} // namespace multistream
