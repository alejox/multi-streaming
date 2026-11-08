#include "dock/update_controller.hpp"

#include "dock/destinations_dock.hpp"
#include "persistence/update_settings_file.hpp"
#include "streaming/localized_text.hpp"
#include "update/update_service.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDesktopServices>
#include <QUrl>

#include <utility>

namespace multistream {

UpdateController::UpdateController(UpdateService &service, DestinationsDock *dock, std::filesystem::path settings_path)
	: service_(service),
	  dock_(dock),
	  settings_path_(std::move(settings_path))
{
	dock_->set_auto_update(service_.enabled());
	connect(&service_, &UpdateService::state_changed, this, &UpdateController::render);
	connect(dock_, &DestinationsDock::auto_update_toggled, this, &UpdateController::set_auto_install);
	connect(dock_, &DestinationsDock::update_retry_requested, this, [this] { service_.retry(); });
	connect(dock_, &DestinationsDock::update_download_requested, this, &UpdateController::open_release_page);
	render();
}

void UpdateController::render()
{
	if (!dock_) {
		return;
	}
	const UpdateState &state = service_.state();
	const QString version = QString::fromStdString(state.version);
	switch (state.status) {
	case UpdateStatus::Idle:
	case UpdateStatus::Checking:
		dock_->set_update_banner(UpdateBannerKind::Hidden, {});
		break;
	case UpdateStatus::Downloading:
		dock_->set_update_banner(UpdateBannerKind::Downloading, version);
		break;
	case UpdateStatus::Ready:
		dock_->set_update_banner(UpdateBannerKind::Ready, version);
		break;
	case UpdateStatus::Manual:
		dock_->set_update_banner(UpdateBannerKind::Available, version);
		break;
	case UpdateStatus::Failed:
		dock_->set_update_banner(UpdateBannerKind::Failed, {});
		break;
	}
}

void UpdateController::set_auto_install(bool on)
{
	UpdateSettings settings;
	settings.auto_install = on;
	if (const auto problem = save_update_settings(settings_path_, settings)) {
		obs_log(LOG_WARNING, "%s", localize(*problem).c_str());
	}
	service_.set_enabled(on);
}

void UpdateController::open_release_page()
{
	const std::string &reported = service_.state().release_url;
	const QUrl url(QString::fromStdString(reported.empty() ? std::string(kReleasesPageUrl) : reported));
	// Only the release page GitHub reported, never another scheme.
	if (url.isValid() && url.scheme() == QStringLiteral("https")) {
		QDesktopServices::openUrl(url);
	}
}

} // namespace multistream
