#include "destinations/session_selection.hpp"

namespace multistream {
namespace {

SessionState state_of(const Destination &destination, const SessionStates &states)
{
	const auto found = states.find(destination.id);
	return found == states.end() ? SessionState::Idle : found->second;
}

bool can_start(SessionState state)
{
	return state == SessionState::Idle || state == SessionState::Failed;
}

template<typename Predicate>
std::vector<std::string> select(const std::vector<Destination> &destinations, Predicate predicate)
{
	std::vector<std::string> selected;
	for (const Destination &destination : destinations) {
		if (predicate(destination)) {
			selected.push_back(destination.id);
		}
	}
	return selected;
}

} // namespace

std::vector<std::string> select_for_start_all(const std::vector<Destination> &destinations,
					      const SessionStates &states)
{
	return select(destinations, [&](const Destination &destination) {
		return destination.enabled && can_start(state_of(destination, states));
	});
}

std::vector<std::string> select_for_stop_all(const std::vector<Destination> &destinations, const SessionStates &states)
{
	return select(destinations,
		      [&](const Destination &destination) { return is_running(state_of(destination, states)); });
}

std::vector<std::string> select_for_main_start(const std::vector<Destination> &destinations,
					       const SessionStates &states)
{
	return select(destinations, [&](const Destination &destination) {
		return destination.enabled && destination.sync_with_main && can_start(state_of(destination, states));
	});
}

std::vector<std::string> select_for_main_stop(const std::vector<Destination> &destinations, const SessionStates &states)
{
	return select(destinations, [&](const Destination &destination) {
		return destination.sync_with_main && is_running(state_of(destination, states));
	});
}

RowActions row_actions(SessionState state)
{
	RowActions actions;
	actions.can_start = can_start(state);
	actions.can_stop = is_running(state);
	actions.can_edit = can_start(state);
	return actions;
}

std::vector<std::string> select_running_on(const std::vector<Destination> &destinations,
					   const std::map<std::string, SessionState> &states, VideoSource source)
{
	std::vector<std::string> selected;
	for (const Destination &destination : destinations) {
		const auto state = states.find(destination.id);
		if (destination.video_source == source && state != states.end() &&
		    (is_running(state->second) || state->second == SessionState::Stopping)) {
			selected.push_back(destination.id);
		}
	}
	return selected;
}

} // namespace multistream
