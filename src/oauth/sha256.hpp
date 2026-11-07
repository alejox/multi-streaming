// Minimal SHA-256, vendored so the OAuth core stays free of Qt and OpenSSL. Used only
// for the PKCE code challenge; it is not a general-purpose crypto provider.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace multistream {

// The 32-byte SHA-256 digest of `data`.
std::array<std::uint8_t, 32> sha256_digest(std::string_view data);

// The digest as 64 lowercase hexadecimal characters, for tests and diagnostics.
std::string sha256_hex(std::string_view data);

} // namespace multistream
