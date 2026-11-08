#include "persistence/update_settings_file.hpp"
#include "update/update_settings.hpp"

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

TEST_CASE(update_settings_round_trip)
{
	UpdateSettings off;
	off.auto_install = false;
	const auto decoded = decode_update_settings(encode_update_settings(off));
	REQUIRE(decoded.has_value());
	CHECK(!decoded->auto_install);
}

TEST_CASE(decode_update_settings_defaults_missing_or_odd_fields_to_on)
{
	CHECK(decode_update_settings("{}").value_or(UpdateSettings{false}).auto_install);
	CHECK(decode_update_settings(R"({"auto_install":"no"})").value_or(UpdateSettings{false}).auto_install);
	CHECK(!decode_update_settings("[]").has_value());
	CHECK(!decode_update_settings("{oops").has_value());
}

TEST_CASE(update_settings_file_defaults_to_on_when_missing)
{
	CHECK(load_update_settings(fresh_directory("usf_missing") / "update.json").auto_install);
}

TEST_CASE(update_settings_file_saves_into_a_new_directory_and_loads_back)
{
	const auto directory = fresh_directory("usf_round");
	CHECK(!save_update_settings(directory / "update.json", UpdateSettings{false}).has_value());
	CHECK(!load_update_settings(directory / "update.json").auto_install);
	std::filesystem::remove_all(directory);
}

TEST_CASE(update_settings_file_falls_back_to_the_backup)
{
	const auto directory = fresh_directory("usf_backup");
	CHECK(!save_update_settings(directory / "update.json", UpdateSettings{false}).has_value());
	CHECK(!save_update_settings(directory / "update.json", UpdateSettings{false}).has_value());
	std::ofstream(directory / "update.json", std::ios::trunc) << "{broken";
	CHECK(!load_update_settings(directory / "update.json").auto_install);
	std::filesystem::remove_all(directory);
}
