// Runs with DBUS_SESSION_BUS_ADDRESS pointing at nothing, set by tests/CMakeLists.txt, so
// that no Secret Service can answer. Every operation must fail and report that, because the
// plugin must never fall back to keeping stream keys in plain text.
#include "secrets/linux_secret_store.hpp"

#include "test_harness.hpp"

using namespace multistream;

TEST_CASE(every_operation_reports_the_missing_service)
{
	LinuxSecretStore store("com.alejox.obs-multi-stream.tests");

	const auto put_error = store.put("id", "live_secret");
	REQUIRE(put_error.has_value());
	CHECK(put_error->kind == SecretError::Kind::Unavailable);
	CHECK(put_error->message.find("live_secret") == std::string::npos);

	const auto lookup = store.get("id");
	CHECK(!lookup.secret.has_value());
	REQUIRE(lookup.error.has_value());
	CHECK(lookup.error->kind == SecretError::Kind::Unavailable);

	const auto remove_error = store.remove("id");
	REQUIRE(remove_error.has_value());
	CHECK(remove_error->kind == SecretError::Kind::Unavailable);
}
