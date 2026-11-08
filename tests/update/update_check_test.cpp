#include "update/update_check.hpp"

#include "oauth/sha256.hpp"

#include "test_harness.hpp"

using namespace multistream;

namespace {

const char *const kRelease = R"({
  "tag_name": "v0.3.0",
  "html_url": "https://github.com/alejox/multi-streaming/releases/tag/v0.3.0",
  "draft": false,
  "prerelease": false,
  "assets": [
    {"name": "obs-multi-stream-0.3.0-linux-x86_64.tar.gz",
     "browser_download_url": "https://github.com/alejox/multi-streaming/releases/download/v0.3.0/obs-multi-stream-0.3.0-linux-x86_64.tar.gz",
     "size": 8686000,
     "digest": "sha256:b425dd1f59c1d1d5a62867b20f404d1eb00fb50fabf596a52b0bf59b40eb2ddf"},
    {"name": "obs-multi-stream-0.3.0-macos-arm64.zip",
     "browser_download_url": "https://github.com/alejox/multi-streaming/releases/download/v0.3.0/obs-multi-stream-0.3.0-macos-arm64.zip",
     "size": 379722,
     "digest": "sha256:A8CDF5DA10B315947AB83289B00735C298395517C1872377608795F6C33C6F34"},
    {"name": "obs-multi-stream-0.3.0-windows-x64.zip",
     "browser_download_url": "https://github.com/alejox/multi-streaming/releases/download/v0.3.0/obs-multi-stream-0.3.0-windows-x64.zip",
     "size": 445675,
     "digest": null}
  ]
})";

} // namespace

TEST_CASE(parse_semver_reads_plain_and_tagged_versions)
{
	const auto plain = parse_semver("0.2.0");
	REQUIRE(plain.has_value());
	CHECK_EQ(plain->major, 0);
	CHECK_EQ(plain->minor, 2);
	CHECK_EQ(plain->patch, 0);
	CHECK(plain->prerelease.empty());

	const auto tagged = parse_semver("v12.34.56-rc.1+build.7");
	REQUIRE(tagged.has_value());
	CHECK_EQ(tagged->major, 12);
	CHECK_EQ(tagged->minor, 34);
	CHECK_EQ(tagged->patch, 56);
	CHECK_EQ(tagged->prerelease, "rc.1");
	CHECK_EQ(format_semver(*tagged), "12.34.56-rc.1");
}

TEST_CASE(parse_semver_rejects_malformed_versions)
{
	CHECK(!parse_semver("").has_value());
	CHECK(!parse_semver("v").has_value());
	CHECK(!parse_semver("1.2").has_value());
	CHECK(!parse_semver("1.2.3.4").has_value());
	CHECK(!parse_semver("1.x.3").has_value());
	CHECK(!parse_semver("1..3").has_value());
	CHECK(!parse_semver(" 1.2.3").has_value());
	CHECK(!parse_semver("1.2.3-").has_value());
	CHECK(!parse_semver("99999999999.0.0").has_value());
}

TEST_CASE(compare_semver_orders_numerically_and_puts_prereleases_first)
{
	const auto v = [](const char *text) { return *parse_semver(text); };
	CHECK(compare_semver(v("0.2.0"), v("0.3.0")) < 0);
	CHECK(compare_semver(v("0.10.0"), v("0.9.9")) > 0);
	CHECK(compare_semver(v("1.0.0"), v("v1.0.0")) == 0);
	CHECK(compare_semver(v("1.0.0-rc.1"), v("1.0.0")) < 0);
	CHECK(compare_semver(v("1.0.0"), v("1.0.0-rc.1")) > 0);
	CHECK(compare_semver(v("2.0.0"), v("1.99.99")) > 0);
}

TEST_CASE(parse_sha256_digest_accepts_only_sha256)
{
	CHECK_EQ(parse_sha256_digest("sha256:A8CDF5DA10B315947AB83289B00735C298395517C1872377608795F6C33C6F34")
			 .value_or(""),
		 "a8cdf5da10b315947ab83289b00735c298395517c1872377608795f6c33c6f34");
	CHECK(!parse_sha256_digest("").has_value());
	CHECK(!parse_sha256_digest("sha512:a8cdf5da10b315947ab83289b00735c298395517c1872377608795f6c33c6f34")
		       .has_value());
	CHECK(!parse_sha256_digest("sha256:abc").has_value());
	CHECK(!parse_sha256_digest("sha256:z8cdf5da10b315947ab83289b00735c298395517c1872377608795f6c33c6f34")
		       .has_value());
}

TEST_CASE(parse_latest_release_reads_tag_url_and_assets)
{
	const auto release = parse_latest_release(kRelease);
	REQUIRE(release.has_value());
	CHECK_EQ(release->tag, "v0.3.0");
	CHECK_EQ(release->html_url, "https://github.com/alejox/multi-streaming/releases/tag/v0.3.0");
	CHECK(!release->draft);
	CHECK(!release->prerelease);
	REQUIRE(release->assets.size() == 3);
	CHECK_EQ(release->assets[1].name, "obs-multi-stream-0.3.0-macos-arm64.zip");
	CHECK_EQ(release->assets[1].size, static_cast<std::uint64_t>(379722));
	CHECK_EQ(release->assets[1].sha256, "a8cdf5da10b315947ab83289b00735c298395517c1872377608795f6c33c6f34");
	CHECK(release->assets[2].sha256.empty());
}

