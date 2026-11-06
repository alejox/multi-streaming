#include "destinations/encoder_groups.hpp"

#include "test_harness.hpp"

using namespace multistream;

namespace {

Destination shared(const std::string &id, VideoSource source)
{
	Destination destination;
	destination.id = id;
	destination.video_source = source;
	return destination;
}

Destination custom(const std::string &id, VideoSource source, int bitrate, const std::string &encoder_id)
{
	Destination destination = shared(id, source);
	destination.encoder.mode = EncoderMode::Custom;
	destination.encoder.video_bitrate_kbps = bitrate;
	destination.encoder.video_encoder_id = encoder_id;
	return destination;
}

} // namespace

TEST_CASE(no_destinations_yield_no_groups)
{
	CHECK(group_by_encoder({}).empty());
}

TEST_CASE(shared_destinations_on_one_source_form_one_group)
{
	const auto groups = group_by_encoder({shared("a", VideoSource::Main), shared("b", VideoSource::Main)});
	REQUIRE(groups.size() == 1);
	CHECK(groups[0].video_source == VideoSource::Main);
	CHECK(groups[0].encoder.mode == EncoderMode::Share);
	CHECK(groups[0].destination_ids == (std::vector<std::string>{"a", "b"}));
}

TEST_CASE(sources_never_share_an_encoder)
{
	const auto groups = group_by_encoder({shared("a", VideoSource::Main), shared("b", VideoSource::Vertical),
					      custom("c", VideoSource::Main, 6000, "x264"),
					      custom("d", VideoSource::Vertical, 6000, "x264")});
	CHECK_EQ(groups.size(), std::size_t{4});
}

TEST_CASE(identical_custom_settings_group_and_different_ones_split)
{
	const auto groups = group_by_encoder({custom("a", VideoSource::Main, 6000, "x264"),
					      custom("b", VideoSource::Main, 6000, "x264"),
					      custom("c", VideoSource::Main, 4000, "x264"),
					      custom("d", VideoSource::Main, 6000, "vt")});
	REQUIRE(groups.size() == 3);
	CHECK(groups[0].destination_ids == (std::vector<std::string>{"a", "b"}));
	CHECK(groups[1].destination_ids == (std::vector<std::string>{"c"}));
	CHECK(groups[2].destination_ids == (std::vector<std::string>{"d"}));
}

TEST_CASE(shared_mode_ignores_leftover_custom_fields)
{
	Destination leftover = shared("b", VideoSource::Main);
	leftover.encoder.video_bitrate_kbps = 6000;
	leftover.encoder.video_encoder_id = "x264";
	const auto groups = group_by_encoder({shared("a", VideoSource::Main), leftover});
	REQUIRE(groups.size() == 1);
	CHECK_EQ(groups[0].encoder.video_bitrate_kbps, 0);
	CHECK(groups[0].encoder.video_encoder_id.empty());
}

TEST_CASE(shared_and_custom_with_equal_fields_stay_separate)
{
	Destination leftover = shared("a", VideoSource::Main);
	leftover.encoder.video_bitrate_kbps = 6000;
	leftover.encoder.video_encoder_id = "x264";
	const auto groups = group_by_encoder({leftover, custom("b", VideoSource::Main, 6000, "x264")});
	CHECK_EQ(groups.size(), std::size_t{2});
}

TEST_CASE(audio_track_does_not_split_a_video_encoder_group)
{
	Destination first = shared("a", VideoSource::Main);
	Destination second = shared("b", VideoSource::Main);
	second.audio_track = 4;
	CHECK_EQ(group_by_encoder({first, second}).size(), std::size_t{1});
}
