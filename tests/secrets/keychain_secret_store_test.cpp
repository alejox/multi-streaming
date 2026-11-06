// Exercises the real login Keychain under a test-only service name. Every item is
// removed again. Exits with 77, reported as skipped by CTest, when the Keychain
// cannot be used from this session.
#include "secrets/keychain_secret_store.hpp"

#include "destinations/destination.hpp"
#include "secret_store_contract.hpp"

#include <cstdio>
#include <cstdlib>

using namespace multistream;

namespace {

constexpr const char *kTestService = "com.alejox.obs-multi-stream.tests";

struct RemoveOnExit {
	SecretStore &store;
	std::string id;
	~RemoveOnExit() { store.remove(id); }
};

} // namespace

TEST_CASE(honors_the_secret_store_contract)
{
	KeychainSecretStore store(kTestService);
	const std::string first_id = "test-" + generate_destination_id();
	const std::string second_id = "test-" + generate_destination_id();
	RemoveOnExit remove_first{store, first_id};
	RemoveOnExit remove_second{store, second_id};

	if (const auto error = store.put(first_id, "probe")) {
		if (error->kind != SecretError::Kind::Failure) {
			std::printf("Keychain unavailable, skipping: %s\n", error->message.c_str());
			std::exit(77);
		}
	}
	REQUIRE(!store.remove(first_id).has_value());

	check_secret_store_contract(store, first_id, second_id);
}

TEST_CASE(services_are_isolated)
{
	KeychainSecretStore store(kTestService);
	KeychainSecretStore production;
	const std::string id = "test-" + generate_destination_id();
	RemoveOnExit remove{store, id};

	REQUIRE(!store.put(id, "live_test").has_value());
	const auto lookup = production.get(id);
	CHECK(!lookup.error.has_value());
	CHECK(!lookup.secret.has_value());
}

TEST_CASE(default_service_is_the_plugin_service)
{
	CHECK_EQ(KeychainSecretStore().service(), "com.alejox.obs-multi-stream");
}
