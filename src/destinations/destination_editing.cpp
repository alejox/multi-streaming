#include "destinations/destination_editing.hpp"

#include <algorithm>
#include <cctype>

namespace multistream {

std::optional<std::string> validate_stream_key_entry(std::string_view entered, bool is_new_destination)
{
	if (entered.empty()) {
		if (is_new_destination) {
			return "A stream key is required";
		}
		return std::nullopt;
	}

	const bool has_whitespace = std::any_of(entered.begin(), entered.end(), [](unsigned char character) {
		return std::isspace(character) != 0;
	});
	if (has_whitespace) {
		return "A stream key cannot contain whitespace";
	}
	return std::nullopt;
}

std::optional<std::string> save_destination_entry(DestinationsConfig &config, ConfigRepository &repository,
						  const Destination &destination)
{
	DestinationsConfig updated = config;
	const auto existing = std::find_if(updated.destinations.begin(), updated.destinations.end(),
					   [&](const Destination &candidate) { return candidate.id == destination.id; });
	if (existing != updated.destinations.end()) {
		*existing = destination;
	} else {
		updated.destinations.push_back(destination);
	}

	if (auto error = repository.save(updated)) {
		return error;
	}
	config = std::move(updated);
	return std::nullopt;
}

bool keep_refresh_token_on_cancel(bool is_new_destination, bool had_stored_token, bool has_new_token)
{
	return !is_new_destination && had_stored_token && has_new_token;
}

SaveResult save_destination(DestinationsConfig &config, ConfigRepository &repository, SecretStore &secrets,
			    const Destination &destination, const std::optional<std::string> &new_key)
{
	SaveResult result;
	result.config_error = save_destination_entry(config, repository, destination);
	if (result.config_error) {
		return result;
	}

	result.saved = true;
	if (new_key) {
		result.secret_error = secrets.put(destination.id, *new_key);
	}
	return result;
}

} // namespace multistream
