#include "oauth/twitch_token_store.hpp"

namespace multistream {
namespace {

constexpr std::string_view kSuffix = ":twitch-refresh";

} // namespace

std::string twitch_refresh_token_key(std::string_view destination_id)
{
	std::string key;
	key.reserve(destination_id.size() + kSuffix.size());
	key.append(destination_id);
	key.append(kSuffix);
	return key;
}

std::optional<SecretError> TwitchTokenStore::put(std::string_view destination_id, std::string_view refresh_token)
{
	return secrets_.put(twitch_refresh_token_key(destination_id), std::string(refresh_token));
}

SecretLookup TwitchTokenStore::get(std::string_view destination_id)
{
	return secrets_.get(twitch_refresh_token_key(destination_id));
}

std::optional<SecretError> TwitchTokenStore::remove(std::string_view destination_id)
{
	return secrets_.remove(twitch_refresh_token_key(destination_id));
}

} // namespace multistream
