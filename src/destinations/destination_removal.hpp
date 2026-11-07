#pragma once

#include "destinations/config_repository.hpp"
#include "destinations/secret_store.hpp"

#include <optional>
#include <string>

namespace multistream {

struct RemovalResult {
	bool removed = false;
	std::optional<Message> config_error;
	std::optional<SecretError> secret_error;
};

// Removes a destination from the config and saves it. Its secrets are left for the caller,
// which deletes them off the UI thread. secret_error is never set.
RemovalResult remove_destination_entry(DestinationsConfig &config, ConfigRepository &repository,
				       const std::string &destination_id);

// Removes a destination from the config, saves it, then deletes its secret. The
// config is saved first so that a failed save leaves a working destination with its
// key. A secret that cannot be deleted is reported so the user can act on it.
RemovalResult remove_destination(DestinationsConfig &config, ConfigRepository &repository, SecretStore &secrets,
				 const std::string &destination_id);

} // namespace multistream
