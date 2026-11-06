#include "secrets/platform_secret_store.hpp"
#include "secrets/unavailable_secret_store.hpp"

#include "test_harness.hpp"

using namespace multistream;

TEST_CASE(every_operation_fails_loudly)
{
	UnavailableSecretStore store("no secret store on this platform");

	const auto put_error = store.put("id", "live_secret");
	REQUIRE(put_error.has_value());
	CHECK(put_error->kind == SecretError::Kind::Unavailable);
	CHECK_EQ(put_error->message, "no secret store on this platform");

	const auto lookup = store.get("id");
	CHECK(!lookup.secret.has_value());
	CHECK(lookup.error.has_value());

	CHECK(store.remove("id").has_value());
}

TEST_CASE(platform_store_is_available_or_fails_explicitly)
{
	// Must not throw or fall back to plaintext. Keychain access itself is covered by
	// the Keychain test, so only construction is checked here.
	CHECK(make_platform_secret_store() != nullptr);
}
