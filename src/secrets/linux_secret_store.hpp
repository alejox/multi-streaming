// SecretStore backed by the freedesktop Secret Service through libsecret, as items in
// the default collection with attributes service = the plugin's service name and
// destination_id = the destination id. Construction does not touch the service, so it
// cannot prompt the user. When no service is reachable every operation reports
// SecretError::Kind::Unavailable; keys are never written anywhere else.
#pragma once

#include "destinations/secret_store.hpp"

#include <string>

namespace multistream {

class LinuxSecretStore final : public SecretStore {
public:
	static constexpr const char *kDefaultService = "com.alejox.obs-multi-stream";

	explicit LinuxSecretStore(std::string service = kDefaultService) : service_(std::move(service)) {}

	const std::string &service() const { return service_; }

	std::optional<SecretError> put(const std::string &destination_id, const std::string &secret) override;
	SecretLookup get(const std::string &destination_id) override;
	std::optional<SecretError> remove(const std::string &destination_id) override;

private:
	std::string service_;
};

} // namespace multistream
