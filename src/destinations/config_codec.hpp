// JSON encoding of the destinations config. The format has no key field, and
// decoding ignores unknown fields, so a key added by hand does not survive a save.
#pragma once

#include "destinations/destination.hpp"

#include <string>
#include <string_view>
#include <variant>

namespace multistream {

inline constexpr int kConfigSchemaVersion = 2;

enum class ConfigErrorKind { Malformed, UnsupportedSchema, InvalidEntry };

struct ConfigError {
	ConfigErrorKind kind;
	std::string detail;
};

std::string encode_config(const DestinationsConfig &config);

// Checks structure only: an unusable value such as a bad server URL decodes
// successfully and is reported by validate_destination() when it matters.
std::variant<DestinationsConfig, ConfigError> decode_config(std::string_view text);

} // namespace multistream
