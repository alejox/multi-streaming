#include "vertical_canvas/twitch_dual_format.hpp"

#include "test_harness.hpp"

using namespace multistream;

namespace {

TwitchDualFormatFacts ready_facts()
{
	TwitchDualFormatFacts facts;
	facts.service_is_twitch = true;
	facts.enhanced_broadcasting = true;
	facts.additional_canvas_uuid = "abc";
	facts.vertical_canvas_uuid = "abc";
	return facts;
}

} // namespace

TEST_CASE(other_services_say_nothing)
{
	TwitchDualFormatFacts facts = ready_facts();
	facts.service_is_twitch = false;
	CHECK_EQ(twitch_dual_format(facts), TwitchDualFormat::NotTwitch);
}

TEST_CASE(needs_enhanced_broadcasting_and_this_canvas)
{
	TwitchDualFormatFacts facts = ready_facts();
	CHECK_EQ(twitch_dual_format(facts), TwitchDualFormat::Ready);
	facts.enhanced_broadcasting = false;
	CHECK_EQ(twitch_dual_format(facts), TwitchDualFormat::Off);
	facts = ready_facts();
	facts.additional_canvas_uuid = "other";
	CHECK_EQ(twitch_dual_format(facts), TwitchDualFormat::Off);
	facts.additional_canvas_uuid.clear();
	facts.vertical_canvas_uuid.clear();
	CHECK_EQ(twitch_dual_format(facts), TwitchDualFormat::Off);
}

TEST_CASE(live_while_obs_streams)
{
	TwitchDualFormatFacts facts = ready_facts();
	facts.streaming = true;
	CHECK_EQ(twitch_dual_format(facts), TwitchDualFormat::Live);
}
