#include "update/update_service.hpp"

#include "oauth/https_client.hpp"
#include "persistence/atomic_file.hpp"
#include "update/https_download.hpp"

#include <plugin-support.h>
#include <obs-module.h>

#include <QMetaObject>
#include <QTimer>

#include <fstream>
#include <system_error>
#include <utility>

namespace multistream {
namespace {

constexpr const char *kManifestName = "staged.json";
constexpr const char *kProbeName = ".obs-multi-stream-update-probe";
// Release packages are a few megabytes; this only stops a runaway download.
constexpr std::uint64_t kMaxPackageBytes = 256ull * 1024 * 1024;

namespace fs = std::filesystem;

std::string to_utf8(const fs::path &path)
{
	const auto text = path.u8string();
	return std::string(text.begin(), text.end());
}

bool can_create_file_in(const fs::path &directory)
{
	const fs::path probe = directory / kProbeName;
	{
		std::ofstream file(probe, std::ios::binary | std::ios::trunc);
		if (!file) {
			return false;
		}
	}
	std::error_code ignored;
	fs::remove(probe, ignored);
	return true;
}

// The installer renames the install directory and creates a new one next to it, so both
// the directory and its parent must be writable for this user.
bool can_replace(const fs::path &install_root)
{
	std::error_code error;
	if (!fs::is_directory(install_root, error)) {
		return false;
	}
	return can_create_file_in(install_root) && can_create_file_in(install_root.parent_path());
}

void remove_all_quietly(const fs::path &path)
{
	std::error_code ignored;
	fs::remove_all(path, ignored);
}

// Whether the file on disk is the package the manifest describes.
bool verify_package(const fs::path &archive, const StagedUpdate &staged)
{
	std::string content;
	if (!read_whole_file(archive, content)) {
		return false;
	}
	if (staged.size != 0 && content.size() != staged.size) {
		return false;
	}
	return staged.sha256.empty() || sha256_matches(content, staged.sha256);
}

std::optional<StagedUpdate> read_manifest(const fs::path &staging_dir)
{
	std::string text;
	if (!read_whole_file(staging_dir / kManifestName, text)) {
		return std::nullopt;
	}
	return decode_staged_update(text);
}

} // namespace

UpdateService::UpdateService(UpdateServiceConfig config, bool enabled, QObject *parent)
	: QObject(parent),
	  config_(std::move(config)),
	  enabled_(enabled)
{
}

UpdateService::~UpdateService()
{
	if (cancelled_) {
		cancelled_->store(true);
	}
	if (worker_.joinable()) {
		worker_.join();
	}
}

void UpdateService::start(int delay_ms)
{
	QTimer::singleShot(delay_ms, this, [this] {
		if (enabled_) {
			run_check();
		}
	});
}

void UpdateService::set_enabled(bool enabled)
{
	if (enabled_ == enabled) {
		return;
	}
	enabled_ = enabled;
	if (!enabled) {
		if (cancelled_) {
			cancelled_->store(true);
		}
		running_ = false;
		ready_.reset();
		state_ = {};
		emit state_changed();
		return;
	}
	run_check();
}

void UpdateService::retry()
{
	if (enabled_) {
		run_check();
	}
}

void UpdateService::run_check()
{
	if (running_) {
		return;
	}
	// The previous worker has posted its outcome (or was cancelled), so this join is quick.
	if (worker_.joinable()) {
		worker_.join();
	}
	running_ = true;
	cancelled_ = std::make_shared<std::atomic<bool>>(false);
	state_ = {UpdateStatus::Checking, {}, {}};
	ready_.reset();
	emit state_changed();

	worker_ = std::thread([this, config = config_, cancelled = cancelled_] {
		const auto on_download = [this, &cancelled](const std::string &version) {
			QMetaObject::invokeMethod(
				this,
				[this, version, cancelled] {
					if (!cancelled->load()) {
						state_ = {UpdateStatus::Downloading, version, {}};
						emit state_changed();
					}
				},
				Qt::QueuedConnection);
		};
		Outcome outcome = check_and_stage(config, *cancelled, on_download);
		if (cancelled->load()) {
			return;
		}
		// The destructor joins this thread, so `this` is alive while posting; Qt drops the
		// queued call if the object is destroyed before it runs.
		QMetaObject::invokeMethod(
			this,
			[this, outcome = std::move(outcome), cancelled]() mutable {
				if (!cancelled->load()) {
					apply(std::move(outcome));
				}
			},
			Qt::QueuedConnection);
	});
}

void UpdateService::apply(Outcome outcome)
{
	running_ = false;
	state_ = std::move(outcome.state);
	ready_ = std::move(outcome.ready);
	emit state_changed();
}

UpdateService::Outcome UpdateService::check_and_stage(const UpdateServiceConfig &config,
						      const std::atomic<bool> &cancelled,
						      const std::function<void(const std::string &)> &on_download)
{
	Outcome outcome;
	const auto current = parse_semver(config.current_version);
	const bool installable = config.platform && config.install_root && can_replace(*config.install_root);

	// A package staged by an earlier session needs no new download.
	if (const auto staged = read_manifest(config.staging_dir)) {
		const auto version = parse_semver(staged->version);
		const fs::path archive = config.staging_dir / fs::u8path(staged->archive_name);
		if (installable && current && version && compare_semver(*version, *current) > 0 &&
		    verify_package(archive, *staged)) {
			obs_log(LOG_INFO, "update %s is staged and installs when OBS closes", staged->version.c_str());
			outcome.state = {UpdateStatus::Ready, staged->version, {}};
			outcome.ready = ReadyUpdate{*staged, archive};
			return outcome;
		}
	}
	// Installed already, unusable, or from an older check: start over.
	remove_all_quietly(config.staging_dir);

	HttpsRequest request;
	request.url = std::string(kLatestReleaseUrl);
	request.headers = {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28",
			   std::string("User-Agent: obs-multi-stream-updater/") + PLUGIN_VERSION};
	const HttpsResponse response = perform_https_request(request, cancelled);
	if (cancelled.load()) {
		return outcome;
	}
	if (!response.body || response.status != 200) {
		if (response.status == 404) {
			// No published release yet.
			return outcome;
		}
		obs_log(LOG_WARNING, "update check failed (HTTP %ld): %s", response.status, response.error.c_str());
		outcome.state.status = UpdateStatus::Failed;
		return outcome;
	}
	const auto release = parse_latest_release(*response.body);
	if (!release) {
		obs_log(LOG_WARNING, "update check failed: unreadable release information");
		outcome.state.status = UpdateStatus::Failed;
		return outcome;
	}

	const UpdatePlatform platform = config.platform.value_or(UpdatePlatform::MacosArm64);
	UpdateDecision decision = decide_update(config.current_version, *release, platform);
	switch (decision.verdict) {
	case UpdateVerdict::UpToDate:
		obs_log(LOG_INFO, "up to date (latest release %s)", release->tag.c_str());
		return outcome;
	case UpdateVerdict::Unknown:
		obs_log(LOG_WARNING, "update check failed: cannot compare %s with %s", config.current_version.c_str(),
			release->tag.c_str());
		outcome.state.status = UpdateStatus::Failed;
		return outcome;
	case UpdateVerdict::NoPackage:
	case UpdateVerdict::Available:
		break;
	}

	const UpdateState manual{UpdateStatus::Manual, decision.version, release->html_url};
	if (decision.verdict == UpdateVerdict::NoPackage || !config.platform) {
		obs_log(LOG_INFO, "update %s has no package for this platform", decision.version.c_str());
		outcome.state = manual;
		return outcome;
	}
	if (!installable) {
		obs_log(LOG_INFO, "update %s is available, but this install cannot be replaced automatically",
			decision.version.c_str());
		outcome.state = manual;
		return outcome;
	}
	const ReleaseAsset &asset = *decision.asset;
	if (asset.sha256.empty() && asset.size == 0) {
		obs_log(LOG_WARNING, "update %s publishes neither a digest nor a size: not downloading",
			decision.version.c_str());
		outcome.state = manual;
		return outcome;
	}
	if (!is_safe_file_name(asset.name)) {
		obs_log(LOG_WARNING, "update %s has an unexpected package name", decision.version.c_str());
		outcome.state = manual;
		return outcome;
	}

	on_download(decision.version);
	std::error_code error;
	fs::create_directories(config.staging_dir, error);
	if (error) {
		obs_log(LOG_WARNING, "update download failed: cannot create %s: %s",
			to_utf8(config.staging_dir).c_str(), error.message().c_str());
		outcome.state.status = UpdateStatus::Failed;
		return outcome;
	}
	const fs::path archive = config.staging_dir / fs::u8path(asset.name);
	const fs::path partial = config.staging_dir / fs::u8path(asset.name + ".part");
	const std::uint64_t limit = asset.size != 0 ? asset.size : kMaxPackageBytes;
	const DownloadResult download = download_to_file(asset.download_url, partial, limit, cancelled);
	if (cancelled.load()) {
		remove_all_quietly(config.staging_dir);
		return outcome;
	}
	if (!download.ok) {
		obs_log(LOG_WARNING, "update download failed: %s", download.error.c_str());
		remove_all_quietly(config.staging_dir);
		outcome.state.status = UpdateStatus::Failed;
		return outcome;
	}

	const StagedUpdate staged{decision.version, asset.name, asset.sha256, asset.size};
	if (!verify_package(partial, staged)) {
		obs_log(LOG_WARNING, "update %s failed verification: discarded", decision.version.c_str());
		remove_all_quietly(config.staging_dir);
		outcome.state.status = UpdateStatus::Failed;
		return outcome;
	}
	fs::rename(partial, archive, error);
	if (error || replace_file_keeping_backup(config.staging_dir / kManifestName, encode_staged_update(staged))) {
		obs_log(LOG_WARNING, "update %s could not be staged", decision.version.c_str());
		remove_all_quietly(config.staging_dir);
		outcome.state.status = UpdateStatus::Failed;
		return outcome;
	}

	obs_log(LOG_INFO, "update %s downloaded and verified (%s); installs when OBS closes",
		decision.version.c_str(), staged.sha256.empty() ? "size" : "SHA-256");
	outcome.state = {UpdateStatus::Ready, decision.version, {}};
	outcome.ready = ReadyUpdate{staged, archive};
	return outcome;
}

} // namespace multistream
