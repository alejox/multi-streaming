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

// OBS reconfigures or replaces its encoders once its stream stops, so only a destination
// that stops with that stream may hold them.
bool can_reuse_main(const Destination &destination, const MainStreamEncoders &main_stream)
{
	return destination.sync_with_main && main_stream.active;
}

bool can_reuse_main_video(const Destination &destination, const EncoderGroup &group,
			  const MainStreamEncoders &main_stream)
{
	if (!can_reuse_main(destination, main_stream) || group.video_source != VideoSource::Main) {
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
		// A running encoder is joined by key, so main's encoders get keys of their own: an
		// independent destination must never join one, nor a follower join a copy.
		assignment.reuse_main_video = can_reuse_main_video(destination, *group, main_stream);
		assignment.video_key = assignment.reuse_main_video ? "video-obs-main" : video_key_for(*group);
		assignment.audio_track = destination.audio_track;
		assignment.reuse_main_audio = can_reuse_main(destination, main_stream) &&
					      main_stream.audio_track == destination.audio_track;
		const std::string track = std::to_string(destination.audio_track);
		assignment.audio_key = (assignment.reuse_main_audio ? "audio-obs-main-track-" : "audio-track-") + track;
		plan.push_back(std::move(assignment));
	}
	return plan;
}

} // namespace multistream
