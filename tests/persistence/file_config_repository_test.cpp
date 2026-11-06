#include "persistence/file_config_repository.hpp"

#include "destinations/config_codec.hpp"
#include "test_harness.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace multistream;
namespace fs = std::filesystem;

namespace {

class TempDirectory {
public:
	TempDirectory()
	{
		std::string pattern = (fs::temp_directory_path() / "multistream-test-XXXXXX").string();
		REQUIRE(mkdtemp(pattern.data()) != nullptr);
		path_ = pattern;
	}
	~TempDirectory()
	{
		std::error_code ignored;
		fs::permissions(path_, fs::perms::owner_all, ignored);
		fs::remove_all(path_, ignored);
	}
	const fs::path &path() const { return path_; }

private:
	fs::path path_;
};

std::string read_file(const fs::path &path)
{
	std::ifstream file(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_file(const fs::path &path, const std::string &content)
{
	std::ofstream file(path, std::ios::binary);
	file << content;
}

std::vector<std::string> names_in(const fs::path &directory)
{
	std::vector<std::string> names;
	for (const auto &entry : fs::directory_iterator(directory)) {
		names.push_back(entry.path().filename().string());
	}
	return names;
}

DestinationsConfig sample_config()
{
	Destination destination;
	destination.id = "dest-1";
	destination.name = "Local";
	destination.server_url = "rtmp://127.0.0.1/live";
	DestinationsConfig config;
	config.destinations.push_back(destination);
	return config;
}

} // namespace

TEST_CASE(missing_file_loads_empty_without_a_problem)
{
	TempDirectory directory;
	FileConfigRepository repository(directory.path() / "destinations.json");
	const ConfigLoad load = repository.load();
	CHECK(load.config.destinations.empty());
	CHECK(!load.problem.has_value());
}

TEST_CASE(saves_and_loads_a_config_creating_parent_directories)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "nested" / "dir" / "destinations.json";
	FileConfigRepository repository(path);
	repository.load();

	CHECK(!repository.save(sample_config()).has_value());

	FileConfigRepository reopened(path);
	const ConfigLoad load = reopened.load();
	CHECK(!load.problem.has_value());
	CHECK(load.config == sample_config());
}

TEST_CASE(save_leaves_no_temporary_files_and_is_private_to_the_user)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	FileConfigRepository repository(path);
	repository.load();
	REQUIRE(!repository.save(sample_config()).has_value());
	REQUIRE(!repository.save(DestinationsConfig{}).has_value());

	CHECK(names_in(directory.path()) == (std::vector<std::string>{"destinations.json"}));
	struct stat info {};
	REQUIRE(stat(path.c_str(), &info) == 0);
	CHECK_EQ(static_cast<unsigned>(info.st_mode & 0777), 0600u);
}

TEST_CASE(failed_save_keeps_the_previous_file_and_leaves_no_temporary_files)
{
	if (geteuid() == 0) {
		return; // Root ignores directory permissions.
	}
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	FileConfigRepository repository(path);
	repository.load();
	REQUIRE(!repository.save(sample_config()).has_value());
	const std::string before = read_file(path);

	fs::permissions(directory.path(), fs::perms::owner_read | fs::perms::owner_exec);
	CHECK(repository.save(DestinationsConfig{}).has_value());
	fs::permissions(directory.path(), fs::perms::owner_all);

	CHECK_EQ(read_file(path), before);
	CHECK(names_in(directory.path()) == (std::vector<std::string>{"destinations.json"}));
}

TEST_CASE(corrupt_file_is_preserved_aside_and_reported)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	write_file(path, "{not json");

	FileConfigRepository repository(path);
	const ConfigLoad load = repository.load();

	CHECK(load.config.destinations.empty());
	REQUIRE(load.problem.has_value());
	CHECK(load.problem->find("destinations.json.corrupt-1") != std::string::npos);
	CHECK(!fs::exists(path));
	CHECK_EQ(read_file(directory.path() / "destinations.json.corrupt-1"), "{not json");
}

TEST_CASE(saving_after_corruption_never_touches_the_preserved_file)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	write_file(path, "first bad");

	FileConfigRepository repository(path);
	repository.load();
	CHECK(!repository.save(sample_config()).has_value());
	CHECK_EQ(read_file(directory.path() / "destinations.json.corrupt-1"), "first bad");
	CHECK(FileConfigRepository(path).load().config == sample_config());
}

TEST_CASE(repeated_corruption_does_not_overwrite_earlier_copies)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	write_file(path, "first bad");
	FileConfigRepository(path).load();
	write_file(path, "second bad");
	const ConfigLoad load = FileConfigRepository(path).load();

	REQUIRE(load.problem.has_value());
	CHECK_EQ(read_file(directory.path() / "destinations.json.corrupt-1"), "first bad");
	CHECK_EQ(read_file(directory.path() / "destinations.json.corrupt-2"), "second bad");
}

TEST_CASE(newer_schema_is_preserved_aside_and_reported)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	const std::string newer = R"({"schema_version": 99, "destinations": []})";
	write_file(path, newer);

	const ConfigLoad load = FileConfigRepository(path).load();

	REQUIRE(load.problem.has_value());
	CHECK(load.problem->find("newer") != std::string::npos);
	CHECK_EQ(read_file(directory.path() / "destinations.json.corrupt-1"), newer);
}

TEST_CASE(unreadable_config_blocks_saving_so_it_is_not_overwritten)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	fs::create_directory(path); // Opening a directory as a file fails to read.

	FileConfigRepository repository(path);
	const ConfigLoad load = repository.load();
	CHECK(load.problem.has_value());
	CHECK(repository.save(sample_config()).has_value());
	CHECK(fs::is_directory(path));
}

TEST_CASE(corrupt_file_that_cannot_be_moved_blocks_saving)
{
	if (geteuid() == 0) {
		return; // Root ignores directory permissions.
	}
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	write_file(path, "{not json");
	fs::permissions(directory.path(), fs::perms::owner_read | fs::perms::owner_exec);

	FileConfigRepository repository(path);
	const ConfigLoad load = repository.load();
	CHECK(load.problem.has_value());
	CHECK(repository.save(sample_config()).has_value());

	fs::permissions(directory.path(), fs::perms::owner_all);
	CHECK_EQ(read_file(path), "{not json");
}

TEST_CASE(saving_before_loading_is_refused)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	write_file(path, "{not json");

	FileConfigRepository repository(path);
	CHECK(repository.save(sample_config()).has_value());
	CHECK_EQ(read_file(path), "{not json");
}

TEST_CASE(saved_file_never_contains_a_key_placed_in_the_input)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	write_file(path, R"({"schema_version": 1, "destinations": [
		{"id": "a", "name": "n", "server_url": "rtmp://h/a", "stream_key": "live_SECRET"}]})");

	FileConfigRepository repository(path);
	const ConfigLoad load = repository.load();
	REQUIRE(!load.problem.has_value());
	REQUIRE(!repository.save(load.config).has_value());
	CHECK(read_file(path).find("live_SECRET") == std::string::npos);
}
