// Test double for runtime tests: reads fake stream keys from a file, so those tests never
// touch the Keychain. It exists only in builds configured with MULTI_STREAM_E2E_SECRETS,
// and the default build checks that its binary does not contain it.
#pragma once

#if !defined(MULTI_STREAM_E2E_SECRETS)
#error "The file secret store is only for builds configured with MULTI_STREAM_E2E_SECRETS."
#endif

#include "destinations/secret_store.hpp"

#include <string>

namespace multistream {

// Each line is: id<TAB>key[<TAB>delay in ms[<TAB>deny]]. The delay stands in for a slow
// Keychain prompt, and "deny" for a refused one. Saving keys is refused.
class E2eFileSecretStore final : public SecretStore {
public:
	explicit E2eFileSecretStore(std::string path);

	std::optional<SecretError> put(const std::string &destination_id, const std::string &secret) override;
	SecretLookup get(const std::string &destination_id) override;
	std::optional<SecretError> remove(const std::string &destination_id) override;

	// Logged at load, and the string the default build's check looks for.
	static const char *marker();

private:
	std::string path_;
};

} // namespace multistream
