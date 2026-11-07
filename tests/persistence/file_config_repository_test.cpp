#include "persistence/file_config_repository.hpp"

#include "destinations/config_codec.hpp"
#include "test_harness.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <system_error>
#include <vector>

using namespace multistream;
namespace fs = std::filesystem;

namespace {

class TempDirectory {
public:
	TempDirectory()
	{
		std::random_device random;
		for (int attempt = 0; attempt < 100 && path_.empty(); ++attempt) {
			const fs::path candidate =
				fs::temp_directory_path() / ("multistream-test-" + std::to_string(random()));
			std::error_code error;
			if (fs::create_directory(candidate, error)) {
				path_ = candidate;
			}
		}
		REQUIRE(!path_.empty());
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

// While alive, makes moving or replacing the file in the directory fail. Directory
// permissions do that on POSIX. They do nothing on Windows, where an open handle that does
// not share delete access does it instead.
class ReplacementBlock {
public:
#if defined(_WIN32)
	ReplacementBlock(const fs::path &, const fs::path &file)
		: handle_(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				      FILE_ATTRIBUTE_NORMAL, nullptr))
	{
		REQUIRE(handle_ != INVALID_HANDLE_VALUE);
	}
	~ReplacementBlock() { CloseHandle(handle_); }

	static bool is_possible() { return true; }
#else
	ReplacementBlock(const fs::path &directory, const fs::path &) : directory_(directory)
	{
		fs::permissions(directory_, fs::perms::owner_read | fs::perms::owner_exec);
	}
	~ReplacementBlock() { fs::permissions(directory_, fs::perms::owner_all); }

	// Root ignores directory permissions.
	static bool is_possible() { return geteuid() != 0; }
#endif
	ReplacementBlock(const ReplacementBlock &) = delete;
	ReplacementBlock &operator=(const ReplacementBlock &) = delete;

private:
#if defined(_WIN32)
	HANDLE handle_;
#else
	fs::path directory_;
#endif
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
	std::sort(names.begin(), names.end());
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

	CHECK(names_in(directory.path()) ==
	      (std::vector<std::string>{"destinations.json", "destinations.json.bak"}));
#if !defined(_WIN32)
	for (const fs::path &file : {path, fs::path(path.string() + ".bak")}) {
		struct stat info {};
		REQUIRE(stat(file.c_str(), &info) == 0);
		CHECK_EQ(static_cast<unsigned>(info.st_mode & 0777), 0600u);
	}
#endif
}

TEST_CASE(failed_save_keeps_the_previous_file_and_leaves_no_temporary_files)
{
	if (!ReplacementBlock::is_possible()) {
		return;
	}
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	FileConfigRepository repository(path);
	repository.load();
	REQUIRE(!repository.save(sample_config()).has_value());
	const std::string before = read_file(path);

	{
		ReplacementBlock block(directory.path(), path);
		CHECK(repository.save(DestinationsConfig{}).has_value());
	}

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
	CHECK_EQ(load.problem->key, std::string("Config.UnusableReset"));
	REQUIRE(load.problem->args.size() == 2);
	CHECK(load.problem->args[1].find("destinations.json.corrupt-1") != std::string::npos);
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
	CHECK_EQ(load.problem->key, std::string("Config.UnusableReset"));
	REQUIRE(!load.problem->args.empty());
	CHECK(load.problem->args[0].find("newer") != std::string::npos);
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
	if (!ReplacementBlock::is_possible()) {
		return;
	}
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	write_file(path, "{not json");

	{
		ReplacementBlock block(directory.path(), path);
		FileConfigRepository repository(path);
		const ConfigLoad load = repository.load();
		CHECK(load.problem.has_value());
		CHECK(repository.save(sample_config()).has_value());
	}

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

DestinationsConfig other_config()
{
	DestinationsConfig config = sample_config();
	config.destinations.front().name = "Renamed";
	return config;
}

TEST_CASE(a_save_keeps_the_previous_version_as_a_backup)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	FileConfigRepository repository(path);
	repository.load();
	REQUIRE(!repository.save(sample_config()).has_value());
	CHECK(!fs::exists(directory.path() / "destinations.json.bak"));

	REQUIRE(!repository.save(other_config()).has_value());

	CHECK_EQ(read_file(directory.path() / "destinations.json.bak"), encode_config(sample_config()));
	CHECK_EQ(read_file(path), encode_config(other_config()));
}

TEST_CASE(a_corrupt_file_is_preserved_and_the_backup_is_restored)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	{
		FileConfigRepository repository(path);
		repository.load();
		REQUIRE(!repository.save(sample_config()).has_value());
		REQUIRE(!repository.save(other_config()).has_value());
	}
	write_file(path, "{not json");

	FileConfigRepository repository(path);
	const ConfigLoad load = repository.load();

	CHECK(load.config == sample_config());
	REQUIRE(load.problem.has_value());
	CHECK_EQ(load.problem->key, std::string("Config.UnusableRestored"));
	CHECK_EQ(read_file(directory.path() / "destinations.json.corrupt-1"), "{not json");
	CHECK(!repository.save(load.config).has_value());
}

TEST_CASE(a_missing_file_is_restored_from_the_backup)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	{
		FileConfigRepository repository(path);
		repository.load();
		REQUIRE(!repository.save(sample_config()).has_value());
		REQUIRE(!repository.save(other_config()).has_value());
	}
	fs::remove(path);

	const ConfigLoad load = FileConfigRepository(path).load();

	CHECK(load.config == sample_config());
	REQUIRE(load.problem.has_value());
	CHECK_EQ(load.problem->key, std::string("Config.RestoredMissing"));
}

TEST_CASE(an_unusable_backup_is_ignored)
{
	TempDirectory directory;
	const fs::path path = directory.path() / "destinations.json";
	write_file(directory.path() / "destinations.json.bak", "{not json");

	FileConfigRepository repository(path);
	const ConfigLoad load = repository.load();

	CHECK(load.config.destinations.empty());
	CHECK(!load.problem.has_value());
	CHECK(!repository.save(sample_config()).has_value());
}
