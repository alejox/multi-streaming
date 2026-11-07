// Persists the Twitch OAuth refresh token through the existing SecretStore, under a key
// derived from the destination id so it never collides with the stored stream key.
#pragma once

#include "destinations/secret_store.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace multistream {

// destination id + a fixed suffix. Destination ids are 32 lowercase hex chars, so the
// suffixed key cannot collide with a real destination id.
std::string twitch_refresh_token_key(std::string_view destination_id);

class TwitchTokenStore {
public:
	explicit TwitchTokenStore(SecretStore &secrets) : secrets_(secrets) {}

	std::optional<SecretError> put(std::string_view destination_id, std::string_view refresh_token);
	SecretLookup get(std::string_view destination_id);
	std::optional<SecretError> remove(std::string_view destination_id);

private:
	SecretStore &secrets_;
};

} // namespace multistream
