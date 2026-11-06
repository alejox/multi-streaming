#include "destinations/config_codec.hpp"

#include "test_harness.hpp"

#include <variant>

using namespace multistream;

namespace {

Destination full_destination()
{
	Destination destination;
	destination.id = "dest-1";
	destination.name = "Vertical \"TikTok\"";
	destination.server_url = "rtmps://live.example.com:443/app";
	destination.enabled = false;
	destination.video_source = VideoSource::Vertical;
	destination.encoder.mode = EncoderMode::Custom;
	destination.encoder.video_bitrate_kbps = 4500;
	destination.encoder.video_encoder_id = "com.apple.videotoolbox.videoencoder.ave.avc";
	destination.audio_track = 3;
	destination.sync_with_main = true;
	destination.reconnect = *ReconnectPolicy::create(5, 3);
	return destination;
}

ConfigErrorKind error_kind_of(std::string_view text)
{
	const auto result = decode_config(text);
	REQUIRE(std::holds_alternative<ConfigError>(result));
	return std::get<ConfigError>(result).kind;
}

} // namespace

TEST_CASE(round_trips_every_field)
{
	DestinationsConfig config;
	config.destinations.push_back(full_destination());
	Destination minimal;
	minimal.id = "dest-2";
	minimal.name = "Main";
	minimal.server_url = "rtmp://localhost/live";
	config.destinations.push_back(minimal);

	const auto decoded = decode_config(encode_config(config));
	REQUIRE(std::holds_alternative<DestinationsConfig>(decoded));
	CHECK(std::get<DestinationsConfig>(decoded) == config);
}

TEST_CASE(encodes_the_schema_version)
{
	const std::string encoded = encode_config(DestinationsConfig{});
	CHECK(encoded.find("\"schema_version\": 1") != std::string::npos);
}

TEST_CASE(never_carries_a_key_found_in_the_input)
{
	const char *hand_edited = R"({"schema_version": 1, "destinations": [{
		"id": "a", "name": "n", "server_url": "rtmp://h/a",
		"key": "live_SECRET", "stream_key": "live_SECRET"}]})";

	const auto decoded = decode_config(hand_edited);
	REQUIRE(std::holds_alternative<DestinationsConfig>(decoded));
	const std::string encoded = encode_config(std::get<DestinationsConfig>(decoded));
	CHECK(encoded.find("SECRET") == std::string::npos);
	CHECK(encoded.find("key") == std::string::npos);
}

TEST_CASE(applies_defaults_to_optional_fields)
{
	const auto decoded =
		decode_config(R"({"schema_version": 1, "destinations": [{"id": "a", "name": "n", "server_url": "u"}]})");
	REQUIRE(std::holds_alternative<DestinationsConfig>(decoded));
	const Destination &destination = std::get<DestinationsConfig>(decoded).destinations.at(0);
	CHECK(destination.enabled);
	CHECK(destination.video_source == VideoSource::Main);
	CHECK(destination.encoder.mode == EncoderMode::Share);
	CHECK_EQ(destination.audio_track, 1);
	CHECK(!destination.sync_with_main);
	CHECK(destination.reconnect == ReconnectPolicy{});
}

TEST_CASE(encoding_survives_invalid_utf8)
{
	DestinationsConfig config;
	Destination destination;
	destination.id = "a";
	destination.name = "bad \xff name";
	config.destinations.push_back(destination);
	CHECK(std::holds_alternative<DestinationsConfig>(decode_config(encode_config(config))));
}

