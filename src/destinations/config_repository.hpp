// Port for loading and saving the destinations config.
#pragma once

#include "destinations/destination.hpp"

#include <optional>
#include <string>

namespace multistream {

struct ConfigLoad {
	// Empty when the config does not exist yet or could not be used.
	DestinationsConfig config;

	// Set when an existing config could not be used. Explains what happened and
	// where the original was preserved, for the user to see.
	std::optional<std::string> problem;
};

class ConfigRepository {
public:
	virtual ~ConfigRepository() = default;

	virtual ConfigLoad load() = 0;

	// Returns an error message when the config could not be saved.
	virtual std::optional<std::string> save(const DestinationsConfig &config) = 0;
};

} // namespace multistream
