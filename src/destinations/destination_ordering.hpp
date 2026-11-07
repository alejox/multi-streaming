// Reordering of the destination list, as a drag and drop in the dock asks for it.
#pragma once

#include "destinations/destination.hpp"

#include <string>
#include <vector>

namespace multistream {

// Moves the destination with this id so it sits right before the destination currently at
// insert_position, counted in the list as it is now; insert_position == size() moves it to
// the end. Returns false, and leaves the list as it was, when the id is unknown, the
// position is out of range, or the move would not change the order.
bool move_destination_before(std::vector<Destination> &destinations, const std::string &id, int insert_position);

} // namespace multistream
