// The destination model. A stream key is deliberately not a field here: it lives
// only behind SecretStore, so it cannot reach the config file through this type.
#pragma once

#include "destinations/platform.hpp"
#include "destinations/reconnect_policy.hpp"

#include <optional>
#include <string>
#include <vector>

namespace multistream {

enum class VideoSource { Main, Vertical };

enum class EncoderMode { Share, Custom };

// video_bitrate_kbps, video_encoder_id and output_short_side apply to Custom. They are
// kept under Share so that switching modes in the editor does not lose them.
struct EncoderPreference {
	EncoderMode mode = EncoderMode::Share;
	int video_bitrate_kbps = 0;
	std::string video_encoder_id;
	// The output resolution by its short side (1080 = 1080p); 0 sends the canvas size.
	// See output_resolution.hpp.
	int output_short_side = 0;

	bool operator==(const EncoderPreference &other) const
	{
		return mode == other.mode && video_bitrate_kbps == other.video_bitrate_kbps &&
		       video_encoder_id == other.video_encoder_id && output_short_side == other.output_short_side;
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
	// Nothing means nobody picked one: the URL decides.
	std::optional<Platform> platform;

	bool operator==(const Destination &other) const
	{
		return id == other.id && name == other.name && server_url == other.server_url &&
		       enabled == other.enabled && video_source == other.video_source && encoder == other.encoder &&
		       audio_track == other.audio_track && sync_with_main == other.sync_with_main &&
		       reconnect == other.reconnect && platform == other.platform;
	}
};

struct DestinationsConfig {
	std::vector<Destination> destinations;
	// Show platform icons in their brand colors instead of the theme's monochrome.
	bool color_icons = false;

	bool operator==(const DestinationsConfig &other) const
	{
		return destinations == other.destinations && color_icons == other.color_icons;
	}
};

// Returns a new random id: 32 lowercase hexadecimal characters.
std::string generate_destination_id();

} // namespace multistream
