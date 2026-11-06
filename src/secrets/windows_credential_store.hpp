// SecretStore backed by the Windows Credential Manager, as generic credentials with
// target = "<target>/<destination id>". Construction does not touch the store, so it
// cannot prompt the user.
#pragma once

#include "destinations/secret_store.hpp"

#include <string>

namespace multistream {

class WindowsCredentialStore final : public SecretStore {
public:
	static constexpr const char *kDefaultTarget = "obs-multi-stream";

	explicit WindowsCredentialStore(std::string target = kDefaultTarget) : target_(std::move(target)) {}

	std::optional<SecretError> put(const std::string &destination_id, const std::string &secret) override;
	SecretLookup get(const std::string &destination_id) override;
	std::optional<SecretError> remove(const std::string &destination_id) override;

private:
	std::string target_;
};

} // namespace multistream
