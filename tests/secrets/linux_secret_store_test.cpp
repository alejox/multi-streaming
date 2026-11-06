// Exercises a real Secret Service under a test-only service name. Every item is removed
// again. Exits with 77, reported as skipped by CTest, when no service is reachable, unless
// MULTI_STREAM_REQUIRE_SECRET_SERVICE is set: CI sets it so that a broken keyring setup
// fails the job instead of silently skipping the only proof that the store works.
#include "secrets/linux_secret_store.hpp"

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

void skip_unless_service_is_required(const SecretError &error)
{
	if (std::getenv("MULTI_STREAM_REQUIRE_SECRET_SERVICE") != nullptr) {
		std::printf("Secret Service required but unusable: %s\n", error.message.c_str());
		std::exit(1);
	}
	std::printf("Secret Service unavailable, skipping: %s\n", error.message.c_str());
	std::exit(77);
}

} // namespace

TEST_CASE(honors_the_secret_store_contract)
{
	LinuxSecretStore store(kTestService);
	const std::string first_id = "test-" + generate_destination_id();
	const std::string second_id = "test-" + generate_destination_id();
	RemoveOnExit remove_first{store, first_id};
	RemoveOnExit remove_second{store, second_id};

	if (const auto error = store.put(first_id, "probe")) {
		skip_unless_service_is_required(*error);
	}
	REQUIRE(!store.remove(first_id).has_value());

	check_secret_store_contract(store, first_id, second_id);
}

TEST_CASE(services_are_isolated)
{
	LinuxSecretStore store(kTestService);
	LinuxSecretStore production;
	const std::string id = "test-" + generate_destination_id();
	RemoveOnExit remove{store, id};

	if (const auto error = store.put(id, "live_test")) {
		skip_unless_service_is_required(*error);
	}
	const auto lookup = production.get(id);
	CHECK(!lookup.error.has_value());
	CHECK(!lookup.secret.has_value());
}

TEST_CASE(default_service_is_the_plugin_service)
{
	CHECK_EQ(LinuxSecretStore().service(), "com.alejox.obs-multi-stream");
}
