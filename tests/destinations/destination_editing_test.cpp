#include "destinations/destination_editing.hpp"

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

Destination named(const std::string &id, const std::string &name)
{
	Destination destination;
	destination.id = id;
	destination.name = name;
	return destination;
}

} // namespace

TEST_CASE(a_new_destination_requires_a_key)
{
	CHECK(validate_stream_key_entry("", true).has_value());
	CHECK(!validate_stream_key_entry("live_abc", true).has_value());
}

TEST_CASE(a_blank_key_on_an_existing_destination_means_unchanged)
{
	CHECK(!validate_stream_key_entry("", false).has_value());
}

TEST_CASE(a_key_with_whitespace_is_rejected_without_echoing_it)
{
	const auto problem = validate_stream_key_entry("live abc\n", false);
	REQUIRE(problem.has_value());
	CHECK(problem->find("live") == std::string::npos);
}

TEST_CASE(adds_a_new_destination_and_stores_its_key)
{
	DestinationsConfig config;
	FakeRepository repository;
	InMemorySecretStore secrets;

	const SaveResult result = save_destination(config, repository, secrets, named("a", "A"), "live_a");

	CHECK(result.saved);
	REQUIRE(config.destinations.size() == 1);
	CHECK(repository.saved == config);
	CHECK_EQ(*secrets.get("a").secret, "live_a");
}

TEST_CASE(replaces_in_place_and_keeps_the_stored_key_without_a_new_one)
{
	DestinationsConfig config;
	config.destinations = {named("a", "A"), named("b", "B")};
	FakeRepository repository;
	InMemorySecretStore secrets;
	secrets.put("a", "live_a");

	const SaveResult result = save_destination(config, repository, secrets, named("a", "Renamed"), std::nullopt);

	CHECK(result.saved);
	REQUIRE(config.destinations.size() == 2);
	CHECK_EQ(config.destinations[0].name, "Renamed");
	CHECK_EQ(config.destinations[1].id, "b");
	CHECK_EQ(*secrets.get("a").secret, "live_a");
}

TEST_CASE(a_new_key_replaces_the_stored_one)
{
	DestinationsConfig config;
	config.destinations = {named("a", "A")};
	FakeRepository repository;
	InMemorySecretStore secrets;
	secrets.put("a", "old");

	save_destination(config, repository, secrets, named("a", "A"), "new");

	CHECK_EQ(*secrets.get("a").secret, "new");
}

TEST_CASE(a_failed_save_changes_nothing)
{
	DestinationsConfig config;
	config.destinations = {named("a", "A")};
	FakeRepository repository;
	repository.fail_saves = true;
	InMemorySecretStore secrets;
	secrets.put("a", "old");

	const SaveResult result = save_destination(config, repository, secrets, named("a", "Renamed"), "new");

	CHECK(!result.saved);
	CHECK(result.config_error.has_value());
	CHECK_EQ(config.destinations[0].name, "A");
	CHECK_EQ(*secrets.get("a").secret, "old");
}

TEST_CASE(a_key_that_cannot_be_stored_is_reported_after_saving)
{
	DestinationsConfig config;
	FakeRepository repository;
	UnavailableSecretStore secrets("no store here");

	const SaveResult result = save_destination(config, repository, secrets, named("a", "A"), "live_a");

	CHECK(result.saved);
	CHECK(result.secret_error.has_value());
	CHECK_EQ(config.destinations.size(), std::size_t{1});
}
