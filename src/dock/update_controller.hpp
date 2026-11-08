// Connects the update service to the destinations dock: shows its state as the update
// notice, and persists the "Install updates automatically" preference.
#pragma once

#include <QObject>
#include <QPointer>

#include <filesystem>

namespace multistream {

class DestinationsDock;
class UpdateService;

class UpdateController final : public QObject {
	Q_OBJECT

public:
	// The service must outlive this controller; the dock may go first (OBS owns it).
	UpdateController(UpdateService &service, DestinationsDock *dock, std::filesystem::path settings_path);

private:
	void render();
	void set_auto_install(bool on);
	void open_release_page();

	UpdateService &service_;
	QPointer<DestinationsDock> dock_;
	std::filesystem::path settings_path_;
};

} // namespace multistream
