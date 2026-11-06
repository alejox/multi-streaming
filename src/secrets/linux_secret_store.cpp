#include "secrets/linux_secret_store.hpp"

#include <libsecret/secret.h>

#include <string>

namespace multistream {
namespace {

constexpr const char *kServiceAttribute = "service";
constexpr const char *kDestinationAttribute = "destination_id";

const SecretSchema *stream_key_schema()
{
	static const SecretSchema schema = [] {
		SecretSchema value = {};
		value.name = "com.alejox.obs-multi-stream.StreamKey";
		value.flags = SECRET_SCHEMA_NONE;
		value.attributes[0] = {kServiceAttribute, SECRET_SCHEMA_ATTRIBUTE_STRING};
		value.attributes[1] = {kDestinationAttribute, SECRET_SCHEMA_ATTRIBUTE_STRING};
		return value;
	}();
	return &schema;
}

// Takes ownership of the error. A locked collection or a dismissed prompt means the user
// said no. A protocol error means the service answered badly. Anything else, such as no
// session bus or no service on it, means there is no usable store.
SecretError make_error(const char *operation, GError *error)
{
	SecretError::Kind kind = SecretError::Kind::Unavailable;
	if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
	    g_error_matches(error, SECRET_ERROR, SECRET_ERROR_IS_LOCKED)) {
		kind = SecretError::Kind::AccessDenied;
	} else if (error->domain == SECRET_ERROR) {
		kind = SecretError::Kind::Failure;
	}
	std::string message = std::string("Secret Service ") + operation + " failed: " + error->message;
	g_error_free(error);
	return {kind, std::move(message)};
}

} // namespace

std::optional<SecretError> LinuxSecretStore::put(const std::string &destination_id, const std::string &secret)
{
	const std::string label = "OBS Multi Stream key " + destination_id;

	GError *error = nullptr;
	const gboolean stored = secret_password_store_sync(stream_key_schema(), SECRET_COLLECTION_DEFAULT,
							   label.c_str(), secret.c_str(), nullptr, &error,
							   kServiceAttribute, service_.c_str(), kDestinationAttribute,
							   destination_id.c_str(), nullptr);
	if (error != nullptr) {
		return make_error("store", error);
	}
	if (!stored) {
		return SecretError{SecretError::Kind::AccessDenied, "Secret Service store was dismissed"};
	}
	return std::nullopt;
}

SecretLookup LinuxSecretStore::get(const std::string &destination_id)
{
	GError *error = nullptr;
	gchar *password = secret_password_lookup_sync(stream_key_schema(), nullptr, &error, kServiceAttribute,
						      service_.c_str(), kDestinationAttribute,
						      destination_id.c_str(), nullptr);
	if (error != nullptr) {
		return {std::nullopt, make_error("lookup", error)};
	}
	if (password == nullptr) {
		return {std::nullopt, std::nullopt};
	}
	std::string secret(password);
	secret_password_free(password);
	return {std::move(secret), std::nullopt};
}

std::optional<SecretError> LinuxSecretStore::remove(const std::string &destination_id)
{
	// Returns FALSE without an error when nothing matched, which the contract treats as success.
	GError *error = nullptr;
	secret_password_clear_sync(stream_key_schema(), nullptr, &error, kServiceAttribute, service_.c_str(),
				   kDestinationAttribute, destination_id.c_str(), nullptr);
	if (error != nullptr) {
		return make_error("clear", error);
	}
	return std::nullopt;
}

} // namespace multistream
