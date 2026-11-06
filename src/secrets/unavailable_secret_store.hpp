// SecretStore that fails every operation. Platforms without a secure store use it
// so that keys are never kept in a plaintext fallback.
#pragma once

#include "destinations/secret_store.hpp"

namespace multistream {

class UnavailableSecretStore final : public SecretStore {
public:
	explicit UnavailableSecretStore(std::string reason) : reason_(std::move(reason)) {}

	std::optional<SecretError> put(const std::string &, const std::string &) override { return error(); }
	SecretLookup get(const std::string &) override { return {std::nullopt, error()}; }
	std::optional<SecretError> remove(const std::string &) override { return error(); }

private:
	SecretError error() const { return {SecretError::Kind::Unavailable, reason_}; }

	std::string reason_;
};

} // namespace multistream
