// Container for the dock: connects what the user asks for to the config, the secret
// store, and the engine, and turns engine statuses into row views. Lives on the UI
// thread. Engine statuses arrive there too, because the engine marshals them.
#pragma once

#include "destinations/config_repository.hpp"
#include "destinations/secret_store.hpp"
#include "dock/destinations_dock.hpp"
#include "streaming/streaming_engine.hpp"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <optional>
#include <string>

namespace multistream {

class DockController final : public QObject {
	Q_OBJECT

public:
	// The dock belongs to OBS's main window and can outlive or predate this controller.
	DockController(DestinationsConfig config, ConfigRepository &repository, SecretStore &secrets,
		       StreamingEngine &engine, DestinationsDock *dock);
	~DockController() override;

private:
	void add_destination();
	void edit_destination(const QString &id);
	void delete_destination(const QString &id);
	bool open_editor(const Destination &initial, bool is_new);
	std::optional<QString> save(const Destination &destination, const std::optional<std::string> &new_key);

	const Destination *find(const QString &id) const;
	SessionState state_of(const QString &id) const;
	void refresh_rows();

	DestinationsConfig config_;
	ConfigRepository &repository_;
	SecretStore &secrets_;
	StreamingEngine &engine_;
	QPointer<DestinationsDock> dock_;
	QTimer stats_timer_;
};

} // namespace multistream
