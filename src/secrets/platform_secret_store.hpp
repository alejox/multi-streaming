#pragma once

#include "destinations/secret_store.hpp"

#include <memory>

namespace multistream {

// Returns the Keychain on macOS, the Credential Manager on Windows, the Secret Service on
// Linux, and a store that fails every operation elsewhere. Construction does not touch the
// secure store, so it cannot prompt the user.
std::unique_ptr<SecretStore> make_platform_secret_store();

} // namespace multistream
