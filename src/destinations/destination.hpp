// The destination model. A stream key is deliberately not a field here: it lives
// only behind SecretStore, so it cannot reach the config file through this type.
#pragma once

#include "destinations/reconnect_policy.hpp"

#include <string>
#include <vector>

namespace multistream {

enum class VideoSource { Main, Vertical };

enum class EncoderMode { Share, Custom };

// video_bitrate_kbps and video_encoder_id apply to Custom. They are kept under
// Share so that switching modes in the editor does not lose them.
struct EncoderPreference {
	EncoderMode mode = EncoderMode::Share;
	int video_bitrate_kbps = 0;
	std::string video_encoder_id;

	bool operator==(const EncoderPreference &other) const
	{
		return mode == other.mode && video_bitrate_kbps == other.video_bitrate_kbps &&
		       video_encoder_id == other.video_encoder_id;
	}
};

struct Destination {
	std::string id;
	std::string name;
	std::string server_url;
	bool enabled = true;
	VideoSource video_source = VideoSource::Main;
	EncoderPreference encoder;
	int audio_track = 1;
	bool sync_with_main = false;
	ReconnectPolicy reconnect;

	bool operator==(const Destination &other) const
	{
		return id == other.id && name == other.name && server_url == other.server_url &&
		       enabled == other.enabled && video_source == other.video_source && encoder == other.encoder &&
		       audio_track == other.audio_track && sync_with_main == other.sync_with_main &&
		       reconnect == other.reconnect;
	}
};

struct DestinationsConfig {
	std::vector<Destination> destinations;

	bool operator==(const DestinationsConfig &other) const { return destinations == other.destinations; }
};

// Returns a new random id: 32 lowercase hexadecimal characters.
std::string generate_destination_id();

} // namespace multistream
