// PKCE (RFC 7636) building blocks for the OAuth authorization-code flow.
#pragma once

#include <string>
#include <string_view>

namespace multistream {

// A new code verifier: 64 characters from the RFC 7636 unreserved set, drawn from
// std::random_device. 64 is comfortably inside the required 43..128 range.
std::string generate_code_verifier();

// base64url(SHA-256(verifier)) without padding: the S256 code challenge.
std::string code_challenge(std::string_view verifier);

} // namespace multistream
