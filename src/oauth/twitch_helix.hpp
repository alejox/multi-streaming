// Twitch Helix API request builders and response parsing. Pure C++ so the standalone
// test build covers it; the Qt transport performs the HTTP and adds the auth headers.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace multistream {

inline constexpr std::string_view kHelixEndpoint = "https://api.twitch.tv/helix";

// GET /users
std::string helix_users_url();

// GET /streams/key?broadcaster_id=<id>
std::string helix_stream_key_url(std::string_view broadcaster_id);

struct TwitchUser {
	std::string id;
	std::string login;
};

// The authenticated user from GET /users, or nullopt when the response has no user.
std::optional<TwitchUser> parse_helix_users(std::string_view json);

// The stream key from GET /streams/key, or nullopt when the response has none.
std::optional<std::string> parse_helix_stream_key(std::string_view json);

} // namespace multistream
