#include "secrets/e2e_file_secret_store.hpp"

#include "test_harness.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

using multistream::E2eFileSecretStore;
using multistream::SecretError;

namespace {

std::string write_file(const std::string &name, const std::string &content)
{
	const std::string path = name + ".txt";
	std::ofstream(path) << content;
	return path;
}

} // namespace

TEST_CASE(reads_the_key_of_the_matching_line)
{
	const std::string path = write_file("e2e_read", "a\tkey-a\nb\tkey-b\n");
	E2eFileSecretStore store(path);

	CHECK_EQ(*store.get("b").secret, std::string("key-b"));
	CHECK(!store.get("missing").secret.has_value());
	CHECK(!store.get("missing").error.has_value());
	std::remove(path.c_str());
}

TEST_CASE(a_deny_line_reports_access_denied)
{
	const std::string path = write_file("e2e_deny", "a\tkey-a\t0\tdeny\n");
	E2eFileSecretStore store(path);

	const auto lookup = store.get("a");
	REQUIRE(lookup.error.has_value());
	CHECK(lookup.error->kind == SecretError::Kind::AccessDenied);
	CHECK(!lookup.secret.has_value());
	std::remove(path.c_str());
}

TEST_CASE(a_delay_line_blocks_the_caller_for_that_long)
{
	const std::string path = write_file("e2e_delay", "a\tkey-a\t120\n");
	E2eFileSecretStore store(path);

	const auto start = std::chrono::steady_clock::now();
	CHECK_EQ(*store.get("a").secret, std::string("key-a"));
	CHECK(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(120));
	std::remove(path.c_str());
}

TEST_CASE(saving_is_refused)
{
	E2eFileSecretStore store("unused");
	CHECK(store.put("a", "b").has_value());
}
