#include "oauth/base64url.hpp"
#include "oauth/pkce.hpp"
#include "oauth/sha256.hpp"

#include "test_harness.hpp"

using namespace multistream;

TEST_CASE(sha256_matches_known_vectors)
{
	CHECK_EQ(sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK_EQ(sha256_hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK_EQ(sha256_hex("The quick brown fox jumps over the lazy dog"),
		 "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST_CASE(base64url_matches_known_vectors)
{
	CHECK_EQ(base64url_encode("f"), "Zg");
	CHECK_EQ(base64url_encode("fo"), "Zm8");
	CHECK_EQ(base64url_encode("foo"), "Zm9v");
	CHECK_EQ(base64url_encode("foobar"), "Zm9vYmFy");
}

TEST_CASE(code_challenge_matches_rfc7636_example)
{
	// RFC 7636 appendix B: verifier -> S256 challenge.
	const std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
	CHECK_EQ(code_challenge(verifier), "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
	CHECK_EQ(code_challenge(verifier).size(), 43u);
}

TEST_CASE(generated_verifier_stays_inside_rfc7636_bounds)
{
	const std::string verifier = generate_code_verifier();
	CHECK(verifier.size() >= 43);
	CHECK(verifier.size() <= 128);
	for (const char character : verifier) {
		const bool unreserved = character == '-' || character == '.' || character == '_' || character == '~' ||
					(character >= 'A' && character <= 'Z') ||
					(character >= 'a' && character <= 'z') ||
					(character >= '0' && character <= '9');
		CHECK(unreserved);
	}
}
