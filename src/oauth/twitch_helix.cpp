#include "oauth/twitch_helix.hpp"

#include <json.hpp>

namespace multistream {
namespace {

using nlohmann::json;

} // namespace

std::string helix_users_url()
{
	std::string out;
	out.reserve(kHelixEndpoint.size() + 6);
	out.append(kHelixEndpoint);
	out += "/users";
	return out;
}

std::string helix_stream_key_url(std::string_view broadcaster_id)
{
	std::string out;
	out.reserve(kHelixEndpoint.size() + 40);
	out.append(kHelixEndpoint);
	out += "/streams/key?broadcaster_id=";
	out.append(broadcaster_id);
	return out;
}

std::optional<TwitchUser> parse_helix_users(std::string_view json_text)
{
	try {
		const json parsed = json::parse(json_text.begin(), json_text.end());
		if (!parsed.contains("data") || !parsed["data"].is_array() || parsed["data"].empty()) {
			return std::nullopt;
		}
		const json &first = parsed["data"][0];
		TwitchUser user;
		user.id = first.value("id", "");
		user.login = first.value("login", "");
		if (user.id.empty()) {
			return std::nullopt;
		}
		return user;
	} catch (const json::exception &) {
		return std::nullopt;
	}
}

std::optional<std::string> parse_helix_stream_key(std::string_view json_text)
{
	try {
		const json parsed = json::parse(json_text.begin(), json_text.end());
		if (!parsed.contains("data") || !parsed["data"].is_array() || parsed["data"].empty()) {
			return std::nullopt;
		}
		const std::string key = parsed["data"][0].value("stream_key", "");
		if (key.empty()) {
			return std::nullopt;
		}
		return key;
	} catch (const json::exception &) {
		return std::nullopt;
	}
}

} // namespace multistream