TEST_CASE(parse_latest_release_survives_bad_input)
{
	CHECK(!parse_latest_release("not json").has_value());
	CHECK(!parse_latest_release("[]").has_value());
	CHECK(!parse_latest_release(R"({"message":"Not Found"})").has_value());
	CHECK(!parse_latest_release(R"({"tag_name":42})").has_value());

	const auto odd_assets = parse_latest_release(
		R"({"tag_name":"v1.0.0","assets":[{"name":"a"},7,{"name":"b","browser_download_url":"https://x/b","size":-3}]})");
	REQUIRE(odd_assets.has_value());
	// Assets without a name and download URL are dropped; a negative size reads as unknown.
	REQUIRE(odd_assets->assets.size() == 1);
	CHECK_EQ(odd_assets->assets[0].name, "b");
	CHECK_EQ(odd_assets->assets[0].size, static_cast<std::uint64_t>(0));
}

TEST_CASE(select_asset_picks_the_package_for_each_platform)
{
	const auto release = *parse_latest_release(kRelease);
	CHECK_EQ(select_asset(release, UpdatePlatform::MacosArm64)->name, "obs-multi-stream-0.3.0-macos-arm64.zip");
	CHECK_EQ(select_asset(release, UpdatePlatform::WindowsX64)->name, "obs-multi-stream-0.3.0-windows-x64.zip");
	CHECK_EQ(select_asset(release, UpdatePlatform::LinuxX86_64)->name,
		 "obs-multi-stream-0.3.0-linux-x86_64.tar.gz");

	ReleaseInfo foreign = release;
	foreign.assets = {{"other-plugin-0.3.0-macos-arm64.zip", "https://x", 1, ""},
			  {"obs-multi-stream-0.3.0-macos-x86_64.zip", "https://x", 1, ""}};
	CHECK(!select_asset(foreign, UpdatePlatform::MacosArm64).has_value());
}

TEST_CASE(decide_update_offers_only_newer_published_releases)
{
	const auto release = *parse_latest_release(kRelease);

	const UpdateDecision newer = decide_update("0.2.0", release, UpdatePlatform::MacosArm64);
	CHECK_EQ(newer.verdict, UpdateVerdict::Available);
	CHECK_EQ(newer.version, "0.3.0");
	REQUIRE(newer.asset.has_value());
	CHECK_EQ(newer.asset->name, "obs-multi-stream-0.3.0-macos-arm64.zip");

	CHECK_EQ(decide_update("0.3.0", release, UpdatePlatform::MacosArm64).verdict, UpdateVerdict::UpToDate);
	CHECK_EQ(decide_update("0.4.0", release, UpdatePlatform::MacosArm64).verdict, UpdateVerdict::UpToDate);
	CHECK_EQ(decide_update("0.3.0-rc.1", release, UpdatePlatform::MacosArm64).verdict, UpdateVerdict::Available);

	ReleaseInfo draft = release;
	draft.draft = true;
	CHECK_EQ(decide_update("0.2.0", draft, UpdatePlatform::MacosArm64).verdict, UpdateVerdict::UpToDate);
	ReleaseInfo pre = release;
	pre.prerelease = true;
	CHECK_EQ(decide_update("0.2.0", pre, UpdatePlatform::MacosArm64).verdict, UpdateVerdict::UpToDate);
}

TEST_CASE(decide_update_reports_missing_packages_and_unreadable_versions)
{
	ReleaseInfo release = *parse_latest_release(kRelease);
	release.assets.clear();
	const UpdateDecision manual = decide_update("0.2.0", release, UpdatePlatform::WindowsX64);
	CHECK_EQ(manual.verdict, UpdateVerdict::NoPackage);
	CHECK_EQ(manual.version, "0.3.0");
	CHECK(!manual.asset.has_value());

	CHECK_EQ(decide_update("garbage", release, UpdatePlatform::WindowsX64).verdict, UpdateVerdict::Unknown);
	release.tag = "latest";
	CHECK_EQ(decide_update("0.2.0", release, UpdatePlatform::WindowsX64).verdict, UpdateVerdict::Unknown);
}

TEST_CASE(sha256_matches_compares_case_insensitively)
{
	const std::string data = "obs-multi-stream";
	const std::string hex = sha256_hex(data);
	CHECK(sha256_matches(data, hex));
	std::string upper = hex;
	for (char &c : upper) {
		if (c >= 'a' && c <= 'f') {
			c = static_cast<char>(c - 'a' + 'A');
		}
	}
	CHECK(sha256_matches(data, upper));
	CHECK(!sha256_matches(data + "!", hex));
	CHECK(!sha256_matches(data, ""));
}

TEST_CASE(current_update_platform_matches_the_build)
{
#if defined(__APPLE__) && defined(__aarch64__)
	CHECK(current_update_platform() == UpdatePlatform::MacosArm64);
#elif defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
	CHECK(current_update_platform() == UpdatePlatform::WindowsX64);
#elif defined(__linux__) && defined(__x86_64__)
	CHECK(current_update_platform() == UpdatePlatform::LinuxX86_64);
#else
	CHECK(!current_update_platform().has_value());
#endif
	CHECK_EQ(std::string(package_suffix(UpdatePlatform::LinuxX86_64)), "-linux-x86_64.tar.gz");
}
