#pragma once

#include "destinations/config_repository.hpp"
#include "destinations/secret_store.hpp"

#include <optional>
#include <string>

namespace multistream {

struct RemovalResult {
	bool removed = false;
	std::optional<std::string> config_error;
	std::optional<SecretError> secret_error;
};

// Removes a destination from the config, saves it, then deletes its secret. The
// config is saved first so that a failed save leaves a working destination with its
// key. A secret that cannot be deleted is reported so the user can act on it.
RemovalResult remove_destination(DestinationsConfig &config, ConfigRepository &repository, SecretStore &secrets,
				 const std::string &destination_id);

} // namespace multistream
