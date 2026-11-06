#include "vertical_canvas/source_icons.hpp"

#include "test_harness.hpp"

#include <string>

using namespace multistream;

TEST_CASE(macos_source_types_get_their_icons)
{
	CHECK(source_icon_for_type("av_capture_input") == SourceIcon::Camera);
	CHECK(source_icon_for_type("macos-avcapture") == SourceIcon::Camera);
	CHECK(source_icon_for_type("macos-avcapture-fast") == SourceIcon::Camera);
	CHECK(source_icon_for_type("screen_capture") == SourceIcon::Display);
	CHECK(source_icon_for_type("display_capture") == SourceIcon::Display);
	CHECK(source_icon_for_type("window_capture") == SourceIcon::Window);
	CHECK(source_icon_for_type("syphon-input") == SourceIcon::Game);
	CHECK(source_icon_for_type("image_source") == SourceIcon::Image);
	CHECK(source_icon_for_type("slideshow") == SourceIcon::Image);
	CHECK(source_icon_for_type("ffmpeg_source") == SourceIcon::Media);
	CHECK(source_icon_for_type("vlc_source") == SourceIcon::Media);
	CHECK(source_icon_for_type("text_ft2_source") == SourceIcon::Text);
	CHECK(source_icon_for_type("browser_source") == SourceIcon::Browser);
	CHECK(source_icon_for_type("color_source") == SourceIcon::Color);
	CHECK(source_icon_for_type("coreaudio_input_capture") == SourceIcon::Audio);
	CHECK(source_icon_for_type("coreaudio_output_capture") == SourceIcon::Audio);
	CHECK(source_icon_for_type("sck_audio_capture") == SourceIcon::Audio);
	CHECK(source_icon_for_type("group") == SourceIcon::Group);
	CHECK(source_icon_for_type("scene") == SourceIcon::Scene);
}

TEST_CASE(other_platforms_source_types_get_their_icons)
{
	CHECK(source_icon_for_type("dshow_input") == SourceIcon::Camera);
	CHECK(source_icon_for_type("v4l2_input") == SourceIcon::Camera);
	CHECK(source_icon_for_type("decklink-input") == SourceIcon::Camera);
	CHECK(source_icon_for_type("monitor_capture") == SourceIcon::Display);
	CHECK(source_icon_for_type("xshm_input") == SourceIcon::Display);
	CHECK(source_icon_for_type("xcomposite_input") == SourceIcon::Window);
	CHECK(source_icon_for_type("game_capture") == SourceIcon::Game);
	CHECK(source_icon_for_type("text_gdiplus") == SourceIcon::Text);
	CHECK(source_icon_for_type("wasapi_input_capture") == SourceIcon::Audio);
	CHECK(source_icon_for_type("wasapi_process_output_capture") == SourceIcon::Audio);
	CHECK(source_icon_for_type("pulse_input_capture") == SourceIcon::Audio);
}

TEST_CASE(unknown_source_types_get_the_fallback)
{
	CHECK(source_icon_for_type("") == SourceIcon::Fallback);
	CHECK(source_icon_for_type("some_plugin_source") == SourceIcon::Fallback);
}

TEST_CASE(every_icon_has_its_own_file)
{
	const SourceIcon all[] = {SourceIcon::Camera, SourceIcon::Display, SourceIcon::Window,  SourceIcon::Game,
				  SourceIcon::Image,  SourceIcon::Media,   SourceIcon::Text,    SourceIcon::Browser,
				  SourceIcon::Color,  SourceIcon::Audio,   SourceIcon::Group,   SourceIcon::Scene,
				  SourceIcon::Fallback};
	for (const SourceIcon first : all) {
		CHECK(std::string(source_icon_file(first)).rfind("icons/sources/", 0) == 0);
		for (const SourceIcon second : all) {
			if (first != second) {
				CHECK(std::string(source_icon_file(first)) != std::string(source_icon_file(second)));
			}
		}
	}
}
