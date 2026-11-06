#include "destinations/encoder_plan.hpp"

#include "destinations/encoder_groups.hpp"

#include <algorithm>

namespace multistream {
namespace {

std::string video_key_for(const EncoderGroup &group)
{
	std::string key = group.video_source == VideoSource::Vertical ? "video-vertical" : "video-main";
	if (group.encoder.mode == EncoderMode::Custom) {
		key += "-" + group.encoder.video_encoder_id + "-" + std::to_string(group.encoder.video_bitrate_kbps);
	} else {
		key += "-share";
	}
	return key;
}

bool can_reuse_main_video(const EncoderGroup &group, const MainStreamEncoders &main_stream)
{
	if (!main_stream.active || group.video_source != VideoSource::Main) {
		return false;
	}
	if (group.encoder.mode == EncoderMode::Share) {
		return true;
	}
	return !main_stream.video_scaled && group.encoder.video_encoder_id == main_stream.video_encoder_id &&
	       group.encoder.video_bitrate_kbps == main_stream.video_bitrate_kbps;
}

} // namespace

std::vector<EncoderAssignment> plan_encoders(const std::vector<Destination> &destinations,
					     const MainStreamEncoders &main_stream)
{
	const std::vector<EncoderGroup> groups = group_by_encoder(destinations);

	std::vector<EncoderAssignment> plan;
	plan.reserve(destinations.size());
	for (const Destination &destination : destinations) {
		const auto group = std::find_if(groups.begin(), groups.end(), [&](const EncoderGroup &candidate) {
			const auto &ids = candidate.destination_ids;
			return std::find(ids.begin(), ids.end(), destination.id) != ids.end();
		});

		EncoderAssignment assignment;
		assignment.destination_id = destination.id;
		assignment.video_source = group->video_source;
		assignment.video_encoder = group->encoder;
		assignment.video_key = video_key_for(*group);
		assignment.reuse_main_video = can_reuse_main_video(*group, main_stream);
		assignment.audio_track = destination.audio_track;
		assignment.audio_key = "audio-track-" + std::to_string(destination.audio_track);
		assignment.reuse_main_audio = main_stream.active && main_stream.audio_track == destination.audio_track;
		plan.push_back(std::move(assignment));
	}
	return plan;
}

} // namespace multistream
