#include "vertical_canvas/source_icons.hpp"

#include <array>

namespace multistream {
namespace {

struct TypeIcon {
	std::string_view type_id;
	SourceIcon icon;
};

// The ids that OBS 33.0.1's bundled plugins register, on any platform.
constexpr std::array<TypeIcon, 34> kTypeIcons = {{
	{"av_capture_input", SourceIcon::Camera},
	{"macos-avcapture", SourceIcon::Camera},
	{"macos-avcapture-fast", SourceIcon::Camera},
	{"dshow_input", SourceIcon::Camera},
	{"v4l2_input", SourceIcon::Camera},
	{"decklink-input", SourceIcon::Camera},
	{"screen_capture", SourceIcon::Display},
	{"display_capture", SourceIcon::Display},
	{"monitor_capture", SourceIcon::Display},
	{"xshm_input", SourceIcon::Display},
	{"window_capture", SourceIcon::Window},
	{"xcomposite_input", SourceIcon::Window},
	{"game_capture", SourceIcon::Game},
	{"syphon-input", SourceIcon::Game},
	{"image_source", SourceIcon::Image},
	{"slideshow", SourceIcon::Image},
	{"ffmpeg_source", SourceIcon::Media},
	{"vlc_source", SourceIcon::Media},
	{"text_ft2_source", SourceIcon::Text},
	{"text_gdiplus", SourceIcon::Text},
	{"browser_source", SourceIcon::Browser},
	{"color_source", SourceIcon::Color},
	{"coreaudio_input_capture", SourceIcon::Audio},
	{"coreaudio_output_capture", SourceIcon::Audio},
	{"sck_audio_capture", SourceIcon::Audio},
	{"wasapi_input_capture", SourceIcon::Audio},
	{"wasapi_output_capture", SourceIcon::Audio},
	{"wasapi_process_output_capture", SourceIcon::Audio},
	{"pulse_input_capture", SourceIcon::Audio},
	{"pulse_output_capture", SourceIcon::Audio},
	{"alsa_input_capture", SourceIcon::Audio},
	{"jack_output_capture", SourceIcon::Audio},
	{"group", SourceIcon::Group},
	{"scene", SourceIcon::Scene},
}};

} // namespace

SourceIcon source_icon_for_type(std::string_view type_id)
{
	for (const TypeIcon &known : kTypeIcons) {
		if (known.type_id == type_id) {
			return known.icon;
		}
	}
	return SourceIcon::Fallback;
}

const char *source_icon_file(SourceIcon icon)
{
	switch (icon) {
	case SourceIcon::Camera:
		return "icons/sources/camera.svg";
	case SourceIcon::Display:
		return "icons/sources/display.svg";
	case SourceIcon::Window:
		return "icons/sources/window.svg";
	case SourceIcon::Game:
		return "icons/sources/game.svg";
	case SourceIcon::Image:
		return "icons/sources/image.svg";
	case SourceIcon::Media:
		return "icons/sources/media.svg";
	case SourceIcon::Text:
		return "icons/sources/text.svg";
	case SourceIcon::Browser:
		return "icons/sources/browser.svg";
	case SourceIcon::Color:
		return "icons/sources/color.svg";
	case SourceIcon::Audio:
		return "icons/sources/audio.svg";
	case SourceIcon::Group:
		return "icons/sources/group.svg";
	case SourceIcon::Scene:
		return "icons/sources/scene.svg";
	case SourceIcon::Fallback:
		return "icons/sources/fallback.svg";
	}
	return "icons/sources/fallback.svg";
}

} // namespace multistream
