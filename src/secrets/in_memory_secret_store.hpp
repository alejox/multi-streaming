// SecretStore for tests. Holds secrets in memory only.
#pragma once

#include "destinations/secret_store.hpp"

#include <map>

namespace multistream {

class InMemorySecretStore final : public SecretStore {
public:
	std::optional<SecretError> put(const std::string &destination_id, const std::string &secret) override
	{
		secrets_[destination_id] = secret;
		return std::nullopt;
	}

	SecretLookup get(const std::string &destination_id) override
	{
		const auto found = secrets_.find(destination_id);
		if (found == secrets_.end()) {
			return {};
		}
		return {found->second, std::nullopt};
	}

	std::optional<SecretError> remove(const std::string &destination_id) override
	{
		secrets_.erase(destination_id);
		return std::nullopt;
	}

private:
	std::map<std::string, std::string> secrets_;
};

} // namespace multistream
