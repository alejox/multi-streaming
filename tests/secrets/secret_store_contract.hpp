// Behavior every SecretStore implementation must honor.
#pragma once

#include "destinations/secret_store.hpp"
#include "test_harness.hpp"

#include <string>

inline void check_secret_store_contract(multistream::SecretStore &store, const std::string &first_id,
					const std::string &second_id)
{
	const auto absent = store.get(first_id);
	CHECK(!absent.error.has_value());
	CHECK(!absent.secret.has_value());

	CHECK(!store.put(first_id, "live_first").has_value());
	CHECK(!store.put(second_id, "live_second").has_value());
	CHECK_EQ(store.get(first_id).secret.value_or(""), "live_first");
	CHECK_EQ(store.get(second_id).secret.value_or(""), "live_second");

	CHECK(!store.put(first_id, "live_replaced \xc3\xa9").has_value());
	CHECK_EQ(store.get(first_id).secret.value_or(""), "live_replaced \xc3\xa9");

	CHECK(!store.remove(first_id).has_value());
	CHECK(!store.get(first_id).secret.has_value());
	CHECK(!store.get(first_id).error.has_value());
	CHECK_EQ(store.get(second_id).secret.value_or(""), "live_second");

	CHECK(!store.remove(first_id).has_value());
	CHECK(!store.remove(second_id).has_value());
}
