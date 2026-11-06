#include "destinations/encoder_plan.hpp"

#include "test_harness.hpp"

using namespace multistream;

namespace {

Destination shared(const std::string &id, VideoSource source = VideoSource::Main, int audio_track = 1)
{
	Destination destination;
	destination.id = id;
	destination.video_source = source;
	destination.audio_track = audio_track;
	return destination;
}

Destination custom(const std::string &id, int bitrate, const std::string &encoder_id,
		   VideoSource source = VideoSource::Main)
{
	Destination destination = shared(id, source);
	destination.encoder.mode = EncoderMode::Custom;
	destination.encoder.video_bitrate_kbps = bitrate;
	destination.encoder.video_encoder_id = encoder_id;
	return destination;
}

MainStreamEncoders active_main()
{
	MainStreamEncoders main_stream;
	main_stream.active = true;
	main_stream.video_encoder_id = "x264";
	main_stream.video_bitrate_kbps = 6000;
	main_stream.audio_track = 1;
	return main_stream;
}

} // namespace

TEST_CASE(no_destinations_yield_no_assignments)
{
	CHECK(plan_encoders({}, active_main()).empty());
}

TEST_CASE(assignments_keep_the_given_order_and_describe_the_destination)
{
	const auto plan = plan_encoders({shared("a", VideoSource::Vertical, 3), custom("b", 4000, "vt")}, {});
	REQUIRE(plan.size() == 2);
	CHECK_EQ(plan[0].destination_id, "a");
	CHECK(plan[0].video_source == VideoSource::Vertical);
	CHECK_EQ(plan[0].audio_track, 3);
	CHECK_EQ(plan[1].destination_id, "b");
	CHECK_EQ(plan[1].video_encoder.video_bitrate_kbps, 4000);
	CHECK_EQ(plan[1].video_encoder.video_encoder_id, "vt");
}

TEST_CASE(destinations_in_one_encoder_group_share_a_video_key)
{
	const auto plan = plan_encoders({shared("a"), custom("b", 6000, "x264"), shared("c"), custom("d", 6000, "x264")}, {});
	REQUIRE(plan.size() == 4);
	CHECK_EQ(plan[0].video_key, plan[2].video_key);
	CHECK_EQ(plan[1].video_key, plan[3].video_key);
	CHECK(plan[0].video_key != plan[1].video_key);
}

TEST_CASE(video_keys_differ_by_source_bitrate_and_encoder)
{
	const auto plan = plan_encoders({shared("a"), shared("b", VideoSource::Vertical), custom("c", 6000, "x264"),
					 custom("d", 4000, "x264"), custom("e", 6000, "vt"),
					 custom("f", 6000, "x264", VideoSource::Vertical)},
					{});
	REQUIRE(plan.size() == 6);
	for (std::size_t first = 0; first < plan.size(); ++first) {
		for (std::size_t second = first + 1; second < plan.size(); ++second) {
			CHECK(plan[first].video_key != plan[second].video_key);
		}
	}
}

TEST_CASE(video_keys_are_stable_across_separate_plans)
{
	const auto first = plan_encoders({shared("a"), custom("b", 6000, "x264")}, {});
	const auto second = plan_encoders({custom("c", 6000, "x264"), shared("d")}, {});
	REQUIRE(first.size() == 2);
	REQUIRE(second.size() == 2);
	CHECK_EQ(first[0].video_key, second[1].video_key);
	CHECK_EQ(first[1].video_key, second[0].video_key);
}

TEST_CASE(audio_is_shared_per_track_and_not_with_video)
{
	const auto plan = plan_encoders({shared("a", VideoSource::Main, 1), shared("b", VideoSource::Vertical, 1),
					 shared("c", VideoSource::Main, 2)},
					{});
	REQUIRE(plan.size() == 3);
	CHECK_EQ(plan[0].audio_key, plan[1].audio_key);
	CHECK(plan[0].audio_key != plan[2].audio_key);
	CHECK(plan[0].audio_key != plan[0].video_key);
}

TEST_CASE(nothing_reuses_an_inactive_main_stream)
{
	MainStreamEncoders inactive = active_main();
	inactive.active = false;
	const auto plan = plan_encoders({shared("a"), custom("b", 6000, "x264")}, inactive);
	REQUIRE(plan.size() == 2);
	CHECK(!plan[0].reuse_main_video);
	CHECK(!plan[0].reuse_main_audio);
	CHECK(!plan[1].reuse_main_video);
}

TEST_CASE(share_on_the_main_mix_reuses_the_active_main_encoder)
{
	const auto plan = plan_encoders({shared("a")}, active_main());
	REQUIRE(plan.size() == 1);
	CHECK(plan[0].reuse_main_video);
}

TEST_CASE(share_reuses_a_scaled_main_encoder_because_it_asks_for_what_main_sends)
{
	MainStreamEncoders scaled = active_main();
	scaled.video_scaled = true;
	const auto plan = plan_encoders({shared("a")}, scaled);
	REQUIRE(plan.size() == 1);
	CHECK(plan[0].reuse_main_video);
}

TEST_CASE(vertical_never_reuses_the_main_video_encoder)
{
	const auto plan = plan_encoders({shared("a", VideoSource::Vertical), custom("b", 6000, "x264", VideoSource::Vertical)},
					active_main());
	REQUIRE(plan.size() == 2);
	CHECK(!plan[0].reuse_main_video);
	CHECK(!plan[1].reuse_main_video);
}

TEST_CASE(custom_reuses_the_main_encoder_only_on_an_exact_match)
{
	const auto plan = plan_encoders(
		{custom("match", 6000, "x264"), custom("rate", 4000, "x264"), custom("id", 6000, "vt")}, active_main());
	REQUIRE(plan.size() == 3);
	CHECK(plan[0].reuse_main_video);
	CHECK(!plan[1].reuse_main_video);
	CHECK(!plan[2].reuse_main_video);
}

TEST_CASE(custom_does_not_reuse_a_scaled_main_encoder)
{
	MainStreamEncoders scaled = active_main();
	scaled.video_scaled = true;
	const auto plan = plan_encoders({custom("a", 6000, "x264")}, scaled);
	REQUIRE(plan.size() == 1);
	CHECK(!plan[0].reuse_main_video);
}

TEST_CASE(audio_reuses_the_main_encoder_only_on_its_track)
{
	const auto plan = plan_encoders({shared("a", VideoSource::Vertical, 1), shared("b", VideoSource::Main, 2)},
					active_main());
	REQUIRE(plan.size() == 2);
	CHECK(plan[0].reuse_main_audio);
	CHECK(!plan[1].reuse_main_audio);
}

TEST_CASE(audio_is_dedicated_when_the_main_track_is_unknown)
{
	MainStreamEncoders unknown = active_main();
	unknown.audio_track.reset();
	const auto plan = plan_encoders({shared("a")}, unknown);
	REQUIRE(plan.size() == 1);
	CHECK(!plan[0].reuse_main_audio);
}
