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

TEST_CASE(only_running_destinations_on_the_given_source_are_selected)
{
	using multistream::VideoSource;
	std::vector<multistream::Destination> destinations(3);
	destinations[0].id = "main";
	destinations[1].id = "vertical-live";
	destinations[1].video_source = VideoSource::Vertical;
	destinations[2].id = "vertical-idle";
	destinations[2].video_source = VideoSource::Vertical;

	const std::map<std::string, multistream::SessionState> states = {
		{"main", multistream::SessionState::Live},
		{"vertical-live", multistream::SessionState::Reconnecting},
		{"vertical-idle", multistream::SessionState::Idle},
	};

	const std::vector<std::string> selected = multistream::select_running_on(destinations, states, VideoSource::Vertical);
	REQUIRE(selected.size() == 1u);
	CHECK_EQ(selected[0], std::string("vertical-live"));
}

TEST_CASE(a_destination_still_stopping_counts_as_running_on_its_source)
{
	using multistream::VideoSource;
	std::vector<multistream::Destination> destinations(1);
	destinations[0].id = "v";
	destinations[0].video_source = VideoSource::Vertical;
	const std::map<std::string, multistream::SessionState> states = {{"v", multistream::SessionState::Stopping}};
	CHECK_EQ(multistream::select_running_on(destinations, states, VideoSource::Vertical).size(), 1u);
}
