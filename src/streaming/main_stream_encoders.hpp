// Reads what OBS's own stream is encoding with, and turns our encoder preferences into
// libobs encoder ids and settings. UI thread only, after OBS finished loading.
#pragma once

#include "destinations/encoder_plan.hpp"

#include <obs.hpp>

#include <optional>
#include <string>
#include <vector>

namespace multistream {

struct MainStreamSnapshot {
	MainStreamEncoders encoders;
	OBSEncoder video;
	OBSEncoder audio;
};

MainStreamSnapshot read_main_stream();

struct EncoderSpec {
	std::string id;
	OBSData settings;
};

// Nothing means the requested encoder does not exist in this OBS. A Share destination
// gets a dedicated encoder configured like OBS's own stream, from the frontend's
// encoder when one exists and otherwise from the current profile.
std::optional<EncoderSpec> video_encoder_spec(const EncoderPreference &preference, obs_encoder_t *main_video);
std::optional<EncoderSpec> audio_encoder_spec();

// Gives a copy of main's video encoder the same output rescale, which libobs keeps on the
// encoder rather than in its settings. Call after obs_encoder_set_video, before first use.
void copy_main_scaling(obs_encoder_t *copy, obs_encoder_t *main_video);

struct VideoEncoderOption {
	std::string id;
	std::string display_name;
};

// The video encoders a destination can pick: general use, with a codec RTMP can carry.
std::vector<VideoEncoderOption> available_video_encoders();

} // namespace multistream
