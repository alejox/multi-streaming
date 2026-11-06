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

SaveResult save_destination(DestinationsConfig &config, ConfigRepository &repository, SecretStore &secrets,
			    const Destination &destination, const std::optional<std::string> &new_key)
{
	SaveResult result;

	DestinationsConfig updated = config;
	const auto existing = std::find_if(updated.destinations.begin(), updated.destinations.end(),
					   [&](const Destination &candidate) { return candidate.id == destination.id; });
	if (existing != updated.destinations.end()) {
		*existing = destination;
	} else {
		updated.destinations.push_back(destination);
	}

	result.config_error = repository.save(updated);
	if (result.config_error) {
		return result;
	}

	config = std::move(updated);
	result.saved = true;
	if (new_key) {
		result.secret_error = secrets.put(destination.id, *new_key);
	}
	return result;
}

} // namespace multistream
