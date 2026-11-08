#include "update/install_plan.hpp"

#include "update/staging.hpp"

#include "test_harness.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace multistream;

namespace {

bool contains(const std::string &text, std::string_view part)
{
	return text.find(part) != std::string::npos;
}

InstallPlan posix_plan()
{
	InstallPlan plan;
	plan.platform = UpdatePlatform::MacosArm64;
	plan.obs_pid = 4242;
	plan.version = "0.3.0";
	plan.install_root = "/Users/me/Library/Application Support/obs-studio/plugins/obs-multi-stream.plugin";
	plan.archive = "/Users/me/Library/Application Support/obs-studio/plugin_config/obs-multi-stream/update/"
		       "obs-multi-stream-0.3.0-macos-arm64.zip";
	plan.staging_dir = "/Users/me/Library/Application Support/obs-studio/plugin_config/obs-multi-stream/update";
	plan.result_file =
		"/Users/me/Library/Application Support/obs-studio/plugin_config/obs-multi-stream/update-result.txt";
	return plan;
}

InstallPlan windows_plan()
{
	InstallPlan plan;
	plan.platform = UpdatePlatform::WindowsX64;
	plan.obs_pid = 777;
	plan.version = "0.3.0";
	plan.install_root = "C:\\ProgramData\\obs-studio\\plugins\\obs-multi-stream";
	plan.archive = "C:\\Users\\O'Brien Smith\\AppData\\Roaming\\obs-studio\\plugin_config\\obs-multi-stream\\update\\"
		       "obs-multi-stream-0.3.0-windows-x64.zip";
	plan.staging_dir = "C:\\Users\\O'Brien Smith\\AppData\\Roaming\\obs-studio\\plugin_config\\obs-multi-stream\\update";
	plan.result_file = "C:\\Users\\O'Brien Smith\\AppData\\Roaming\\obs-studio\\plugin_config\\obs-multi-stream\\"
			   "update-result.txt";
	return plan;
}

} // namespace

TEST_CASE(posix_quote_wraps_and_escapes_single_quotes)
{
	CHECK_EQ(posix_quote("plain"), "'plain'");
	CHECK_EQ(posix_quote("with space"), "'with space'");
	CHECK_EQ(posix_quote("it's"), "'it'\\''s'");
	CHECK_EQ(posix_quote("$HOME `x` \"y\""), "'$HOME `x` \"y\"'");
	CHECK_EQ(posix_quote(""), "''");
}

TEST_CASE(powershell_quote_doubles_single_quotes)
{
	CHECK_EQ(powershell_quote("C:\\Program Files\\x"), "'C:\\Program Files\\x'");
	CHECK_EQ(powershell_quote("O'Brien"), "'O''Brien'");
	CHECK_EQ(powershell_quote("$env:APPDATA"), "'$env:APPDATA'");
}

TEST_CASE(macos_script_quotes_paths_and_uses_ditto)
{
	const auto script = render_install_script(posix_plan());
	REQUIRE(script.has_value());
	CHECK(contains(*script, "#!/bin/sh"));
	CHECK(contains(*script, "obs_pid=4242\n"));
	CHECK(contains(*script, "install_root='/Users/me/Library/Application Support/obs-studio/plugins/"
				"obs-multi-stream.plugin'"));
	CHECK(contains(*script, "package_root='obs-multi-stream.plugin'"));
	CHECK(contains(*script, "ditto -x -k \"$archive\" \"$extract\""));
	CHECK(contains(*script, "kill -0 \"$obs_pid\""));
	CHECK(contains(*script, "mv \"$backup\" \"$install_root\""));
	CHECK(!contains(*script, "tar -xzf"));
	CHECK_EQ(std::string(install_script_name(UpdatePlatform::MacosArm64)), "install.sh");
}

TEST_CASE(linux_script_uses_tar)
{
	InstallPlan plan = posix_plan();
	plan.platform = UpdatePlatform::LinuxX86_64;
	plan.install_root = "/home/me/.config/obs-studio/plugins/obs-multi-stream";
	plan.archive = "/home/me/.config/obs-studio/plugin_config/obs-multi-stream/update/x.tar.gz";
	plan.staging_dir = "/home/me/.config/obs-studio/plugin_config/obs-multi-stream/update";
	plan.result_file = "/home/me/.config/obs-studio/plugin_config/obs-multi-stream/update-result.txt";
	const auto script = render_install_script(plan);
	REQUIRE(script.has_value());
	CHECK(contains(*script, "tar -xzf \"$archive\" -C \"$extract\""));
	CHECK(contains(*script, "cp -R \"$extract/$package_root\" \"$install_root\""));
	CHECK(!contains(*script, "ditto"));
}

