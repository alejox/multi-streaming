// Qt driver for automatic updates: checks GitHub for a newer release on a worker thread,
// downloads this platform's package into the staging directory, verifies it, and records it
// for the installer that runs when OBS closes. Every failure is logged and shown as a state;
// nothing here blocks the UI thread or throws.
#pragma once

#include "update/staging.hpp"
#include "update/update_check.hpp"
#include "update/update_state.hpp"

#include <QObject>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace multistream {

struct UpdateServiceConfig {
	std::string current_version;
	// Holds the package and its manifest; created on demand.
	std::filesystem::path staging_dir;
	// The installer's outcome from the last close; read and removed on the next check.
	std::filesystem::path result_file;
	// The installed plugin directory; nullopt when this install cannot be replaced.
	std::optional<std::filesystem::path> install_root;
	std::optional<UpdatePlatform> platform;
};

// A verified package ready to install, as found by the last check.
struct ReadyUpdate {
	StagedUpdate staged;
	std::filesystem::path archive;
};

class UpdateService final : public QObject {
	Q_OBJECT

public:
	UpdateService(UpdateServiceConfig config, bool enabled, QObject *parent = nullptr);
	// Cancels a check or download in flight and waits (about a second at most) for its thread.
	~UpdateService() override;

	// Checks once after `delay_ms`, if updates are on, so OBS finishes starting first.
	void start(int delay_ms);
	// Turning updates on checks right away unless something is already known; turning them
	// off cancels a download and hides the state. A staged package is kept but not installed.
	void set_enabled(bool enabled);
	void retry();

	bool enabled() const { return enabled_; }
	const UpdateState &state() const { return state_; }
	const UpdateServiceConfig &config() const { return config_; }
	// Set while the state is Ready.
	const std::optional<ReadyUpdate> &ready() const { return ready_; }

signals:
	void state_changed();

private:
	struct Outcome {
		UpdateState state;
		std::optional<ReadyUpdate> ready;
	};

	void run_check();
	void apply(Outcome outcome);
	static Outcome check_and_stage(const UpdateServiceConfig &config, const std::atomic<bool> &cancelled,
				       const std::function<void(const std::string &version)> &on_download);

	UpdateServiceConfig config_;
	bool enabled_ = false;
	bool running_ = false;
	UpdateState state_;
	std::optional<ReadyUpdate> ready_;
	std::shared_ptr<std::atomic<bool>> cancelled_;
	std::thread worker_;
};

} // namespace multistream
