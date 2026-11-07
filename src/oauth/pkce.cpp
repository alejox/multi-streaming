#include "oauth/pkce.hpp"

#include "oauth/base64url.hpp"
#include "oauth/sha256.hpp"

#include <cstddef>
#include <random>

namespace multistream {
namespace {

constexpr std::string_view kUnreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
constexpr std::size_t kVerifierLength = 64;

} // namespace

std::string generate_code_verifier()
{
	std::random_device random;
	std::uniform_int_distribution<std::size_t> distribution(0, kUnreserved.size() - 1);

	std::string verifier;
	verifier.reserve(kVerifierLength);
	for (std::size_t i = 0; i < kVerifierLength; ++i) {
		verifier += kUnreserved[distribution(random)];
	}
	return verifier;
}

std::string code_challenge(std::string_view verifier)
{
	const std::array<std::uint8_t, 32> digest = sha256_digest(verifier);
	return base64url_encode(std::string_view(reinterpret_cast<const char *>(digest.data()), digest.size()));
}

} // namespace multistream
