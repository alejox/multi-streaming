#include "destinations/session_selection.hpp"

#include "test_harness.hpp"

using namespace multistream;

namespace {

using Ids = std::vector<std::string>;

Destination destination(const std::string &id, bool enabled = true, bool sync = false)
{
	Destination result;
	result.id = id;
	result.enabled = enabled;
	result.sync_with_main = sync;
	return result;
}

} // namespace

TEST_CASE(start_all_takes_enabled_destinations_that_can_start)
{
	const std::vector<Destination> all{destination("idle"), destination("failed"), destination("live"),
					   destination("stopping"), destination("disabled", false),
					   destination("unknown")};
	const SessionStates states{{"idle", SessionState::Idle},
				   {"failed", SessionState::Failed},
				   {"live", SessionState::Live},
				   {"stopping", SessionState::Stopping},
				   {"disabled", SessionState::Idle}};
	CHECK(select_for_start_all(all, states) == (Ids{"idle", "failed", "unknown"}));
}

TEST_CASE(stop_all_takes_everything_running_even_if_disabled)
{
	const std::vector<Destination> all{destination("starting"), destination("live", false),
					   destination("reconnecting"), destination("stopping"), destination("idle"),
					   destination("failed"), destination("unknown")};
	const SessionStates states{{"starting", SessionState::Starting},
				   {"live", SessionState::Live},
				   {"reconnecting", SessionState::Reconnecting},
				   {"stopping", SessionState::Stopping},
				   {"idle", SessionState::Idle},
				   {"failed", SessionState::Failed}};
	CHECK(select_for_stop_all(all, states) == (Ids{"starting", "live", "reconnecting"}));
}

TEST_CASE(main_start_takes_only_enabled_synced_destinations_that_can_start)
{
	const std::vector<Destination> all{destination("synced", true, true), destination("manual", true, false),
					   destination("disabled", false, true), destination("live", true, true),
					   destination("failed", true, true)};
	const SessionStates states{{"live", SessionState::Live}, {"failed", SessionState::Failed}};
	CHECK(select_for_main_start(all, states) == (Ids{"synced", "failed"}));
}

TEST_CASE(main_stop_leaves_manual_destinations_running)
{
	const std::vector<Destination> all{destination("synced", true, true), destination("manual", true, false),
					   destination("synced_disabled", false, true), destination("idle", true, true)};
	const SessionStates states{{"synced", SessionState::Live},
				   {"manual", SessionState::Live},
				   {"synced_disabled", SessionState::Reconnecting}};
	CHECK(select_for_main_stop(all, states) == (Ids{"synced", "synced_disabled"}));
}

TEST_CASE(row_actions_follow_the_session_state)
{
	CHECK(row_actions(SessionState::Idle).can_start);
	CHECK(!row_actions(SessionState::Idle).can_stop);
	CHECK(row_actions(SessionState::Idle).can_edit);

	CHECK(row_actions(SessionState::Failed).can_start);
	CHECK(!row_actions(SessionState::Failed).can_stop);
	CHECK(row_actions(SessionState::Failed).can_edit);

	for (const SessionState running : {SessionState::Starting, SessionState::Live, SessionState::Reconnecting}) {
		CHECK(!row_actions(running).can_start);
		CHECK(row_actions(running).can_stop);
		CHECK(!row_actions(running).can_edit);
	}

	CHECK(!row_actions(SessionState::Stopping).can_start);
	CHECK(!row_actions(SessionState::Stopping).can_stop);
	CHECK(!row_actions(SessionState::Stopping).can_edit);
}
