#include "destinations/destination_ordering.hpp"

#include "test_harness.hpp"

#include <string>
#include <vector>

using namespace multistream;

namespace {

std::vector<Destination> named(const std::vector<std::string> &ids)
{
	std::vector<Destination> destinations;
	for (const std::string &id : ids) {
		Destination destination;
		destination.id = id;
		destinations.push_back(destination);
	}
	return destinations;
}

std::vector<std::string> ids_of(const std::vector<Destination> &destinations)
{
	std::vector<std::string> ids;
	for (const Destination &destination : destinations) {
		ids.push_back(destination.id);
	}
	return ids;
}

} // namespace

TEST_CASE(moves_a_destination_down_before_a_later_slot)
{
	std::vector<Destination> destinations = named({"a", "b", "c", "d"});

	CHECK(move_destination_before(destinations, "a", 3));

	CHECK(ids_of(destinations) == std::vector<std::string>({"b", "c", "a", "d"}));
}

TEST_CASE(moves_a_destination_to_the_end)
{
	std::vector<Destination> destinations = named({"a", "b", "c"});

	CHECK(move_destination_before(destinations, "a", 3));

	CHECK(ids_of(destinations) == std::vector<std::string>({"b", "c", "a"}));
}

TEST_CASE(moves_a_destination_up_to_the_start)
{
	std::vector<Destination> destinations = named({"a", "b", "c"});

	CHECK(move_destination_before(destinations, "c", 0));

	CHECK(ids_of(destinations) == std::vector<std::string>({"c", "a", "b"}));
}

TEST_CASE(dropping_on_its_own_slot_changes_nothing)
{
	std::vector<Destination> destinations = named({"a", "b", "c"});

	CHECK(!move_destination_before(destinations, "b", 1));
	CHECK(!move_destination_before(destinations, "b", 2));

	CHECK(ids_of(destinations) == std::vector<std::string>({"a", "b", "c"}));
}

TEST_CASE(unknown_id_or_out_of_range_slot_changes_nothing)
{
	std::vector<Destination> destinations = named({"a", "b"});

	CHECK(!move_destination_before(destinations, "missing", 0));
	CHECK(!move_destination_before(destinations, "a", 3));
	CHECK(!move_destination_before(destinations, "a", -1));

	CHECK(ids_of(destinations) == std::vector<std::string>({"a", "b"}));
}
