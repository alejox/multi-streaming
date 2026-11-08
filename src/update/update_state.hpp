// What the updater is doing, as the dock shows it.
#pragma once

#include <string>

namespace multistream {

enum class UpdateStatus {
	// Nothing to show: up to date, not checked yet, or updates are off.
	Idle,
	Checking,
	Downloading,
	// Verified and staged; installs when OBS closes.
	Ready,
	// A newer version exists but cannot be installed automatically; offer the release page.
	Manual,
	// The check or the download failed; offer a retry.
	Failed,
};

struct UpdateState {
	UpdateStatus status = UpdateStatus::Idle;
	// The newer version, for Downloading, Ready and Manual.
	std::string version;
	// The release page, for Manual.
	std::string release_url;
};

} // namespace multistream
