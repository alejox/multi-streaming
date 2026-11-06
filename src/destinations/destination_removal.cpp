#include "destinations/destination_removal.hpp"

#include <algorithm>

namespace multistream {

RemovalResult remove_destination(DestinationsConfig &config, ConfigRepository &repository, SecretStore &secrets,
				 const std::string &destination_id)
{
	RemovalResult result;

	DestinationsConfig remaining = config;
	const auto removed_from = std::remove_if(remaining.destinations.begin(), remaining.destinations.end(),
						 [&](const Destination &destination) {
							 return destination.id == destination_id;
						 });
	if (removed_from == remaining.destinations.end()) {
		return result;
	}
	remaining.destinations.erase(removed_from, remaining.destinations.end());

	result.config_error = repository.save(remaining);
	if (result.config_error) {
		return result;
	}

	config = std::move(remaining);
	result.removed = true;
	result.secret_error = secrets.remove(destination_id);
	return result;
}

} // namespace multistream