TEST_CASE(reports_malformed_documents)
{
	CHECK(error_kind_of("") == ConfigErrorKind::Malformed);
	CHECK(error_kind_of("{not json") == ConfigErrorKind::Malformed);
	CHECK(error_kind_of("[]") == ConfigErrorKind::Malformed);
	CHECK(error_kind_of(R"({"destinations": []})") == ConfigErrorKind::Malformed);
	CHECK(error_kind_of(R"({"schema_version": "1", "destinations": []})") == ConfigErrorKind::Malformed);
	CHECK(error_kind_of(R"({"schema_version": 0, "destinations": []})") == ConfigErrorKind::Malformed);
	CHECK(error_kind_of(R"({"schema_version": 1})") == ConfigErrorKind::Malformed);
	CHECK(error_kind_of(R"({"schema_version": 1, "destinations": {}})") == ConfigErrorKind::Malformed);
}

TEST_CASE(reports_a_newer_schema_as_unsupported)
{
	CHECK(error_kind_of(R"({"schema_version": 2, "destinations": []})") == ConfigErrorKind::UnsupportedSchema);
}

TEST_CASE(reports_invalid_entries)
{
	const auto entry_error = [](const std::string &entry) {
		return error_kind_of(R"({"schema_version": 1, "destinations": [)" + entry + "]}");
	};
	CHECK(entry_error("1") == ConfigErrorKind::InvalidEntry);
	CHECK(entry_error(R"({"name": "n", "server_url": "u"})") == ConfigErrorKind::InvalidEntry);
	CHECK(entry_error(R"({"id": 3, "name": "n", "server_url": "u"})") == ConfigErrorKind::InvalidEntry);
	CHECK(entry_error(R"({"id": "a", "name": "n", "server_url": "u", "enabled": "yes"})") ==
	      ConfigErrorKind::InvalidEntry);
	CHECK(entry_error(R"({"id": "a", "name": "n", "server_url": "u", "video_source": "diagonal"})") ==
	      ConfigErrorKind::InvalidEntry);
	CHECK(entry_error(R"({"id": "a", "name": "n", "server_url": "u", "audio_track": 99999999999})") ==
	      ConfigErrorKind::InvalidEntry);
	CHECK(entry_error(R"({"id": "a", "name": "n", "server_url": "u", "encoder": {"mode": "magic"}})") ==
	      ConfigErrorKind::InvalidEntry);
	CHECK(entry_error(R"({"id": "a", "name": "n", "server_url": "u",
		"reconnect": {"max_retries": -1, "base_delay_seconds": 2}})") ==
	      ConfigErrorKind::InvalidEntry);
}

TEST_CASE(reports_duplicate_ids)
{
	CHECK(error_kind_of(R"({"schema_version": 1, "destinations": [
		{"id": "a", "name": "n", "server_url": "u"},
		{"id": "a", "name": "m", "server_url": "v"}]})") == ConfigErrorKind::InvalidEntry);
}

TEST_CASE(keeps_semantically_invalid_values_for_later_validation)
{
	const auto decoded = decode_config(
		R"({"schema_version": 1, "destinations": [{"id": "a", "name": "n", "server_url": "not a url"}]})");
	REQUIRE(std::holds_alternative<DestinationsConfig>(decoded));
	CHECK_EQ(std::get<DestinationsConfig>(decoded).destinations.at(0).server_url, "not a url");
}

TEST_CASE(does_not_store_a_reconnect_delay_cap_libobs_cannot_enforce)
{
	DestinationsConfig config;
	config.destinations.push_back(full_destination());
	CHECK(encode_config(config).find("max_delay_seconds") == std::string::npos);
}

TEST_CASE(older_configs_with_a_reconnect_delay_cap_still_load)
{
	const auto decoded = decode_config(R"({"schema_version": 1, "destinations": [{"id": "a", "name": "n",
		"server_url": "u", "reconnect": {"max_retries": 7, "base_delay_seconds": 4, "max_delay_seconds": 2}}]})");
	REQUIRE(std::holds_alternative<DestinationsConfig>(decoded));
	const ReconnectPolicy &policy = std::get<DestinationsConfig>(decoded).destinations.at(0).reconnect;
	CHECK_EQ(policy.max_retries(), 7);
	CHECK_EQ(policy.base_delay_seconds(), 4);
}
