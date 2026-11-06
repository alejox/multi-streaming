#include "persistence/vertical_settings_file.hpp"

#include "test_harness.hpp"

#include <filesystem>
#include <fstream>

using namespace multistream;

namespace {

std::filesystem::path fresh_directory(const char *name)
{
	const std::filesystem::path directory = std::filesystem::temp_directory_path() / name;
	std::filesystem::remove_all(directory);
	return directory;
}

} // namespace

TEST_CASE(a_missing_file_gives_defaults)
{
	const VerticalSettings settings = load_vertical_settings(fresh_directory("vsf_missing") / "v.json");
	CHECK_EQ(settings.width, 1080);
	CHECK_EQ(settings.height, 1920);
}

TEST_CASE(saved_settings_load_back_in_a_directory_that_did_not_exist)
{
	const auto directory = fresh_directory("vsf_round");
	VerticalSettings settings;
	settings.width = 720;
	settings.height = 1280;
	settings.active_scene = "Game";

	CHECK(!save_vertical_settings(directory / "v.json", settings).has_value());
	const VerticalSettings loaded = load_vertical_settings(directory / "v.json");
	CHECK_EQ(loaded.width, 720);
	CHECK_EQ(loaded.active_scene, std::string("Game"));
	std::filesystem::remove_all(directory);
}

TEST_CASE(a_corrupt_file_gives_defaults)
{
	const auto directory = fresh_directory("vsf_corrupt");
	std::filesystem::create_directories(directory);
	std::ofstream(directory / "v.json") << "{not json";
	CHECK_EQ(load_vertical_settings(directory / "v.json").width, 1080);
	std::filesystem::remove_all(directory);
}
