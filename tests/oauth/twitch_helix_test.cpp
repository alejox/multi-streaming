#include "oauth/twitch_helix.hpp"

#include "test_harness.hpp"

using namespace multistream;

TEST_CASE(helix_urls_are_built_verbatim)
{
	CHECK_EQ(helix_users_url(), "https://api.twitch.tv/helix/users");
	CHECK_EQ(helix_stream_key_url("12345"), "https://api.twitch.tv/helix/streams/key?broadcaster_id=12345");
}

TEST_CASE(parse_helix_users_reads_the_first_user)
{
	const std::optional<TwitchUser> user = parse_helix_users(
		R"({"data":[{"id":"424596340","login":"mychannel","display_name":"My Channel"}]})");
	CHECK(user.has_value());
	CHECK_EQ(user->id, "424596340");
	CHECK_EQ(user->login, "mychannel");
}

TEST_CASE(parse_helix_users_rejects_empty_data)
{
	CHECK(!parse_helix_users(R"({"data":[]})").has_value());
	CHECK(!parse_helix_users(R"({"data":null})").has_value());
	CHECK(!parse_helix_users(R"({})").has_value());
}

TEST_CASE(parse_helix_stream_key_reads_the_key)
{
	const std::optional<std::string> key = parse_helix_stream_key(
		R"({"data":[{"stream_key":"live_12345_abcDEFghiJKLmnoPQRstuVWXyz"}]})");
	CHECK(key.has_value());
	CHECK_EQ(*key, "live_12345_abcDEFghiJKLmnoPQRstuVWXyz");
}

TEST_CASE(parse_helix_stream_key_rejects_missing_data)
{
	CHECK(!parse_helix_stream_key(R"({"data":[]})").has_value());
	CHECK(!parse_helix_stream_key(R"({"data":[{}]})").has_value());
	CHECK(!parse_helix_stream_key(R"({})").has_value());
}

TEST_CASE(helix_parsers_survive_non_json)
{
	CHECK(!parse_helix_users("not json").has_value());
	CHECK(!parse_helix_stream_key("not json").has_value());
}
