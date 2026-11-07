#include "destinations/platform.hpp"

#include "destinations/validation.hpp"

#include "test_harness.hpp"

using namespace multistream;

TEST_CASE(platform_ids_round_trip)
{
	for (const Platform platform : kAllPlatforms) {
		CHECK(platform_from_id(platform_id(platform)) == platform);
	}
	CHECK(!platform_from_id("myspace"));
	CHECK(!platform_from_id(""));
}

TEST_CASE(detects_services_by_host)
{
	CHECK(detect_platform("rtmps://a.rtmps.youtube.com:443/live2") == Platform::YouTube);
	CHECK(detect_platform("rtmp://a.rtmp.youtube.com/live2") == Platform::YouTube);
	CHECK(detect_platform("rtmps://rtmp-api.facebook.com:443/rtmp/") == Platform::Facebook);
	CHECK(detect_platform("rtmp://live.twitch.tv/app") == Platform::Twitch);
	CHECK(detect_platform("rtmp://ca.pscp.tv:80/x") == Platform::X);
	CHECK(detect_platform("rtmps://live-upload.instagram.com:443/rtmp/") == Platform::Instagram);
	CHECK(detect_platform("rtmp://push.tiktokcdn.com/game") == Platform::TikTok);
	CHECK(detect_platform("rtmps://ingest.kick.com/app") == Platform::Kick);
}

TEST_CASE(detection_ignores_case)
{
	CHECK(detect_platform("RTMPS://A.RTMPS.YOUTUBE.COM/live2") == Platform::YouTube);
}

TEST_CASE(twitch_regional_ingest_is_detected_but_shared_ivs_hosts_are_not)
{
	// OBS 33.0.1's service list has these for Twitch.
	CHECK(detect_platform("rtmp://aps30.contribute.live-video.net/app/") == Platform::Twitch);
	CHECK(detect_platform("rtmps://fa723fc1b171.global-contribute.live-video.net/app") == Platform::Custom);
	CHECK(detect_platform("rtmps://something.live-video.net/app") == Platform::Custom);
}

TEST_CASE(detection_matches_whole_host_labels)
{
	CHECK(detect_platform("rtmp://notyoutube.com/live2") == Platform::Custom);
	CHECK(detect_platform("rtmp://youtube.com.example.org/live2") == Platform::Custom);
	CHECK(detect_platform("rtmp://example.org/youtube.com") == Platform::Custom);
	CHECK(detect_platform("rtmp://max.com/app") == Platform::Custom);
}

TEST_CASE(detection_of_unusual_urls_is_custom)
{
	CHECK(detect_platform("") == Platform::Custom);
	CHECK(detect_platform("not a url") == Platform::Custom);
	CHECK(detect_platform("rtmp://127.0.0.1:1935/live") == Platform::Custom);
	CHECK(detect_platform("rtmp://[::1]:1935/live") == Platform::Custom);
}

TEST_CASE(a_stored_platform_beats_the_url)
{
	const char *const shared_host = "rtmps://fa723fc1b171.global-contribute.live-video.net/app";
	CHECK(effective_platform(Platform::Kick, shared_host) == Platform::Kick);
	CHECK(effective_platform(std::nullopt, shared_host) == Platform::Custom);
	CHECK(effective_platform(Platform::Custom, "rtmp://a.rtmp.youtube.com/live2") == Platform::Custom);
	CHECK(effective_platform(std::nullopt, "rtmp://a.rtmp.youtube.com/live2") == Platform::YouTube);
}

TEST_CASE(presets_exist_only_where_verified)
{
	CHECK_EQ(preset_ingest_url(Platform::YouTube).value_or(""), "rtmps://a.rtmps.youtube.com:443/live2");
	CHECK_EQ(preset_ingest_url(Platform::Facebook).value_or(""), "rtmps://rtmp-api.facebook.com:443/rtmp/");
	CHECK_EQ(preset_ingest_url(Platform::Twitch).value_or(""), "rtmp://live.twitch.tv/app");
	CHECK(!preset_ingest_url(Platform::Kick));
	CHECK(!preset_ingest_url(Platform::TikTok));
	CHECK(!preset_ingest_url(Platform::X));
	CHECK(!preset_ingest_url(Platform::Instagram));
	CHECK(!preset_ingest_url(Platform::Custom));
}

TEST_CASE(presets_pass_validation_and_detect_as_their_service)
{
	for (const Platform platform : kAllPlatforms) {
		if (const auto url = preset_ingest_url(platform)) {
			Destination destination;
			destination.id = "abc";
			destination.name = "name";
			destination.server_url = *url;
			CHECK(validate_destination(destination).empty());
			CHECK(detect_platform(*url) == platform);
		}
	}
}

TEST_CASE(picking_a_service_with_a_preset_fills_its_url)
{
	CHECK_EQ(url_after_picking(Platform::YouTube, "rtmp://old.example.com/app"),
		 "rtmps://a.rtmps.youtube.com:443/live2");
	CHECK_EQ(url_after_picking(Platform::Twitch, "rtmp://old.example.com/app"),
		 "rtmp://live.twitch.tv/app");
}

TEST_CASE(picking_a_service_without_a_preset_keeps_a_pasted_url)
{
	const std::string pasted = "rtmps://fa723fc1b171.global-contribute.live-video.net/app";
	CHECK_EQ(url_after_picking(Platform::Kick, pasted), pasted);
}

TEST_CASE(picking_a_service_without_a_preset_clears_a_leftover_preset)
{
	CHECK_EQ(url_after_picking(Platform::Kick, "rtmps://a.rtmps.youtube.com:443/live2"), "");
	CHECK_EQ(url_after_picking(Platform::Custom, "rtmps://rtmp-api.facebook.com:443/rtmp/"), "");
}

TEST_CASE(stream_key_pages_exist_only_for_stable_dashboards)
{
	CHECK_EQ(stream_key_page_url(Platform::YouTube).value_or(""),
		 "https://studio.youtube.com/channel/UC/livestreaming");
	CHECK_EQ(stream_key_page_url(Platform::Twitch).value_or(""), "https://dashboard.twitch.tv/settings/stream");
	CHECK_EQ(stream_key_page_url(Platform::Kick).value_or(""), "https://kick.com/dashboard/settings/stream");
	CHECK_EQ(stream_key_page_url(Platform::Facebook).value_or(""), "https://www.facebook.com/live/producer");
	CHECK(!stream_key_page_url(Platform::Custom));
	CHECK(!stream_key_page_url(Platform::TikTok));
	CHECK(!stream_key_page_url(Platform::X));
	CHECK(!stream_key_page_url(Platform::Instagram));
}

TEST_CASE(stream_key_pages_are_https_and_on_their_service)
{
	for (const Platform platform : kAllPlatforms) {
		if (const auto url = stream_key_page_url(platform)) {
			CHECK(url->rfind("https://", 0) == 0);
			CHECK(detect_platform(*url) == platform);
		}
	}
}
