#include "destinations/destination_removal.hpp"

#include "secrets/in_memory_secret_store.hpp"
#include "secrets/unavailable_secret_store.hpp"
#include "test_harness.hpp"

using namespace multistream;

namespace {

class FakeRepository final : public ConfigRepository {
public:
	ConfigLoad load() override { return {saved, std::nullopt}; }
	std::optional<std::string> save(const DestinationsConfig &config) override
	{
		if (fail_saves) {
			return "disk full";
		}
		saved = config;
		return std::nullopt;
	}

	DestinationsConfig saved;
	bool fail_saves = false;
};

DestinationsConfig two_destinations()
{
	Destination first;
	first.id = "first";
	Destination second;
	second.id = "second";
	DestinationsConfig config;
	config.destinations = {first, second};
	return config;
}

} // namespace

TEST_CASE(removes_the_destination_and_its_secret_only)
{
	DestinationsConfig config = two_destinations();
	FakeRepository repository;
	InMemorySecretStore secrets;
	secrets.put("first", "live_first");
	secrets.put("second", "live_second");

	const RemovalResult result = remove_destination(config, repository, secrets, "first");

	CHECK(result.removed);
	CHECK(!result.config_error.has_value());
	CHECK(!result.secret_error.has_value());
	REQUIRE(config.destinations.size() == 1);
	CHECK_EQ(config.destinations[0].id, "second");
	CHECK(repository.saved == config);
	CHECK(!secrets.get("first").secret.has_value());
	CHECK_EQ(secrets.get("second").secret.value_or(""), "live_second");
}

TEST_CASE(save_failure_keeps_the_destination_and_its_secret)
{
	DestinationsConfig config = two_destinations();
	FakeRepository repository;
	repository.fail_saves = true;
	InMemorySecretStore secrets;
	secrets.put("first", "live_first");

	const RemovalResult result = remove_destination(config, repository, secrets, "first");

	CHECK(!result.removed);
	CHECK(result.config_error.has_value());
	CHECK(config == two_destinations());
	CHECK_EQ(secrets.get("first").secret.value_or(""), "live_first");
}

TEST_CASE(secret_failure_is_reported_after_the_destination_is_gone)
{
	DestinationsConfig config = two_destinations();
	FakeRepository repository;
	UnavailableSecretStore secrets("keychain locked");

	const RemovalResult result = remove_destination(config, repository, secrets, "first");

	CHECK(result.removed);
	REQUIRE(result.secret_error.has_value());
	CHECK_EQ(result.secret_error->message, "keychain locked");
	CHECK_EQ(config.destinations.size(), std::size_t{1});
}

TEST_CASE(unknown_destination_changes_nothing)
{
	DestinationsConfig config = two_destinations();
	FakeRepository repository;
	InMemorySecretStore secrets;
	secrets.put("first", "live_first");

	const RemovalResult result = remove_destination(config, repository, secrets, "missing");

	CHECK(!result.removed);
	CHECK(config == two_destinations());
	CHECK(repository.saved == DestinationsConfig{});
	CHECK_EQ(secrets.get("first").secret.value_or(""), "live_first");
}
