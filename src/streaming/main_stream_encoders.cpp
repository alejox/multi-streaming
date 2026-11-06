#include "streaming/main_stream_encoders.hpp"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <algorithm>
#include <cstring>

namespace multistream {
namespace {

constexpr int kDefaultVideoBitrateKbps = 2500;
constexpr int kAudioBitrateKbps = 160;
constexpr int kKeyframeIntervalSeconds = 2;

// libobs creates a placeholder rather than failing on an unknown id, so ids are checked first.
bool is_usable_encoder(const char *id, obs_encoder_type type)
{
	return id != nullptr && obs_get_encoder_codec(id) != nullptr && obs_get_encoder_type(id) == type;
}

OBSData new_data()
{
	OBSDataAutoRelease data = obs_data_create();
	return data.Get();
}

OBSData cbr_settings(int bitrate_kbps)
{
	OBSData settings = new_data();
	obs_data_set_int(settings, "bitrate", bitrate_kbps);
	obs_data_set_string(settings, "rate_control", "CBR");
	obs_data_set_int(settings, "keyint_sec", kKeyframeIntervalSeconds);
	return settings;
}

// The first H.264 encoder offered for general use, preferring VideoToolbox, then x264.
std::optional<std::string> default_video_encoder_id()
{
	std::vector<std::string> candidates;
	for (const VideoEncoderOption &option : available_video_encoders()) {
		if (std::strcmp(obs_get_encoder_codec(option.id.c_str()), "h264") == 0) {
			candidates.push_back(option.id);
		}
	}

	for (const char *fragment : {"videotoolbox", "x264"}) {
		const auto found = std::find_if(candidates.begin(), candidates.end(), [&](const std::string &candidate) {
			return candidate.find(fragment) != std::string::npos;
		});
		if (found != candidates.end()) {
			return *found;
		}
	}
	if (!candidates.empty()) {
		return candidates.front();
	}
	return std::nullopt;
}

OBSData profile_stream_encoder_settings()
{
	char *profile_path = obs_frontend_get_current_profile_path();
	if (profile_path == nullptr) {
		return nullptr;
	}
	const std::string path = std::string(profile_path) + "/streamEncoder.json";
	bfree(profile_path);

	OBSDataAutoRelease settings = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	return settings.Get();
}

std::optional<EncoderSpec> spec_from_profile()
{
	config_t *profile = obs_frontend_get_profile_config();
	const char *mode = profile != nullptr ? config_get_string(profile, "Output", "Mode") : nullptr;

	if (mode != nullptr && std::strcmp(mode, "Advanced") == 0) {
		const char *id = config_get_string(profile, "AdvOut", "Encoder");
		if (is_usable_encoder(id, OBS_ENCODER_VIDEO)) {
			OBSData settings = profile_stream_encoder_settings();
			return EncoderSpec{id, settings ? settings : cbr_settings(kDefaultVideoBitrateKbps)};
		}
	}

	const auto id = default_video_encoder_id();
	if (!id) {
		return std::nullopt;
	}
	const auto bitrate = profile != nullptr ? config_get_int(profile, "SimpleOutput", "VBitrate") : 0;
	return EncoderSpec{*id, cbr_settings(bitrate > 0 ? static_cast<int>(bitrate) : kDefaultVideoBitrateKbps)};
}

} // namespace

MainStreamSnapshot read_main_stream()
{
	MainStreamSnapshot snapshot;

	OBSOutputAutoRelease output = obs_frontend_get_streaming_output();
	if (!output) {
		return snapshot;
	}
	snapshot.video = obs_output_get_video_encoder(output);
	snapshot.audio = obs_output_get_audio_encoder(output, 0);

	// With multitrack video, the first encoder is just one rung of an ABR ladder.
	const bool multitrack = obs_output_get_video_encoder2(output, 1) != nullptr;
	snapshot.encoders.active = obs_output_active(output) && !multitrack && snapshot.video && snapshot.audio;
	if (snapshot.encoders.active) {
		OBSDataAutoRelease settings = obs_encoder_get_settings(snapshot.video);
		snapshot.encoders.video_encoder_id = obs_encoder_get_id(snapshot.video);
		snapshot.encoders.video_bitrate_kbps = static_cast<int>(obs_data_get_int(settings, "bitrate"));
		snapshot.encoders.video_scaled = obs_encoder_scaling_enabled(snapshot.video);
		snapshot.encoders.audio_track = static_cast<int>(obs_encoder_get_mixer_index(snapshot.audio)) + 1;
	}
	return snapshot;
}

std::optional<EncoderSpec> video_encoder_spec(const EncoderPreference &preference, obs_encoder_t *main_video)
{
	if (preference.mode == EncoderMode::Custom) {
		if (!is_usable_encoder(preference.video_encoder_id.c_str(), OBS_ENCODER_VIDEO)) {
			return std::nullopt;
		}
		return EncoderSpec{preference.video_encoder_id, cbr_settings(preference.video_bitrate_kbps)};
	}

	if (main_video != nullptr) {
		// A copy: libobs keeps the settings object it is given, and it must not alias main's.
		OBSDataAutoRelease main_settings = obs_encoder_get_settings(main_video);
		OBSData settings = new_data();
		obs_data_apply(settings, main_settings);
		return EncoderSpec{obs_encoder_get_id(main_video), settings};
	}
	return spec_from_profile();
}

std::vector<VideoEncoderOption> available_video_encoders()
{
	std::vector<VideoEncoderOption> options;
	const char *id = nullptr;
	for (size_t index = 0; obs_enum_encoder_types(index, &id); ++index) {
		const char *codec = obs_get_encoder_codec(id);
		const bool hidden = (obs_get_encoder_caps(id) & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)) != 0;
		if (hidden || obs_get_encoder_type(id) != OBS_ENCODER_VIDEO || codec == nullptr) {
			continue;
		}
		if (std::strcmp(codec, "h264") == 0 || std::strcmp(codec, "hevc") == 0 || std::strcmp(codec, "av1") == 0) {
			const char *display_name = obs_encoder_get_display_name(id);
			options.push_back({id, display_name != nullptr ? display_name : id});
		}
	}
	return options;
}

std::optional<EncoderSpec> audio_encoder_spec()
{
	for (const char *id : {"CoreAudio_AAC", "libfdk_aac", "ffmpeg_aac"}) {
		if (is_usable_encoder(id, OBS_ENCODER_AUDIO)) {
			OBSData settings = new_data();
			obs_data_set_int(settings, "bitrate", kAudioBitrateKbps);
			return EncoderSpec{id, settings};
		}
	}
	return std::nullopt;
}

} // namespace multistream
