#include "update/staging.hpp"

#include "test_harness.hpp"

using namespace multistream;

TEST_CASE(staged_update_round_trips)
{
	StagedUpdate staged;
	staged.version = "0.3.0";
	staged.archive_name = "obs-multi-stream-0.3.0-macos-arm64.zip";
	staged.sha256 = "a8cdf5da10b315947ab83289b00735c298395517c1872377608795f6c33c6f34";
	staged.size = 379722;

	const auto decoded = decode_staged_update(encode_staged_update(staged));
	REQUIRE(decoded.has_value());
	CHECK_EQ(decoded->version, staged.version);
	CHECK_EQ(decoded->archive_name, staged.archive_name);
	CHECK_EQ(decoded->sha256, staged.sha256);
	CHECK_EQ(decoded->size, staged.size);
}

TEST_CASE(decode_staged_update_rejects_incomplete_or_unsafe_manifests)
{
	CHECK(!decode_staged_update("").has_value());
	CHECK(!decode_staged_update("{").has_value());
	CHECK(!decode_staged_update(R"({"version":"0.3.0"})").has_value());
	CHECK(!decode_staged_update(R"({"version":"x","archive":"a.zip","size":1})").has_value());
	CHECK(!decode_staged_update(R"({"version":"0.3.0","archive":"../a.zip","size":1})").has_value());
	CHECK(!decode_staged_update(R"({"version":"0.3.0","archive":"a.zip","size":1,"sha256":"nothex"})")
		       .has_value());
	CHECK(decode_staged_update(R"({"version":"0.3.0","archive":"a.zip","size":1})").has_value());
}

TEST_CASE(is_safe_file_name_refuses_paths)
{
	CHECK(is_safe_file_name("obs-multi-stream-0.3.0-windows-x64.zip"));
	CHECK(!is_safe_file_name(""));
	CHECK(!is_safe_file_name("."));
	CHECK(!is_safe_file_name(".."));
	CHECK(!is_safe_file_name(".hidden"));
	CHECK(!is_safe_file_name("a/b.zip"));
	CHECK(!is_safe_file_name("a\\b.zip"));
	CHECK(!is_safe_file_name("C:b.zip"));
	CHECK(!is_safe_file_name(std::string(300, 'a')));
}

TEST_CASE(install_root_from_binary_finds_the_macos_bundle)
{
	CHECK_EQ(install_root_from_binary("/Users/me/Library/Application Support/obs-studio/plugins/"
					  "obs-multi-stream.plugin/Contents/MacOS/obs-multi-stream",
					  UpdatePlatform::MacosArm64)
			 .value_or("none"),
		 "/Users/me/Library/Application Support/obs-studio/plugins/obs-multi-stream.plugin");
	// Built into the app bundle or loaded from a build tree: not ours to replace.
	CHECK(!install_root_from_binary("/Applications/OBS.app/Contents/PlugIns/obs-multi-stream.plugin/Contents/"
					"MacOS/obs-multi-stream",
					UpdatePlatform::MacosArm64)
		       .has_value());
	CHECK(!install_root_from_binary("/tmp/build/obs-multi-stream.dylib", UpdatePlatform::MacosArm64).has_value());
	CHECK(!install_root_from_binary("/x/other.plugin/Contents/MacOS/other", UpdatePlatform::MacosArm64)
		       .has_value());
}

TEST_CASE(install_root_from_binary_finds_the_windows_and_linux_folder)
{
	CHECK_EQ(install_root_from_binary("C:\\ProgramData\\obs-studio\\plugins\\obs-multi-stream\\bin\\64bit\\"
					  "obs-multi-stream.dll",
					  UpdatePlatform::WindowsX64)
			 .value_or("none"),
		 "C:\\ProgramData\\obs-studio\\plugins\\obs-multi-stream");
	CHECK_EQ(install_root_from_binary("C:/ProgramData/obs-studio/plugins/obs-multi-stream/bin/64bit/"
					  "obs-multi-stream.dll",
					  UpdatePlatform::WindowsX64)
			 .value_or("none"),
		 "C:/ProgramData/obs-studio/plugins/obs-multi-stream");
	CHECK_EQ(install_root_from_binary("/home/me/.config/obs-studio/plugins/obs-multi-stream/bin/64bit/"
					  "obs-multi-stream.so",
					  UpdatePlatform::LinuxX86_64)
			 .value_or("none"),
		 "/home/me/.config/obs-studio/plugins/obs-multi-stream");

	// The OBS program folder and distribution packages use the flat obs-plugins layout.
	CHECK(!install_root_from_binary("C:\\Program Files\\obs-studio\\obs-plugins\\64bit\\obs-multi-stream.dll",
					UpdatePlatform::WindowsX64)
		       .has_value());
	CHECK(!install_root_from_binary("/usr/lib/x86_64-linux-gnu/obs-plugins/obs-multi-stream.so",
					UpdatePlatform::LinuxX86_64)
		       .has_value());
	CHECK(!install_root_from_binary("", UpdatePlatform::LinuxX86_64).has_value());
}

TEST_CASE(package_root_name_matches_the_release_layout)
{
	CHECK_EQ(std::string(package_root_name(UpdatePlatform::MacosArm64)), "obs-multi-stream.plugin");
	CHECK_EQ(std::string(package_root_name(UpdatePlatform::WindowsX64)), "obs-multi-stream");
	CHECK_EQ(std::string(package_root_name(UpdatePlatform::LinuxX86_64)), "obs-multi-stream");
}
