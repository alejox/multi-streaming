#pragma once

#include "destinations/destination.hpp"

#include <optional>
#include <string>
#include <vector>

namespace multistream {

// What the active main stream is encoding with, as far as sharing is concerned.
struct MainStreamEncoders {
	bool active = false;
	std::string video_encoder_id;
	int video_bitrate_kbps = 0;
	bool video_scaled = false;
	std::optional<int> audio_track; // 1-based, like Destination::audio_track.
};

// How one destination gets its encoders. Equal keys mean one shared encoder.
struct EncoderAssignment {
	std::string destination_id;
	VideoSource video_source = VideoSource::Main;
	EncoderPreference video_encoder;
	std::string video_key;
	bool reuse_main_video = false;
	int audio_track = 1;
	std::string audio_key;
	bool reuse_main_audio = false;
};

// Plans encoders for the destinations being started, in their given order. Video
// follows group_by_encoder; audio is shared per track.
//
// The main stream's encoders are reused only while that stream is active, only by a
// destination that follows it (and so stops with it), and for video only on the main
// mix: a Share destination asks for whatever main uses, and a Custom one
// must match its encoder id and bitrate on an unscaled encoder. Otherwise the plan
// asks for a dedicated encoder. Reused encoders get keys apart from dedicated ones.
std::vector<EncoderAssignment> plan_encoders(const std::vector<Destination> &destinations,
					     const MainStreamEncoders &main_stream);

} // namespace multistream
