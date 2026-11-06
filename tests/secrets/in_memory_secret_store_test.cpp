#include "secrets/in_memory_secret_store.hpp"

#include "secret_store_contract.hpp"

TEST_CASE(honors_the_secret_store_contract)
{
	multistream::InMemorySecretStore store;
	check_secret_store_contract(store, "first", "second");
}
