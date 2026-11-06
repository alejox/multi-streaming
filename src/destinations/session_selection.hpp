// Which destinations a bulk action applies to, and what one row may do. `enabled`
// only decides membership in the bulk actions; an explicit start of a row still works.
#pragma once

#include "destinations/destination.hpp"
#include "destinations/session.hpp"

#include <map>
#include <string>
#include <vector>

namespace multistream {

// A destination without an entry counts as idle.
using SessionStates = std::map<std::string, SessionState>;

std::vector<std::string> select_for_start_all(const std::vector<Destination> &destinations,
					      const SessionStates &states);
std::vector<std::string> select_for_stop_all(const std::vector<Destination> &destinations,
					     const SessionStates &states);

std::vector<std::string> select_for_main_start(const std::vector<Destination> &destinations,
					       const SessionStates &states);
std::vector<std::string> select_for_main_stop(const std::vector<Destination> &destinations,
					      const SessionStates &states);

struct RowActions {
	bool can_start = false;
	bool can_stop = false;
	bool can_edit = false; // Also covers deleting.
};

RowActions row_actions(SessionState state);

} // namespace multistream
