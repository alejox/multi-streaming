#include "destinations/validation.hpp"

#include "test_harness.hpp"

#include <algorithm>
#include <set>

using namespace multistream;

namespace {

Destination valid_destination()
{
	Destination destination;
	destination.id = "abc-123";
	destination.name = "Twitch";
	destination.server_url = "rtmp://live.example.com/app";
	return destination;
}

bool has_issue(const std::vector<ValidationIssue> &issues, DestinationField field)
{
	return std::any_of(issues.begin(), issues.end(),
			   [field](const ValidationIssue &issue) { return issue.field == field; });
}

} // namespace

TEST_CASE(accepts_valid_server_urls)
{
	for (const char *url : {"rtmp://example.com/live", "rtmps://a.rtmp.youtube.com:443/live2", "RTMP://HOST/x",
				"rtmp://[::1]:1935/live", "rtmp://127.0.0.1:1935/live/", "rtmp://example.com",
				"rtmps://live-api-s.facebook.com:443/rtmp/", "rtmp://my_host.local/a/b"}) {
		if (validate_server_url(url).has_value()) {
			test_harness::record_failure(__FILE__, __LINE__, std::string("rejected ") + url);
		}
	}
}

TEST_CASE(rejects_invalid_server_urls)
{
	for (const char *url : {"", "http://example.com/live", "srt://example.com", "rtmp:/example.com", "rtmp://",
				"rtmp:///live", "example.com/live", "rtmp://exa mple.com/live",
				"rtmp://example.com/li ve", "rtmp://example.com/live\n", "rtmp://example.com:0/live",
				"rtmp://example.com:65536/live", "rtmp://example.com:abc/live",
				"rtmp://example.com:/live", "rtmp://[::1/live", "rtmp://[zz]/live",
				"rtmp://exa$mple.com/live"}) {
		if (!validate_server_url(url).has_value()) {
			test_harness::record_failure(__FILE__, __LINE__, std::string("accepted ") + url);
		}
	}
}

TEST_CASE(rejects_urls_that_could_carry_credentials_into_the_config)
{
	CHECK(validate_server_url("rtmp://user:secret@example.com/live").has_value());
	CHECK(validate_server_url("rtmp://example.com/live?key=secret").has_value());
	CHECK(validate_server_url("rtmp://example.com/live#secret").has_value());
}

TEST_CASE(valid_destination_has_no_issues)
{
	CHECK(validate_destination(valid_destination()).empty());
}

TEST_CASE(reports_invalid_identity_and_name)
{
	Destination destination = valid_destination();
	destination.id = "bad id\n";
	destination.name = "   ";
	const auto issues = validate_destination(destination);
	CHECK(has_issue(issues, DestinationField::Id));
	CHECK(has_issue(issues, DestinationField::Name));

	destination.id = "";
	CHECK(has_issue(validate_destination(destination), DestinationField::Id));
	destination.id = std::string(65, 'a');
	CHECK(has_issue(validate_destination(destination), DestinationField::Id));
}

TEST_CASE(reports_invalid_url_with_its_field)
{
	Destination destination = valid_destination();
	destination.server_url = "http://example.com";
	CHECK(has_issue(validate_destination(destination), DestinationField::ServerUrl));
}

TEST_CASE(audio_track_must_be_a_libobs_mix)
{
	Destination destination = valid_destination();
	destination.audio_track = 0;
	CHECK(has_issue(validate_destination(destination), DestinationField::AudioTrack));
	destination.audio_track = 7;
	CHECK(has_issue(validate_destination(destination), DestinationField::AudioTrack));
	destination.audio_track = 6;
	CHECK(validate_destination(destination).empty());
}

TEST_CASE(custom_encoder_requires_bitrate_and_encoder_id)
{
	Destination destination = valid_destination();
	destination.encoder.mode = EncoderMode::Custom;
	auto issues = validate_destination(destination);
	CHECK(has_issue(issues, DestinationField::VideoBitrate));
	CHECK(has_issue(issues, DestinationField::VideoEncoder));

	destination.encoder.video_bitrate_kbps = 6000;
	destination.encoder.video_encoder_id = "obs_x264";
	CHECK(validate_destination(destination).empty());

	destination.encoder.video_bitrate_kbps = 500001;
	CHECK(has_issue(validate_destination(destination), DestinationField::VideoBitrate));
}

TEST_CASE(shared_encoder_ignores_custom_fields)
{
	Destination destination = valid_destination();
	destination.encoder.mode = EncoderMode::Share;
	destination.encoder.video_bitrate_kbps = -1;
	CHECK(validate_destination(destination).empty());
}

TEST_CASE(generated_ids_are_valid_and_distinct)
{
	std::set<std::string> ids;
	for (int i = 0; i < 100; ++i) {
		Destination destination = valid_destination();
		destination.id = generate_destination_id();
		CHECK(validate_destination(destination).empty());
		ids.insert(destination.id);
	}
	CHECK_EQ(ids.size(), std::size_t{100});
}

TEST_CASE(url_problems_are_reported_as_locale_keys_without_echoing_the_url)
{
	const auto key_of = [](const char *url) {
		const auto problem = validate_server_url(url);
		return problem ? problem->key + (problem->args.empty() ? "" : " with args") : std::string("none");
	};
	CHECK_EQ(key_of("http://example.com/live"), std::string("Validation.UrlScheme"));
	CHECK_EQ(key_of("rtmp://example.com/live?key=secret"), std::string("Validation.UrlQuery"));
	CHECK_EQ(key_of("rtmp://user:secret@example.com/live"), std::string("Validation.UrlCredentials"));
	CHECK_EQ(key_of("rtmp://exa mple.com/live"), std::string("Validation.UrlWhitespace"));
	CHECK_EQ(key_of("rtmp://example.com:0/live"), std::string("Validation.PortInvalid"));
	CHECK_EQ(key_of("rtmp://[zz]/live"), std::string("Validation.IpInvalid"));
	CHECK_EQ(key_of("rtmp://[::1]x/live"), std::string("Validation.TextAfterIp"));
	CHECK_EQ(key_of("rtmp://exa$mple.com/live"), std::string("Validation.HostInvalid"));
}

TEST_CASE(destination_problems_are_reported_as_locale_keys)
{
	Destination destination = valid_destination();
	destination.name = " ";
	destination.audio_track = 7;
	destination.encoder.mode = EncoderMode::Custom;
	destination.encoder.video_bitrate_kbps = 0;
	destination.encoder.video_encoder_id.clear();

	std::set<std::string> keys;
	for (const ValidationIssue &issue : validate_destination(destination)) {
		keys.insert(issue.message.key);
	}
	CHECK(keys == (std::set<std::string>{"Validation.NameRequired", "Validation.AudioTrack", "Validation.Bitrate",
					     "Validation.EncoderRequired"}));
}
