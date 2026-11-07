#include "secrets/platform_secret_store.hpp"

#if defined(__APPLE__)
#include "secrets/keychain_secret_store.hpp"
#elif defined(_WIN32)
#include "secrets/windows_credential_store.hpp"
#elif defined(__linux__)
#include "secrets/linux_secret_store.hpp"
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
#elif defined(__linux__)
	return std::make_unique<LinuxSecretStore>();
#else
	return std::make_unique<UnavailableSecretStore>(std::string());
#endif
}

} // namespace multistream
