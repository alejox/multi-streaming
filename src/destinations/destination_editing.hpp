#pragma once

#include "destinations/config_repository.hpp"
#include "destinations/secret_store.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace multistream {

// Why an entered stream key cannot be used, or nothing when it can. A blank entry means
// "keep the stored key", which only an existing destination has. Never contains the key.
std::optional<std::string> validate_stream_key_entry(std::string_view entered, bool is_new_destination);

struct SaveResult {
	bool saved = false;
	std::optional<std::string> config_error;
	std::optional<SecretError> secret_error;
};

// Adds the destination, or replaces the one with its id, and saves the config. A failed
// save changes nothing. Keys are left for the caller, which stores them off the UI thread.
std::optional<std::string> save_destination_entry(DestinationsConfig &config, ConfigRepository &repository,
						  const Destination &destination);

// Whether a refresh token from a Twitch connect is stored although the editor was
// cancelled. Twitch rotates refresh tokens on use, so after a connect that used the
// stored one, only the new token works; an existing destination keeps it. A new
// destination stores nothing, since no entry would own the token.
bool keep_refresh_token_on_cancel(bool is_new_destination, bool had_stored_token, bool has_new_token);

// Adds the destination, or replaces the one with its id, and stores a new key if one is
// given. The config is saved first, so a failed save changes nothing. A key that cannot
// be stored afterwards is reported: starting that destination then says what is missing.
SaveResult save_destination(DestinationsConfig &config, ConfigRepository &repository, SecretStore &secrets,
			    const Destination &destination, const std::optional<std::string> &new_key);

} // namespace multistream