TEST_CASE(windows_script_quotes_paths_and_expands_hidden)
{
	const auto script = render_install_script(windows_plan());
	REQUIRE(script.has_value());
	CHECK(contains(*script, "$obsPid = 777\n"));
	CHECK(contains(*script, "$archive = 'C:\\Users\\O''Brien Smith\\AppData\\Roaming\\"));
	CHECK(contains(*script, "$installRoot = 'C:\\ProgramData\\obs-studio\\plugins\\obs-multi-stream'"));
	CHECK(contains(*script, "Expand-Archive -LiteralPath $archive -DestinationPath $extract -Force"));
	CHECK(contains(*script, "Move-Item -LiteralPath $backup -Destination $installRoot"));
	CHECK(contains(*script, "WaitForExit"));
	CHECK_EQ(std::string(install_script_name(UpdatePlatform::WindowsX64)), "install.ps1");
}

TEST_CASE(render_install_script_refuses_unsafe_plans)
{
	InstallPlan plan = posix_plan();
	plan.obs_pid = 0;
	CHECK(!render_install_script(plan).has_value());

	plan = posix_plan();
	plan.install_root = "relative/obs-multi-stream.plugin";
	CHECK(!render_install_script(plan).has_value());

	plan = posix_plan();
	plan.install_root = "/";
	CHECK(!render_install_script(plan).has_value());

	plan = posix_plan();
	plan.install_root = "/Users/me/Library/Application Support/obs-studio/plugins/something-else";
	CHECK(!render_install_script(plan).has_value());

	plan = posix_plan();
	plan.staging_dir = plan.install_root + "/staging";
	CHECK(!render_install_script(plan).has_value());

	plan = posix_plan();
	plan.archive += "\nrm -rf /";
	CHECK(!render_install_script(plan).has_value());

	plan = posix_plan();
	plan.version = "0.3.0'; rm -rf /";
	CHECK(!render_install_script(plan).has_value());

	InstallPlan windows = windows_plan();
	windows.install_root = "/not/a/windows/path/obs-multi-stream";
	CHECK(!render_install_script(windows).has_value());
	windows = windows_plan();
	windows.install_root = "\\\\server\\share\\plugins\\obs-multi-stream";
	CHECK(render_install_script(windows).has_value());
}

TEST_CASE(parse_install_result_reads_both_outcomes)
{
	const auto ok = parse_install_result("ok 0.3.0 installed\n");
	REQUIRE(ok.has_value());
	CHECK(ok->ok);
	CHECK_EQ(ok->version, "0.3.0");
	CHECK_EQ(ok->detail, "installed");

	const auto failed = parse_install_result("\xEF\xBB\xBF" "failed 0.3.0 cannot extract the package\r\n");
	REQUIRE(failed.has_value());
	CHECK(!failed->ok);
	CHECK_EQ(failed->version, "0.3.0");
	CHECK_EQ(failed->detail, "cannot extract the package");

	CHECK(!parse_install_result("").has_value());
	CHECK(!parse_install_result("maybe 0.3.0").has_value());
	CHECK(!parse_install_result("ok").has_value());
	CHECK(!parse_install_result("ok notaversion").has_value());
}

#if !defined(_WIN32)

namespace {

namespace fs = std::filesystem;

std::string read_text(const fs::path &path)
{
	std::ifstream file(path);
	std::ostringstream content;
	content << file.rdbuf();
	return content.str();
}

void write_text(const fs::path &path, const std::string &text)
{
	std::ofstream(path) << text;
}

// A process id that is certainly not running: a child that already exited and was reaped.
long long finished_pid()
{
	const pid_t child = fork();
	if (child == 0) {
		_exit(0);
	}
	int status = 0;
	waitpid(child, &status, 0);
	return child;
}

struct Sandbox {
	fs::path base;
	InstallPlan plan;
	fs::path package_source;
};

// Paths with a space and a single quote, to prove the quoting holds when the shell runs it.
Sandbox make_sandbox(const char *name)
{
	const auto platform = current_update_platform().value_or(UpdatePlatform::LinuxX86_64);
	Sandbox box;
	box.base = fs::temp_directory_path() / (std::string("multi stream o'test ") + name);
	fs::remove_all(box.base);
	const fs::path plugins = box.base / "plugins";
	const fs::path config = box.base / "plugin config";
	const fs::path root = plugins / std::string(package_root_name(platform));
	fs::create_directories(root);
	write_text(root / "version.txt", "old");
	fs::create_directories(config / "update");

	box.package_source = box.base / "package" / std::string(package_root_name(platform));
	fs::create_directories(box.package_source / "bin");
	write_text(box.package_source / "version.txt", "new");
	write_text(box.package_source / "bin" / "plugin.bin", "binary");

	box.plan.platform = platform;
	box.plan.obs_pid = finished_pid();
	box.plan.version = "9.9.9";
	box.plan.install_root = root.string();
	box.plan.staging_dir = (config / "update").string();
	box.plan.result_file = (config / "update-result.txt").string();
	return box;
}

// `path_prefix` puts substitute tools ahead of the system ones.
int run_script(const Sandbox &box, const std::string &path_prefix = {})
{
	const auto script = render_install_script(box.plan);
	REQUIRE(script.has_value());
	const fs::path script_path = box.base / "install.sh";
	write_text(script_path, *script);
	std::string command;
	if (!path_prefix.empty()) {
		command = "PATH=" + posix_quote(path_prefix) + ":\"$PATH\" ";
	}
	command += "/bin/sh " + posix_quote(script_path.string()) + " >/dev/null 2>&1";
	return std::system(command.c_str());
}

std::string pack(Sandbox &box)
{
	const fs::path package_dir = box.package_source.parent_path();
	const std::string root = box.package_source.filename().string();
	std::string command;
	if (box.plan.platform == UpdatePlatform::MacosArm64) {
		box.plan.archive = (fs::path(box.plan.staging_dir) / "package.zip").string();
		command = "ditto -c -k --keepParent " + posix_quote(box.package_source.string()) + " " +
			  posix_quote(box.plan.archive);
	} else {
		box.plan.archive = (fs::path(box.plan.staging_dir) / "package.tar.gz").string();
		command = "tar -C " + posix_quote(package_dir.string()) + " -czf " + posix_quote(box.plan.archive) +
			  " " + posix_quote(root);
	}
	return command;
}

} // namespace

