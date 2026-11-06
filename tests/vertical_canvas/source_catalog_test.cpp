#include "vertical_canvas/source_catalog.hpp"

#include "test_harness.hpp"

using namespace multistream;

namespace {

InputType type(const char *id, const char *name, bool has_video = true, bool disabled = false, bool deprecated = false)
{
	InputType result;
	result.id = id;
	result.unversioned_id = id;
	result.display_name = name;
	result.has_video = has_video;
	result.disabled = disabled;
	result.deprecated = deprecated;
	return result;
}

} // namespace

TEST_CASE(audio_only_types_are_not_offered)
{
	const auto types = creatable_video_types({type("mic", "Audio Input", false), type("image", "Image")});
	REQUIRE(types.size() == 1);
	CHECK_EQ(types[0].id, "image");
}

TEST_CASE(disabled_and_deprecated_types_are_not_offered)
{
	const auto types = creatable_video_types(
		{type("scene", "Scene", true, true), type("old", "Old", true, false, true), type("new", "New")});
	REQUIRE(types.size() == 1);
	CHECK_EQ(types[0].id, "new");
}

TEST_CASE(types_are_sorted_by_name_ignoring_case)
{
	const auto types = creatable_video_types({type("c", "color"), type("b", "Browser"), type("a", "Image")});
	REQUIRE(types.size() == 3);
	CHECK_EQ(types[0].id, "b");
	CHECK_EQ(types[1].id, "c");
	CHECK_EQ(types[2].id, "a");
}

TEST_CASE(an_empty_query_keeps_every_name_sorted)
{
	const auto names = filter_names({"webcam", "Camera", "Banner"}, "");
	REQUIRE(names.size() == 3);
	CHECK_EQ(names[0], "Banner");
	CHECK_EQ(names[2], "webcam");
}

TEST_CASE(a_query_matches_anywhere_ignoring_case_and_spaces)
{
	const auto names = filter_names({"Webcam", "Main Camera", "Banner"}, "  CAM ");
	REQUIRE(names.size() == 2);
	CHECK_EQ(names[0], "Main Camera");
	CHECK_EQ(names[1], "Webcam");
}

TEST_CASE(a_query_with_no_match_gives_nothing)
{
	CHECK(filter_names({"Webcam"}, "zzz").empty());
}
