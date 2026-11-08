#include "update/update_check.hpp"

#include "oauth/sha256.hpp"

#include <json.hpp>

#include <limits>

namespace multistream {
namespace {

using nlohmann::json;

bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

char to_lower_ascii(char c)
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool is_hex(char c)
{
	return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Reads one numeric component up to `stop` or the end; advances `text` past it.
std::optional<int> read_number(std::string_view &text, char stop)
{
	std::size_t length = 0;
	long long value = 0;
	while (length < text.size() && text[length] != stop) {
		if (!is_digit(text[length])) {
			return std::nullopt;
		}
		value = value * 10 + (text[length] - '0');
		if (value > std::numeric_limits<int>::max()) {
			return std::nullopt;
		}
		++length;
	}
	if (length == 0) {
		return std::nullopt;
	}
	text.remove_prefix(length);
	return static_cast<int>(value);
}

bool consume(std::string_view &text, char expected)
{
	if (text.empty() || text.front() != expected) {
		return false;
	}
	text.remove_prefix(1);
	return true;
}

std::string text_field(const json &object, const char *key)
{
	const auto found = object.find(key);
	return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

bool bool_field(const json &object, const char *key)
{
	const auto found = object.find(key);
	return found != object.end() && found->is_boolean() && found->get<bool>();
}

} // namespace

std::optional<SemVer> parse_semver(std::string_view text)
{
	if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
		text.remove_prefix(1);
	}
	if (const std::size_t plus = text.find('+'); plus != std::string_view::npos) {
		text = text.substr(0, plus);
	}
	std::string_view prerelease;
	if (const std::size_t dash = text.find('-'); dash != std::string_view::npos) {
		prerelease = text.substr(dash + 1);
		text = text.substr(0, dash);
		if (prerelease.empty()) {
			return std::nullopt;
		}
	}

	SemVer version;
	const auto major = read_number(text, '.');
	if (!major || !consume(text, '.')) {
		return std::nullopt;
	}
	const auto minor = read_number(text, '.');
	if (!minor || !consume(text, '.')) {
		return std::nullopt;
	}
	const auto patch = read_number(text, '.');
	if (!patch || !text.empty()) {
		return std::nullopt;
	}
	version.major = *major;
	version.minor = *minor;
	version.patch = *patch;
	version.prerelease = std::string(prerelease);
	return version;
}

int compare_semver(const SemVer &left, const SemVer &right)
{
	if (left.major != right.major) {
		return left.major < right.major ? -1 : 1;
	}
	if (left.minor != right.minor) {
		return left.minor < right.minor ? -1 : 1;
	}
	if (left.patch != right.patch) {
		return left.patch < right.patch ? -1 : 1;
	}
	if (left.prerelease == right.prerelease) {
		return 0;
	}
	if (left.prerelease.empty()) {
		return 1;
	}
	if (right.prerelease.empty()) {
		return -1;
	}
	// Good enough for the "rc.N" style this project would use; not full semver precedence.
	return left.prerelease < right.prerelease ? -1 : 1;
}

std::string format_semver(const SemVer &version)
{
	std::string out = std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
			  std::to_string(version.patch);
	if (!version.prerelease.empty()) {
		out += "-" + version.prerelease;
	}
	return out;
}

std::optional<std::string> parse_sha256_digest(std::string_view digest)
{
	constexpr std::string_view kPrefix = "sha256:";
	if (digest.substr(0, kPrefix.size()) != kPrefix) {
		return std::nullopt;
	}
	digest.remove_prefix(kPrefix.size());
	if (digest.size() != 64) {
		return std::nullopt;
	}
	std::string hex;
	hex.reserve(64);
	for (const char c : digest) {
		if (!is_hex(c)) {
			return std::nullopt;
		}
		hex += to_lower_ascii(c);
	}
	return hex;
}

std::optional<ReleaseInfo> parse_latest_release(std::string_view json_text)
{
	try {
		const json parsed = json::parse(json_text.begin(), json_text.end());
		if (!parsed.is_object()) {
			return std::nullopt;
		}
		ReleaseInfo release;
		release.tag = text_field(parsed, "tag_name");
		if (release.tag.empty()) {
			return std::nullopt;
		}
		release.html_url = text_field(parsed, "html_url");
		release.draft = bool_field(parsed, "draft");
		release.prerelease = bool_field(parsed, "prerelease");

		const auto assets = parsed.find("assets");
		if (assets != parsed.end() && assets->is_array()) {
			for (const json &item : *assets) {
				if (!item.is_object()) {
					continue;
				}
				ReleaseAsset asset;
				asset.name = text_field(item, "name");
				asset.download_url = text_field(item, "browser_download_url");
				if (asset.name.empty() || asset.download_url.empty()) {
					continue;
				}
				const auto size = item.find("size");
				if (size != item.end() && size->is_number_unsigned()) {
					asset.size = size->get<std::uint64_t>();
				}
				asset.sha256 = parse_sha256_digest(text_field(item, "digest")).value_or("");
				release.assets.push_back(std::move(asset));
			}
		}
		return release;
	} catch (const json::exception &) {
		return std::nullopt;
	}
}

std::optional<UpdatePlatform> current_update_platform()
{
#if defined(__APPLE__) && defined(__aarch64__)
	return UpdatePlatform::MacosArm64;
#elif defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
	return UpdatePlatform::WindowsX64;
#elif defined(__linux__) && defined(__x86_64__)
	return UpdatePlatform::LinuxX86_64;
#else
	return std::nullopt;
#endif
}

std::string_view package_suffix(UpdatePlatform platform)
{
	switch (platform) {
	case UpdatePlatform::MacosArm64:
		return "-macos-arm64.zip";
	case UpdatePlatform::WindowsX64:
		return "-windows-x64.zip";
	case UpdatePlatform::LinuxX86_64:
		return "-linux-x86_64.tar.gz";
	}
	return {};
}

std::optional<ReleaseAsset> select_asset(const ReleaseInfo &release, UpdatePlatform platform)
{
	const std::string_view suffix = package_suffix(platform);
	for (const ReleaseAsset &asset : release.assets) {
		const std::string_view name = asset.name;
		if (name.size() > kPackagePrefix.size() + suffix.size() &&
		    name.substr(0, kPackagePrefix.size()) == kPackagePrefix &&
		    name.substr(name.size() - suffix.size()) == suffix) {
			return asset;
		}
	}
	return std::nullopt;
}

UpdateDecision decide_update(std::string_view current_version, const ReleaseInfo &release, UpdatePlatform platform)
{
	UpdateDecision decision;
	const auto current = parse_semver(current_version);
	const auto latest = parse_semver(release.tag);
	if (!current || !latest) {
		return decision;
	}
	if (release.draft || release.prerelease || compare_semver(*latest, *current) <= 0) {
		decision.verdict = UpdateVerdict::UpToDate;
		return decision;
	}
	decision.version = format_semver(*latest);
	decision.asset = select_asset(release, platform);
	decision.verdict = decision.asset ? UpdateVerdict::Available : UpdateVerdict::NoPackage;
	return decision;
}

bool sha256_matches(std::string_view data, std::string_view expected_hex)
{
	if (expected_hex.size() != 64) {
		return false;
	}
	const std::string actual = sha256_hex(data);
	for (std::size_t i = 0; i < actual.size(); ++i) {
		if (actual[i] != to_lower_ascii(expected_hex[i])) {
			return false;
		}
	}
	return true;
}

} // namespace multistream
