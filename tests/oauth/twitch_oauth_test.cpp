#include "oauth/twitch_oauth.hpp"

#include "test_harness.hpp"

using namespace multistream;

TEST_CASE(authorize_url_encodes_its_parameters)
{
	const std::string url = twitch_authorize_url("client", "http://localhost:8090/", "state", "challenge");
	CHECK_EQ(url,
		 "https://id.twitch.tv/oauth2/authorize?client_id=client&redirect_uri=http%3A%2F%2Flocalhost%3A8090%2F"
		 "&response_type=code&scope=channel%3Aread%3Astream_key&code_challenge=challenge"
		 "&code_challenge_method=S256&state=state");
}

TEST_CASE(redirect_parses_code_and_state)
{
	const TwitchRedirect redirect = parse_redirect_query("code=abc123&state=xyz&scope=channel%3Aread%3Astream_key");
	CHECK(redirect.ok());
	CHECK_EQ(redirect.code.value_or(""), "abc123");
	CHECK_EQ(redirect.state.value_or(""), "xyz");
	CHECK(!redirect.error);
}

TEST_CASE(redirect_parses_errors)
{
	const TwitchRedirect redirect = parse_redirect_query("error=access_denied&error_description=The%20user%20denied");
	CHECK(!redirect.ok());
	CHECK_EQ(redirect.error.value_or(""), "access_denied");
	CHECK_EQ(redirect.error_description.value_or(""), "The user denied");
}

TEST_CASE(redirect_with_no_code_or_error_is_empty)
{
	const TwitchRedirect redirect = parse_redirect_query("some=thing");
	CHECK(!redirect.ok());
	CHECK(!redirect.error);
}

TEST_CASE(token_exchange_body_encodes_redirect_uri)
{
	const std::string body = token_exchange_body("client", "code123", "http://localhost:8090/", "verifier");
	CHECK_EQ(body,
		 "client_id=client&code=code123&grant_type=authorization_code"
		 "&redirect_uri=http%3A%2F%2Flocalhost%3A8090%2F&code_verifier=verifier");
}

TEST_CASE(token_refresh_body_uses_refresh_grant)
{
	CHECK_EQ(token_refresh_body("client", "rt456"), "client_id=client&refresh_token=rt456&grant_type=refresh_token");
}

TEST_CASE(token_response_parses_success)
{
	const TwitchTokenResponse response = parse_token_response(
		R"({"access_token":"at123","refresh_token":"rt456","expires_in":14400,"scope":["channel:read:stream_key"],"token_type":"bearer"})");
	CHECK(response.ok());
	CHECK_EQ(response.access_token.value_or(""), "at123");
	CHECK_EQ(response.refresh_token.value_or(""), "rt456");
	CHECK_EQ(response.expires_in_seconds.value_or(0), 14400);
	CHECK(!response.error);
}

TEST_CASE(token_response_parses_twitch_error)
{
	const TwitchTokenResponse response =
		parse_token_response(R"({"error":"invalid_grant","status":400,"message":"Invalid authorization code"})");
	CHECK(!response.ok());
	CHECK_EQ(response.error.value_or(""), "invalid_grant");
	CHECK_EQ(response.error_description.value_or(""), "Invalid authorization code");
}

TEST_CASE(token_response_survives_non_json)
{
	const TwitchTokenResponse response = parse_token_response("not json");
	CHECK(!response.ok());
	CHECK_EQ(response.error.value_or(""), "invalid_response");
}
