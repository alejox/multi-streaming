// Deciding whether a GitHub release is an update for the running plugin: version
// comparison, release JSON parsing, and picking the package for this platform. Pure C++ so
// the standalone test build covers it; the Qt service performs the HTTP and the download.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace multistream {

inline constexpr std::string_view kLatestReleaseUrl =
	"https://api.github.com/repos/alejox/multi-streaming/releases/latest";
inline constexpr std::string_view kPackagePrefix = "obs-multi-stream-";

struct SemVer {
	int major = 0;
	int minor = 0;
	int patch = 0;
	// After '-', without the build metadata; empty for a release.
	std::string prerelease;
};

// Accepts "1.2.3", "v1.2.3", "1.2.3-rc.1" and "1.2.3+build"; nullopt otherwise.
std::optional<SemVer> parse_semver(std::string_view text);

// Negative, zero or positive, like strcmp. A prerelease sorts before its release.
int compare_semver(const SemVer &left, const SemVer &right);

std::string format_semver(const SemVer &version);

// The 64 lowercase hex characters of a GitHub asset digest such as "sha256:AB12...";
// nullopt for any other algorithm or a malformed value.
std::optional<std::string> parse_sha256_digest(std::string_view digest);

struct ReleaseAsset {
	std::string name;
	std::string download_url;
	std::uint64_t size = 0;
	// Lowercase hex; empty when the API did not publish a digest.
	std::string sha256;
};

struct ReleaseInfo {
	std::string tag;
	std::string html_url;
	bool draft = false;
	bool prerelease = false;
	std::vector<ReleaseAsset> assets;
};

// A GET /repos/{owner}/{repo}/releases/latest response; nullopt when it has no tag.
std::optional<ReleaseInfo> parse_latest_release(std::string_view json);

enum class UpdatePlatform { MacosArm64, WindowsX64, LinuxX86_64 };

// The platform this binary was built for, or nullopt where no package is published.
std::optional<UpdatePlatform> current_update_platform();

// The package name ending, such as "-macos-arm64.zip".
std::string_view package_suffix(UpdatePlatform platform);

// The asset named "obs-multi-stream-<version><suffix>" for the platform, if any.
std::optional<ReleaseAsset> select_asset(const ReleaseInfo &release, UpdatePlatform platform);

enum class UpdateVerdict {
	// The release is not newer, or is a draft or a prerelease.
	UpToDate,
	// Newer, and a package for this platform exists.
	Available,
	// Newer, but without a package for this platform: only a manual download helps.
	NoPackage,
	// The running version or the release tag could not be read.
	Unknown,
};

struct UpdateDecision {
	UpdateVerdict verdict = UpdateVerdict::Unknown;
	// Set for Available and NoPackage.
	std::string version;
	std::optional<ReleaseAsset> asset;
};

UpdateDecision decide_update(std::string_view current_version, const ReleaseInfo &release, UpdatePlatform platform);

// Whether `data` hashes to `expected_hex` (case-insensitive).
bool sha256_matches(std::string_view data, std::string_view expected_hex);

} // namespace multistream
