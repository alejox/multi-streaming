#include "secrets/e2e_file_secret_store.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace multistream {

E2eFileSecretStore::E2eFileSecretStore(std::string path) : path_(std::move(path)) {}

const char *E2eFileSecretStore::marker()
{
	return "MULTI_STREAM_E2E_SECRETS_ACTIVE";
}

std::optional<SecretError> E2eFileSecretStore::put(const std::string &, const std::string &)
{
	return SecretError{SecretError::Kind::Unavailable, "The end-to-end secret store is read-only."};
}

SecretLookup E2eFileSecretStore::get(const std::string &destination_id)
{
	std::ifstream file(path_);
	std::string line;
	while (std::getline(file, line)) {
		std::istringstream fields(line);
		std::string id;
		std::string key;
		std::string delay;
		std::string mode;
		std::getline(fields, id, '\t');
		std::getline(fields, key, '\t');
		std::getline(fields, delay, '\t');
		std::getline(fields, mode, '\t');
		if (id != destination_id) {
			continue;
		}

		if (!delay.empty()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(std::stol(delay)));
		}

		SecretLookup lookup;
		if (mode == "deny") {
			lookup.error = SecretError{SecretError::Kind::AccessDenied, "Access to the stream key was denied."};
		} else {
			lookup.secret = key;
		}
		return lookup;
	}
	return {};
}

std::optional<SecretError> E2eFileSecretStore::remove(const std::string &)
{
	return std::nullopt;
}

} // namespace multistream
