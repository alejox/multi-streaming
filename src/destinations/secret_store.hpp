// Port for storing stream keys outside the config file.
#pragma once

#include <optional>
#include <string>

namespace multistream {

struct SecretError {
	enum class Kind { Unavailable, AccessDenied, Failure };

	Kind kind;
	std::string message;
};

struct SecretLookup {
	std::optional<std::string> secret; // Empty when no secret is stored.
	std::optional<SecretError> error;
};

// Secrets are keyed by destination id. Error messages never contain the secret.
class SecretStore {
public:
	virtual ~SecretStore() = default;

	virtual std::optional<SecretError> put(const std::string &destination_id, const std::string &secret) = 0;
	virtual SecretLookup get(const std::string &destination_id) = 0;

	// Removing a secret that does not exist succeeds.
	virtual std::optional<SecretError> remove(const std::string &destination_id) = 0;
};

} // namespace multistream