TEST_CASE(posix_script_installs_the_package_and_cleans_up)
{
	Sandbox box = make_sandbox("install");
	REQUIRE(std::system(pack(box).c_str()) == 0);

	CHECK_EQ(run_script(box), 0);
	CHECK_EQ(read_text(fs::path(box.plan.install_root) / "version.txt"), "new");
	CHECK_EQ(read_text(fs::path(box.plan.install_root) / "bin" / "plugin.bin"), "binary");
	CHECK(!fs::exists(box.plan.install_root + ".update-backup"));
	CHECK(!fs::exists(box.plan.staging_dir));
	const auto result = parse_install_result(read_text(box.plan.result_file));
	REQUIRE(result.has_value());
	CHECK(result->ok);
	CHECK_EQ(result->version, "9.9.9");
	fs::remove_all(box.base);
}

TEST_CASE(posix_script_keeps_the_old_version_when_the_package_is_broken)
{
	Sandbox box = make_sandbox("broken");
	box.plan.archive = (fs::path(box.plan.staging_dir) / "package.bin").string();
	write_text(box.plan.archive, "this is not an archive");

	CHECK_EQ(run_script(box), 0);
	CHECK_EQ(read_text(fs::path(box.plan.install_root) / "version.txt"), "old");
	CHECK(!fs::exists(box.plan.install_root + ".update-backup"));
	const auto result = parse_install_result(read_text(box.plan.result_file));
	REQUIRE(result.has_value());
	CHECK(!result->ok);
	fs::remove_all(box.base);
}

TEST_CASE(posix_script_keeps_the_old_version_when_the_layout_is_unexpected)
{
	Sandbox box = make_sandbox("layout");
	// A package whose root is a file instead of a folder.
	fs::remove_all(box.package_source);
	write_text(box.package_source, "not a folder");
	REQUIRE(std::system(pack(box).c_str()) == 0);

	CHECK_EQ(run_script(box), 0);
	CHECK_EQ(read_text(fs::path(box.plan.install_root) / "version.txt"), "old");
	const auto result = parse_install_result(read_text(box.plan.result_file));
	REQUIRE(result.has_value());
	CHECK(!result->ok);
	fs::remove_all(box.base);
}

TEST_CASE(posix_script_restores_the_old_version_when_the_copy_fails)
{
	Sandbox box = make_sandbox("rollback");
	REQUIRE(std::system(pack(box).c_str()) == 0);

	// A copy tool that copies, then reports failure: the half-written install must go and
	// the previous version must come back.
	const fs::path tools = box.base / "failing tools";
	fs::create_directories(tools);
	const char *tool = box.plan.platform == UpdatePlatform::MacosArm64 ? "ditto" : "cp";
	const std::string real = box.plan.platform == UpdatePlatform::MacosArm64 ? "/usr/bin/ditto" : "/bin/cp";
	write_text(tools / tool, "#!/bin/sh\ncase \"$1\" in -x|-c) exec " + real + " \"$@\";; esac\n" + real +
					 " \"$@\"\nexit 1\n");
	fs::permissions(tools / tool, fs::perms::owner_all);

	CHECK_EQ(run_script(box, tools.string()), 0);
	CHECK_EQ(read_text(fs::path(box.plan.install_root) / "version.txt"), "old");
	CHECK(!fs::exists(fs::path(box.plan.install_root) / "bin"));
	CHECK(!fs::exists(box.plan.install_root + ".update-backup"));
	CHECK(!fs::exists(box.plan.staging_dir));
	const auto result = parse_install_result(read_text(box.plan.result_file));
	REQUIRE(result.has_value());
	CHECK(!result->ok);
	fs::remove_all(box.base);
}

#endif
