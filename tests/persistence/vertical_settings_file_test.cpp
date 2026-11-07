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

TEST_CASE(a_save_keeps_the_previous_version_as_a_backup)
{
	const auto directory = fresh_directory("vsf_backup");
	VerticalSettings first;
	first.width = 720;
	first.height = 1280;
	VerticalSettings second = first;
	second.width = 1080;
	second.height = 1920;

	CHECK(!save_vertical_settings(directory / "v.json", first).has_value());
	CHECK(!save_vertical_settings(directory / "v.json", second).has_value());

	CHECK(std::filesystem::exists(directory / "v.json.bak"));
	CHECK_EQ(load_vertical_settings(directory / "v.json").width, 1080);
	std::filesystem::remove_all(directory);
}

TEST_CASE(a_corrupt_file_is_recovered_from_the_backup)
{
	const auto directory = fresh_directory("vsf_recover");
	VerticalSettings first;
	first.width = 720;
	first.height = 1280;
	first.active_scene = "Game";
	VerticalSettings second = first;
	second.active_scene = "Chat";

	CHECK(!save_vertical_settings(directory / "v.json", first).has_value());
	CHECK(!save_vertical_settings(directory / "v.json", second).has_value());
	std::ofstream(directory / "v.json") << "{not json";

	const VerticalSettings loaded = load_vertical_settings(directory / "v.json");
	CHECK_EQ(loaded.width, 720);
	CHECK_EQ(loaded.active_scene, std::string("Game"));
	std::filesystem::remove_all(directory);
}

TEST_CASE(save_leaves_no_temporary_files)
{
	const auto directory = fresh_directory("vsf_temp");
	CHECK(!save_vertical_settings(directory / "v.json", VerticalSettings{}).has_value());
	CHECK(!save_vertical_settings(directory / "v.json", VerticalSettings{}).has_value());

	int files = 0;
	for (const auto &entry : std::filesystem::directory_iterator(directory)) {
		const std::string name = entry.path().filename().string();
		CHECK(name == "v.json" || name == "v.json.bak");
		++files;
	}
	CHECK_EQ(files, 2);
	std::filesystem::remove_all(directory);
}
