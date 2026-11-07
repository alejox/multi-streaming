#include "destinations/destination_ordering.hpp"

#include <algorithm>
#include <iterator>

namespace multistream {

bool move_destination_before(std::vector<Destination> &destinations, const std::string &id, int insert_position)
{
	const int count = static_cast<int>(destinations.size());
	if (insert_position < 0 || insert_position > count) {
		return false;
	}

	const auto found = std::find_if(destinations.begin(), destinations.end(),
					[&](const Destination &destination) { return destination.id == id; });
	if (found == destinations.end()) {
		return false;
	}

	const int from = static_cast<int>(std::distance(destinations.begin(), found));
	// Removing the moved item shifts every later slot up by one.
	const int to = insert_position > from ? insert_position - 1 : insert_position;
	if (to == from) {
		return false;
	}

	if (from < to) {
		std::rotate(destinations.begin() + from, destinations.begin() + from + 1, destinations.begin() + to + 1);
	} else {
		std::rotate(destinations.begin() + to, destinations.begin() + from, destinations.begin() + from + 1);
	}
	return true;
}

} // namespace multistream
