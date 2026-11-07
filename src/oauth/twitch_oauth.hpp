// Twitch OAuth 2.0 (authorization code + PKCE) request builders and response parsing.
// Pure C++ so the standalone test build covers it; the Qt transport performs the HTTP.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace multistream {

inline constexpr std::string_view kTwitchAuthorizeEndpoint = "https://id.twitch.tv/oauth2/authorize";
inline constexpr std::string_view kTwitchTokenEndpoint = "https://id.twitch.tv/oauth2/token";
inline constexpr std::string_view kTwitchStreamKeyScope = "channel:read:stream_key";

// The URL to open in the browser. `state` ties the redirect back to this attempt.
std::string twitch_authorize_url(std::string_view client_id, std::string_view redirect_uri,
				 std::string_view state, std::string_view code_challenge);

// What the browser redirect brought back, parsed from the query part (after '?'). The
// caller compares `state` to the value it sent; a mismatched state is not this attempt's.
struct TwitchRedirect {
	std::optional<std::string> code;
	std::optional<std::string> state;
	std::optional<std::string> error;
	std::optional<std::string> error_description;

	bool ok() const { return code.has_value(); }
};
TwitchRedirect parse_redirect_query(std::string_view query);

// application/x-www-form-urlencoded bodies for POST /oauth2/token.
std::string token_exchange_body(std::string_view client_id, std::string_view code, std::string_view redirect_uri,
				std::string_view code_verifier);
std::string token_refresh_body(std::string_view client_id, std::string_view refresh_token);

// The JSON returned by POST /oauth2/token.
struct TwitchTokenResponse {
	std::optional<std::string> access_token;
	std::optional<std::string> refresh_token;
	std::optional<int> expires_in_seconds;
	std::optional<std::string> error;
	std::optional<std::string> error_description;

	bool ok() const { return access_token.has_value(); }
};
TwitchTokenResponse parse_token_response(std::string_view json);

} // namespace multistream
