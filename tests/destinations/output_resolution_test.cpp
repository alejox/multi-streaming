#include "destinations/output_resolution.hpp"

#include "test_harness.hpp"

using namespace multistream;

TEST_CASE(canvas_output_is_not_scaled)
{
	CHECK(!scaled_output_size(1920, 1080, kCanvasOutput));
	CHECK(!scaled_output_size(1920, 1080, 1080));
	CHECK(!scaled_output_size(1080, 1920, 1080));
}

TEST_CASE(landscape_scales_by_height)
{
	CHECK((scaled_output_size(2560, 1440, 1080) == OutputSize{1920, 1080}));
	CHECK((scaled_output_size(3840, 2160, 1440) == OutputSize{2560, 1440}));
	CHECK((scaled_output_size(1920, 1080, 720) == OutputSize{1280, 720}));
	// Upscaling is allowed: it is the user's choice.
	CHECK((scaled_output_size(1920, 1080, 1440) == OutputSize{2560, 1440}));
}

TEST_CASE(portrait_scales_by_width)
{
	CHECK((scaled_output_size(1440, 2560, 1080) == OutputSize{1080, 1920}));
	CHECK((scaled_output_size(1080, 1920, 720) == OutputSize{720, 1280}));
}

TEST_CASE(sides_stay_even_on_odd_aspects)
{
	// 2560x1080 (21:9) at 720p: 1706.67 rounds to the nearest even width.
	const auto size = scaled_output_size(2560, 1080, 720);
	CHECK(size.has_value());
	CHECK_EQ(size->width % 2, 0);
	CHECK_EQ(size->width, 1706);
	CHECK_EQ(size->height, 720);
}

TEST_CASE(invalid_short_sides_are_refused)
{
	CHECK(is_valid_output_short_side(kCanvasOutput));
	for (const int preset : output_short_side_presets()) {
		CHECK(is_valid_output_short_side(preset));
	}
	CHECK(!is_valid_output_short_side(1081));
	CHECK(!is_valid_output_short_side(100));
	CHECK(!is_valid_output_short_side(8640));
	CHECK(!scaled_output_size(1920, 1080, 1081));
	CHECK(!scaled_output_size(0, 0, 720));
}
