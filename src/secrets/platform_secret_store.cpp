#include "secrets/platform_secret_store.hpp"

#if defined(__APPLE__)
#include "secrets/keychain_secret_store.hpp"
#elif defined(_WIN32)
#include "secrets/windows_credential_store.hpp"
#else
#include "secrets/unavailable_secret_store.hpp"
#endif

namespace multistream {

std::unique_ptr<SecretStore> make_platform_secret_store()
{
#if defined(__APPLE__)
	return std::make_unique<KeychainSecretStore>();
#elif defined(_WIN32)
	return std::make_unique<WindowsCredentialStore>();
#else
	return std::make_unique<UnavailableSecretStore>("No secure secret store is implemented for this platform.");
#endif
}

} // namespace multistream
