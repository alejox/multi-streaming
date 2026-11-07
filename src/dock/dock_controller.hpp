// Container for the dock: connects what the user asks for to the config, the secret
// store, and the engine, and turns engine statuses into row views. Lives on the UI
// thread. Engine statuses arrive there too, because the engine marshals them. Secret
// store calls run on the SecretWorker, whose results come back to this object's thread.
#pragma once

#include "destinations/config_repository.hpp"
#include "destinations/secret_store.hpp"
#include "dock/destination_editor_dialog.hpp"
#include "dock/destinations_dock.hpp"
#include "secrets/secret_worker.hpp"
#include "streaming/streaming_engine.hpp"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace multistream {

class DockController final : public QObject {
	Q_OBJECT

public:
	// The dock belongs to OBS's main window and can outlive or predate this controller.
	DockController(DestinationsConfig config, ConfigRepository &repository, std::shared_ptr<SecretStore> secrets,
		       StreamingEngine &engine, DestinationsDock *dock);
	~DockController() override;

private:
	void add_destination();
	void edit_destination(const QString &id);
	void delete_destination(const QString &id);
	void stop_all();
	void move_destination(const QString &id, bool up);
	void move_destination_to(const QString &id, int insert_position);
	void set_color_icons(bool on);
	bool open_editor(const Destination &initial, bool is_new);
	void save(const Destination &destination, const std::optional<std::string> &new_key,
		  const std::optional<std::string> &refresh_token, DestinationEditorDialog::SaveDone done);

	const Destination *find(const QString &id) const;
	SessionState state_of(const QString &id) const;
	void refresh_rows();
	void on_status(const DestinationStatus &status);
	DestinationRowStatus row_status(const DestinationStatus &status);
	// Start all / Stop all enable states, and the stats timer, which runs only while
	// something is on air.
	void refresh_bulk_state();

	DestinationsConfig config_;
	ConfigRepository &repository_;
	StreamingEngine &engine_;
	QPointer<DestinationsDock> dock_;
	QTimer stats_timer_;
	// When each on-air destination went live, for its elapsed time. Kept across reconnects.
	std::map<std::string, std::chrono::steady_clock::time_point> live_since_;
	// Last, so it closes first: no result reaches this object once destruction starts.
	SecretWorker secret_worker_;
};

} // namespace multistream
