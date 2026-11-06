// SecretStore backed by the macOS login Keychain, as generic password items with
// service = the plugin's service name and account = the destination id.
#pragma once

#include "destinations/secret_store.hpp"

namespace multistream {

class KeychainSecretStore final : public SecretStore {
public:
	static constexpr const char *kDefaultService = "com.alejox.obs-multi-stream";

	explicit KeychainSecretStore(std::string service = kDefaultService) : service_(std::move(service)) {}

	const std::string &service() const { return service_; }

	std::optional<SecretError> put(const std::string &destination_id, const std::string &secret) override;
	SecretLookup get(const std::string &destination_id) override;
	std::optional<SecretError> remove(const std::string &destination_id) override;

private:
	std::string service_;
};

} // namespace multistream
