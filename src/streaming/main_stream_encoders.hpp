// Reads what OBS's own stream is encoding with, and turns our encoder preferences into
// libobs encoder ids and settings. UI thread only, after OBS finished loading.
#pragma once

#include "destinations/encoder_plan.hpp"

#include <obs.hpp>

#include <optional>
#include <string>

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

} // namespace multistream
