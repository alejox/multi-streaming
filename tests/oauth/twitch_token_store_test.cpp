#include "oauth/twitch_token_store.hpp"

#include "test_harness.hpp"

#include <map>

using namespace multistream;

namespace {

class FakeSecretStore final : public SecretStore {
public:
	std::optional<SecretError> put(const std::string &destination_id, const std::string &secret) override
	{
		if (fail_put) {
			return SecretError{SecretError::Kind::Failure, "put"};
		}
		values[destination_id] = secret;
		return std::nullopt;
	}

	SecretLookup get(const std::string &destination_id) override
	{
		const auto found = values.find(destination_id);
		if (found == values.end()) {
			return SecretLookup{std::nullopt, std::nullopt};
		}
		return SecretLookup{found->second, std::nullopt};
	}

	std::optional<SecretError> remove(const std::string &destination_id) override
	{
		values.erase(destination_id);
		return std::nullopt;
	}

	std::map<std::string, std::string> values;
	bool fail_put = false;
};

} // namespace

TEST_CASE(refresh_token_key_is_suffixed_and_distinct)
{
	CHECK_EQ(twitch_refresh_token_key("abc123"), "abc123:twitch-refresh");
	CHECK(twitch_refresh_token_key("abc123") != "abc123");
}

TEST_CASE(token_store_round_trips_through_secret_store)
{
	FakeSecretStore secrets;
	TwitchTokenStore store(secrets);

	CHECK(!store.put("dest1", "rt-1").has_value());
	const SecretLookup lookup = store.get("dest1");
	CHECK(lookup.secret.has_value());
	CHECK_EQ(lookup.secret.value_or(""), "rt-1");

	// The refresh token lives under a different key than the destination id.
	CHECK_EQ(secrets.values.count("dest1"), 0u);
	CHECK_EQ(secrets.values.count("dest1:twitch-refresh"), 1u);
}

TEST_CASE(token_store_remove_clears_the_token)
{
	FakeSecretStore secrets;
	TwitchTokenStore store(secrets);

	store.put("dest1", "rt-1");
	CHECK(!store.remove("dest1").has_value());
	CHECK(!store.get("dest1").secret.has_value());
}

TEST_CASE(token_store_reports_put_failures)
{
	FakeSecretStore secrets;
	secrets.fail_put = true;
	TwitchTokenStore store(secrets);

	const auto error = store.put("dest1", "rt-1");
	CHECK(error.has_value());
	CHECK(error->kind == SecretError::Kind::Failure);
}
